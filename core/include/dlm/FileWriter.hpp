#pragma once

#include <cstdint>
#include <string>

namespace dlm {

// Writes to arbitrary offsets in a single file. Windows has no pwrite, so
// each writeAt opens its own file handle and does a seek+write. Having
// several FileWriters pointing at the same file (one per worker) is fine —
// see Stage 3.
class FileWriter {
public:
    explicit FileWriter(std::string path);

    // Creates the file (if it doesn't exist yet) and stretches it to
    // totalSize bytes. Called once before chunk downloading starts.
    static bool preallocate(const std::string& path, std::int64_t totalSize);

    // Writes size bytes from data starting at offset.
    bool writeAt(std::int64_t offset, const char* data, std::size_t size);

private:
    std::string path_;
};

} // namespace dlm
