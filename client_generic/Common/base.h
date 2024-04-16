/*
        BASE.H
        Author: Keffo.

        Self-explanatory. Should be included first of all.
*/
#ifndef _BASE_H_
#define _BASE_H_

#include <atomic>

#ifdef LINUX_GNU
#include <inttypes.h>
#endif

#ifdef MAC
#include <os/signpost.h>
#endif




#define DISPATCH_ONCE(flag, lambda)                                            \
    do                                                                         \
    {                                                                          \
        static std::atomic_flag once_flag = ATOMIC_FLAG_INIT;                  \
        if (!once_flag.test_and_set())                                         \
        {                                                                      \
            lambda();                                                          \
        }                                                                      \
    } while (0)

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
/*
#ifdef __cplusplus

extern "C++"
{
    template <size_t S> struct _ENUM_FLAG_INTEGER_FOR_SIZE;
    template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<1>
    {
        typedef uint8_t type;
    };
    template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<2>
    {
        typedef uint16_t type;
    };
    template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<4>
    {
        typedef uint32_t type;
    };
    template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<8>
    {
        typedef uint64_t type;
    };

    // used as an approximation of std::underlying_type<T>
    template <class T> struct _ENUM_FLAG_SIZED_INTEGER
    {
        typedef typename _ENUM_FLAG_INTEGER_FOR_SIZE<sizeof(T)>::type type;
    };
}

#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE)                                   \
    extern "C++"                                                               \
    {                                                                          \
        inline ENUMTYPE operator|(ENUMTYPE a, ENUMTYPE b) throw()              \
        {                                                                      \
            return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) |    \
                            ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));    \
        }                                                                      \
        inline ENUMTYPE& operator|=(ENUMTYPE& a, ENUMTYPE b) throw()           \
        {                                                                      \
            return (                                                           \
                ENUMTYPE&)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type&)a) |=   \
                           ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));     \
        }                                                                      \
        inline ENUMTYPE operator&(ENUMTYPE a, ENUMTYPE b) throw()              \
        {                                                                      \
            return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) &    \
                            ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));    \
        }                                                                      \
        inline ENUMTYPE& operator&=(ENUMTYPE& a, ENUMTYPE b) throw()           \
        {                                                                      \
            return (                                                           \
                ENUMTYPE&)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type&)a) &=   \
                           ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));     \
        }                                                                      \
        inline ENUMTYPE operator~(ENUMTYPE a) throw()                          \
        {                                                                      \
            return ENUMTYPE(~((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a));   \
        }                                                                      \
        inline ENUMTYPE operator^(ENUMTYPE a, ENUMTYPE b) throw()              \
        {                                                                      \
            return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) ^    \
                            ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));    \
        }                                                                      \
        inline ENUMTYPE& operator^=(ENUMTYPE& a, ENUMTYPE b) throw()           \
        {                                                                      \
            return (                                                           \
                ENUMTYPE&)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type&)a) ^=   \
                           ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));     \
        }                                                                      \
    }
#else
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE) // NOP, C allows these operators.
#endif*/

#endif /*_BASE_H_*/
