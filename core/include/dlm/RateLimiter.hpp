#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace dlm {

// A simple token bucket: caps the overall data read rate at
// maxBytesPerSecond. A thread that receives n bytes calls acquire(n) and
// blocks for exactly as long as needed to stay within the limit.
// A single RateLimiter can be shared between all workers of one (or even
// several) downloads — it's thread-safe.
class RateLimiter {
public:
    explicit RateLimiter(std::int64_t maxBytesPerSecond)
        : maxBytesPerSecond_(maxBytesPerSecond),
          tokens_(static_cast<double>(maxBytesPerSecond)),
          lastRefill_(std::chrono::steady_clock::now()) {}

    void acquire(std::size_t bytes) {
        if (maxBytesPerSecond_ <= 0) {
            return; // 0 or a negative value = no limit
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
