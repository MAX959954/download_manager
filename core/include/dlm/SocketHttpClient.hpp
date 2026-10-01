#pragma once

#include "dlm/IHttpClient.hpp"

namespace dlm {

// Минимальный HTTP/1.1-клиент на сырых сокетах + OpenSSL для TLS — своя
// реализация того, что под капотом делает libcurl, спрятанная за тем же
// IHttpClient, что и CurlHttpClient, так что Downloader не знает (и не
// должен знать) разницы между ними.
//
// Поддерживает: GET с произвольными заголовками (в т.ч. Range/If-Range),
// редиректы (301/302/303/307/308, до maxRedirects подряд), тело по
// Content-Length, chunked transfer-encoding, проверку сертификата сервера
// (включая имя хоста) и SNI.
//
// НЕ поддерживает (сознательное упрощение для учебного клиента):
// keep-alive — каждый perform() открывает новое TCP(+TLS)-соединение и
// всегда шлёт "Connection: close"; HTTP/2; сжатие ответа (gzip/br).
//
// На Windows SSL_CTX_set_default_verify_paths() полагается на переменные
// окружения OpenSSL, а не на системное хранилище сертификатов Windows —
// если проверка сертификата не проходит, укажи путь к CA-бандлу (например,
// https://curl.se/ca/cacert.pem) в переменной окружения DLM_CA_BUNDLE.
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
