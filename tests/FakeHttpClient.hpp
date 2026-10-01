#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <dlm/IHttpClient.hpp>

namespace dlm_test {

// A test double for IHttpClient with Range and If-Range support, without
// touching the network.
// - acceptRanges = true: responds to a request with a Range header with 206
//   and a piece of the body, setting Content-Range (via contentRangeTotal),
//   like a real server that supports resuming downloads. Also always
//   returns etag_ in HttpResponse::etag (the way the real CurlHttpClient
//   parses the ETag header).
// - acceptRanges = false: ignores Range and always returns 200 with the
//   whole body — the way a server without Range support behaves.
// - writeChunkSize: if > 0, the chunk body is delivered to onData in
//   several calls of writeChunkSize bytes each instead of one — the way
//   libcurl sometimes does, to verify that writes at an offset within the
//   chunk accumulate correctly.
// - failIfRangeMismatch: if true and the request has an If-Range header
//   that differs from etag_, the server "changes its mind" and returns 200
//   with the whole body — the way a real server signals "the file changed,
//   resuming is not possible".
// - rangeRequestCount counts how many times a request with Range arrived —
//   tests need this to verify that already-completed chunks are not
//   downloaded again.
class FakeHttpClient final : public dlm::IHttpClient {
public:
    explicit FakeHttpClient(std::string body, bool acceptRanges = true,
                             std::size_t writeChunkSize = 0, std::string etag = "",
                             bool failIfRangeMismatch = false,
                             std::chrono::microseconds delayPerPiece = std::chrono::microseconds(0))
        : body_(std::move(body)), acceptRanges_(acceptRanges),
          writeChunkSize_(writeChunkSize), etag_(std::move(etag)),
          failIfRangeMismatch_(failIfRangeMismatch), delayPerPiece_(delayPerPiece) {}

    dlm::HttpResponse perform(const dlm::HttpRequest& request, const dlm::WriteCallback& onData,
                                                    const dlm::CancelToken* cancelToken = nullptr) override {
        dlm::HttpResponse response;
        response.effectiveUrl = request.url;
        response.acceptRanges = acceptRanges_;
        response.etag = etag_;

        const auto rangeIt = request.headers.find("Range");
        if (rangeIt == request.headers.end() || !acceptRanges_) {
            response.statusCode = 200;
            response.contentLength = static_cast<std::int64_t>(body_.size());
            if (!deliver(body_.data(), body_.size(), onData, cancelToken)) {
                response.statusCode = 0;
            }
            return response;
        }

        ++rangeRequestCount;

        const auto ifRangeIt = request.headers.find("If-Range");
        if (failIfRangeMismatch_ && ifRangeIt != request.headers.end() && ifRangeIt->second != etag_) {
            // The server ignores If-Range when it doesn't match and returns the whole file.
            response.statusCode = 200;
            response.contentLength = static_cast<std::int64_t>(body_.size());
            if (!deliver(body_.data(), body_.size(), onData, cancelToken)) {
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
        if (!deliver(body_.data() + start, static_cast<std::size_t>(len), onData, cancelToken)) {
            response.statusCode = 0;
        }
        return response;
    }

    std::atomic<int> rangeRequestCount{0};

private:
    bool deliver(const char* data, std::size_t size, const dlm::WriteCallback& onData,
                 const dlm::CancelToken* cancelToken) const {
        if (writeChunkSize_ == 0) {
            if (cancelToken && cancelToken->shouldAbortTransfer()) {
                return false;
            }
            return onData(data, size);
        }
        std::size_t sent = 0;
        while (sent < size) {
            if (cancelToken && cancelToken->shouldAbortTransfer()) {
                return false; // simulates CURLE_ABORTED_BY_CALLBACK
            }
            if (delayPerPiece_.count() > 0) {
                std::this_thread::sleep_for(delayPerPiece_);
            }
            const std::size_t piece = std::min(writeChunkSize_, size - sent);
            if (!onData(data + sent, piece)) {
                return false;
            }
            sent += piece;
        }
        return true;
    }

    static bool parseRange(const std::string& value, std::int64_t& start, std::int64_t& end) {
        // Expecting "bytes=start-end".
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
    std::string etag_;
    bool failIfRangeMismatch_;
    std::chrono::microseconds delayPerPiece_;
};

} // namespace dlm_test
