#include "dlm/ChunkedDecoder.hpp"

#include <cstdlib>

namespace dlm::detail {

bool consumeChunkedBytes(std::string& buffer, const WriteCallback& onData, bool& finished) {
    finished = false;

    for (;;) {
        const auto lineEnd = buffer.find("\r\n");
        if (lineEnd == std::string::npos) {
            return true; // ждём данных для строки размера чанка
        }

        std::string sizeLine = buffer.substr(0, lineEnd);
        const auto semi = sizeLine.find(';'); // отбрасываем chunk-extension, если есть
        if (semi != std::string::npos) {
            sizeLine = sizeLine.substr(0, semi);
        }
        if (sizeLine.empty()) {
            return false;
        }

        char* end = nullptr;
        const unsigned long chunkSize = std::strtoul(sizeLine.c_str(), &end, 16);
        if (end == sizeLine.c_str()) {
            return false; // не hex-число
        }

        if (chunkSize == 0) {
            // Завершающий чанк — ждём пустую строку, закрывающую трейлеры.
            const auto trailerEnd = buffer.find("\r\n\r\n", lineEnd);
            if (trailerEnd == std::string::npos) {
                return true; // трейлеры ещё не докачались полностью
            }
            buffer.erase(0, trailerEnd + 4);
            finished = true;
            return true;
        }

        const std::size_t dataStart = lineEnd + 2;
        const std::size_t dataEnd = dataStart + static_cast<std::size_t>(chunkSize);
        if (buffer.size() < dataEnd + 2) {
            return true; // данные этого чанка ещё не пришли целиком
        }
        if (buffer[dataEnd] != '\r' || buffer[dataEnd + 1] != '\n') {
            return false; // нет завершающего CRLF после данных чанка — битый формат
        }

        if (!onData(buffer.data() + dataStart, static_cast<std::size_t>(chunkSize))) {
            return false;
        }

        buffer.erase(0, dataEnd + 2);
        // Не выходим из цикла — вдруг в буфере уже лежит целиком следующий чанк.
    }
}

} // namespace dlm::detail
