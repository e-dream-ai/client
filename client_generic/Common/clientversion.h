#ifndef CLIENTVERSION_H_INCLUDED
#define CLIENTVERSION_H_INCLUDED

#define VER_MAJOR "0"
#define VER_MINOR "1"
#define VER_BUILD "0"

#if defined(WIN32)
#define OS_PREFIX "WIN_"
#define OS_PREFIX_PRETTY "Windows "
#elif defined(MAC)
#define OS_PREFIX "MACOS_"
#define OS_PREFIX_PRETTY "macOS "
#else
#define OS_PREFIX "LINUX_"
#define OS_PREFIX_PRETTY "Linux "
#endif

#define CLIENT_VERSION OS_PREFIX VER_MAJOR "." VER_MINOR "." VER_BUILD
#define CLIENT_VERSION_PRETTY                                                  \
    OS_PREFIX_PRETTY VER_MAJOR "." VER_MINOR "." VER_BUILD
#define CLIENT_SETTINGS "settings"

#define BETA_RELEASE

#endif // CLIENTVERSION_H_INCLUDED
