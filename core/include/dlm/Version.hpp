#pragma once
#include <string>

namespace dlm {

// Версия приложения.
inline constexpr const char* kVersion = "0.1.0";

// Строка вида "libcurl/8.x.x Schannel" — на Этапе 0 доказывает,
// что core реально слинкован с libcurl.
std::string httpBackendInfo();

} // namespace dlm