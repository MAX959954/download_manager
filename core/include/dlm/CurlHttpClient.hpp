#pragma once

#include "dlm/IHttpClient.hpp"

namespace dlm {

class CurlHttpClient final : public IHttpClient {
public:
    CurlHttpClient();
    ~CurlHttpClient() override;

    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;

    HttpResponse perform(const HttpRequest& request, const WriteCallback& onData) override;
};

} // namespace dlm
