#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace dlm {

struct ChunkRange {
    std::int64_t start;
    std::int64_t end; // exclusive

    std::int64_t size() const { return end - start; }
};

// A thread-safe queue of ranges with adaptive splitting ("work stealing"
// for HTTP resumable downloads).
//
// Statically splitting into N fixed-size chunks breaks down when there are
// fewer chunks than workers (a 20 MB file, 4 MB chunkSize -> only 5 chunks
// total, and 3 of 8 workers are guaranteed to sit idle for the whole
// download — see the benchmark in benchmarks/). pop() solves this right at
// the moment work is handed out: if the queue has fewer ranges left than
// there are workers in total, the largest remaining range is split in half,
// and so on until there are enough ranges (or splitting further stops
// being worthwhile).
//
// Important: the threshold is the total number of workers (numWorkers_),
// which is CONSTANT and known up front, not "how many workers are idle
// right now." The first version counted it that way (an atomic
// busy/free counter), and that was a bug: workers pick up the first N
// chunks in microseconds — faster than the rest even manage to reach the
// pop() call — so the "live" counter almost never got to see that someone
// was short on work, and splitting never kicked in. Since we know in
// advance how many workers will ever run, it's enough to deterministically
// keep queue.size() >= numWorkers ranges in the queue on every pop(), with
// no race at all.
//
// This isn't classic in-flight work stealing (we don't interrupt a Range
// request already in progress — a TCP connection can't be cut mid-stream
// without tearing down and reopening it): once a range has been handed to
// a worker, it's "invisible" to this queue and nothing can be carved out
// of it. So the gain is limited to what can be split BEFORE handout —
// hence the residual imbalance when the original chunk is much bigger than
// a worker's "fair share" (see the README/benchmark for the difference
// between "removed the obvious cliff" and "perfect linearity").
class ChunkQueue {
public:
    ChunkQueue(std::int64_t totalSize, std::int64_t chunkSize, std::size_t numWorkers,
               std::int64_t minSplitSize = 256 * 1024)
        : numWorkers_(numWorkers), minSplitSize_(minSplitSize) {
        for (std::int64_t offset = 0; offset < totalSize; offset += chunkSize) {
            queue_.push_back(ChunkRange{offset, std::min(offset + chunkSize, totalSize)});
        }
    }

    std::optional<ChunkRange> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }

        while (numWorkers_ > queue_.size()) {
            auto biggestIt = std::max_element(
                queue_.begin(), queue_.end(),
                [](const ChunkRange& a, const ChunkRange& b) { return a.size() < b.size(); });

            if (biggestIt->size() <= minSplitSize_ * 2) {
                break; // not worth splitting further — the overhead of an extra HTTP request would eat the gain
            }

            const std::int64_t mid = biggestIt->start + biggestIt->size() / 2;
            const ChunkRange tail{mid, biggestIt->end};
            biggestIt->end = mid;
            queue_.push_back(tail);
        }

        const ChunkRange next = queue_.front();
        queue_.pop_front();
        return next;
    }

private:
    std::deque<ChunkRange> queue_;
    std::mutex mutex_;
    std::size_t numWorkers_;
    std::int64_t minSplitSize_;
};

} // namespace dlm
