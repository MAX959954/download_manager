// Integration test: a real TCP+TLS connection to a real server.
// Unlike the other tests (which go through FakeHttpClient and don't touch
// the network), this one requires internet access and hits the live
// GitHub — run it separately, it is not expected to pass offline/without
// network access.
#include <cassert>
#include <cstdio>
#include <string>

#include <dlm/SocketHttpClient.hpp>

int main() {
    dlm::SocketHttpClient client;

    // 1) A regular GET over HTTPS, without Range — read a small text file.
    {
        dlm::HttpRequest request;
        request.url = "https://raw.githubusercontent.com/octocat/Hello-World/master/README";

        std::string body;
        const dlm::HttpResponse response = client.perform(request, [&](const char* data, std::size_t n) {
            body.append(data, n);
            return true;
        });

        std::printf("[full GET] status=%ld bytes=%zu\n", response.statusCode, body.size());
        assert(response.statusCode == 200);
        assert(!body.empty());
    }

    // 2) A Range request to the same file — verify Content-Range/206 and
    //    that the requested byte range was actually returned.
    {
        dlm::HttpRequest request;
        request.url = "https://raw.githubusercontent.com/octocat/Hello-World/master/README";
        request.headers["Range"] = "bytes=0-4";

        std::string body;
        const dlm::HttpResponse response = client.perform(request, [&](const char* data, std::size_t n) {
            body.append(data, n);
            return true;
        });

        std::printf("[range GET] status=%ld bytes=%zu contentRangeTotal=%lld body=\"%s\"\n",
                     response.statusCode, body.size(),
                     static_cast<long long>(response.contentRangeTotal), body.c_str());
        assert(response.statusCode == 206);
        assert(body.size() == 5);
        assert(response.contentRangeTotal > 0);
    }

    // 3) Redirect: github.com/... -> usually a 301/302 to a different host.
    {
        dlm::HttpRequest request;
        request.url = "https://github.com/octocat/Hello-World/raw/master/README";

        std::string body;
        const dlm::HttpResponse response = client.perform(request, [&](const char* data, std::size_t n) {
            body.append(data, n);
            return true;
        });

        std::printf("[redirect GET] status=%ld bytes=%zu\n", response.statusCode, body.size());
        assert(response.statusCode == 200); // the redirect must be followed automatically
        assert(!body.empty());
    }

    std::printf("test_socket_http_client_live: OK\n");
    return 0;
}
