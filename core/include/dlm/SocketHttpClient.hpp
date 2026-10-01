#pragma once

#include "dlm/IHttpClient.hpp"

namespace dlm {

// A minimal HTTP/1.1 client on raw sockets + OpenSSL for TLS — our own
// implementation of what libcurl does under the hood, hidden behind the
// same IHttpClient as CurlHttpClient, so Downloader doesn't know (and
// doesn't need to know) the difference between them.
//
// Supports: GET with arbitrary headers (including Range/If-Range),
// redirects (301/302/303/307/308, up to maxRedirects in a row), a body
// delivered via Content-Length, chunked transfer-encoding, verifying the
// server's certificate (including the hostname), and SNI.
//
// Does NOT support (a deliberate simplification for a learning-project
// client): keep-alive — every perform() opens a new TCP(+TLS) connection
// and always sends "Connection: close"; HTTP/2; response compression
// (gzip/br).
//
// On Windows, SSL_CTX_set_default_verify_paths() relies on OpenSSL
// environment variables rather than the Windows system certificate store —
// if certificate verification fails, point the DLM_CA_BUNDLE environment
// variable at a CA bundle (e.g. https://curl.se/ca/cacert.pem).
class SocketHttpClient final : public IHttpClient {
public:
    explicit SocketHttpClient(int maxRedirects = 5);
    ~SocketHttpClient() override;

    SocketHttpClient(const SocketHttpClient&) = delete;
    SocketHttpClient& operator=(const SocketHttpClient&) = delete;

    HttpResponse perform(const HttpRequest& request, const WriteCallback& onData,
                          const CancelToken* cancelToken = nullptr) override;

private:
    int maxRedirects_;
};

} // namespace dlm
