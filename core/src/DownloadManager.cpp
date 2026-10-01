#include "dlm/DownloadManager.hpp"

namespace dlm {

DownloadManager::DownloadManager(IHttpClient& httpClient, std::size_t maxConcurrentDownloads,
                                  std::size_t workersPerDownload)
    : httpClient_(httpClient), pool_(maxConcurrentDownloads), workersPerDownload_(workersPerDownload) {}

DownloadManager::~DownloadManager() {
    waitAll();
}

std::uint64_t DownloadManager::enqueue(std::string url, std::string outputPath,
                                        std::int64_t chunkSize) {
    auto job = std::make_shared<Job>();
    job->info.id = nextId_++;
    job->info.url = std::move(url);
    job->info.outputPath = std::move(outputPath);
    job->info.chunkSize = chunkSize;
    job->info.state = JobState::Queued;

    {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        jobs_[job->info.id] = job;
    }
    ++pendingCount_;

    pool_.enqueue([this, job] { runJob(job); });
    return job->info.id;
}

void DownloadManager::runJob(const std::shared_ptr<Job>& job) {
    {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        job->info.state = JobState::Running;
    }

    Downloader downloader(httpClient_);
    const DownloadResult result = downloader.downloadResumable(
        job->info.url, job->info.outputPath, job->info.chunkSize,
        workersPerDownload_, &job->cancelToken);

    {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        job->info.result = result;
        if (result.success) {
            job->info.state = JobState::Completed;
        } else if (job->cancelToken.isCancelled()) {
            job->info.state = JobState::Cancelled;
        } else if (job->cancelToken.isPaused()) {
            job->info.state = JobState::Paused;
        } else {
            job->info.state = JobState::Failed;
        }
    }

    if (--pendingCount_ == 0) {
        std::lock_guard<std::mutex> lock(waitMutex_);
        waitCv_.notify_all();
    }
}

void DownloadManager::pause(std::uint64_t jobId) {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    const auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        it->second->cancelToken.pause();
    }
}

void DownloadManager::resume(std::uint64_t jobId) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        const auto it = jobs_.find(jobId);
        if (it == jobs_.end()) {
            return;
        }
        job = it->second;
        job->cancelToken.resume();
    }

    // Пауза всегда прерывает текущий вызов downloadResumable() целиком —
    // чтобы реально продолжить именно эту закачку, ставим job в очередь
    // пула ещё раз. Уже докачанные чанки в .dlm метафайле не потеряются,
    // downloadResumable() продолжит ровно с того места, где остановился.
    ++pendingCount_;
    pool_.enqueue([this, job] { runJob(job); });
}

void DownloadManager::cancel(std::uint64_t jobId) {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    const auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        it->second->cancelToken.cancel();
    }
}

JobInfo DownloadManager::status(std::uint64_t jobId) const {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    const auto it = jobs_.find(jobId);
    return it != jobs_.end() ? it->second->info : JobInfo{};
}

std::vector<JobInfo> DownloadManager::allJobs() const {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    std::vector<JobInfo> result;
    result.reserve(jobs_.size());
    for (const auto& [id, job] : jobs_) {
        result.push_back(job->info);
    }
    return result;
}

void DownloadManager::waitAll() {
    std::unique_lock<std::mutex> lock(waitMutex_);
    waitCv_.wait(lock, [this] { return pendingCount_.load() == 0; });
}

} // namespace dlm
