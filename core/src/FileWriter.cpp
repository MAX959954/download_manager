#include "dlm/FileWriter.hpp"

#include <filesystem>
#include <fstream>

namespace dlm {

FileWriter::FileWriter(std::string path) : path_(std::move(path)) {}

bool FileWriter::preallocate(const std::string& path, std::int64_t totalSize) {
    if (totalSize < 0) {
        return false;
    }

    // Создаём файл, если его ещё нет (resize_file не создаёт файлы сам).
    {
        std::ofstream create(path, std::ios::binary | std::ios::app);
        if (!create) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::resize_file(path, static_cast<std::uintmax_t>(totalSize), ec);
    return !ec;
}

bool FileWriter::writeAt(std::int64_t offset, const char* data, std::size_t size) {
    std::fstream out(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!out) {
        return false;
    }
    out.seekp(offset);
    if (!out) {
        return false;
    }
    out.write(data, static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

} // namespace dlm
