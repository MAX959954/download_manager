#include "dlm/CurlHttpClient.hpp"

#include "dlm/HttpHeaderParsing.hpp"

#include <curl/curl.h>

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

size_t writeThunk(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t bytes = size * nmemb;
    const auto* callback = static_cast<const WriteCallback*>(userdata);
    if (!*callback) {
        return bytes;
    }
    // Возврат значения, отличного от bytes, сигналит libcurl прервать трансфер.
    return (*callback)(ptr, bytes) ? bytes : 0;
}

int progressThunk(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* token = static_cast<const CancelToken*>(userdata);
    // Ненулевой возврат — сигнал libcurl немедленно прервать трансфер.
    return (token && token->shouldAbortTransfer()) ? 1 : 0;
}

size_t headerThunk(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t bytes = size * nitems;
    auto* response = static_cast<HttpResponse*>(userdata);

    const std::string line(buffer, bytes);
    std::string name, value;
    if (detail::parseHeaderFieldLine(line, name, value)) {
        detail::applyHeaderField(name, value, *response);
    }
    return bytes;
}

} // namespace

CurlHttpClient::CurlHttpClient() { curlGlobalAcquire(); }

CurlHttpClient::~CurlHttpClient() { curlGlobalRelease(); }

HttpResponse CurlHttpClient::perform(const HttpRequest& request, const WriteCallback& onData,
                                      const CancelToken* cancelToken) {
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

    // Пауза/отмена: включаем progress-колбэк и даём ему указатель на токен.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressThunk);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelToken);

    const CURLcode curlResult = curl_easy_perform(curl);

    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    response.statusCode = statusCode;

    if (curlResult == CURLE_ABORTED_BY_CALLBACK) {
        // Трансфер прерван нами самими (пауза/отмена) — это не HTTP-ответ,
        // явно обнуляем statusCode, чтобы Downloader не принял его за успех.
        response.statusCode = 0;
    }

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
