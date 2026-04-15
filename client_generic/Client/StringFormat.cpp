#include "StringFormat.h"
#include <cstdarg>
#include <cstdio>
#include <vector>

std::string string_format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

#if defined(_MSC_VER) && (_MSC_VER < 1900)
    // Pre-VS2015: _vsnprintf doesn't return required size when buffer too small
    // (returns -1). Use a growing buffer instead.
    std::vector<char> buf(256);
    for (;;)
    {
        va_list argsCopy;
        va_copy(argsCopy, args);
        int ret = _vsnprintf(buf.data(), buf.size(), fmt, argsCopy);
        va_end(argsCopy);
        if (ret >= 0 && static_cast<size_t>(ret) < buf.size())
        {
            va_end(args);
            return std::string(buf.data(), static_cast<size_t>(ret));
        }
        if (buf.size() > 64 * 1024)
        {
            va_end(args);
            return "";
        }
        buf.resize(buf.size() * 2);
    }
#else
    // C11 / VS2015+: vsnprintf(nullptr, 0, ...) returns required length
    va_list argsCopy;
    va_copy(argsCopy, args);
    int size = std::vsnprintf(nullptr, 0, fmt, argsCopy);
    va_end(argsCopy);

    if (size < 0)
    {
        va_end(args);
        return "";
    }

    std::vector<char> buf(static_cast<size_t>(size) + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);

    return std::string(buf.data(), static_cast<size_t>(size));
#endif
}
