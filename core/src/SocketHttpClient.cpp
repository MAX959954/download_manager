#include "dlm/SocketHttpClient.hpp"

#include "dlm/ChunkedDecoder.hpp"
#include "dlm/HttpHeaderParsing.hpp"
#include "dlm/Url.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace dlm {

namespace {

// --- Platform socket initialization (Winsock is only needed on Windows,
//     same as curl_global_init/cleanup in CurlHttpClient — we count live
//     instances so it's called exactly once per process). ---

std::mutex g_initMutex;
int g_initCount = 0;

void socketsGlobalAcquire() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initCount++ == 0) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }
}

void socketsGlobalRelease() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (--g_initCount == 0) {
#ifdef _WIN32
        WSACleanup();
#endif
    }
}

void closeSocket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

socket_t tcpConnect(const std::string& host, std::uint16_t port) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* resolved = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &resolved) != 0) {
        return kInvalidSocket;
    }

    socket_t sock = kInvalidSocket;
    for (struct addrinfo* it = resolved; it != nullptr; it = it->ai_next) {
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock == kInvalidSocket) {
            continue;
        }
        if (connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            break; // connected
        }
        closeSocket(sock);
        sock = kInvalidSocket;
    }

    freeaddrinfo(resolved);
    return sock;
}

// A wrapper around the connection: works transparently with both TLS
// (SSL_read/write) and plain TCP (for http://), so the rest of the code
// doesn't need to know the difference.
class Connection {
public:
    Connection() = default;
    ~Connection() { close(); }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool connectPlain(const std::string& host, std::uint16_t port) {
        socketsGlobalAcquire();
        ownsSockets_ = true;
        fd_ = tcpConnect(host, port);
        return fd_ != kInvalidSocket;
    }

    bool connectTls(const std::string& host, std::uint16_t port) {
        if (!connectPlain(host, port)) {
            return false;
        }

        ctx_ = SSL_CTX_new(TLS_client_method());
        if (!ctx_) {
            return false;
        }
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_default_verify_paths(ctx_);
        if (const char* caBundle = std::getenv("DLM_CA_BUNDLE")) {
            SSL_CTX_load_verify_locations(ctx_, caBundle, nullptr);
        }

        ssl_ = SSL_new(ctx_);
        if (!ssl_) {
            return false;
        }
        SSL_set_fd(ssl_, static_cast<int>(fd_));
        SSL_set_tlsext_host_name(ssl_, host.c_str()); // SNI
        SSL_set1_host(ssl_, host.c_str());            // verify the hostname against the certificate

        if (SSL_connect(ssl_) != 1) {
            return false;
        }
        return SSL_get_verify_result(ssl_) == X509_V_OK;
    }

    bool writeAll(const char* data, std::size_t size) {
        std::size_t sent = 0;
        while (sent < size) {
            const int n = ssl_ ? SSL_write(ssl_, data + sent, static_cast<int>(size - sent))
                                : send(fd_, data + sent, static_cast<int>(size - sent), 0);
            if (n <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Returns the number of bytes read; 0 — the server closed the
    // connection cleanly; -1 — a read error.
    int readSome(char* buf, int maxLen) {
        return ssl_ ? SSL_read(ssl_, buf, maxLen) : static_cast<int>(recv(fd_, buf, maxLen, 0));
    }

    void close() {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (ctx_) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        if (fd_ != kInvalidSocket) {
            closeSocket(fd_);
            fd_ = kInvalidSocket;
        }
        if (ownsSockets_) {
            socketsGlobalRelease();
            ownsSockets_ = false;
        }
    }

private:
    socket_t fd_ = kInvalidSocket;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    bool ownsSockets_ = false;
};

std::string buildRequestText(const ParsedUrl& url, const HttpRequest& request) {
    std::string req = "GET " + url.target + " HTTP/1.1\r\n";
    req += "Host: " + url.host + "\r\n";
    req += "Connection: close\r\n"; // no keep-alive — simpler, and enough for a single request
    req += "User-Agent: dlm-socket-client/1.0\r\n";
    for (const auto& [key, value] : request.headers) {
        req += key + ": " + value + "\r\n";
    }
    req += "\r\n";
    return req;
}

} // namespace

SocketHttpClient::SocketHttpClient(int maxRedirects) : maxRedirects_(maxRedirects) {}

SocketHttpClient::~SocketHttpClient() = default;

HttpResponse SocketHttpClient::perform(const HttpRequest& originalRequest, const WriteCallback& onData,
                                        const CancelToken* cancelToken) {
    HttpResponse failure; // statusCode stays 0 — the same error signal as in CurlHttpClient
    std::string currentUrl = originalRequest.url;

    for (int redirectCount = 0; redirectCount <= maxRedirects_; ++redirectCount) {
        ParsedUrl url;
        if (!parseUrl(currentUrl, url)) {
            return failure;
        }

        Connection conn;
        const bool connected =
            url.https ? conn.connectTls(url.host, url.port) : conn.connectPlain(url.host, url.port);
        if (!connected) {
            return failure;
        }

        HttpRequest requestForThisHost = originalRequest;
        requestForThisHost.url = currentUrl;
        const std::string requestText = buildRequestText(url, requestForThisHost);
        if (!conn.writeAll(requestText.data(), requestText.size())) {
            return failure;
        }

        // Keep reading until we have the whole header block (up to "\r\n\r\n").
        std::string buffer;
        char recvBuf[16 * 1024];
        std::size_t headerEnd = std::string::npos;
        for (;;) {
            if (cancelToken && cancelToken->shouldAbortTransfer()) {
                return failure;
            }
            headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                break;
            }
            const int n = conn.readSome(recvBuf, sizeof(recvBuf));
            if (n <= 0) {
                return failure; // the connection dropped before all headers arrived
            }
            buffer.append(recvBuf, static_cast<std::size_t>(n));
            if (buffer.size() > 64 * 1024) {
                return failure; // headers are suspiciously large — something's wrong
            }
        }

        const std::string headerBlock = buffer.substr(0, headerEnd);
        std::string bodyStart = buffer.substr(headerEnd + 4);

        HttpResponse parsed;
        parsed.effectiveUrl = currentUrl;
        std::string locationHeader;
        bool chunked = false;
        bool firstLine = true;
        std::size_t lineStart = 0;

        while (lineStart <= headerBlock.size()) {
            const auto lineEnd = headerBlock.find("\r\n", lineStart);
            const std::string line = headerBlock.substr(
                lineStart, (lineEnd == std::string::npos ? headerBlock.size() : lineEnd) - lineStart);

            if (firstLine) {
                if (!detail::parseStatusLine(line, parsed.statusCode)) {
                    return failure;
                }
                firstLine = false;
            } else if (!line.empty()) {
                std::string name, value;
                if (detail::parseHeaderFieldLine(line, name, value)) {
                    detail::applyHeaderField(name, value, parsed);
                    if (name == "location") {
                        locationHeader = value;
                    } else if (name == "transfer-encoding" &&
                               detail::toLowerAscii(value).find("chunked") != std::string::npos) {
                        chunked = true;
                    }
                }
            }

            if (lineEnd == std::string::npos) {
                break;
            }
            lineStart = lineEnd + 2;
        }

        const bool isRedirect = parsed.statusCode == 301 || parsed.statusCode == 302 ||
                                 parsed.statusCode == 303 || parsed.statusCode == 307 ||
                                 parsed.statusCode == 308;
        if (isRedirect && !locationHeader.empty() && redirectCount < maxRedirects_) {
            currentUrl = locationHeader; // simplification: we expect an absolute URL in Location
            continue;
        }

        bool bodyOk = true;
        if (chunked) {
            bool finished = false;
            bodyOk = detail::consumeChunkedBytes(bodyStart, onData, finished);
            while (bodyOk && !finished) {
                if (cancelToken && cancelToken->shouldAbortTransfer()) {
                    bodyOk = false;
                    break;
                }
                const int n = conn.readSome(recvBuf, sizeof(recvBuf));
                if (n <= 0) {
                    bodyOk = false; // the connection dropped before the terminating chunk
                    break;
                }
                bodyStart.append(recvBuf, static_cast<std::size_t>(n));
                bodyOk = detail::consumeChunkedBytes(bodyStart, onData, finished);
            }
        } else if (parsed.contentLength >= 0) {
            std::int64_t delivered = 0;
            if (!bodyStart.empty()) {
                const std::int64_t take = std::min<std::int64_t>(
                    static_cast<std::int64_t>(bodyStart.size()), parsed.contentLength);
                bodyOk = onData(bodyStart.data(), static_cast<std::size_t>(take));
                delivered += take;
            }
            while (bodyOk && delivered < parsed.contentLength) {
                if (cancelToken && cancelToken->shouldAbortTransfer()) {
                    bodyOk = false;
                    break;
                }
                const int n = conn.readSome(recvBuf, sizeof(recvBuf));
                if (n <= 0) {
                    bodyOk = false; // the server closed earlier than promised by Content-Length
                    break;
                }
                const std::int64_t take = std::min<std::int64_t>(n, parsed.contentLength - delivered);
                bodyOk = onData(recvBuf, static_cast<std::size_t>(take));
                delivered += take;
            }
        } else {
            // Neither Content-Length nor chunked — read until the connection
            // closes (we always send "Connection: close", so this is correct).
            if (!bodyStart.empty()) {
                bodyOk = onData(bodyStart.data(), bodyStart.size());
            }
            while (bodyOk) {
                if (cancelToken && cancelToken->shouldAbortTransfer()) {
                    bodyOk = false;
                    break;
                }
                const int n = conn.readSome(recvBuf, sizeof(recvBuf));
                if (n <= 0) {
                    break; // a normal connection close by the server = end of body
                }
                bodyOk = onData(recvBuf, static_cast<std::size_t>(n));
            }
        }

        if (!bodyOk) {
            failure.statusCode = 0;
            return failure;
        }

        return parsed;
    }

    return failure; // too many redirects in a row
}

} // namespace dlm
