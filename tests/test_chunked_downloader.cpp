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
    // Не просто "все нули" — чтобы ошибка в offset'ах не осталась незамеченной.
    std::string body(size, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        body[i] = static_cast<char>('A' + (i % 26));
    }
    return body;
}

} // namespace

int main() {
    // Размер не кратен размеру чанка — проверяем и обычные, и последний
    // (укороченный) чанк.
    const std::string body = makeBody(10 * 1000 + 123);
    const std::int64_t chunkSize = 4000;

    // 1) Чанки бьются ровно так, как договорились: несколько Range-запросов,
    //    запись по offset, без склейки временных файлов.
    {
        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/777);
        dlm::Downloader downloader(fakeClient);

        const std::string outputPath = "test_chunked_output.tmp";
        const dlm::DownloadResult result =
            downloader.downloadChunked("http://example.invalid/file", outputPath, chunkSize);

        assert(result.success);
        assert(result.bytesWritten == static_cast<std::int64_t>(body.size()));
        assert(readFile(outputPath) == body);

        std::remove(outputPath.c_str());
    }

    // 2) Сверка с обычной (Этап 1) загрузкой — файлы должны совпасть побайтово.
    {
        dlm_test::FakeHttpClient fakeClientChunked(body, /*acceptRanges=*/true);
        dlm::Downloader chunkedDownloader(fakeClientChunked);
        const std::string chunkedPath = "test_chunked_vs_plain_chunked.tmp";
        const dlm::DownloadResult chunkedResult =
            chunkedDownloader.downloadChunked("http://example.invalid/file", chunkedPath, chunkSize);
        assert(chunkedResult.success);

        dlm_test::FakeHttpClient fakeClientPlain(body, /*acceptRanges=*/true);
        dlm::Downloader plainDownloader(fakeClientPlain);
        const std::string plainPath = "test_chunked_vs_plain_plain.tmp";
        const dlm::DownloadResult plainResult =
            plainDownloader.downloadToFile("http://example.invalid/file", plainPath);
        assert(plainResult.success);

        assert(readFile(chunkedPath) == readFile(plainPath));

        std::remove(chunkedPath.c_str());
        std::remove(plainPath.c_str());
    }

    // 3) Сервер без поддержки Range — откат на downloadToFile, файл всё
    //    равно должен получиться корректным.
    {
        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/false);
        dlm::Downloader downloader(fakeClient);

        const std::string outputPath = "test_chunked_fallback.tmp";
        const dlm::DownloadResult result =
            downloader.downloadChunked("http://example.invalid/file", outputPath, chunkSize);

        assert(result.success);
        assert(readFile(outputPath) == body);

        std::remove(outputPath.c_str());
    }

    std::printf("test_chunked_downloader: OK\n");
    return 0;
}
