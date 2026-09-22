#include "dlm/Version.hpp"

#include <curl/curl.h>

namespace dlm {
std::string httpBackendInfo() {
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    std::string s = "libcurl/";
    s += (v && v->version) ? v->version : "unknown";
    if (v && v->ssl_version) {
        s += " ";
        s += v->ssl_version;
    }
    return s;
}
}
