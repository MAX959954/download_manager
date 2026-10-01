#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dlm {

// Streaming SHA-256 (FIPS 180-4), no external dependencies — computed
// ourselves so we don't have to pull in OpenSSL for just one function.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    // Finalizes the hash (appends the padding) and returns it as a
    // 64-character hex string. Calling update() again after this is no
    // longer valid — create a new Sha256 for the next file.
    std::string hexDigest();

private:
    void processBlock(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint64_t bitLength_ = 0;
    std::uint8_t buffer_[64] = {};
    std::size_t bufferLength_ = 0;
};

// Helper: computes the SHA-256 of a whole file, reading it in chunks (not
// loading it entirely into memory). ok = false if the file failed to open.
std::string sha256File(const std::string& path, bool& ok);

} // namespace dlm
