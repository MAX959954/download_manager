#pragma once

#include "dlm/CancelToken.hpp"
#include "dlm/Types.hpp"

namespace dlm {

// Abstraction over the HTTP backend. Lets us swap libcurl for our own
// socket-based client (Stage 8) without changing Downloader.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // Performs the request, passing the response body to onData as it arrives.
    virtual HttpResponse perform(const HttpRequest& request, const WriteCallback& onData , 
                                                    const CancelToken * cancelToken = nullptr) = 0;
};

} // namespace dlm
