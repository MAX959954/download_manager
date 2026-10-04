#include "dlm/Downloader.hpp"
#include "dlm/ChunkQueue.hpp"
#include "dlm/FileWriter.hpp"
#include "dlm/MetaFile.hpp"
#include "dlm/Sha256.hpp"
#include "dlm/ThreadPool.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <thread>
#include <vector>

namespace dlm {

namespace {

bool verifySha256(DownloadResult& result, const std::string& outputPath,
                   const std::string& expectedSha256) {
    bool hashOk = false;
    result.sha256 = sha256File(outputPath, hashOk);
    if (!hashOk) {
        result.success = false;
        result.error = "failed to compute SHA-256: " + outputPath;
        return false;
    }
    if (expectedSha256.empty()) {
        return true;
    }
    auto toLower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    if (toLower(result.sha256) != toLower(expectedSha256)) {
        result.success = false;
        result.error = "SHA-256 mismatch: expected " + expectedSha256 +
                        ", got " + result.sha256;
        return false;
    }
    return true;
}

} // namespace

Downloader::Downloader(IHttpClient& httpClient) : httpClient_(httpClient) {}

DownloadResult Downloader::downloadToFile(const std::string& url, const std::string& outputPath) {
    DownloadResult result;

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "failed to open the file for writing: " + outputPath;
        return result;
    }

    bool writeFailed = false;
    WriteCallback onData = [&](const char* data, std::size_t size) {
        out.write(data, static_cast<std::streamsize>(size));
        if (!out) {
            writeFailed = true;
            return false;
        }
        result.bytesWritten += static_cast<std::int64_t>(size);
        return true;
    };

    HttpRequest request;
    request.url = url;
    result.response = httpClient_.perform(request, onData);

    const bool httpOk = result.response.statusCode >= 200 && result.response.statusCode < 300;
    result.success = httpOk && !writeFailed;

    if (!result.success) {
        result.error = writeFailed ? ("error writing to file: " + outputPath)
                                    : ("HTTP status " + std::to_string(result.response.statusCode));
    }

    return result;
}

DownloadResult Downloader::downloadChunked(const std::string& url,
                                            const std::string& outputPath,
                                            std::int64_t chunkSize) {
    DownloadResult result;

    if (chunkSize <= 0) {
        result.error = "chunkSize must be positive";
        return result;
    }

    HttpRequest probeRequest;
    probeRequest.url = url;
    probeRequest.headers["Range"] = "bytes=0-0";
    WriteCallback discard = [](const char*, std::size_t) { return true; };
    const HttpResponse probe = httpClient_.perform(probeRequest, discard);

    if (probe.statusCode != 206 || probe.contentRangeTotal <= 0) {
        return downloadToFile(url, outputPath);
    }

    const std::int64_t totalSize = probe.contentRangeTotal;
    if (!FileWriter::preallocate(outputPath, totalSize)) {
        result.error = "failed to preallocate the file: " + outputPath;
        return result;
    }

    FileWriter writer(outputPath);

    for (std::int64_t offset = 0; offset < totalSize; offset += chunkSize) {
        const std::int64_t size = std::min(chunkSize, totalSize - offset);

        HttpRequest chunkRequest;
        chunkRequest.url = url;
        chunkRequest.headers["Range"] =
            "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + size - 1);

        std::int64_t writtenInChunk = 0;
        bool writeFailed = false;
        WriteCallback onData = [&](const char* data, std::size_t n) {
            if (!writer.writeAt(offset + writtenInChunk, data, n)) {
                writeFailed = true;
                return false;
            }
            writtenInChunk += static_cast<std::int64_t>(n);
            return true;
        };

        const HttpResponse chunkResponse = httpClient_.perform(chunkRequest, onData);
        const bool chunkOk = chunkResponse.statusCode == 206 && !writeFailed &&
                              writtenInChunk == size;

        if (!chunkOk) {
            result.error = "chunk error [" + std::to_string(offset) + ", " +
                            std::to_string(offset + size) + "): HTTP " +
                            std::to_string(chunkResponse.statusCode);
            result.response = chunkResponse;
            return result;
        }

        result.bytesWritten += writtenInChunk;
    }

    result.success = true;
    result.response.statusCode = 206;
    result.response.contentLength = totalSize;
    result.response.contentRangeTotal = totalSize;
    result.response.acceptRanges = true;
    result.response.effectiveUrl = url;
    return result;
}

DownloadResult Downloader::downloadParallel(const std::string& url,
                                             const std::string& outputPath,
                                             std::int64_t chunkSize,
                                             std::size_t numWorkers,
                                             CancelToken* cancelToken,
                                             std::size_t maxRetries,
                                             const std::string& expectedSha256,
                                             RateLimiter* rateLimiter) {
    DownloadResult result;

    if (chunkSize <= 0 || numWorkers == 0) {
        result.error = "chunkSize and numWorkers must be positive";
        return result;
    }

    HttpRequest probeRequest;
    probeRequest.url = url;
    probeRequest.headers["Range"] = "bytes=0-0";
    WriteCallback discard = [](const char*, std::size_t) { return true; };
    const HttpResponse probe = httpClient_.perform(probeRequest, discard);

    if (probe.statusCode != 206 || probe.contentRangeTotal <= 0) {
        return downloadToFile(url, outputPath); // server doesn't support Range — fall back
    }

    const std::int64_t totalSize = probe.contentRangeTotal;
    if (!FileWriter::preallocate(outputPath, totalSize)) {
        result.error = "failed to preallocate the file: " + outputPath;
        return result;
    }

    FileWriter writer(outputPath);
    ChunkQueue chunkQueue(totalSize, chunkSize, numWorkers);

    std::atomic<std::int64_t> bytesDone{0};
    std::atomic<bool> anyFailed{false};
    std::mutex errorMutex;
    std::string firstError;

    std::atomic<std::size_t> remaining{numWorkers};
    std::mutex doneMutex;
    std::condition_variable doneCv;

    // Downloads one range (with retries), writes the result into the shared
    // counters. Pulled out into a lambda because ranges are no longer fixed
    // up front: a worker may get either its "own" chunk or a half stolen by
    // ChunkQueue from someone else's — the download logic is the same either way.
    auto downloadOneRange = [&](const ChunkRange& range) {
        HttpRequest chunkRequest;
        chunkRequest.url = url;
        chunkRequest.headers["Range"] =
            "bytes=" + std::to_string(range.start) + "-" + std::to_string(range.end - 1);

        HttpResponse chunkResponse;
        bool chunkOk = false;
        std::int64_t writtenInChunk = 0;
        const std::int64_t expectedSize = range.size();

        for (std::size_t attempt = 0; attempt <= maxRetries; ++attempt) {
            if (cancelToken && cancelToken->shouldAbortTransfer()) {
                break; // no need to retry a pause/cancel
            }

            writtenInChunk = 0;
            bool writeFailed = false;
            WriteCallback onData = [&](const char* data, std::size_t n) {
                if (rateLimiter) {
                    rateLimiter->acquire(n);
                }
                if (!writer.writeAt(range.start + writtenInChunk, data, n)) {
                    writeFailed = true;
                    return false;
                }
                writtenInChunk += static_cast<std::int64_t>(n);
                return true;
            };

            chunkResponse = httpClient_.perform(chunkRequest, onData, cancelToken);
            chunkOk = chunkResponse.statusCode == 206 && !writeFailed && writtenInChunk == expectedSize;

            if (chunkOk) {
                break;
            }
            if (attempt < maxRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200 << attempt));
            }
        }

        if (!chunkOk) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (firstError.empty()) {
                firstError = "range error [" + std::to_string(range.start) + ", " +
                    std::to_string(range.end) + "): HTTP " + std::to_string(chunkResponse.statusCode);
            }
            anyFailed = true;
        } else {
            bytesDone += writtenInChunk;
        }
    };

    {
        ThreadPool pool(numWorkers);
        for (std::size_t w = 0; w < numWorkers; ++w) {
            pool.enqueue([&] {
                while (!anyFailed.load() && !(cancelToken && cancelToken->shouldAbortTransfer())) {
                    // ChunkQueue itself makes sure the queue never holds fewer
                    // than numWorkers ranges (splitting the biggest remaining
                    // one in half as needed) — see ChunkQueue.hpp.
                    const auto rangeOpt = chunkQueue.pop();

                    if (!rangeOpt) {
                        break; // no ranges left (even splittable ones)
                    }
                    downloadOneRange(*rangeOpt);
                }

                // The decrement and notify must happen under doneMutex:
                // otherwise a classic lost wakeup is possible — a worker
                // manages to zero out remaining and fire notify_one() in
                // exactly the window between the main thread checking the
                // predicate (still false) and it actually going to sleep on
                // the cv, so the notify is lost with no one left to wake it.
                // Under TSan this window widens, from the instrumentation
                // overhead, by just enough to make the race observable
                // (hence the hang happening exactly there and only there,
                // with no race reported — this is not a data race but an
                // ordering/synchronization bug).
                std::lock_guard<std::mutex> doneLock(doneMutex);
                if (--remaining == 0) { doneCv.notify_one(); }
            });
        }

        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return remaining.load() == 0; });
    } // ~ThreadPool() has already joined all threads

    if (anyFailed) {
        result.error = (cancelToken && cancelToken->isCancelled()) ? "download cancelled"
                      : (cancelToken && cancelToken->isPaused())   ? "download paused"
                                                                    : firstError;
        return result;
    }

    result.success = true;
    result.bytesWritten = bytesDone.load();
    result.response.statusCode = 206;
    result.response.contentLength = totalSize;
    result.response.contentRangeTotal = totalSize;
    result.response.acceptRanges = true;
    result.response.effectiveUrl = url;
    verifySha256(result, outputPath, expectedSha256);
    return result;
}

DownloadResult Downloader::downloadResumable(const std::string& url,
                                              const std::string& outputPath,
                                              std::int64_t chunkSize,
                                              std::size_t numWorkers,
                                              CancelToken* cancelToken,
                                              std::size_t maxRetries,
                                              const std::string& expectedSha256,
                                              RateLimiter* rateLimiter,
                                              std::atomic<std::int64_t>* progressBytes,
                                              std::atomic<std::int64_t>* progressTotalBytes) {
    DownloadResult result;

    if (chunkSize <= 0 || numWorkers == 0) {
        result.error = "chunkSize and numWorkers must be positive";
        return result;
    }

    const std::string metaPath = MetaFile::pathFor(outputPath);
    DownloadMeta meta;
    bool resuming = false;

    // Try to resume an existing download.
    if (MetaFile::load(metaPath, meta) && meta.url == url && meta.chunkSize == chunkSize
        && meta.totalSize > 0) {
        HttpRequest probeRequest;
        probeRequest.url = url;
        probeRequest.headers["Range"] = "bytes=0-0";
        if (!meta.etag.empty()) {
            probeRequest.headers["If-Range"] = meta.etag;
        } else if (!meta.lastModified.empty()) {
            probeRequest.headers["If-Range"] = meta.lastModified;
        }
        WriteCallback discard = [](const char*, std::size_t) { return true; };
        const HttpResponse probe = httpClient_.perform(probeRequest, discard, cancelToken);

        // 206 + the same size = the file on the server hasn't changed, resume.
        // 200 (If-Range didn't match) or a different size = the file changed —
        // start over (meta is recreated below).
        resuming = probe.statusCode == 206 && probe.contentRangeTotal == meta.totalSize;
    }

    if (!resuming) {
        HttpRequest probeRequest;
        probeRequest.url = url;
        probeRequest.headers["Range"] = "bytes=0-0";
        WriteCallback discard = [](const char*, std::size_t) { return true; };
        const HttpResponse probe = httpClient_.perform(probeRequest, discard, cancelToken);

        if (probe.statusCode != 206 || probe.contentRangeTotal <= 0) {
            MetaFile::remove(metaPath);
            return downloadToFile(url, outputPath); // server doesn't support Range at all
        }

        meta = DownloadMeta{};
        meta.url = url;
        meta.totalSize = probe.contentRangeTotal;
        meta.chunkSize = chunkSize;
        meta.etag = probe.etag;
        meta.lastModified = probe.lastModified;

        const std::size_t numChunks =
            static_cast<std::size_t>((meta.totalSize + chunkSize - 1) / chunkSize);
        meta.chunkDone.assign(numChunks, false);

        if (!FileWriter::preallocate(outputPath, meta.totalSize)) {
            result.error = "failed to preallocate the file: " + outputPath;
            return result;
        }
        if (!MetaFile::save(metaPath, meta)) {
            result.error = "failed to save the meta file: " + metaPath;
            return result;
        }
    }

    if (progressTotalBytes) {
        progressTotalBytes->store(meta.totalSize);
    }

    // Build the list of missing chunks + count the bytes already done.
    std::vector<ChunkSpec> pending;
    std::atomic<std::int64_t> bytesDone{0};
    for (std::size_t i = 0; i < meta.chunkDone.size(); ++i) {
        const std::int64_t offset = static_cast<std::int64_t>(i) * meta.chunkSize;
        const std::int64_t size = std::min(meta.chunkSize, meta.totalSize - offset);
        if (meta.chunkDone[i]) {
            bytesDone += size;
        } else {
            pending.push_back({offset, size});
        }
    }
    if (progressBytes) {
        progressBytes->store(bytesDone.load());
    }

    if (pending.empty()) {
        MetaFile::remove(metaPath);
        result.success = true;
        result.bytesWritten = meta.totalSize;
        result.response.statusCode = 206;
        result.response.contentLength = meta.totalSize;
        result.response.acceptRanges = true;
        result.response.effectiveUrl = url;
        verifySha256(result, outputPath, expectedSha256);
        return result;
    }

    FileWriter writer(outputPath);
    std::atomic<bool> anyFailed{false};
    std::mutex errorMutex;
    std::string firstError;

    std::mutex metaMutex; // protects meta.chunkDone + saving the meta file

    std::atomic<std::size_t> remaining{pending.size()};
    std::mutex doneMutex;
    std::condition_variable doneCv;

    {
        ThreadPool pool(numWorkers);
        for (const ChunkSpec& chunk : pending) {
            pool.enqueue([&, chunk] {
                if (anyFailed.load() || (cancelToken && cancelToken->shouldAbortTransfer())) {
                    // See the comment at the equivalent spot in downloadParallel:
                    // the decrement + notify must happen under doneMutex,
                    // otherwise a lost wakeup.
                    std::lock_guard<std::mutex> doneLock(doneMutex);
                    if (--remaining == 0) { doneCv.notify_one(); }
                    return;
                }

                HttpRequest chunkRequest;
                chunkRequest.url = url;
                chunkRequest.headers["Range"] = "bytes=" + std::to_string(chunk.offset) + "-" +
                    std::to_string(chunk.offset + chunk.size - 1);

                HttpResponse chunkResponse;
                bool chunkOk = false;
                std::int64_t writtenInChunk = 0;

                for (std::size_t attempt = 0; attempt <= maxRetries; ++attempt) {
                    if (cancelToken && cancelToken->shouldAbortTransfer()) {
                        break; // no need to retry a pause/cancel
                    }

                    writtenInChunk = 0;
                    bool writeFailed = false;
                    WriteCallback onData = [&](const char* data, std::size_t n) {
                        if (rateLimiter) {
                            rateLimiter->acquire(n);
                        }
                        if (!writer.writeAt(chunk.offset + writtenInChunk, data, n)) {
                            writeFailed = true;
                            return false;
                        }
                        writtenInChunk += static_cast<std::int64_t>(n);
                        // Updated live, as bytes actually land on disk —
                        // not just once the whole chunk finishes — so a
                        // caller polling progressBytes (e.g. a GUI) sees
                        // smooth progress even with few, large chunks
                        // instead of it jumping only at each chunk
                        // boundary. A retried chunk can double-count the
                        // bytes from its failed attempt; that's an
                        // acceptable, purely cosmetic approximation for a
                        // progress indicator, not the authoritative byte
                        // count (bytesDone/the final DownloadResult are
                        // unaffected — they're only touched below, once
                        // per chunk, after a chunk fully succeeds).
                        if (progressBytes) {
                            progressBytes->fetch_add(static_cast<std::int64_t>(n));
                        }
                        return true;
                    };

                    chunkResponse = httpClient_.perform(chunkRequest, onData, cancelToken);
                    chunkOk = chunkResponse.statusCode == 206 && !writeFailed &&
                              writtenInChunk == chunk.size;

                    if (chunkOk) {
                        break;
                    }
                    if (attempt < maxRetries) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200 << attempt));
                    }
                }

                if (!chunkOk) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (firstError.empty()) {
                        firstError = "chunk error [" + std::to_string(chunk.offset) + ", " +
                            std::to_string(chunk.offset + chunk.size) + "): HTTP " +
                            std::to_string(chunkResponse.statusCode);
                    }
                    anyFailed = true;
                } else {
                    bytesDone += writtenInChunk;
                    // Mark the chunk done and save the meta file — so progress
                    // isn't lost if the download is interrupted partway through.
                    std::lock_guard<std::mutex> lock(metaMutex);
                    const std::size_t idx =
                        static_cast<std::size_t>(chunk.offset / meta.chunkSize);
                    meta.chunkDone[idx] = true;
                    MetaFile::save(MetaFile::pathFor(outputPath), meta);
                }

                {
                    std::lock_guard<std::mutex> doneLock(doneMutex);
                    if (--remaining == 0) { doneCv.notify_one(); }
                }
            });
        }

        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return remaining.load() == 0; });
    }

    if (anyFailed) {
        // The meta file already reflects what's actually been downloaded —
        // the next downloadResumable() call will continue from exactly here.
        result.bytesWritten = bytesDone.load();
        result.error = (cancelToken && cancelToken->isCancelled()) ? "download cancelled"
                      : (cancelToken && cancelToken->isPaused())   ? "download paused"
                                                                    : firstError;
        return result;
    }

    MetaFile::remove(metaPath); // everything downloaded — the meta file is no longer needed
    result.success = true;
    result.bytesWritten = bytesDone.load();
    result.response.statusCode = 206;
    result.response.contentLength = meta.totalSize;
    result.response.contentRangeTotal = meta.totalSize;
    result.response.acceptRanges = true;
    result.response.effectiveUrl = url;
    verifySha256(result, outputPath, expectedSha256);
    return result;
}


} // namespace dlm
