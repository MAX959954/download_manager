#include <cassert>
#include <cstdio>

#include <dlm/Url.hpp>

int main() {
    using dlm::ParsedUrl;
    using dlm::parseUrl;

    {
        ParsedUrl u;
        assert(parseUrl("https://example.com/path/to/file", u));
        assert(u.https);
        assert(u.host == "example.com");
        assert(u.port == 443);
        assert(u.target == "/path/to/file");
    }
    {
        ParsedUrl u;
        assert(parseUrl("http://example.com", u));
        assert(!u.https);
        assert(u.host == "example.com");
        assert(u.port == 80);
        assert(u.target == "/"); // путь по умолчанию
    }
    {
        ParsedUrl u;
        assert(parseUrl("http://example.com:8080/x?y=1", u));
        assert(u.host == "example.com");
        assert(u.port == 8080);
        assert(u.target == "/x?y=1");
    }
    {
        ParsedUrl u;
        assert(!parseUrl("ftp://example.com/file", u)); // неподдерживаемая схема
    }
    {
        ParsedUrl u;
        assert(!parseUrl("https://", u)); // пустая authority
    }
    {
        ParsedUrl u;
        assert(!parseUrl("http://example.com:notanumber/x", u)); // битый порт
    }

    std::printf("test_url_parsing: OK\n");
    return 0;
}
