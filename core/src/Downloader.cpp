#include "dlm/Downloader.hpp"
#include "dlm/FileWriter.hpp"

#include <algorithm>
#include <fstream>

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

} // namespace dlm
