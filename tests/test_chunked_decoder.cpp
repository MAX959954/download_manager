#include <cassert>
#include <cstdio>
#include <string>

#include <dlm/ChunkedDecoder.hpp>

int main() {
    using dlm::detail::consumeChunkedBytes;

    // Один полный ответ, пришедший сразу целиком.
    {
        std::string buffer = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
        std::string received;
        bool finished = false;
        const bool ok = consumeChunkedBytes(
            buffer, [&](const char* data, std::size_t size) { received.append(data, size); return true; },
            finished);
        assert(ok);
        assert(finished);
        assert(received == "hello world");
        assert(buffer.empty());
    }

    // Данные приходят по кусочкам (имитация нескольких recv()) — буфер
    // должен копить недостающее и ничего не терять между вызовами.
    {
        std::string received;
        std::string buffer;
        bool finished = false;

        buffer += "5\r\nhel";
        assert(consumeChunkedBytes(buffer, [&](const char* d, std::size_t n) {
            received.append(d, n); return true;
        }, finished));
        assert(!finished);
        assert(received.empty()); // чанк ещё не дочитан целиком

        buffer += "lo\r\n0\r\n\r\n";
        assert(consumeChunkedBytes(buffer, [&](const char* d, std::size_t n) {
            received.append(d, n); return true;
        }, finished));
        assert(finished);
        assert(received == "hello");
        assert(buffer.empty());
    }

    // Трейлеры после завершающего чанка нулевой длины.
    {
        std::string buffer = "0\r\nX-Trailer: value\r\n\r\n";
        std::string received;
        bool finished = false;
        assert(consumeChunkedBytes(buffer, [&](const char* d, std::size_t n) {
            received.append(d, n); return true;
        }, finished));
        assert(finished);
        assert(received.empty());
        assert(buffer.empty());
    }

    // Битый формат (не hex-размер) — должны аккуратно вернуть false.
    {
        std::string buffer = "zz\r\nhello\r\n";
        bool finished = false;
        const bool ok = consumeChunkedBytes(
            buffer, [](const char*, std::size_t) { return true; }, finished);
        assert(!ok);
    }

    // onData вернул false — прерываем разбор (пауза/отмена).
    {
        std::string buffer = "5\r\nhello\r\n0\r\n\r\n";
        bool finished = false;
        const bool ok = consumeChunkedBytes(
            buffer, [](const char*, std::size_t) { return false; }, finished);
        assert(!ok);
    }

    std::printf("test_chunked_decoder: OK\n");
    return 0;
}
