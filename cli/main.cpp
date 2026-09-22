#include <cstdio>
#include <string>

#include <dlm/CurlHttpClient.hpp>
#include <dlm/Downloader.hpp>
#include <dlm/Version.hpp>

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr, "Использование: %s <url> -o <file>\n", argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string url;
    std::string outputPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            outputPath = argv[++i];
        } else if (url.empty()) {
            url = arg;
        }
    }

    if (url.empty() || outputPath.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    std::printf("dlm %s (%s)\n", dlm::kVersion, dlm::httpBackendInfo().c_str());

    dlm::CurlHttpClient httpClient;
    dlm::Downloader downloader(httpClient);
    const dlm::DownloadResult result = downloader.downloadToFile(url, outputPath);

    if (!result.success) {
        std::fprintf(stderr, "Ошибка загрузки: %s\n", result.error.c_str());
        return 1;
    }

    std::printf("Загружено: %s\n", outputPath.c_str());
    std::printf("Размер: %lld байт\n", static_cast<long long>(result.bytesWritten));
    std::printf("Accept-Ranges: %s\n", result.response.acceptRanges ? "yes" : "no");
    if (!result.response.etag.empty()) {
        std::printf("ETag: %s\n", result.response.etag.c_str());
    }
    if (!result.response.lastModified.empty()) {
        std::printf("Last-Modified: %s\n", result.response.lastModified.c_str());
    }

    return 0;
}
