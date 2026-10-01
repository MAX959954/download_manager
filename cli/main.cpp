#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dlm/CancelToken.hpp>
#include <dlm/CurlHttpClient.hpp>
#include <dlm/Downloader.hpp>
#include <dlm/RateLimiter.hpp>
#include <dlm/Version.hpp>

namespace {

struct Options {
    std::string url;
    std::string outputPath;
    std::int64_t chunkSize = 4 * 1024 * 1024; // 4 MB
    std::size_t numWorkers = 4;
    std::size_t maxRetries = 3;
    std::int64_t rateLimitBytesPerSec = 0; // 0 = unlimited
    std::string expectedSha256;
    bool parallel = false; // --parallel / implied by --resume
    bool resume = false;   // --resume
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <url> -o <file> [options]\n"
        "\n"
        "Options:\n"
        "  -o, --output <file>       Output file path (required)\n"
        "  --parallel                Download over multiple connections\n"
        "                            (adaptive chunking / work stealing)\n"
        "  --resume                  Like --parallel, but resumable: saves\n"
        "                            progress to a .dlm metafile and\n"
        "                            continues an interrupted download on\n"
        "                            the next run with the same output path\n"
        "  --workers <N>             Number of parallel workers (default: 4)\n"
        "  --chunk-size <size>       Chunk size, e.g. 256K, 4M, 10M (default: 4M)\n"
        "  --rate-limit <size>       Cap total bandwidth, e.g. 500K, 2M/s\n"
        "                            (default: unlimited)\n"
        "  --retries <N>             Retries per chunk on failure (default: 3)\n"
        "  --sha256 <hex>            Verify the downloaded file against this\n"
        "                            SHA-256 checksum\n"
        "  -h, --help                Show this help\n"
        "\n"
        "Ctrl+C sends a cooperative cancel: workers finish or abort their\n"
        "current request and exit cleanly instead of leaving a half-written\n"
        "file locked.\n",
        argv0);
}

// Parses sizes like "256K", "4M", "10MB", "2G", or a plain byte count.
// Returns false on a malformed value.
bool parseSize(const std::string& text, std::int64_t& outBytes) {
    if (text.empty()) {
        return false;
    }

    std::size_t end = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &end);
    } catch (const std::exception&) {
        return false;
    }
    if (value < 0.0) {
        return false;
    }

    std::string suffix = text.substr(end);
    // Trim an optional trailing "B" / "/s" (so "4MB" and "2M/s" both work).
    while (!suffix.empty() && (suffix.back() == 'b' || suffix.back() == 'B' ||
                                suffix.back() == 's' || suffix.back() == '/')) {
        suffix.pop_back();
    }

    double multiplier = 1.0;
    if (suffix.empty()) {
        multiplier = 1.0;
    } else if (suffix.size() == 1) {
        switch (std::tolower(static_cast<unsigned char>(suffix[0]))) {
            case 'k': multiplier = 1024.0; break;
            case 'm': multiplier = 1024.0 * 1024.0; break;
            case 'g': multiplier = 1024.0 * 1024.0 * 1024.0; break;
            default: return false;
        }
    } else {
        return false;
    }

    outBytes = static_cast<std::int64_t>(value * multiplier);
    return outBytes > 0 || (value == 0.0 && outBytes == 0);
}

// Parsed command-line arguments into Options. Returns false (and prints
// usage) on a malformed or incomplete argument list.
bool parseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto nextArg = [&](const char* flagName) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", flagName);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (arg == "-o" || arg == "--output") {
            const char* v = nextArg("--output");
            if (!v) return false;
            opts.outputPath = v;
        } else if (arg == "--parallel") {
            opts.parallel = true;
        } else if (arg == "--resume") {
            opts.resume = true;
            opts.parallel = true; // downloadResumable implies chunked+parallel
        } else if (arg == "--workers") {
            const char* v = nextArg("--workers");
            if (!v) return false;
            opts.numWorkers = static_cast<std::size_t>(std::atoll(v));
            if (opts.numWorkers == 0) {
                std::fprintf(stderr, "--workers must be a positive integer\n");
                return false;
            }
        } else if (arg == "--chunk-size") {
            const char* v = nextArg("--chunk-size");
            if (!v || !parseSize(v, opts.chunkSize) || opts.chunkSize <= 0) {
                std::fprintf(stderr, "invalid --chunk-size: %s\n", v ? v : "(missing)");
                return false;
            }
        } else if (arg == "--rate-limit") {
            const char* v = nextArg("--rate-limit");
            if (!v || !parseSize(v, opts.rateLimitBytesPerSec)) {
                std::fprintf(stderr, "invalid --rate-limit: %s\n", v ? v : "(missing)");
                return false;
            }
        } else if (arg == "--retries") {
            const char* v = nextArg("--retries");
            if (!v) return false;
            opts.maxRetries = static_cast<std::size_t>(std::atoll(v));
        } else if (arg == "--sha256") {
            const char* v = nextArg("--sha256");
            if (!v) return false;
            opts.expectedSha256 = v;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        } else if (opts.url.empty()) {
            opts.url = arg;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", arg.c_str());
            return false;
        }
    }

    return !opts.url.empty() && !opts.outputPath.empty();
}

// Set by the SIGINT handler so the main thread can request a cooperative
// cancel; CancelToken itself is what the download workers actually poll.
dlm::CancelToken* g_cancelToken = nullptr;

void handleSigint(int /*signal*/) {
    if (g_cancelToken) {
        g_cancelToken->cancel();
    }
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    std::printf("dlm %s (%s)\n", dlm::kVersion, dlm::httpBackendInfo().c_str());

    dlm::CurlHttpClient httpClient;
    dlm::Downloader downloader(httpClient);

    dlm::CancelToken cancelToken;
    g_cancelToken = &cancelToken;
    std::signal(SIGINT, handleSigint);

    dlm::RateLimiter rateLimiter(opts.rateLimitBytesPerSec);
    dlm::RateLimiter* rateLimiterPtr = opts.rateLimitBytesPerSec > 0 ? &rateLimiter : nullptr;

    const auto t0 = std::chrono::steady_clock::now();
    dlm::DownloadResult result;

    if (opts.resume) {
        std::printf("Mode: resumable parallel download (workers=%zu, chunk=%lld bytes)\n",
                     opts.numWorkers, static_cast<long long>(opts.chunkSize));
        result = downloader.downloadResumable(opts.url, opts.outputPath, opts.chunkSize,
                                               opts.numWorkers, &cancelToken, opts.maxRetries,
                                               opts.expectedSha256, rateLimiterPtr);
    } else if (opts.parallel) {
        std::printf("Mode: parallel download (workers=%zu, chunk=%lld bytes)\n",
                     opts.numWorkers, static_cast<long long>(opts.chunkSize));
        result = downloader.downloadParallel(opts.url, opts.outputPath, opts.chunkSize,
                                              opts.numWorkers, &cancelToken, opts.maxRetries,
                                              opts.expectedSha256, rateLimiterPtr);
    } else {
        std::printf("Mode: single connection (use --parallel or --resume for multiple "
                     "connections)\n");
        result = downloader.downloadToFile(opts.url, opts.outputPath);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (!result.success) {
        std::fprintf(stderr, "Download error: %s\n", result.error.c_str());
        if (opts.resume) {
            std::fprintf(stderr,
                "Progress was saved — rerun the same command to resume.\n");
        }
        return 1;
    }

    const double megabytes = static_cast<double>(result.bytesWritten) / (1024.0 * 1024.0);
    const double throughput = elapsed > 0.0 ? megabytes / elapsed : 0.0;

    std::printf("Downloaded: %s\n", opts.outputPath.c_str());
    std::printf("Size: %lld bytes\n", static_cast<long long>(result.bytesWritten));
    std::printf("Time: %.2fs (%.2f MB/s)\n", elapsed, throughput);
    std::printf("Accept-Ranges: %s\n", result.response.acceptRanges ? "yes" : "no");
    if (!result.response.etag.empty()) {
        std::printf("ETag: %s\n", result.response.etag.c_str());
    }
    if (!result.response.lastModified.empty()) {
        std::printf("Last-Modified: %s\n", result.response.lastModified.c_str());
    }
    if (!result.sha256.empty()) {
        std::printf("SHA-256: %s\n", result.sha256.c_str());
    }

    return 0;
}
