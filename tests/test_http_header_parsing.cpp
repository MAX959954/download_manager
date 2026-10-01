#include <cassert>
#include <cstdio>

#include <dlm/HttpHeaderParsing.hpp>
#include <dlm/Types.hpp>

int main() {
    using namespace dlm::detail;

    {
        long status = 0;
        assert(parseStatusLine("HTTP/1.1 206 Partial Content", status));
        assert(status == 206);
    }
    {
        long status = 0;
        assert(parseStatusLine("HTTP/1.1 404 Not Found", status));
        assert(status == 404);
    }
    {
        long status = 0;
        assert(!parseStatusLine("not a status line", status));
    }

    {
        std::string name, value;
        assert(parseHeaderFieldLine("Content-Type: text/html; charset=utf-8", name, value));
        assert(name == "content-type"); // приводится к нижнему регистру
        assert(value == "text/html; charset=utf-8"); // обрезаны только крайние пробелы
    }
    {
        std::string name, value;
        assert(!parseHeaderFieldLine("no colon here", name, value));
    }

    {
        dlm::HttpResponse response;
        applyHeaderField("accept-ranges", "bytes", response);
        assert(response.acceptRanges);
    }
    {
        dlm::HttpResponse response;
        applyHeaderField("etag", "\"abc123\"", response);
        assert(response.etag == "\"abc123\"");
    }
    {
        dlm::HttpResponse response;
        applyHeaderField("content-range", "bytes 0-4194303/104857600", response);
        assert(response.contentRangeTotal == 104857600);
    }
    {
        dlm::HttpResponse response;
        applyHeaderField("content-range", "bytes 0-9/*", response); // общий размер неизвестен
        assert(response.contentRangeTotal == -1);
    }
    {
        dlm::HttpResponse response;
        applyHeaderField("content-length", "12345", response);
        assert(response.contentLength == 12345);
    }

    std::printf("test_http_header_parsing: OK\n");
    return 0;
}
