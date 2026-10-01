#pragma once
#include <string>

namespace dlm {
// The application's own version.
inline constexpr const char* kVersion = "0.1.0";

// A string like "libcurl/8.x.x ...". Needed at Stage 0 only to prove that
// core is actually linked against libcurl and calls its code.
std::string httpBackendInfo();
}