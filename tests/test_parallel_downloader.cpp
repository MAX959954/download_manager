#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <dlm/Downloader.hpp>

#include "FakeHttpClient.hpp"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string makeBody(std::size_t size) {
    std::string body(size, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        body[i] = static_cast<char>('A' + (i % 26));
    }
    return body;
}

} // namespace

int main() {
    // Размер не кратен размеру чанка — проверяем и обычные, и последний
    // (укороченный) чанк, скачанный параллельно несколькими воркерами.
    const std::string body = makeBody(50 * 1000 + 777);
    const std::int64_t chunkSize = 4000;

    // 1) Параллельная загрузка даёт побайтово тот же результат, что и
    //    обычная (Этап 1).
    {
        dlm_test::FakeHttpClient fakeClientParallel(body, /*acceptRanges=*/true, /*writeChunkSize=*/500);
        dlm::Downloader parallelDownloader(fakeClientParallel);
        const std::string parallelPath = "test_parallel_output.tmp";
        const dlm::DownloadResult parallelResult =
            parallelDownloader.downloadParallel("http://example.invalid/file", parallelPath,
                                                 chunkSize, /*numWorkers=*/8);

        assert(parallelResult.success);
        assert(parallelResult.bytesWritten == static_cast<std::int64_t>(body.size()));
        assert(readFile(parallelPath) == body);

        std::remove(parallelPath.c_str());
    }

    // 2) Сервер без поддержки Range — фолбэк на downloadToFile.
    {
        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/false);
        dlm::Downloader downloader(fakeClient);

        const std::string outputPath = "test_parallel_fallback.tmp";
        const dlm::DownloadResult result =
            downloader.downloadParallel("http://example.invalid/file", outputPath, chunkSize, 8);

        assert(result.success);
        assert(readFile(outputPath) == body);

        std::remove(outputPath.c_str());
    }

    std::printf("test_parallel_downloader: OK\n");
    return 0;
}
