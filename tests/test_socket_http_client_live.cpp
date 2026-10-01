// Интеграционный тест: реальное TCP+TLS соединение с реальным сервером.
// В отличие от остальных тестов (которые работают через FakeHttpClient и
// не трогают сеть), этот требует интернет и бьёт по живому GitHub —
// запускай его отдельно, он не обязан проходить в офлайне/без сети.
#include <cassert>
#include <cstdio>
#include <string>

#include <dlm/SocketHttpClient.hpp>

int main() {
    dlm::SocketHttpClient client;

    // 1) Обычный GET по HTTPS, без Range — читаем небольшой текстовый файл.
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

    // 2) Range-запрос к тому же файлу — проверяем Content-Range/206 и что
    //    реально вернулся именно запрошенный диапазон байт.
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

    // 3) Редирект: github.com/... -> обычно 301/302 на другой хост.
    {
        dlm::HttpRequest request;
        request.url = "https://github.com/octocat/Hello-World/raw/master/README";

        std::string body;
        const dlm::HttpResponse response = client.perform(request, [&](const char* data, std::size_t n) {
            body.append(data, n);
            return true;
        });

        std::printf("[redirect GET] status=%ld bytes=%zu\n", response.statusCode, body.size());
        assert(response.statusCode == 200); // редирект должен быть пройден автоматически
        assert(!body.empty());
    }

    std::printf("test_socket_http_client_live: OK\n");
    return 0;
}
