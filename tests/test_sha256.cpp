#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <dlm/Sha256.hpp>

namespace {

std::string hashString(const std::string& s) {
    dlm::Sha256 h;
    h.update(s.data(), s.size());
    return h.hexDigest();
}

} // namespace

int main() {
    // Эталонные значения SHA-256 из спецификации FIPS 180-4 / общеизвестные тестовые векторы.
    assert(hashString("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(hashString("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Длинное сообщение (больше одного 64-байтового блока) — проверяет путь
    // с несколькими update() подряд и паддинг через границу блока.
    {
        dlm::Sha256 h;
        const std::string part(100, 'x');
        h.update(part.data(), part.size());
        h.update(part.data(), part.size());
        const std::string digest = h.hexDigest();
        assert(digest.size() == 64);
        // Должно совпасть с хэшем от той же строки, посчитанным за один update().
        assert(digest == hashString(part + part));
    }

    // sha256File: считаем хэш реального файла и сверяем с hashString тех же байт.
    {
        const std::string path = "test_sha256_file.tmp";
        const std::string content = "hello, sha256 file test!";
        {
            std::ofstream out(path, std::ios::binary);
            out << content;
        }
        bool ok = false;
        const std::string digest = dlm::sha256File(path, ok);
        assert(ok);
        assert(digest == hashString(content));
        std::remove(path.c_str());
    }

    // Несуществующий файл — ok должен стать false.
    {
        bool ok = true;
        dlm::sha256File("no_such_file_12345.tmp", ok);
        assert(!ok);
    }

    std::printf("test_sha256: OK\n");
    return 0;
}
