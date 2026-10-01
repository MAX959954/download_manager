#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dlm {

// Потоковый SHA-256 (FIPS 180-4), без внешних зависимостей — считаем сами,
// чтобы не тянуть OpenSSL только ради одной функции.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    // Финализирует хэш (дописывает паддинг) и возвращает его в виде
    // 64-символьной hex-строки. Повторный вызов update() после этого уже
    // некорректен — создавай новый Sha256 для следующего файла.
    std::string hexDigest();

private:
    void processBlock(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint64_t bitLength_ = 0;
    std::uint8_t buffer_[64] = {};
    std::size_t bufferLength_ = 0;
};

// Хелпер: считает SHA-256 всего файла, читая его порциями (не грузит в
// память целиком). ok = false, если файл не открылся.
std::string sha256File(const std::string& path, bool& ok);

} // namespace dlm
