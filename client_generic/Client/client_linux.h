#ifndef CLIENT_LINUX_H_INCLUDED
#define CLIENT_LINUX_H_INCLUDED

#ifdef WIN32
#error This file is not supposed to be used for this platform...
#endif

#include "DisplayVulkan.h"
#include "Exception.h"
#include "Log.h"
#include "MathBase.h"
#include "PlatformUtils.h"
#include "Player.h"
#include "Settings.h"
#include "SimplePlaylist.h"
#include "Timer.h"
#include "base.h"
#include "storage.h"
#include <cstdio>
#include <string>

/*
        CElectricSheep_Linux().
        Linux specific client code.
*/
class CElectricSheep_Linux : public CElectricSheep
{
    /*	std::vector<uint32_t> m_glContextList;*/

  public:
    CElectricSheep_Linux() : CElectricSheep()
    {
        printf("CElectricSheep_Linux()\n");
    }

    //
    virtual bool Startup()
    {
        using namespace DisplayOutput;

        printf("Startup()\n");

        m_AppData = std::string(getenv("HOME")) + "/.config/infinidream/";
        m_WorkingDir = PlatformUtils::GetWorkingDir();

        InitStorage();
        AttachLog();

        std::string tmp = "Working dir: " + m_WorkingDir;
        g_Log->Info(tmp.c_str());

        //	Run gui.

        // Start windowed by default; --fullscreen (m_StartFullscreen) opts into
        // fullscreen at launch. F toggles at runtime; ESC exits fullscreen.
        g_Player().Fullscreen(m_StartFullscreen);

        g_Player().AddDisplay(g_Settings()->Get("settings.player.screen", 0), nullptr);

        // if( true )
        {
            g_Log->Info("Running config...");
            // g_Settings()->Storage()->ConfigUI( m_WorkingDir +
            // "Scripts/config.lua"
            // );
        }

        if (CElectricSheep::Startup() == false)
            return false;

        //	Start downloader.	This should be moved to client.h!
        // g_Log->Info( "Starting downloader..." );
        // g_ContentDownloader().Startup( false /*m_ScrMode == ePreview*/ );
        // //	Removed for 2.7b11...

        return true;
    }

    virtual bool HandleOneEvent(DisplayOutput::spCEvent& spEvent) override
    {
        if (spEvent->Type() != DisplayOutput::CEvent::Event_KEY)
            return false;

        auto spKey = std::static_pointer_cast<DisplayOutput::CKeyEvent>(spEvent);
        if (!spKey->m_bPressed)
            return true; // swallow all key releases

        switch (spKey->m_Code)
        {
        // Linux-only: ESC exits fullscreen back to windowed (never quits).
        case DisplayOutput::CKeyEvent::KEY_Esc:
        {
            auto spDisp = g_Player().Display();
            if (spDisp && spDisp->IsFullscreen())
            {
                auto spVk = std::dynamic_pointer_cast<DisplayOutput::CDisplayVulkan>(spDisp);
                if (spVk) spVk->ToggleFullscreen(); // fullscreen -> windowed
            }
            return true;
        }

        // Linux-only: Ctrl+Q quits.
        case DisplayOutput::CKeyEvent::KEY_Q:
            if (spKey->m_bCtrl)
            {
                if (auto spDisp = g_Player().Display())
                    spDisp->Close();
                return true;
            }
            CElectricSheep::HandleOneEvent(spEvent);
            return true;

        // F: toggle fullscreen
        case DisplayOutput::CKeyEvent::KEY_F:
        {
            auto spDisp = std::dynamic_pointer_cast<DisplayOutput::CDisplayVulkan>(
                g_Player().Display());
            if (spDisp) spDisp->ToggleFullscreen();
            return true;
        }

        // Linux-only: HUD overlays
        case DisplayOutput::CKeyEvent::KEY_F1:
            m_F1F4Timer.Reset();
            m_HudManager->Toggle("helpmessage");
            return true;
        case DisplayOutput::CKeyEvent::KEY_F2:
            m_F1F4Timer.Reset();
            m_HudManager->Toggle("dreamstats");
            return true;
        case DisplayOutput::CKeyEvent::KEY_F3:
            m_F1F4Timer.Reset();
            m_HudManager->Toggle("renderstats");
            return true;
        case DisplayOutput::CKeyEvent::KEY_F4:
            m_F1F4Timer.Reset();
            m_HudManager->Toggle("displaystats");
            return true;

        // Linux-only: Ctrl+R opens the remote-control page in a browser
        case DisplayOutput::CKeyEvent::KEY_R:
            if (spKey->m_bCtrl) {
#ifdef STAGE
                PlatformUtils::OpenURLExternally("https://stage.infinidream.ai/rc");
#else
                PlatformUtils::OpenURLExternally("https://alpha.infinidream.ai/rc");
#endif
                return true;
            }
            CElectricSheep::HandleOneEvent(spEvent);
            return true;

        // Linux-only: Ctrl+B opens the playlists page in a browser
        case DisplayOutput::CKeyEvent::KEY_B:
            if (spKey->m_bCtrl) {
#ifdef STAGE
                PlatformUtils::OpenURLExternally("https://stage.infinidream.ai/playlists");
#else
                PlatformUtils::OpenURLExternally("https://alpha.infinidream.ai/playlists");
#endif
                return true;
            }
            CElectricSheep::HandleOneEvent(spEvent);
            return true;

        // Delegate everything else silently to parent
        default:
            CElectricSheep::HandleOneEvent(spEvent);
            return true;
        }
    }

    virtual bool HandleEvents()
    {
        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();

        //	Handle events.
        DisplayOutput::spCEvent spEvent;
        while (spDisplay->GetEvent(spEvent))
        {
            if (HandleOneEvent(spEvent) == false)
            {
                if (CElectricSheep::HandleOneEvent(spEvent) == false)
                    return false;
            }
        }

        ProcessCommandQueue();
        return true;
    }

    //
    bool Update()
    {
        using namespace DisplayOutput;

        g_Player().BeginFrameUpdate();
        if (!DoRealFrameUpdate(0))
            return false;
        g_Player().EndFrameUpdate();

        HandleEvents();

        return true;
    }

    //
    virtual void Shutdown()
    {
        try
        {
            printf("CElectricSheep_Linux::Shutdown()\n");
            CElectricSheep::Shutdown();
        }
        catch (Base::CException& _e)
        {
            _e.ReportCatch();
        }
    }

    /* gf: try
    void AddGLContext( uint32_t _glContext )
    {
            if ( g_Player().Display() == NULL )
            {
                    m_glContextList.push_back( _glContext );
            }
            else
            {
                    g_Player().AddDisplay( _glContext );
            }
            } */

};

#endif // CLIENT_H_INCLUDED
