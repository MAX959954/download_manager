#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dlm {

// Возвращает false, чтобы прервать передачу (см. CURLOPT_WRITEFUNCTION).
using WriteCallback = std::function<bool(const char* data, std::size_t size)>;

struct HttpRequest {
    std::string url;
    std::map<std::string, std::string> headers;
};

struct HttpResponse {
    long statusCode = 0;
    std::int64_t contentLength = -1;
    std::int64_t contentRangeTotal = -1;
    bool acceptRanges = false;
    std::string etag;
    std::string lastModified;
    std::string effectiveUrl;
};

struct DownloadResult {
    HttpResponse response;
    std::int64_t bytesWritten = 0;
    bool success = false;
    std::string error;
    std::string sha256; // заполняется, если скачивание дошло до финальной проверки
};

struct ChunkSpec {
    std::int64_t offset = 0;
    std::int64_t size = 0;
};

struct DownloadMeta {
    std::string url;
    std::int64_t chunkSize = 0;
    std::int64_t totalSize = 0;
    std::string etag;
    std::string lastModified;
    std::vector<bool> chunkDone;
};

} // namespace dlm
