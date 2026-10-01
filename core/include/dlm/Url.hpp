#pragma once

#include <cstdint>
#include <string>

namespace dlm {

struct ParsedUrl {
    bool https = false;
    std::string host;
    std::uint16_t port = 0;
    std::string target; // path + query — what goes into the GET request line
};

// Parses http(s)://host[:port]/path?query. Returns false for unsupported
// schemes (anything other than http/https) or thoroughly malformed URLs.
// Bracketed IPv6 addresses are not supported — this is enough for ordinary
// domain names and IPv4.
bool parseUrl(const std::string& url, ParsedUrl& out);

} // namespace dlm
