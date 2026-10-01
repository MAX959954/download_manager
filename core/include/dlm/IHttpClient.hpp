#pragma once

#include "dlm/CancelToken.hpp"
#include "dlm/Types.hpp"

namespace dlm {

// Абстракция HTTP-бэкенда. Позволяет подменить libcurl на собственный
// сокетный клиент (Этап 8), не меняя Downloader.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // Выполняет запрос, передавая тело ответа в onData по мере получения.
    virtual HttpResponse perform(const HttpRequest& request, const WriteCallback& onData , 
                                                    const CancelToken * cancelToken = nullptr) = 0;
};

} // namespace dlm
