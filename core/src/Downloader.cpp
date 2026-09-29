#include "dlm/Downloader.hpp"
#include "dlm/FileWriter.hpp"
#include "dlm/MetaFile.hpp"
#include "dlm/ThreadPool.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <vector>

namespace dlm {

Downloader::Downloader(IHttpClient& httpClient) : httpClient_(httpClient) {}

DownloadResult Downloader::downloadToFile(const std::string& url, const std::string& outputPath) {
    DownloadResult result;

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "не удалось открыть файл для записи: " + outputPath;
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
        result.error = writeFailed ? ("ошибка записи в файл: " + outputPath)
                                    : ("HTTP статус " + std::to_string(result.response.statusCode));
    }

    return result;
}

DownloadResult Downloader::downloadChunked(const std::string& url,
                                            const std::string& outputPath,
                                            std::int64_t chunkSize) {
    DownloadResult result;

    if (chunkSize <= 0) {
        result.error = "chunkSize должен быть положительным";
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
        result.error = "не удалось преаллоцировать файл: " + outputPath;
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
            result.error = "ошибка чанка [" + std::to_string(offset) + ", " +
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
                                             std::size_t numWorkers) {
    DownloadResult result;

    if (chunkSize <= 0 || numWorkers == 0) {
        result.error = "chunkSize и numWorkers должны быть положительными";
        return result;
    }

    HttpRequest probeRequest;
    probeRequest.url = url;
    probeRequest.headers["Range"] = "bytes=0-0";
    WriteCallback discard = [](const char*, std::size_t) { return true; };
    const HttpResponse probe = httpClient_.perform(probeRequest, discard);

    if (probe.statusCode != 206 || probe.contentRangeTotal <= 0) {
        return downloadToFile(url, outputPath); // сервер без Range — фолбэк
    }

    const std::int64_t totalSize = probe.contentRangeTotal;
    if (!FileWriter::preallocate(outputPath, totalSize)) {
        result.error = "не удалось преаллоцировать файл: " + outputPath;
        return result;
    }

    std::vector<ChunkSpec> chunks;
    for (std::int64_t offset = 0; offset < totalSize; offset += chunkSize) {
        chunks.push_back({offset, std::min(chunkSize, totalSize - offset)});
    }

    FileWriter writer(outputPath);
    std::atomic<std::int64_t> bytesDone{0};
    std::atomic<bool> anyFailed{false};
    std::mutex errorMutex;
    std::string firstError;

    std::atomic<std::size_t> remaining{chunks.size()};
    std::mutex doneMutex;
    std::condition_variable doneCv;

    {
        ThreadPool pool(numWorkers);
        for (const ChunkSpec& chunk : chunks) {
            pool.enqueue([&, chunk] {
                if (anyFailed.load()) {
                    if (--remaining == 0) { doneCv.notify_one(); }
                    return;
                }

                HttpRequest chunkRequest;
                chunkRequest.url = url;
                chunkRequest.headers["Range"] = "bytes=" + std::to_string(chunk.offset) + "-" +
                    std::to_string(chunk.offset + chunk.size - 1);

                std::int64_t writtenInChunk = 0;
                bool writeFailed = false;
                WriteCallback onData = [&](const char* data, std::size_t n) {
                    if (!writer.writeAt(chunk.offset + writtenInChunk, data, n)) {
                        writeFailed = true;
                        return false;
                    }
                    writtenInChunk += static_cast<std::int64_t>(n);
                    return true;
                };

                const HttpResponse chunkResponse = httpClient_.perform(chunkRequest, onData);
                const bool chunkOk = chunkResponse.statusCode == 206 && !writeFailed &&
                                      writtenInChunk == chunk.size;

                if (!chunkOk) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (firstError.empty()) {
                        firstError = "ошибка чанка [" + std::to_string(chunk.offset) + ", " +
                            std::to_string(chunk.offset + chunk.size) + "): HTTP " +
                            std::to_string(chunkResponse.statusCode);
                    }
                    anyFailed = true;
                } else {
                    bytesDone += writtenInChunk;
                }

                if (--remaining == 0) { doneCv.notify_one(); }
            });
        }

        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return remaining.load() == 0; });
    } // ~ThreadPool() уже присоединил все потоки

    if (anyFailed) {
        result.error = firstError;
        return result;
    }

    result.success = true;
    result.bytesWritten = bytesDone.load();
    result.response.statusCode = 206;
    result.response.contentLength = totalSize;
    result.response.contentRangeTotal = totalSize;
    result.response.acceptRanges = true;
    result.response.effectiveUrl = url;
    return result;
}

DownloadResult Downloader::downloadResumable(const std::string& url,
                                              const std::string& outputPath,
                                              std::int64_t chunkSize,
                                              std::size_t numWorkers) {
    DownloadResult result;

    if (chunkSize <= 0 || numWorkers == 0) {
        result.error = "chunkSize и numWorkers должны быть положительными";
        return result;
    }

    const std::string metaPath = MetaFile::pathFor(outputPath);
    DownloadMeta meta;
    bool resuming = false;

    // Пытаемся продолжить существующую закачку.
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
        const HttpResponse probe = httpClient_.perform(probeRequest, discard);

        // 206 + тот же размер = файл на сервере не менялся, докачиваем.
        // 200 (If-Range не совпал) или другой размер = файл сменился —
        // начинаем заново (meta ниже будет пересоздан).
        resuming = probe.statusCode == 206 && probe.contentRangeTotal == meta.totalSize;
    }

    if (!resuming) {
        HttpRequest probeRequest;
        probeRequest.url = url;
        probeRequest.headers["Range"] = "bytes=0-0";
        WriteCallback discard = [](const char*, std::size_t) { return true; };
        const HttpResponse probe = httpClient_.perform(probeRequest, discard);

        if (probe.statusCode != 206 || probe.contentRangeTotal <= 0) {
            MetaFile::remove(metaPath);
            return downloadToFile(url, outputPath); // сервер вообще не поддерживает Range
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
            result.error = "не удалось преаллоцировать файл: " + outputPath;
            return result;
        }
        if (!MetaFile::save(metaPath, meta)) {
            result.error = "не удалось сохранить метафайл: " + metaPath;
            return result;
        }
    }

    // Собираем список недостающих чанков + считаем уже готовые байты.
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

    if (pending.empty()) {
        MetaFile::remove(metaPath);
        result.success = true;
        result.bytesWritten = meta.totalSize;
        result.response.statusCode = 206;
        result.response.contentLength = meta.totalSize;
        result.response.acceptRanges = true;
        result.response.effectiveUrl = url;
        return result;
    }

    FileWriter writer(outputPath);
    std::atomic<bool> anyFailed{false};
    std::mutex errorMutex;
    std::string firstError;

    std::mutex metaMutex; // защищает meta.chunkDone + сохранение метафайла

    std::atomic<std::size_t> remaining{pending.size()};
    std::mutex doneMutex;
    std::condition_variable doneCv;

    {
        ThreadPool pool(numWorkers);
        for (const ChunkSpec& chunk : pending) {
            pool.enqueue([&, chunk] {
                if (anyFailed.load()) {
                    if (--remaining == 0) { doneCv.notify_one(); }
                    return;
                }

                HttpRequest chunkRequest;
                chunkRequest.url = url;
                chunkRequest.headers["Range"] = "bytes=" + std::to_string(chunk.offset) + "-" +
                    std::to_string(chunk.offset + chunk.size - 1);

                std::int64_t writtenInChunk = 0;
                bool writeFailed = false;
                WriteCallback onData = [&](const char* data, std::size_t n) {
                    if (!writer.writeAt(chunk.offset + writtenInChunk, data, n)) {
                        writeFailed = true;
                        return false;
                    }
                    writtenInChunk += static_cast<std::int64_t>(n);
                    return true;
                };

                const HttpResponse chunkResponse = httpClient_.perform(chunkRequest, onData);
                const bool chunkOk = chunkResponse.statusCode == 206 && !writeFailed &&
                                      writtenInChunk == chunk.size;

                if (!chunkOk) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (firstError.empty()) {
                        firstError = "ошибка чанка [" + std::to_string(chunk.offset) + ", " +
                            std::to_string(chunk.offset + chunk.size) + "): HTTP " +
                            std::to_string(chunkResponse.statusCode);
                    }
                    anyFailed = true;
                } else {
                    bytesDone += writtenInChunk;
                    // Отмечаем чанк готовым и сохраняем метафайл — так при
                    // обрыве посреди закачки прогресс не теряется.
                    std::lock_guard<std::mutex> lock(metaMutex);
                    const std::size_t idx =
                        static_cast<std::size_t>(chunk.offset / meta.chunkSize);
                    meta.chunkDone[idx] = true;
                    MetaFile::save(MetaFile::pathFor(outputPath), meta);
                }

                if (--remaining == 0) { doneCv.notify_one(); }
            });
        }

        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&] { return remaining.load() == 0; });
    }

    if (anyFailed) {
        // Метафайл уже отражает то, что реально докачано — следующий вызов
        // downloadResumable() продолжит именно с этого места.
        result.error = firstError;
        result.bytesWritten = bytesDone.load();
        return result;
    }

    MetaFile::remove(metaPath); // всё скачано — метафайл больше не нужен
    result.success = true;
    result.bytesWritten = bytesDone.load();
    result.response.statusCode = 206;
    result.response.contentLength = meta.totalSize;
    result.response.contentRangeTotal = meta.totalSize;
    result.response.acceptRanges = true;
    result.response.effectiveUrl = url;
    return result;
}

} // namespace dlm
