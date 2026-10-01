#include <cassert>
#include <chrono>
#include <cstdio>

#include <dlm/RateLimiter.hpp>

int main() {
    // Лимит 1000 байт/сек. Запрашиваем 3000 байт тремя кусками по 1000 —
    // первый должен пройти почти сразу (бакет стартует полным), а два
    // следующих — потребовать суммарно не меньше ~2 секунд ожидания.
    dlm::RateLimiter limiter(1000);

    const auto start = std::chrono::steady_clock::now();
    limiter.acquire(1000); // из начального запаса токенов — без ожидания
    limiter.acquire(1000); // токенов не осталось — ждём пополнения
    limiter.acquire(1000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
    assert(elapsedSeconds > 1.5); // должно было уйти заметное время на ожидание

    // Лимит <= 0 означает "без ограничения" — acquire() не должен блокировать.
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
