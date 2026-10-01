#include "dlm/Url.hpp"

#include <cstdlib>

namespace dlm {

bool parseUrl(const std::string& url, ParsedUrl& out) {
    std::string rest;
    if (url.rfind("https://", 0) == 0) {
        out.https = true;
        out.port = 443;
        rest = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        out.https = false;
        out.port = 80;
        rest = url.substr(7);
    } else {
        return false;
    }
    if (rest.empty()) {
        return false;
    }

    const auto slashPos = rest.find('/');
    const std::string authority = (slashPos == std::string::npos) ? rest : rest.substr(0, slashPos);
    out.target = (slashPos == std::string::npos) ? "/" : rest.substr(slashPos);
    if (out.target.empty()) {
        out.target = "/";
    }
    if (authority.empty()) {
        return false;
    }

    const auto colonPos = authority.rfind(':');
    if (colonPos != std::string::npos) {
        out.host = authority.substr(0, colonPos);
        const std::string portStr = authority.substr(colonPos + 1);
        if (portStr.empty()) {
            return false;
        }
        char* end = nullptr;
        const long portValue = std::strtol(portStr.c_str(), &end, 10);
        if (end == portStr.c_str() || portValue <= 0 || portValue > 65535) {
            return false;
        }
        out.port = static_cast<std::uint16_t>(portValue);
    } else {
        out.host = authority;
    }

    return !out.host.empty();
}

} // namespace dlm
