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

    // 1) A fresh resumable download: downloads in full, the meta file is removed at the end.
    {
        dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/500, etag);
        dlm::Downloader downloader(fakeClient);

        const std::string outputPath = "test_resumable_fresh.tmp";
        const std::string metaPath = dlm::MetaFile::pathFor(outputPath);

        const dlm::DownloadResult result = downloader.downloadResumable(url, outputPath, chunkSize, 4);

        assert(result.success);
        assert(readFile(outputPath) == body);
        assert(!fileExists(metaPath)); // the meta file is removed after successful completion

        std::remove(outputPath.c_str());
    }

    // 2) Resuming an interrupted download: the first two chunks are already
    //    "downloaded" and marked in the meta file — downloadResumable should
    //    request only the remaining ones and produce a byte-for-byte correct
    //    file overall.
    {
        const std::string outputPath = "test_resumable_partial.tmp";
        const std::string metaPath = dlm::MetaFile::pathFor(outputPath);

        const std::int64_t totalSize = static_cast<std::int64_t>(body.size());
        const std::size_t numChunks =
            static_cast<std::size_t>((totalSize + chunkSize - 1) / chunkSize);

        // Preallocate the file and manually "download" the first 2 chunks
        // ahead of time, as if the previous run had only managed to download those.
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

        // 1 probe request (bytes=0-0, with If-Range) + one for each missing
        // chunk — the 2 already-complete chunks must not be downloaded again.
        const int expectedRangeRequests =
            1 + static_cast<int>(numChunks - preloadedChunks);
        assert(fakeClient.rangeRequestCount == expectedRangeRequests);

        std::remove(outputPath.c_str());
    }

    // 3) The file on the server has changed (ETag mismatch) — falls back to
    //    a full re-download instead of resuming from a stale chunk map.
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
        staleMeta.chunkDone.assign(numChunks, true); // supposedly everything is already done per the old data

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
