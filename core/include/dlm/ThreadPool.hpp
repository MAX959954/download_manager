#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace dlm {

// A simple general-purpose thread pool: N workers pull tasks off a queue
// of std::function<void()>. Used for downloading chunks — a task is
// "download chunk i", but the pool itself knows nothing about chunks.
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
