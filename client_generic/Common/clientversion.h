#ifndef CLIENTVERSION_H_INCLUDED
#define CLIENTVERSION_H_INCLUDED

// Not the app's version. The version shown in Settings/About, reported to the
// server and packaged by the installer comes from build.py -v on Windows
// (MSVC/edream_build_version_generated.h, written by
// WinBuild/generate_build_version.py; the latest git tag for dev builds) and
// from Info.plist on the Mac. VER_* only feed the legacy CLIENT_VERSION
// strings below (e.g. the Windows single-instance mutex name).
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
/* Login redirect host (query.php?q=redir); override in build if needed. */
#define REDIRECT_SERVER "electricsheep.org"
#define CLIENT_SETTINGS "settings"

#define BETA_RELEASE

#endif // CLIENTVERSION_H_INCLUDED
