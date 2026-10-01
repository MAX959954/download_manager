#include <cassert>
#include <cstdio>
#include <string>

#include <dlm/ChunkedDecoder.hpp>

int main() {
    using dlm::detail::consumeChunkedBytes;

    // A single complete response that arrives all at once.
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

    // Data arrives in pieces (simulating several recv() calls) — the buffer
    // must accumulate what's missing and lose nothing between calls.
    {
        std::string received;
        std::string buffer;
        bool finished = false;

        buffer += "5\r\nhel";
        assert(consumeChunkedBytes(buffer, [&](const char* d, std::size_t n) {
            received.append(d, n); return true;
        }, finished));
        assert(!finished);
        assert(received.empty()); // the chunk hasn't been fully read yet

        buffer += "lo\r\n0\r\n\r\n";
        assert(consumeChunkedBytes(buffer, [&](const char* d, std::size_t n) {
            received.append(d, n); return true;
        }, finished));
        assert(finished);
        assert(received == "hello");
        assert(buffer.empty());
    }

    // Trailers after the final zero-length chunk.
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

    // Malformed format (not a hex size) — should cleanly return false.
    {
        std::string buffer = "zz\r\nhello\r\n";
        bool finished = false;
        const bool ok = consumeChunkedBytes(
            buffer, [](const char*, std::size_t) { return true; }, finished);
        assert(!ok);
    }

    // onData returned false — abort parsing (pause/cancel).
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
