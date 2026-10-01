#pragma once

#include <string>

#include "dlm/Types.hpp"

namespace dlm::detail {

std::string toLowerAscii(std::string s);
std::string trimAscii(const std::string& s);

// Парсит строку статуса вида "HTTP/1.1 206 Partial Content".
// Возвращает false, если строка не похожа на HTTP status line.
bool parseStatusLine(const std::string& line, long& statusCode);

// Разбивает "Name: value" на lowercase-имя и обрезанное по пробелам
// значение. Возвращает false, если двоеточия в строке вообще нет.
bool parseHeaderFieldLine(const std::string& line, std::string& name, std::string& value);

// Применяет один уже распарсенный заголовок к HttpResponse. Та же логика,
// что раньше была зашита только в CurlHttpClient::headerThunk — вынесена
// сюда, чтобы ей мог пользоваться и libcurl-клиент, и сокетный клиент на
// сырых сокетах, не дублируя код и не рискуя им разойтись.
void applyHeaderField(const std::string& name, const std::string& value, HttpResponse& response);

} // namespace dlm::detail
