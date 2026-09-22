#pragma once

#include <cstdint>
#include <string>

#include "dlm/IHttpClient.hpp"
#include "dlm/Types.hpp"

namespace dlm {

class Downloader {
public:
    explicit Downloader(IHttpClient& httpClient);

    // Этап 1: одно соединение, файл целиком, без чанков и докачки.
    DownloadResult downloadToFile(const std::string& url, const std::string& outputPath);

    // Этап 2: файл делится на чанки фиксированного размера и качается
    // последовательно; каждый чанк — отдельный Range-запрос, запись сразу
    // по своему offset. Если сервер не подтвердил поддержку Range —
    // откат на downloadToFile.
    DownloadResult downloadChunked(const std::string& url,
                                    const std::string& outputPath,
                                    std::int64_t chunkSize = 4 * 1024 * 1024);

private:
    IHttpClient& httpClient_;
};

} // namespace dlm
