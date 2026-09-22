#pragma once
#include <string>

namespace dlm {
// Версия самого приложения.
inline constexpr const char* kVersion = "0.1.0";

// Строка вида "libcurl/8.x.x ...". Нужна на Этапе 0 только чтобы доказать,
// что core реально слинкован с libcurl и вызывает его код.
std::string httpBackendInfo();
}