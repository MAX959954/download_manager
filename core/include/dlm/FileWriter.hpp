#pragma once

#include <cstdint>
#include <string>

namespace dlm {

// Запись по произвольным смещениям в один файл. На Windows нет pwrite,
// поэтому каждый writeAt открывает свой файловый дескриптор и делает
// seek+write. Несколько FileWriter, указывающих на один и тот же файл
// (по одному на воркер), — это нормально, см. Этап 3.
class FileWriter {
public:
    explicit FileWriter(std::string path);

    // Создаёт файл (если его ещё нет) и растягивает до totalSize байт.
    // Вызывается один раз перед стартом закачки чанков.
    static bool preallocate(const std::string& path, std::int64_t totalSize);

    // Пишет size байт из data начиная с offset.
    bool writeAt(std::int64_t offset, const char* data, std::size_t size);

private:
    std::string path_;
};

} // namespace dlm
