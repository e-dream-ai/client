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

#ifdef WIN32
class ExceptionHandler
{
  public:
    ExceptionHandler() { LoadLibraryA("exchndl.dll"); }
};

static ExceptionHandler gExceptionHandler; //  global instance of class

#include "client_win32.h"
typedef CElectricSheep_Win32 CElectricSheepClient;
#else
#ifdef MAC
#include "client_mac.h"
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
typedef CElectricSheep_Mac CElectricSheepClient;
#else
#include "client_linux.h"
#include <GL/gl.h>
#include <GL/glut.h>
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
    glutInit(&argc, argv);
#endif

    //	Start log (unattached).
    g_Log->Startup();

    CElectricSheepClient client;

    if (client.Startup())
        client.Run();

    //    g_Log->Info( "Raising access violation...\n" );
    //    asm( "movl $0, %eax" );
    //    asm( "movl $1, (%eax)" );

    //    __asm("int3");

    client.Shutdown();

    g_Log->Shutdown();

    return 0;
}
