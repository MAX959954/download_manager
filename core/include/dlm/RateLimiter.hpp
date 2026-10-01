#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace dlm {

// Простой token bucket: ограничивает суммарную скорость чтения данных до
// maxBytesPerSecond. Поток, получивший n байт, вызывает acquire(n) и
// блокируется ровно настолько, насколько нужно, чтобы не превысить лимит.
// Один RateLimiter можно шарить между всеми воркерами одной (или даже
// нескольких) закачек — он потокобезопасен.
class RateLimiter {
public:
    explicit RateLimiter(std::int64_t maxBytesPerSecond)
        : maxBytesPerSecond_(maxBytesPerSecond),
          tokens_(static_cast<double>(maxBytesPerSecond)),
          lastRefill_(std::chrono::steady_clock::now()) {}

    void acquire(std::size_t bytes) {
        if (maxBytesPerSecond_ <= 0) {
            return; // 0 или отрицательное значение = лимита нет
        }

        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            refill();
            if (tokens_ >= static_cast<double>(bytes)) {
                tokens_ -= static_cast<double>(bytes);
                return;
            }
            const double missing = static_cast<double>(bytes) - tokens_;
            const auto waitTime =
                std::chrono::duration<double>(missing / static_cast<double>(maxBytesPerSecond_));
            lock.unlock();
            std::this_thread::sleep_for(waitTime);
            lock.lock();
        }
    }

private:
    void refill() {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - lastRefill_).count();
        lastRefill_ = now;
        tokens_ = std::min(static_cast<double>(maxBytesPerSecond_),
                            tokens_ + elapsed * static_cast<double>(maxBytesPerSecond_));
    }

    std::int64_t maxBytesPerSecond_;
    double tokens_;
    std::chrono::steady_clock::time_point lastRefill_;
    std::mutex mutex_;
};

} // namespace dlm
