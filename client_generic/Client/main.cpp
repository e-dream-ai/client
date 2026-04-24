#include <cstddef>
#include <cstdio>
#include <string>
#include <sys/types.h>
#if defined(WIN32) && !defined(_MSC_VER)
#include <dirent.h>
#endif
#include <cstring>
#ifdef WIN32
#include <process.h>
#include <windows.h>
#endif
#include <float.h>
#include <signal.h>

#include "client.h"
#include "FrameGeneration/FrameGenerationMode.h"

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
#else
int32_t main(int argc, char* argv[])
{
    // Parse our flags before glutInit so GLUT doesn't interfere.
    bool cachedOnlyMode = false;
    FrameGeneration::EFrameGenerationMode framegenMode = FrameGeneration::EFrameGenerationMode::Off;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--cached") == 0)
        {
            cachedOnlyMode = true;
        }
        else if (strncmp(argv[i], "--framegen=", 11) == 0)
        {
            const char* value = argv[i] + 11;
            if (strcmp(value, "blend_2x") == 0)
                framegenMode = FrameGeneration::EFrameGenerationMode::Blend2X;
            else if (strcmp(value, "rife") == 0)
                framegenMode = FrameGeneration::EFrameGenerationMode::RIFE;
            else
            {
                fprintf(stderr, "error: --framegen value must be blend_2x or rife, got: %s\n", value);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--framegen") == 0)
        {
            fprintf(stderr, "error: --framegen requires a value: --framegen=blend_2x or --framegen=rife\n");
            return 1;
        }
    }

#if defined(MAC) || (defined(USE_GLUT) && !defined(WIN32))
    glutInit(&argc, argv);
#endif
#endif

    //	Start log (unattached).
    g_Log->Startup();

    CElectricSheepClient client;
    client.SetCachedOnlyMode(cachedOnlyMode);
    client.SetFrameGenerationOverrideMode(framegenMode);

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
