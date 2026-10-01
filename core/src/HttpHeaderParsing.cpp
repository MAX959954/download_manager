#include "dlm/HttpHeaderParsing.hpp"

#include <cctype>
#include <cstdlib>

namespace dlm::detail {

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trimAscii(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool parseStatusLine(const std::string& line, long& statusCode) {
    // Ожидаем "HTTP/1.1 206 ...". Нам важна только цифровая часть.
    const auto firstSpace = line.find(' ');
    if (firstSpace == std::string::npos) {
        return false;
    }
    const auto secondSpace = line.find(' ', firstSpace + 1);
    const std::string codeStr = line.substr(
        firstSpace + 1, (secondSpace == std::string::npos ? line.size() : secondSpace) - firstSpace - 1);
    if (codeStr.empty()) {
        return false;
    }
    char* end = nullptr;
    const long code = std::strtol(codeStr.c_str(), &end, 10);
    if (end == codeStr.c_str() || code <= 0) {
        return false;
    }
    statusCode = code;
    return true;
}

bool parseHeaderFieldLine(const std::string& line, std::string& name, std::string& value) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    name = toLowerAscii(trimAscii(line.substr(0, colon)));
    value = trimAscii(line.substr(colon + 1));
    return true;
}

void applyHeaderField(const std::string& name, const std::string& value, HttpResponse& response) {
    if (name == "accept-ranges") {
        response.acceptRanges = toLowerAscii(value).find("bytes") != std::string::npos;
    } else if (name == "etag") {
        response.etag = value;
    } else if (name == "last-modified") {
        response.lastModified = value;
    } else if (name == "content-range") {
        // "bytes 0-4194303/104857600" — заголовок пишется через дефис, не "_"
        const auto slash = value.find('/');
        if (slash != std::string::npos) {
            const std::string totalStr = value.substr(slash + 1);
            if (totalStr != "*") {
                char* end = nullptr;
                const long long total = std::strtoll(totalStr.c_str(), &end, 10);
                if (end != totalStr.c_str()) {
                    response.contentRangeTotal = total;
                }
            }
        }
    } else if (name == "content-length") {
        char* end = nullptr;
        const long long length = std::strtoll(value.c_str(), &end, 10);
        if (end != value.c_str()) {
            response.contentLength = length;
        }
    }
}

} // namespace dlm::detail
