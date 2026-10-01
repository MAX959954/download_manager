#pragma once

#include <cstdint>
#include <string>

namespace dlm {

struct ParsedUrl {
    bool https = false;
    std::string host;
    std::uint16_t port = 0;
    std::string target; // путь + query — то, что идёт в строке запроса GET
};

// Разбирает http(s)://host[:port]/path?query. Возвращает false для
// неподдерживаемых схем (не http/https) или совсем некорректных URL.
// IPv6-адреса в квадратных скобках не поддерживаются — для обычных
// доменных имён и IPv4 этого достаточно.
bool parseUrl(const std::string& url, ParsedUrl& out);

} // namespace dlm
