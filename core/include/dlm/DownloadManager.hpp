#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dlm/CancelToken.hpp"
#include "dlm/Downloader.hpp"
#include "dlm/IHttpClient.hpp"
#include "dlm/RateLimiter.hpp"
#include "dlm/ThreadPool.hpp"
#include "dlm/Types.hpp"

namespace dlm {

enum class JobState { Queued, Running, Paused, Completed, Failed, Cancelled };

struct JobInfo {
    std::uint64_t id = 0;
    std::string url;
    std::string outputPath;
    std::int64_t chunkSize = 4 * 1024 * 1024;
    JobState state = JobState::Queued;
    DownloadResult result;
};

// Manages a queue of downloads with a cap on how many run at once.
// Internally, each download still has its own worker pool for chunks
// (workersPerDownload), but no more than maxConcurrentDownloads downloads
// can run at the same time — the rest wait their turn in the shared
// ThreadPool (reusing the one from Stage 3; a separate queue isn't needed).
class DownloadManager {
public:
    explicit DownloadManager(IHttpClient& httpClient,
                              std::size_t maxConcurrentDownloads = 3,
                              std::size_t workersPerDownload = 4);
    ~DownloadManager();

    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    std::uint64_t enqueue(std::string url, std::string outputPath,
                           std::int64_t chunkSize = 4 * 1024 * 1024);

    void pause(std::uint64_t jobId);
    void resume(std::uint64_t jobId);
    void cancel(std::uint64_t jobId);

    JobInfo status(std::uint64_t jobId) const;
    std::vector<JobInfo> allJobs() const;

    // Blocks the calling thread until all tasks enqueued so far have
    // finished (successfully, with an error, or cancelled).
    void waitAll();

private:
    struct Job {
        JobInfo info;
        CancelToken cancelToken;
    };

    void runJob(const std::shared_ptr<Job>& job);

    IHttpClient& httpClient_;
    ThreadPool pool_; // size = maxConcurrentDownloads
    std::size_t workersPerDownload_;

    mutable std::mutex jobsMutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Job>> jobs_;
    std::atomic<std::uint64_t> nextId_{1};

    std::atomic<std::size_t> pendingCount_{0};
    std::mutex waitMutex_;
    std::condition_variable waitCv_;
};

} // namespace dlm
