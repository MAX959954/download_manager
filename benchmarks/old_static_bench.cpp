// Baseline measurement of the OLD algorithm (static partitioning: one task
// per fixed chunk, no splitting) — to honestly show the before/after
// difference from ChunkQueue instead of just estimating it on paper. This
// is NOT part of the project, used only once to produce numbers for the
// README.
#include <dlm/CancelToken.hpp>
#include <dlm/FileWriter.hpp>
#include <dlm/RateLimiter.hpp>
#include <dlm/SocketHttpClient.hpp>
#include <dlm/Types.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Copy of the ThreadPool-free static-partition logic as it was before
// ChunkQueue: exactly one HTTP request per chunk, no adaptive splitting.
static double runOldStatic(dlm::IHttpClient& client, const std::string& url, const std::string& outputPath,
                            std::int64_t chunkSize, std::size_t numWorkers, std::int64_t totalSize) {
    dlm::FileWriter::preallocate(outputPath, totalSize);
    dlm::FileWriter writer(outputPath);

    std::vector<dlm::ChunkSpec> chunks;
    for (std::int64_t offset = 0; offset < totalSize; offset += chunkSize) {
        chunks.push_back({offset, std::min(chunkSize, totalSize - offset)});
    }

    std::atomic<std::size_t> remaining{chunks.size()};
    std::mutex doneMutex;
    std::condition_variable doneCv;
    std::atomic<std::size_t> nextChunk{0};

    const auto t0 = std::chrono::steady_clock::now();

    // Emulate "one task per chunk, worker pool" the same way the original
    // code did: when there are FEWER chunks than threads, we simply don't
    // create the extra threads at all (exactly as would have happened in
    // the old ThreadPool variant — surplus workers never received a task).
    const std::size_t activeWorkers = std::min(numWorkers, chunks.size());
    std::vector<std::thread> workers;
    for (std::size_t w = 0; w < activeWorkers; ++w) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t idx = nextChunk.fetch_add(1);
                if (idx >= chunks.size()) break;
                const auto& chunk = chunks[idx];

                dlm::HttpRequest req;
                req.url = url;
                req.headers["Range"] =
                    "bytes=" + std::to_string(chunk.offset) + "-" + std::to_string(chunk.offset + chunk.size - 1);
                std::int64_t written = 0;
                dlm::WriteCallback onData = [&](const char* data, std::size_t n) {
                    writer.writeAt(chunk.offset + written, data, n);
                    written += static_cast<std::int64_t>(n);
                    return true;
                };
                client.perform(req, onData);
                if (--remaining == 0) doneCv.notify_one();
            }
        });
    }
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return remaining.load() == 0; });
    }
    for (auto& w : workers) w.join();

    const auto t1 = std::chrono::steady_clock::now();
    std::remove(outputPath.c_str());
    return std::chrono::duration<double>(t1 - t0).count();
}

int main(int argc, char** argv) {
    const std::string url = argc > 1 ? argv[1] : "http://127.0.0.1:8787/file";
    const std::int64_t totalSize = argc > 2 ? std::atoll(argv[2]) : 20 * 1024 * 1024;
    const std::int64_t chunkSize = argc > 3 ? std::atoll(argv[3]) : 10 * 1024 * 1024;
    const int repeats = argc > 4 ? std::atoi(argv[4]) : 3;

    dlm::SocketHttpClient client;
    const std::vector<std::size_t> threadCounts = {1, 4, 8};

    for (auto threads : threadCounts) {
        for (int run = 1; run <= repeats; ++run) {
            const double elapsed = runOldStatic(client, url, "old_bench_out.tmp", chunkSize, threads, totalSize);
            const double mb = totalSize / (1024.0 * 1024.0);
            std::printf("[OLD static] threads=%zu chunk=%lldB run=%d elapsed=%.3fs throughput=%.2f MB/s\n",
                        threads, static_cast<long long>(chunkSize), run, elapsed, mb / elapsed);
        }
    }
    return 0;
}
