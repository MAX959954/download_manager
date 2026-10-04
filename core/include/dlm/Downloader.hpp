#pragma once

#include <atomic>
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

    // Stage 1: a single connection, the whole file, no chunking or resume.
    DownloadResult downloadToFile(const std::string& url, const std::string& outputPath);

    // Stage 2: the file is split into fixed-size chunks and downloaded
    // sequentially; each chunk is a separate Range request, written
    // straight to its own offset. If the server doesn't confirm Range
    // support — fall back to downloadToFile.
    DownloadResult downloadChunked(const std::string& url,
                                    const std::string& outputPath,
                                    std::int64_t chunkSize = 4 * 1024 * 1024);

    // Stage 3: the same chunks as in downloadChunked, but downloaded in
    // parallel via ThreadPool — one task per chunk.
    DownloadResult downloadParallel(const std::string& url,
                                     const std::string& outputPath,
                                     std::int64_t chunkSize = 4 * 1024 * 1024,
                                     std::size_t numWorkers = 4,
                                     CancelToken* cancelToken = nullptr,
                                     std::size_t maxRetries = 3,
                                     const std::string& expectedSha256 = "",
                                     RateLimiter* rateLimiter = nullptr);

    // Stage 4: like downloadParallel, but with a meta file (.dlm) next to
    // the output file — on a repeated call, resumes only the missing
    // chunks, provided the server confirmed via If-Range that the file
    // hasn't changed.
    //
    // progressBytes / progressTotalBytes are optional: when given, they're
    // updated live from worker threads as the download runs (bytes written
    // so far, and the file's total size once known from the probe) — a
    // caller on another thread (e.g. a GUI polling on a timer) can read
    // them at any time without touching anything else here. Neither is
    // reset at the end, so a caller comparing bytesDone to the final
    // DownloadResult should read totalBytes first.
    DownloadResult downloadResumable(const std::string& url,
                                      const std::string& outputPath,
                                      std::int64_t chunkSize = 4 * 1024 * 1024,
                                      std::size_t numWorkers = 4,
                                      CancelToken* cancelToken = nullptr,
                                      std::size_t maxRetries = 3,
                                      const std::string& expectedSha256 = "",
                                      RateLimiter* rateLimiter = nullptr,
                                      std::atomic<std::int64_t>* progressBytes = nullptr,
                                      std::atomic<std::int64_t>* progressTotalBytes = nullptr);

private:
    IHttpClient& httpClient_;
};

} // namespace dlm
