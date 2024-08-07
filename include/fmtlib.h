#pragma once
#include <version>

#ifdef __cpp_lib_format

#include <format>

#else

#include <fmt/format.h>

namespace std {

using fmt::format;
using fmt::format_string;

} // namespace std

#endif
