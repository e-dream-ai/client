// Minimal stand-in for Common/Log.h so sio_beast.cpp builds alone in this test.
#pragma once
#include <cstdarg>
#include <cstdio>

namespace sio_test
{
struct Log
{
    void Print(const char* level, const char* fmt, va_list ap)
    {
        if (!verbose)
            return;
        std::fprintf(stderr, "[%s] ", level);
        std::vfprintf(stderr, fmt, ap);
        std::fputc('\n', stderr);
    }
    void Info(const char* fmt, ...) { va_list ap; va_start(ap, fmt); Print("info", fmt, ap); va_end(ap); }
    void Warning(const char* fmt, ...) { va_list ap; va_start(ap, fmt); Print("warn", fmt, ap); va_end(ap); }
    void Error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); Print("error", fmt, ap); va_end(ap); }
    bool verbose = false;
};
inline Log g_log;
} // namespace sio_test

#define g_Log (&::sio_test::g_log)
