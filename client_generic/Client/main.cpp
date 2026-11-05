#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#if defined(WIN32) && !defined(_MSC_VER)
#include <dirent.h>
#endif
#include <cstring>
#ifdef WIN32
#include <process.h>
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW — must follow windows.h
#include <cwchar>
#pragma comment(lib, "shell32.lib")
#endif
#include <float.h>
#include <signal.h>

#include "client.h"

#ifdef WIN32
#include "client_win32.h"
typedef CElectricSheep_Win32 CElectricSheepClient;
#else
#ifdef MAC
#include "client_mac.h"
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
typedef CElectricSheep_Mac CElectricSheepClient;
#else  // Linux
#include "client_linux.h"
typedef CElectricSheep_Linux CElectricSheepClient;
#endif
#endif

//
#ifdef WIN32
int32_t APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                         LPSTR lpCmdLine, int nCmdShow)
{
    // lpCmdLine is ANSI and untokenised. CommandLineToArgvW splits the wide command
    // line with the same quoting rules the CRT gives argv, so we get argv semantics
    // without hand-rolling a parser. Screensaver flags (/s, /c, /p) are parsed
    // separately in client_win32.h off GetCommandLineA(); this only wants --cached.
    bool cachedOnlyMode = false;
    int wideArgCount = 0;
    LPWSTR* wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideArgCount);
    if (wideArgs != nullptr)
    {
        for (int i = 1; i < wideArgCount; ++i)
            if (wcscmp(wideArgs[i], L"--cached") == 0) { cachedOnlyMode = true; break; }

        LocalFree(wideArgs);
    }
#else
#if !defined(WIN32) && !defined(MAC)
// Mesa's Intel Vulkan driver only exposes Vulkan Video when ANV_DEBUG contains
// video-decode, and reads it when a Vulkan instance is created. Must run before
// that and before any other thread starts, because setenv isn't thread-safe.
// Other drivers ignore the variable; CRendererVulkan decides whether to use it.
static void EnableIntelVulkanVideoDecode()
{
    if (getenv("INFINIDREAM_NO_HW_DECODE"))
        return;
    const char* current = getenv("ANV_DEBUG");
    if (!current || !*current)
        setenv("ANV_DEBUG", "video-decode", 1);
    else if (!strstr(current, "video-decode"))
        setenv("ANV_DEBUG", (std::string(current) + ",video-decode").c_str(), 1);
}
#endif

int32_t main(int argc, char* argv[])
{
#if !defined(WIN32) && !defined(MAC)
    EnableIntelVulkanVideoDecode();
#endif
    // Parse our flags before glutInit so GLUT doesn't interfere.
    bool cachedOnlyMode = false;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--cached") == 0) { cachedOnlyMode = true; break; }

#if !defined(MAC)  // Linux: --fullscreen starts fullscreen; default is windowed.
    bool startFullscreen = false;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--fullscreen") == 0) { startFullscreen = true; break; }
#endif

#if defined(MAC) || (defined(USE_GLUT) && !defined(WIN32))
    glutInit(&argc, argv);
#endif
#endif

    //	Start log (unattached).
    g_Log->Startup();

    CElectricSheepClient client;
    client.SetCachedOnlyMode(cachedOnlyMode);
#if !defined(WIN32) && !defined(MAC)
    client.SetStartFullscreen(startFullscreen);
#endif

    if (client.Startup())
        client.Run();

    //    g_Log->Info( "Raising access violation...\n" );
    //    asm( "movl $0, %eax" );
    //    asm( "movl $1, (%eax)" );

    //    __asm("int3");

    client.Shutdown();

    //g_Log->Shutdown();

    return 0;
}
