/*
        BASE.H
        Author: Keffo.

        Self-explanatory. Should be included first of all.
*/
#ifndef _BASE_H_
#define _BASE_H_

#ifdef LINUX_GNU
#include <inttypes.h>
#endif

#ifdef MAC
#include <os/signpost.h>
#endif

//	Lazy.
#define isBit(v, b) ((v) & (b))
#define setBit(v, b) ((v) |= (b))
#define remBit(v, b) ((v) &= ~(b))

/*
        Helper macros to make some stuff easier.

*/
#define SAFE_DELETE(p)                                                         \
    {                                                                          \
        if (p)                                                                 \
        {                                                                      \
            delete (p);                                                        \
            (p) = NULL;                                                        \
        }                                                                      \
    }
#define SAFE_DELETE_ARRAY(p)                                                   \
    {                                                                          \
        if (p)                                                                 \
        {                                                                      \
            delete[] (p);                                                      \
            (p) = NULL;                                                        \
        }                                                                      \
    }
#define SAFE_RELEASE(p)                                                        \
    {                                                                          \
        if (p)                                                                 \
        {                                                                      \
            (p)->Release();                                                    \
            (p) = NULL;                                                        \
        }                                                                      \
    }

//	Duh.
#define NO_DEFAULT_CTOR(_x) _x()

//	Disallows the compiler defined copy constructor.
#define NO_COPY_CTOR(_x) _x(const _x&)

//	Disallows the compiler defined assignment operator.
#define NO_ASSIGNMENT(_x) void operator=(const _x&)

//	Lazy.
#define NO_CLASS_STANDARDS(_x)                                                 \
    NO_ASSIGNMENT(_x);                                                         \
    NO_COPY_CTOR(_x)

//
#define PureVirtual 0

#ifndef NULL
#define NULL 0
#endif

#define USE_HW_ACCELERATION true

#ifndef USE_METAL
#if USE_HW_ACCELERATION
#undef USE_HW_ACCELERATION
#define USE_HW_ACCELERATION 0
#endif
#endif

#define UNFFERRTAG(tag)                                                        \
    (const char[])                                                             \
    {                                                                          \
        (char)(-tag & 0xFF), (char)((-tag >> 8) & 0xFF),                       \
            (char)((-tag >> 16) & 0xFF), (char)((-tag >> 24) & 0xFF), 0        \
    }

#define LOG_PROFILER_EVENTS 0

#if LOG_PROFILER_EVENTS
#define LOGEVENT(x, ...) g_Log->Info(x, ##__VA_ARGS__)
#else
#define LOGEVENT(x, ...)
#endif

#ifdef MAC
extern os_log_t g_SignpostHandle;
#define ES_SIGNPOST_BEGIN(...) os_signpost_interval_begin(__VA_ARGS__)
#define ES_SIGNPOST_END(...) os_signpost_interval_end(__VA_ARGS__)
#define ES_SIGNPOST_EVENT(...) os_signpost_event_emit(__VA_ARGS__)
#else
#define ES_SIGNPOST_BEGIN(...)
#define ES_SIGNPOST_END(...)
#define ES_SIGNPOST_EVENT(...)
#endif

#define PROFILER_BEGIN_F(eventName, format, ...)                               \
    do                                                                         \
    {                                                                          \
        ES_SIGNPOST_BEGIN(g_SignpostHandle, OS_SIGNPOST_ID_EXCLUSIVE,          \
                          eventName, format, ##__VA_ARGS__);                   \
        LOGEVENT("PROFILER BEGIN: " eventName " " format, ##__VA_ARGS__);      \
    } while (0)
#define PROFILER_BEGIN(eventName) PROFILER_BEGIN_F(eventName, "")
#define PROFILER_END_F(eventName, format, ...)                                 \
    do                                                                         \
    {                                                                          \
        ES_SIGNPOST_END(g_SignpostHandle, OS_SIGNPOST_ID_EXCLUSIVE, eventName, \
                        format, ##__VA_ARGS__);                                \
        LOGEVENT("PROFILER END: " eventName " " format, ##__VA_ARGS__);        \
    } while (0)
#define PROFILER_END(eventName) PROFILER_END_F(eventName, "")
#define PROFILER_EVENT_F(eventName, format, ...)                               \
    do                                                                         \
    {                                                                          \
        ES_SIGNPOST_EVENT(g_SignpostHandle, OS_SIGNPOST_ID_EXCLUSIVE,          \
                          eventName, format, ##__VA_ARGS__);                   \
        LOGEVENT("PROFILER EVENT: " eventName " " format, ##__VA_ARGS__);      \
    } while (0)
#define PROFILER_EVENT(eventName) PROFILER_EVENT_F(eventName, "")

/*
        Assert disco.

*/
#ifdef DEBUG
#include <assert.h>
#define ASSERT assert
#else
#define ASSERT(b) (b)
#endif

#ifdef __cplusplus
#include <memory>
#include <string>
#include <stdexcept>

template <typename... Args>
std::string string_format(const std::string& format, Args... args)
{
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) +
                 1; // Extra space for '\0'
    if (size_s <= 0)
    {
        throw std::runtime_error("Error during formatting.");
    }
    auto size = static_cast<size_t>(size_s);
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(),
                       buf.get() + size - 1); // We don't want the '\0' inside
}
#endif

#endif /*_BASE_H_*/
