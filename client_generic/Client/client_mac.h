#ifndef CLIENT_MAC_H_INCLUDED
#define CLIENT_MAC_H_INCLUDED

#ifndef MAC
#error This file is not supposed to be used for this platform...
#endif

#include <string>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <SystemConfiguration/SystemConfiguration.h>

#include "Exception.h"
#include "Log.h"
#include "MathBase.h"
#include "Player.h"
#include "Settings.h"
#include "SimplePlaylist.h"
#include "Splash.h"
#include "Timer.h"
#include "base.h"
#include "storage.h"
#include "dlfcn.h"
#include "libgen.h"

#include "../MacBuild/ESScreensaver.h"

#include "proximus.h"

/*
        CElectricSheep_Mac().
        Mac specific client code.
*/
class CElectricSheep_Mac : public CElectricSheep
{
    std::vector<CGraphicsContext> m_graphicsContextList;
    bool m_bIsPreview;
    UInt8 m_proxyHost[256];
    UInt8 m_proxyUser[32];
    UInt8 m_proxyPass[32];
    Boolean m_proxyEnabled;
    std::string m_verStr;
    int m_lckFile;

  public:
    CElectricSheep_Mac() : CElectricSheep()
    {
        printf("CElectricSheep_Mac()\n");

        m_bIsPreview = false;

        *m_proxyHost = 0;
        *m_proxyUser = 0;
        *m_proxyPass = 0;

        m_lckFile = -1;

        FSRef foundRef;

        UInt8 path[1024];

        OSErr err = FSFindFolder(kUserDomain, kDomainLibraryFolderType, false,
                                 &foundRef);

        if (err == noErr)
        {
            CFURLRef appSupportURL =
                CFURLCreateFromFSRef(kCFAllocatorDefault, &foundRef);
            if (appSupportURL != NULL)
            {
                if (CFURLGetFileSystemRepresentation(appSupportURL, true, path,
                                                     sizeof(path) - 1))
                {
                    m_AppData = "/Users/Shared/e-dream.ai/";
#ifndef SCREEN_SAVER
                    //m_AppData += "/Containers/com.apple.ScreenSaver.Engine.legacyScreenSaver/Data/Library";
#endif
                    //m_AppData += "/Application Support/e-dream.ai/";
                }

                CFRelease(appSupportURL);
            }
        }

        CFBundleRef bundle = CopyDLBundle_ex();

        if (bundle != NULL)
        {
            CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);

            if (resourcesURL != NULL)
            {
                if (CFURLGetFileSystemRepresentation(resourcesURL, true, path,
                                                     sizeof(path) - 1))
                {
                    m_WorkingDir = (char*)path;

                    m_WorkingDir += "/";
                }

                CFRelease(resourcesURL);
            }

            CFStringRef cfver =
                (CFStringRef)CFBundleGetValueForInfoDictionaryKey(
                    bundle, CFSTR("CFBundleShortVersionString"));

            char verstr[64];

            verstr[0] = '\0';

            CFStringGetCString(cfver, verstr, sizeof(verstr) - 1,
                               kCFStringEncodingASCII);

            m_verStr.assign(verstr);

            CFRelease(bundle);
        }

        GetClientProxy();
    }

    virtual ~CElectricSheep_Mac() {}

    CFStringRef GetBundleVersion()
    {
        CFBundleRef bundle = CopyDLBundle_ex();

        if (bundle == NULL)
            return NULL;

        CFStringRef verStr = (CFStringRef)CFBundleGetValueForInfoDictionaryKey(
            bundle, CFSTR("CFBundleShortVersionString"));

        CFRelease(bundle);

        return (CFStringRef)verStr;
    }

    void GetClientProxy(void)
    {
        m_proxyEnabled = get_proxy_for_server105(
            (const UInt8*)DREAM_SERVER, m_proxyHost, sizeof(m_proxyHost) - 1,
            m_proxyUser, sizeof(m_proxyUser) - 1, m_proxyPass,
            sizeof(m_proxyPass) - 1);
    }

    void AddGraphicsContext(CGraphicsContext _context)
    {
        if (g_Player().Display() == NULL)
        {
            m_graphicsContextList.push_back(_context);
        }
        else
        {
#ifdef DO_THREAD_UPDATE
            boost::mutex::scoped_lock lock(m_BarrierMutex);

            DestroyUpdateThreads();
#endif

            g_Player().AddDisplay(_context);

#ifdef DO_THREAD_UPDATE
            CreateUpdateThreads();
#endif
        }
    }

    void SetIsPreview(bool _isPreview) { m_bIsPreview = _isPreview; }

    void SetUpConfig(void) { InitStorage(); }

    //
    virtual bool Startup()
    {
        using namespace DisplayOutput;

        printf("Startup()\n");

        InitStorage();

        AttachLog();

        // if m_proxyHost is set, the proxy resolver found one. If not, we
        // should not override preferences.
        if (*m_proxyHost)
        {
            g_Settings()->Set("settings.content.use_proxy",
                              (bool)(m_proxyEnabled != 0));

            g_Settings()->Set("settings.content.proxy",
                              std::string((char*)m_proxyHost));

            // for now the resolver doesn't support user/password combination,
            // so we should not touch it so user can set it in prefs.
            // g_Settings()->Set( "settings.content.proxy_username",
            // std::string( (char*)m_proxyUser ) ); g_Settings()->Set(
            // "settings.content.proxy_password", std::string(
            // (char*)m_proxyPass ) );
        }

        std::string tmp = "Working dir: " + m_WorkingDir;
        g_Log->Info(tmp.c_str());

        std::vector<CGraphicsContext>::const_iterator it =
            m_graphicsContextList.begin();

        for (; it != m_graphicsContextList.end(); it++)
        {
            g_Player().AddDisplay(*it);
        }

        m_graphicsContextList.clear();

        // check the exclusive file lock to see if we are running alone...
        std::string lockfile = m_AppData + ".instance-lock";

        m_lckFile =
            open(lockfile.c_str(), O_WRONLY + O_EXLOCK + O_NONBLOCK + O_CREAT,
                 S_IWUSR + S_IWGRP + S_IWOTH);

        m_MultipleInstancesMode = (m_lckFile < 0);

        return CElectricSheep::Startup();
    }

    virtual void Shutdown()
    {
        CElectricSheep::Shutdown();

        if (m_lckFile >= 0)
        {
            close(m_lckFile);
            m_lckFile = -1;
        }
    }

    //
    virtual bool Update(boost::barrier& _beginFrameBarrier,
                        boost::barrier& _endFrameBarrier)
    {
        using namespace DisplayOutput;

        PROFILER_BEGIN("Render Frame");

        g_Player().Framerate(m_CurrentFps);

        if (!CElectricSheep::Update(_beginFrameBarrier, _endFrameBarrier))
            return false;

        //	Update display events.
        g_Player().Display()->Update();

        HandleEvents();

        PROFILER_END("Renderer Frame");

        return true;
    }

    virtual int GetACLineStatus()
    {
        CFTypeRef blob = IOPSCopyPowerSourcesInfo();
        CFArrayRef sources = IOPSCopyPowerSourcesList(blob);

        CFDictionaryRef pSource = NULL;

        CFIndex srcCnt = CFArrayGetCount(sources);

        if (srcCnt == 0)
        {
            CFRelease(blob);
            CFRelease(sources);
            return -1; // Could not retrieve battery information.  System may
                       // not have a battery.
        }

        bool charging = false;

        for (CFIndex i = 0; i < srcCnt; i++)
        {
            pSource = IOPSGetPowerSourceDescription(
                blob, CFArrayGetValueAtIndex(sources, i));
            CFStringRef cfStateStr = (CFStringRef)CFDictionaryGetValue(
                pSource, CFSTR(kIOPSPowerSourceStateKey));

            if (cfStateStr != NULL &&
                CFEqual(cfStateStr, CFSTR(kIOPSACPowerValue)))
            {
                charging = true;
                break;
            }
        }

        CFRelease(blob);
        CFRelease(sources);

        return (charging ? 1 : 0);
    }
};

#endif // CLIENT_H_INCLUDED
