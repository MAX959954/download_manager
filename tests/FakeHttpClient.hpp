#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include <dlm/IHttpClient.hpp>

namespace dlm_test {

// Тестовый двойник IHttpClient с поддержкой Range, без обращения к сети.
// - acceptRanges = true: на запрос с заголовком Range отвечает 206 и куском
//   тела, выставляя Content-Range (через contentRangeTotal), как реальный
//   сервер, поддерживающий докачку.
// - acceptRanges = false: игнорирует Range и всегда отдаёт 200 + тело
//   целиком — так ведёт себя сервер без поддержки Range.
// - writeChunkSize: если > 0, тело чанка отдаётся onData несколькими
//   вызовами по writeChunkSize байт вместо одного — так же, как это
//   иногда делает libcurl, чтобы проверить, что запись по смещению внутри
//   чанка накапливается правильно (см. Downloader::downloadChunked).
class FakeHttpClient final : public dlm::IHttpClient {
public:
    explicit FakeHttpClient(std::string body, bool acceptRanges = true, std::size_t writeChunkSize = 0)
        : body_(std::move(body)), acceptRanges_(acceptRanges), writeChunkSize_(writeChunkSize) {}

    dlm::HttpResponse perform(const dlm::HttpRequest& request, const dlm::WriteCallback& onData) override {
        dlm::HttpResponse response;
        response.effectiveUrl = request.url;
        response.acceptRanges = acceptRanges_;

        const auto rangeIt = request.headers.find("Range");
        if (rangeIt == request.headers.end() || !acceptRanges_) {
            response.statusCode = 200;
            response.contentLength = static_cast<std::int64_t>(body_.size());
            if (!deliver(body_.data(), body_.size(), onData)) {
                response.statusCode = 0;
            }
            return response;
        }

        std::int64_t start = 0;
        std::int64_t end = 0;
        if (!parseRange(rangeIt->second, start, end)) {
            response.statusCode = 416; // Range Not Satisfiable
            return response;
        }
        end = std::min<std::int64_t>(end, static_cast<std::int64_t>(body_.size()) - 1);
        const std::int64_t len = end - start + 1;
        if (len <= 0) {
            response.statusCode = 416;
            return response;
        }

        response.statusCode = 206;
        response.contentLength = len;
        response.contentRangeTotal = static_cast<std::int64_t>(body_.size());
        if (!deliver(body_.data() + start, static_cast<std::size_t>(len), onData)) {
            response.statusCode = 0;
        }
        return response;
    }

private:
    bool deliver(const char* data, std::size_t size, const dlm::WriteCallback& onData) const {
        if (writeChunkSize_ == 0) {
            return onData(data, size);
        }
        std::size_t sent = 0;
        while (sent < size) {
            const std::size_t piece = std::min(writeChunkSize_, size - sent);
            if (!onData(data + sent, piece)) {
                return false;
            }
            sent += piece;
        }
        return true;
    }

    static bool parseRange(const std::string& value, std::int64_t& start, std::int64_t& end) {
        // Ожидаем "bytes=start-end".
        const auto eq = value.find('=');
        const auto dash = value.find('-', eq == std::string::npos ? 0 : eq);
        if (eq == std::string::npos || dash == std::string::npos) {
            return false;
        }
        try {
            start = std::stoll(value.substr(eq + 1, dash - eq - 1));
            end = std::stoll(value.substr(dash + 1));
        } catch (const std::exception&) {
            return false;
        }
        return start >= 0 && end >= start;
    }

    std::string body_;
    bool acceptRanges_;
    std::size_t writeChunkSize_;
};

} // namespace dlm_test
