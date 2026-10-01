#include "dlm/ChunkedDecoder.hpp"

#include <cstdlib>

namespace dlm::detail {

bool consumeChunkedBytes(std::string& buffer, const WriteCallback& onData, bool& finished) {
    finished = false;

    for (;;) {
        const auto lineEnd = buffer.find("\r\n");
        if (lineEnd == std::string::npos) {
            return true; // waiting for more data to complete the chunk-size line
        }

        std::string sizeLine = buffer.substr(0, lineEnd);
        const auto semi = sizeLine.find(';'); // discard the chunk-extension, if present
        if (semi != std::string::npos) {
            sizeLine = sizeLine.substr(0, semi);
        }
        if (sizeLine.empty()) {
            return false;
        }

        char* end = nullptr;
        const unsigned long chunkSize = std::strtoul(sizeLine.c_str(), &end, 16);
        if (end == sizeLine.c_str()) {
            return false; // not a hex number
        }

        if (chunkSize == 0) {
            // Terminating chunk — wait for the blank line that closes the trailers.
            const auto trailerEnd = buffer.find("\r\n\r\n", lineEnd);
            if (trailerEnd == std::string::npos) {
                return true; // trailers haven't fully arrived yet
            }
            buffer.erase(0, trailerEnd + 4);
            finished = true;
            return true;
        }

        const std::size_t dataStart = lineEnd + 2;
        const std::size_t dataEnd = dataStart + static_cast<std::size_t>(chunkSize);
        if (buffer.size() < dataEnd + 2) {
            return true; // this chunk's data hasn't fully arrived yet
        }
        if (buffer[dataEnd] != '\r' || buffer[dataEnd + 1] != '\n') {
            return false; // no terminating CRLF after the chunk data — malformed format
        }

        if (!onData(buffer.data() + dataStart, static_cast<std::size_t>(chunkSize))) {
            return false;
        }

        buffer.erase(0, dataEnd + 2);
        // Don't exit the loop — the next chunk might already be fully in the buffer.
    }
}

} // namespace dlm::detail
