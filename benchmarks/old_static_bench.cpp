// Контрольный замер СТАРОГО алгоритма (статическая раздача: один таск на
// один фиксированный чанк, без дробления) — чтобы честно показать разницу
// до/после ChunkQueue, а не просто посчитать на бумаге. Это НЕ часть
// проекта, используется только один раз для README.
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

// Копия ThreadPool-free static-partition логики, как было до ChunkQueue:
// ровно один HTTP-запрос на чанк, без адаптивного дробления.
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

    // Эмулируем "один таск на чанк, пул воркеров" так же, как было в
    // исходном коде: чанков МЕНЬШЕ, чем потоков, поэтому лишние потоки
    // просто не создаём вовсе (ровно как это и происходило бы в старом
    // ThreadPool-варианте — излишние воркеры не получали ни одной задачи).
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
