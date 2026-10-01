#include <cassert>
#include <chrono>
#include <cstdio>

#include <dlm/RateLimiter.hpp>

int main() {
    // Limit of 1000 bytes/sec. Request 3000 bytes in three chunks of 1000 —
    // the first should go through almost immediately (the bucket starts
    // full), while the next two should together require at least ~2 seconds
    // of waiting.
    dlm::RateLimiter limiter(1000);

    const auto start = std::chrono::steady_clock::now();
    limiter.acquire(1000); // from the initial token supply — no waiting
    limiter.acquire(1000); // no tokens left — wait for refill
    limiter.acquire(1000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
    assert(elapsedSeconds > 1.5); // a noticeable amount of time should have been spent waiting

    // Limit <= 0 means "unlimited" — acquire() must not block.
    {
        dlm::RateLimiter noLimit(0);
        const auto t0 = std::chrono::steady_clock::now();
        noLimit.acquire(10 * 1000 * 1000);
        const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        assert(dt < 0.1);
    }

    std::printf("test_rate_limiter: OK\n");
    return 0;
}
