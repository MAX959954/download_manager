// Integration test: Downloader + a real SocketHttpClient against a live
// server (GitHub raw). Requires internet access — unlike the other tests,
// which use FakeHttpClient, this one is not meant for offline/CI without
// network access.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <dlm/Downloader.hpp>
#include <dlm/SocketHttpClient.hpp>

namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

int main() {
    const std::string url = "https://raw.githubusercontent.com/nodejs/node/main/LICENSE";
    // The file's hash, known ahead of time (verified separately via curl+sha256sum).
    const std::string expectedSha256 =
        "37110192cd7621a80510e2f2630ae08f0420f97257d4ae1c4d51c11261f1f4b7";

    dlm::SocketHttpClient httpClient;
    dlm::Downloader downloader(httpClient);

    const std::string outputPath = "test_downloader_live_output.tmp";
    const dlm::DownloadResult result = downloader.downloadParallel(
        url, outputPath, /*chunkSize=*/20000, /*numWorkers=*/4,
        /*cancelToken=*/nullptr, /*maxRetries=*/3, expectedSha256);

    std::printf("success=%d error=\"%s\" bytesWritten=%lld sha256=%s\n", result.success,
                 result.error.c_str(), static_cast<long long>(result.bytesWritten),
                 result.sha256.c_str());

    assert(result.success);
    assert(result.sha256 == expectedSha256);
    assert(readFile(outputPath).size() == 154607);

    std::remove(outputPath.c_str());

    std::printf("test_downloader_live: OK — our own HTTP+TLS client on raw sockets\n"
                 "downloaded a real file over HTTPS with several parallel Range requests,\n"
                 "and its SHA-256 matched the reference value.\n");
    return 0;
}
