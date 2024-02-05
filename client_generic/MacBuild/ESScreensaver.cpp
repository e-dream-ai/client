#include <sys/stat.h>

#include "ESScreensaver.h"
#include "DisplayMetal.h"
#include "client.h"
#include "client_mac.h"
#include <GLUT/glut.h>
#include <OpenGL/OpenGL.h>

CElectricSheep_Mac gClient;

CFBundleRef CopyDLBundle_ex(void)
{

    CFBundleRef bundle = NULL;
    Dl_info info;

    if (dladdr((const void*)__func__, &info))
    {

        const char* bundle_path = dirname((char*)info.dli_fname);

        do
        {
            if (bundle != NULL)
            {
                CFRelease(bundle);
                bundle = NULL;
            }

            CFURLRef bundleURL = CFURLCreateFromFileSystemRepresentation(
                kCFAllocatorDefault, (UInt8*)bundle_path,
                (CFIndex)strlen(bundle_path), true);

            bundle = CFBundleCreate(kCFAllocatorDefault, bundleURL);

            CFRelease(bundleURL);

            if (bundle == NULL)
                return NULL;

            if (CFBundleGetValueForInfoDictionaryKey(
                    bundle, kCFBundleExecutableKey) != NULL)
            {
                break;
            }
            bundle_path = dirname((char*)bundle_path);
        } while (strcmp(bundle_path, "."));
    }

    return (bundle);
}

void ESScreenSaver_AddGraphicsContext(void* _glContext)
{
    gClient.AddGraphicsContext((CGraphicsContext)_glContext);
}

bool ESScreensaver_Start(bool _bPreview, uint32 _width, uint32 _height)
{
    if (g_Player().Display() == NULL)
    {
        DisplayOutput::CDisplayMetal::SetDefaultWidthAndHeight(_width, _height);

        gClient.SetIsPreview(_bPreview);

        gClient.Startup();
        // we should stop the player, if it is started by default, just to be
        // sure. g_Player().Stop();
    }

    // g_Player().Display()->SetContext((CGLContextObj)_glContext);
    g_Player().ForceWidthAndHeight(0, _width, _height);

    // g_Player().SetGLContext((CGLContextObj)_glContext, _bPreview);

    // if (g_Player().Renderer() != NULL)
    //{
    // g_Player().Renderer()->Defaults();
    //}

    // if (g_Player().Stopped())
    // g_Player().Start();

    return true;
}

bool ESScreensaver_DoFrame(boost::barrier& _beginFrameBarrier,
                           boost::barrier& _endFrameBarrier)
{
    bool retval = true;

    if (gClient.Update(_beginFrameBarrier, _endFrameBarrier) == false)
    {
        retval = false;
    }

    return retval;
}

void ESScreensaver_Stop(void) { g_Player().Stop(); }

bool ESScreensaver_Stopped(void) { return g_Player().Stopped(); }

void ESScreensaver_ForceWidthAndHeight(uint32 _width, uint32 _height)
{
    g_Player().ForceWidthAndHeight(0, _width, _height);
}

void ESScreensaver_Deinit(void) { gClient.Shutdown(); }

void ESScreensaver_AppendKeyEvent(DisplayOutput::CKeyEvent::eKeyCode keyCode)
{
    using namespace DisplayOutput;

    if (g_Player().Display() != NULL)
    {
        spCKeyEvent spEvent = std::make_shared<CKeyEvent>();
        spEvent->m_bPressed = true;
        spEvent->m_Code = keyCode;

        g_Player().Display()->AppendEvent(spEvent);
    }
}

CFStringRef ESScreensaver_GetVersion(void)
{
    return gClient.GetBundleVersion();
}

void ESScreensaver_InitClientStorage(void) { gClient.SetUpConfig(); }

CFStringRef ESScreensaver_CopyGetRoot(void)
{
    std::string root = g_Settings()->Get("settings.content.sheepdir",
                                         g_Settings()->Root() + "content");

    if (root.empty())
    {
        root = g_Settings()->Root() + "content";
        g_Settings()->Set("settings.content.sheepdir", root);
    }

    return CFStringCreateWithCString(NULL, root.c_str(), kCFStringEncodingUTF8);
}

CFStringRef ESScreensaver_CopyGetStringSetting(const char* url,
                                               std::string_view defval)
{
    std::string val = g_Settings()->Get(std::string(url), std::string(defval));

    return CFStringCreateWithCString(NULL, val.c_str(), kCFStringEncodingUTF8);
}

SInt32 ESScreensaver_GetIntSetting(const char* url, const SInt32 defval)
{
    return g_Settings()->Get(std::string(url), defval);
}

bool ESScreensaver_GetBoolSetting(const char* url, const bool defval)
{
    return g_Settings()->Get(std::string(url), (bool)defval);
}

double ESScreensaver_GetDoubleSetting(const char* url, const double defval)
{
    return g_Settings()->Get(std::string(url), defval);
}

void ESScreensaver_SetStringSetting(const char* url, const char* val)
{
    g_Settings()->Set(std::string(url), std::string(val));
}

void ESScreensaver_SetIntSetting(const char* url, const SInt32 val)
{
    g_Settings()->Set(std::string(url), val);
}

void ESScreensaver_SetBoolSetting(const char* url, const bool val)
{
    g_Settings()->Set(std::string(url), val);
}

void ESScreensaver_SetDoubleSetting(const char* url, const double val)
{
    g_Settings()->Set(std::string(url), val);
}

void ESScreensaver_DeinitClientStorage(void)
{
    g_Settings()->Storage()->Commit();
}

void ESScreensaver_SetUpdateAvailable(const char* verinfo)
{
    gClient.SetUpdateAvailable(verinfo);
}

void ESScreensaver_SetIsFullScreen(bool _bFullScreen)
{
    gClient.SetIsFullScreen(_bFullScreen);
}

#include <boost/filesystem.hpp>
using namespace boost::filesystem;

static uint64 GetFlockSizeBytes(const std::string& path, int sheeptype)
{
    std::string mp4path(path);

    if (mp4path.substr(mp4path.size() - 1, 1) != "/")
        mp4path += "/";
    uint64 retval = 0;

    try
    {
        boost::filesystem::path p(mp4path.c_str());

        directory_iterator end_itr; // default construction yields past-the-end
        for (directory_iterator itr(p); itr != end_itr; ++itr)
        {
            if (!is_directory(itr->status()))
            {
                std::string fname(itr->path().filename().string());

                std::string ext(itr->path().extension().string());

                if (ext == std::string(".mp4"))
                {
                    int generation;
                    int id;
                    int first;
                    int last;
                    struct stat sbuf;

                    if (stat((mp4path + "/" + fname).c_str(), &sbuf) == 0)
                        retval += (uint64)sbuf.st_size;
                }
            }
            else
                retval += GetFlockSizeBytes(itr->path().string(), sheeptype);
        }
    }
    catch (boost::filesystem::filesystem_error& err)
    {
        g_Log->Error("Path enumeration threw error: %s", err.what());
        return 0;
    }
    return retval;
}

size_t ESScreensaver_GetFlockSizeMBs(const char* mp4path, int sheeptype)
{
    return GetFlockSizeBytes(mp4path, sheeptype) / 1024 / 1024;
}
