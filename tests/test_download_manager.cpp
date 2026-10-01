#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <dlm/DownloadManager.hpp>

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
    const std::string body = makeBody(5000);

    // maxConcurrentDownloads = 1 — jobs run strictly sequentially, so
    // FakeHttpClient can safely be shared between them without races.
    dlm_test::FakeHttpClient fakeClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/200);
    dlm::DownloadManager manager(fakeClient, /*maxConcurrentDownloads=*/1, /*workersPerDownload=*/2);

    const std::uint64_t id1 = manager.enqueue("http://example.invalid/a", "dm_test_a.tmp", 1000);
    const std::uint64_t id2 = manager.enqueue("http://example.invalid/b", "dm_test_b.tmp", 1000);
    const std::uint64_t id3 = manager.enqueue("http://example.invalid/c", "dm_test_c.tmp", 1000);

    manager.waitAll();

    for (const std::uint64_t id : {id1, id2, id3}) {
        const dlm::JobInfo info = manager.status(id);
        assert(info.state == dlm::JobState::Completed);
        assert(info.result.success);
    }
    assert(readFile("dm_test_a.tmp") == body);
    assert(readFile("dm_test_b.tmp") == body);
    assert(readFile("dm_test_c.tmp") == body);
    assert(manager.allJobs().size() == 3);

    std::remove("dm_test_a.tmp");
    std::remove("dm_test_b.tmp");
    std::remove("dm_test_c.tmp");

    // Cancellation: queue a download with an artificial delay (so it doesn't
    // finish instantly) and cancel it almost immediately after it starts.
    dlm_test::FakeHttpClient slowClient(body, /*acceptRanges=*/true, /*writeChunkSize=*/100,
                                         /*etag=*/"", /*failIfRangeMismatch=*/false,
                                         /*delayPerPiece=*/std::chrono::milliseconds(2));
    dlm::DownloadManager manager2(slowClient, /*maxConcurrentDownloads=*/1, /*workersPerDownload=*/2);

    const std::uint64_t cancelId =
        manager2.enqueue("http://example.invalid/d", "dm_test_cancel.tmp", 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    manager2.cancel(cancelId);
    manager2.waitAll();

    const dlm::JobInfo cancelled = manager2.status(cancelId);
    assert(cancelled.state == dlm::JobState::Cancelled);
    assert(!cancelled.result.success);

    std::remove("dm_test_cancel.tmp");
    std::remove("dm_test_cancel.tmp.dlm");

    std::printf("test_download_manager: OK\n");
    return 0;
}
