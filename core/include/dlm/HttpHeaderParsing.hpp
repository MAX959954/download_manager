#pragma once

#include <string>

#include "dlm/Types.hpp"

namespace dlm::detail {

std::string toLowerAscii(std::string s);
std::string trimAscii(const std::string& s);

// Parses a status line like "HTTP/1.1 206 Partial Content".
// Returns false if the line doesn't look like an HTTP status line.
bool parseStatusLine(const std::string& line, long& statusCode);

// Splits "Name: value" into a lowercase name and a whitespace-trimmed
// value. Returns false if the line has no colon at all.
bool parseHeaderFieldLine(const std::string& line, std::string& name, std::string& value);

// Applies one already-parsed header to an HttpResponse. The same logic
// that used to be hardcoded only in CurlHttpClient::headerThunk — pulled
// out here so both the libcurl client and the raw-socket client can use
// it, without duplicating the code and risking it drifting apart.
void applyHeaderField(const std::string& name, const std::string& value, HttpResponse& response);

} // namespace dlm::detail
