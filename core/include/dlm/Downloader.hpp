#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "dlm/CancelToken.hpp"
#include "dlm/IHttpClient.hpp"
#include "dlm/MetaFile.hpp"
#include "dlm/RateLimiter.hpp"
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

    // Этап 3: те же чанки, что и в downloadChunked, но качаются
    // параллельно через ThreadPool — задача = один чанк.
    DownloadResult downloadParallel(const std::string& url,
                                     const std::string& outputPath,
                                     std::int64_t chunkSize = 4 * 1024 * 1024,
                                     std::size_t numWorkers = 4,
                                     CancelToken* cancelToken = nullptr,
                                     std::size_t maxRetries = 3,
                                     const std::string& expectedSha256 = "",
                                     RateLimiter* rateLimiter = nullptr);

    // Этап 4: как downloadParallel, но с метафайлом (.dlm) рядом с файлом —
    // при повторном вызове докачивает только недостающие чанки, если сервер
    // по If-Range подтвердил, что файл не менялся.
    DownloadResult downloadResumable(const std::string& url,
                                      const std::string& outputPath,
                                      std::int64_t chunkSize = 4 * 1024 * 1024,
                                      std::size_t numWorkers = 4,
                                      CancelToken* cancelToken = nullptr,
                                      std::size_t maxRetries = 3,
                                      const std::string& expectedSha256 = "",
                                      RateLimiter* rateLimiter = nullptr);

private:
    IHttpClient& httpClient_;
};

} // namespace dlm
