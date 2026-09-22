#include "dlm/CurlHttpClient.hpp"

#include <curl/curl.h>

#include <cctype>
#include <exception>
#include <mutex>

namespace dlm {

namespace {

// curl_global_init/cleanup не потокобезопасны и должны вызываться ровно
// один раз за процесс — считаем живые экземпляры CurlHttpClient.
std::mutex g_initMutex;
int g_initCount = 0;

void curlGlobalAcquire() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initCount++ == 0) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
}

void curlGlobalRelease() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (--g_initCount == 0) {
        curl_global_cleanup();
    }
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

size_t writeThunk(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t bytes = size * nmemb;
    const auto* callback = static_cast<const WriteCallback*>(userdata);
    if (!*callback) {
        return bytes;
    }
    // Возврат значения, отличного от bytes, сигналит libcurl прервать трансфер.
    return (*callback)(ptr, bytes) ? bytes : 0;
}

size_t headerThunk(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t bytes = size * nitems;
    auto* response = static_cast<HttpResponse*>(userdata);

    const std::string line(buffer, bytes);
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return bytes;
    }

    const std::string name = toLower(trim(line.substr(0, colon)));
    const std::string value = trim(line.substr(colon + 1));

    if (name == "accept-ranges") {
        response->acceptRanges = toLower(value).find("bytes") != std::string::npos;
    } else if (name == "etag") {
        response->etag = value;
    } else if (name == "last-modified") {
        response->lastModified = value;
    } else if (name == "content-range") {
        // "bytes 0-4194303/104857600" — заголовок пишется через дефис, не "_"
        const auto slash = value.find('/');
        if (slash != std::string::npos) {
            const std::string totalStr = value.substr(slash + 1);
            if (totalStr != "*") {
                try {
                    response->contentRangeTotal = std::stoll(totalStr);
                } catch (const std::exception&) {
                    // Некорректный заголовок — просто игнорируем.
                }
            }
        }
    }
    return bytes;
}

} // namespace

CurlHttpClient::CurlHttpClient() { curlGlobalAcquire(); }

CurlHttpClient::~CurlHttpClient() { curlGlobalRelease(); }

HttpResponse CurlHttpClient::perform(const HttpRequest& request, const WriteCallback& onData) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return response; // statusCode остаётся 0 — сигнал ошибки для вызывающего кода
    }

    curl_slist* headerList = nullptr;
    for (const auto& [key, value] : request.headers) {
        const std::string header = key + ": " + value;
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerThunk);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeThunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &onData);

    curl_easy_perform(curl);

    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    response.statusCode = statusCode;

    curl_off_t contentLength = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);
    response.contentLength = static_cast<std::int64_t>(contentLength);

    char* effectiveUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    if (effectiveUrl) {
        response.effectiveUrl = effectiveUrl;
    }

    if (headerList) {
        curl_slist_free_all(headerList);
    }
    curl_easy_cleanup(curl);

    return response;
}

} // namespace dlm
