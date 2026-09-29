#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <dlm/Downloader.hpp>
#include <dlm/FileWriter.hpp>
#include <dlm/MetaFile.hpp>

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

bool fileExists(const std::string& path) {
    std::ifstream in(path);
    return static_cast<bool>(in);
}

} // namespace

int main() {
    const std::string body = makeBody(20 * 1000 + 321);
    const std::int64_t chunkSize = 4000;
    const std::string etag = "\"v1-abc\"";
    const std::string url = "http://example.invalid/file";

    // 1) Свежая докачиваемая загрузка: качается целиком, метафайл в конце удаляется.
    {
        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/500, etag);
        dlm::Downloader downloader(fakeClient);

        const std::string outputPath = "test_resumable_fresh.tmp";
        const std::string metaPath = dlm::MetaFile::pathFor(outputPath);

        const dlm::DownloadResult result = downloader.downloadResumable(url, outputPath, chunkSize, 4);

        assert(result.success);
        assert(readFile(outputPath) == body);
        assert(!fileExists(metaPath)); // метафайл убран после успешного завершения

        std::remove(outputPath.c_str());
    }

    // 2) Продолжение прерванной загрузки: первые два чанка уже "скачаны" и
    //    отмечены в метафайле — downloadResumable должен запросить только
    //    оставшиеся и в сумме дать побайтово верный файл.
    {
        const std::string outputPath = "test_resumable_partial.tmp";
        const std::string metaPath = dlm::MetaFile::pathFor(outputPath);

        const std::int64_t totalSize = static_cast<std::int64_t>(body.size());
        const std::size_t numChunks =
            static_cast<std::size_t>((totalSize + chunkSize - 1) / chunkSize);

        // Преаллоцируем файл и вручную "докачиваем" первые 2 чанка заранее,
        // как будто предыдущий запуск успел скачать только их.
        assert(dlm::FileWriter::preallocate(outputPath, totalSize));
        dlm::FileWriter writer(outputPath);

        dlm::DownloadMeta meta;
        meta.url = url;
        meta.totalSize = totalSize;
        meta.chunkSize = chunkSize;
        meta.etag = etag;
        meta.chunkDone.assign(numChunks, false);

        const std::size_t preloadedChunks = 2;
        for (std::size_t i = 0; i < preloadedChunks && i < numChunks; ++i) {
            const std::int64_t offset = static_cast<std::int64_t>(i) * chunkSize;
            const std::int64_t size = std::min<std::int64_t>(chunkSize, totalSize - offset);
            assert(writer.writeAt(offset, body.data() + offset, static_cast<std::size_t>(size)));
            meta.chunkDone[i] = true;
        }
        assert(dlm::MetaFile::save(metaPath, meta));

        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/500, etag);
        dlm::Downloader downloader(fakeClient);

        const dlm::DownloadResult result = downloader.downloadResumable(url, outputPath, chunkSize, 4);

        assert(result.success);
        assert(readFile(outputPath) == body);
        assert(!fileExists(metaPath));

        // 1 пробный запрос (bytes=0-0, с If-Range) + по одному на каждый
        // недостающий чанк — уже готовые 2 чанка перекачиваться не должны.
        const int expectedRangeRequests =
            1 + static_cast<int>(numChunks - preloadedChunks);
        assert(fakeClient.rangeRequestCount == expectedRangeRequests);

        std::remove(outputPath.c_str());
    }

    // 3) Файл на сервере изменился (ETag не совпал) — откат на полную
    //    перезагрузку, а не докачку по устаревшей карте чанков.
    {
        const std::string outputPath = "test_resumable_changed.tmp";
        const std::string metaPath = dlm::MetaFile::pathFor(outputPath);

        dlm::DownloadMeta staleMeta;
        staleMeta.url = url;
        staleMeta.totalSize = static_cast<std::int64_t>(body.size());
        staleMeta.chunkSize = chunkSize;
        staleMeta.etag = "\"stale-etag\"";
        const std::size_t numChunks = static_cast<std::size_t>(
            (staleMeta.totalSize + chunkSize - 1) / chunkSize);
        staleMeta.chunkDone.assign(numChunks, true); // якобы всё уже готово по старым данным

        assert(dlm::FileWriter::preallocate(outputPath, staleMeta.totalSize));
        assert(dlm::MetaFile::save(metaPath, staleMeta));

        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/500,
                                             etag, /*failIfRangeMismatch=*/true);
        dlm::Downloader downloader(fakeClient);

        const dlm::DownloadResult result = downloader.downloadResumable(url, outputPath, chunkSize, 4);

        assert(result.success);
        assert(readFile(outputPath) == body);
        assert(!fileExists(metaPath));

        std::remove(outputPath.c_str());
    }

    std::printf("test_resumable_downloader: OK\n");
    return 0;
}
