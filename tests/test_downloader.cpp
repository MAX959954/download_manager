#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <dlm/Downloader.hpp>
#include <dlm/IHttpClient.hpp>

namespace {

// Тестовый двойник IHttpClient: отдаёт заранее заданное тело без обращения к сети.
class FakeHttpClient final : public dlm::IHttpClient {
public:
    explicit FakeHttpClient(std::string body, long statusCode = 200, bool acceptRanges = true)
        : body_(std::move(body)), statusCode_(statusCode), acceptRanges_(acceptRanges) {}

    dlm::HttpResponse perform(const dlm::HttpRequest& request, const dlm::WriteCallback& onData) override {
        dlm::HttpResponse response;
        response.statusCode = statusCode_;
        response.contentLength = static_cast<std::int64_t>(body_.size());
        response.acceptRanges = acceptRanges_;
        response.effectiveUrl = request.url;

        if (!onData(body_.data(), body_.size())) {
            response.statusCode = 0;
        }
        return response;
    }

private:
    std::string body_;
    long statusCode_;
    bool acceptRanges_;
};

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main() {
    const std::string body = "hello, download manager!";
    FakeHttpClient fakeClient(body);
    dlm::Downloader downloader(fakeClient);

    const std::string outputPath = "test_downloader_output.tmp";
    const dlm::DownloadResult result = downloader.downloadToFile("http://example.invalid/file", outputPath);

    assert(result.success);
    assert(result.bytesWritten == static_cast<std::int64_t>(body.size()));
    assert(result.response.acceptRanges);
    assert(readFile(outputPath) == body);

    std::remove(outputPath.c_str());

    std::printf("test_downloader: OK\n");
    return 0;
}
