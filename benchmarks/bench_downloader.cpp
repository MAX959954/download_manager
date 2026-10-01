// Benchmark: 1 vs 4 vs 8 threads x several chunk sizes.
//
// Downloads the same file via Downloader::downloadParallel on top of
// SocketHttpClient (our own HTTP client over raw sockets, Stage 8) against
// throttled_server.py on localhost — the server artificially caps the
// throughput of EACH TCP connection, so aggregate throughput scales with
// the number of parallel workers almost linearly until it hits a ceiling.
//
// This is not a unit test and not part of ctest — it's only built with
// -DDLM_BUILD_BENCHMARKS=ON and run manually:
//   python3 benchmarks/throttled_server.py 8787 &
//   ./build/.../bench_downloader http://127.0.0.1:8787/file benchmarks/results.csv 3

#include <dlm/Downloader.hpp>
#include <dlm/SocketHttpClient.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string url = argc > 1 ? argv[1] : "http://127.0.0.1:8787/file";
    const std::string csvPath = argc > 2 ? argv[2] : "benchmarks/results.csv";
    const int repeats = argc > 3 ? std::atoi(argv[3]) : 3;
    const std::string outputPath = "bench_downloader_output.tmp";

    const std::vector<std::size_t> threadCounts = {1, 4, 8};
    const std::vector<std::int64_t> chunkSizes = {256 * 1024, 1024 * 1024, 4 * 1024 * 1024, 10 * 1024 * 1024};

    std::ofstream csv(csvPath);
    if (!csv) {
        std::fprintf(stderr, "bench_downloader: cannot open %s for writing\n", csvPath.c_str());
        return 1;
    }
    csv << "threads,chunk_size_bytes,run,elapsed_seconds,bytes,throughput_mb_s\n";

    dlm::SocketHttpClient client;
    dlm::Downloader downloader(client);

    for (const std::int64_t chunkSize : chunkSizes) {
        for (const std::size_t threads : threadCounts) {
            for (int run = 1; run <= repeats; ++run) {
                std::remove(outputPath.c_str());

                const auto t0 = std::chrono::steady_clock::now();
                const dlm::DownloadResult result =
                    downloader.downloadParallel(url, outputPath, chunkSize, threads);
                const auto t1 = std::chrono::steady_clock::now();

                const double elapsed = std::chrono::duration<double>(t1 - t0).count();
                const double megabytes = static_cast<double>(result.bytesWritten) / (1024.0 * 1024.0);
                const double throughput = elapsed > 0.0 ? megabytes / elapsed : 0.0;

                std::printf("threads=%zu chunk=%lldB run=%d elapsed=%.3fs throughput=%.2f MB/s success=%d\n",
                            threads, static_cast<long long>(chunkSize), run, elapsed, throughput,
                            result.success ? 1 : 0);

                if (!result.success) {
                    std::fprintf(stderr, "bench_downloader: download failed (threads=%zu chunk=%lld), aborting\n",
                                 threads, static_cast<long long>(chunkSize));
                    std::remove(outputPath.c_str());
                    return 1;
                }

                csv << threads << ',' << chunkSize << ',' << run << ',' << elapsed << ','
                    << result.bytesWritten << ',' << throughput << '\n';
                csv.flush();
            }
        }
    }

    std::remove(outputPath.c_str());
    std::printf("bench_downloader: done, results written to %s\n", csvPath.c_str());
    return 0;
}
