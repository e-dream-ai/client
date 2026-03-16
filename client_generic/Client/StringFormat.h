#ifndef STRINGFORMAT_H
#define STRINGFORMAT_H

#include <string>

/// printf-style formatting returning std::string.
/// Uses same format specifiers as printf (e.g. %s, %d, %u, %f).
std::string string_format(const char* fmt, ...);

#endif // STRINGFORMAT_H
