//
//  StringFormat.cpp
//  e-dream
//
//  Created by Tibi Hencz on 28.12.2023.
//

#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>
#ifdef WIN32
#include <cstdarg>
#endif // WIN32


std::string string_format(std::string_view _format, ...)
{
    va_list ArgPtr;
    va_start(ArgPtr, _format);
    int size_s = std::vsnprintf(nullptr, 0, _format.data(), ArgPtr) +
                 1; // Extra space for '\0'
    va_end(ArgPtr);
    if (size_s <= 0)
    {
        throw std::runtime_error("Error during formatting.");
    }
    auto size = (size_t)size_s;
    std::unique_ptr<char[]> buf(new char[size]);
    va_start(ArgPtr, _format);
    std::vsnprintf(buf.get(), size, _format.data(), ArgPtr);
    va_end(ArgPtr);
    return std::string(buf.get(),
                       buf.get() + size - 1); // We don't want the '\0' inside
}
