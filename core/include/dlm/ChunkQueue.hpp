#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace dlm {

struct ChunkRange {
    std::int64_t start;
    std::int64_t end; // не включая

    std::int64_t size() const { return end - start; }
};

// Потокобезопасная очередь диапазонов с адаптивным дроблением ("work
// stealing" для HTTP-докачки).
//
// Статическое разбиение на N чанков фиксированного размера ломается, когда
// чанков меньше, чем воркеров (файл 20 MB, chunkSize 4 MB -> всего 5 чанков,
// и 3 воркера из 8 гарантированно простаивают всю закачку — см. бенчмарк в
// benchmarks/). pop() решает это прямо в момент выдачи работы: если в
// очереди осталось меньше диапазонов, чем всего воркеров — самый большой из
// оставшихся диапазонов делится пополам, и так до тех пор, пока диапазонов
// не станет достаточно (или пока дробить дальше уже невыгодно).
//
// Важно: порог — это общее число воркеров (numWorkers_), ПОСТОЯННОЕ и
// известное заранее, а не "сколько воркеров простаивают прямо сейчас".
// Первая версия именно так и считала (атомарный счётчик "занят/свободен"),
// и это было ошибкой: воркеры разбирают первые N чанков за микросекунды —
// быстрее, чем остальные успевают вообще дойти до вызова pop() — поэтому
// "живой" счётчик почти никогда не успевал увидеть, что кому-то не хватило
// работы, и дробление не срабатывало ни разу. Раз мы заранее знаем, сколько
// воркеров всего будет работать, достаточно детерминированно поддерживать
// в очереди не меньше queue.size() >= numWorkers диапазонов на каждый pop(),
// без всякой гонки.
//
// Это не классический in-flight work-stealing (мы не прерываем уже идущий
// Range-запрос — TCP-соединение нельзя обрезать посередине без разрыва и
// переоткрытия): если диапазон уже выдан воркеру, он "невидим" для этой
// очереди и достелить из него ничего нельзя. Поэтому выигрыш ограничен
// тем, что можно успеть раздробить ДО выдачи — отсюда остаточный разбаланс,
// когда исходный чанк намного крупнее "справедливой доли" на воркера
// (см. README/бенчмарк: разница между "убрали явный провал" и "идеальная
// линейность").
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
                break; // дальше дробить невыгодно — накладные расходы на лишний HTTP-запрос съедят выигрыш
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
