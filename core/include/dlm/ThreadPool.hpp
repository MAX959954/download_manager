#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace dlm {

// Простой пул потоков общего назначения: N воркеров разбирают очередь
// задач std::function<void()>. Используется для скачивания чанков —
// задача = "скачать чанк i", но сам пул про чанки ничего не знает.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> task);

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

} // namespace dlm
