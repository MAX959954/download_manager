// Интеграционный тест: настоящий CurlHttpClient (после рефакторинга на общий
// detail::applyHeaderField) против живого сервера. Требует интернет.
#include <cassert>
#include <cstdio>
#include <string>

#include <dlm/CurlHttpClient.hpp>
#include <dlm/Downloader.hpp>

int main() {
    dlm::CurlHttpClient client;

    dlm::HttpRequest request;
    request.url = "https://raw.githubusercontent.com/octocat/Hello-World/master/README";
    request.headers["Range"] = "bytes=0-4";

    std::string body;
    const dlm::HttpResponse response = client.perform(request, [&](const char* data, std::size_t n) {
        body.append(data, n);
        return true;
    });

    std::printf("status=%ld bytes=%zu contentRangeTotal=%lld etag=%s body=\"%s\"\n", response.statusCode,
                 body.size(), static_cast<long long>(response.contentRangeTotal), response.etag.c_str(),
                 body.c_str());

    assert(response.statusCode == 206);
    assert(body == "Hello");
    assert(response.contentRangeTotal == 13);
    assert(response.acceptRanges);

    // И полный Downloader::downloadResumable поверх настоящего CurlHttpClient с SHA-256.
    dlm::Downloader downloader(client);
    const std::string outputPath = "test_curl_live_output.tmp";
    const dlm::DownloadResult result = downloader.downloadResumable(
        "https://raw.githubusercontent.com/nodejs/node/main/LICENSE", outputPath, 20000, 4, nullptr, 3,
        "37110192cd7621a80510e2f2630ae08f0420f97257d4ae1c4d51c11261f1f4b7");

    std::printf("downloadResumable: success=%d bytesWritten=%lld sha256=%s\n", result.success,
                 static_cast<long long>(result.bytesWritten), result.sha256.c_str());
    assert(result.success);
    std::remove(outputPath.c_str());

    std::printf("test_curl_http_client_live: OK\n");
    return 0;
}
