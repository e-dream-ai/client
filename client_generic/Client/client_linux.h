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
    CTextNotification.
    Transient centered on-screen notification that auto-removes after a duration.
*/
class CTextNotification : public Hud::CHudEntry
{
    DisplayOutput::spCBaseFont m_font;
    DisplayOutput::spCBaseText m_text;
    std::string m_msg;

  public:
    CTextNotification(const std::string& msg)
        : Hud::CHudEntry(Base::Math::CRect(0, 0, 1, 1)), m_msg(msg)
    {
    }

    bool Render(const double _time, DisplayOutput::spCRenderer _r) override
    {
        if (!Hud::CHudEntry::Render(_time, _r))
            return false;
        if (!_r) return true;

        // Lazy init — renderer guaranteed to exist by first Render call.
        if (!m_font)
        {
            DisplayOutput::CFontDescription d;
            d.Height(32);
            d.TypeFace("Lato");
            d.AntiAliased(true);
            m_font = _r->GetFont(d);
        }
        if (m_font && !m_text)
            m_text = _r->NewText(m_font, m_msg);
        if (!m_text) return true;

        auto spDisplay = _r->Display();
        if (spDisplay)
            m_text->SyncLayoutDisplay(spDisplay->Width(), spDisplay->Height());

        auto ext = m_text->GetExtent();
        if (ext.m_X < 0.0001f) return true;  // not ready yet

        const float pad = 0.012f;
        float x0 = 0.5f - ext.m_X * 0.5f - pad;
        float x1 = 0.5f + ext.m_X * 0.5f + pad;
        float y0 = 0.03f - pad;
        float y1 = 0.03f + ext.m_Y + pad;

        _r->Reset(DisplayOutput::eTexture | DisplayOutput::eShader | DisplayOutput::eBlend);
        _r->SetBlend("alphablend");
        _r->Apply();
        _r->DrawQuad(Base::Math::CRect(x0, y0, x1, y1), Base::Math::CVector4(0, 0, 0, 0.7f));

        m_text->SetRect(Base::Math::CRect(x0 + pad, y0 + pad, x1 - pad, y1 - pad));
        _r->DrawText(m_text, Base::Math::CVector4(1, 1, 1, 1));
        return true;
    }
};

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
        // Linux-only: exit
        case DisplayOutput::CKeyEvent::KEY_Esc:
            g_Player().Display()->Close();
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

        // Navigation
        case DisplayOutput::CKeyEvent::KEY_LEFT:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Previous dream");
            return true;
        case DisplayOutput::CKeyEvent::KEY_RIGHT:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Next dream");
            return true;
        case DisplayOutput::CKeyEvent::KEY_UP:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Liked");
            return true;
        case DisplayOutput::CKeyEvent::KEY_DOWN:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Disliked");
            return true;
        case DisplayOutput::CKeyEvent::KEY_J:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Skip back 10s");
            return true;
        case DisplayOutput::CKeyEvent::KEY_L:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Skip forward 10s");
            return true;

        // Playback speed
        case DisplayOutput::CKeyEvent::KEY_A:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Speed: " + getSpeedStr());
            return true;
        case DisplayOutput::CKeyEvent::KEY_D:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Speed: " + getSpeedStr());
            return true;

        // Playback control
        case DisplayOutput::CKeyEvent::KEY_R:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Repeat");
            return true;
        case DisplayOutput::CKeyEvent::KEY_G:
        {
            const auto nextMode = g_Player().CycleFrameGenerationMode();
            g_Log->Info("Linux key handler cycled frame generation to %s",
                        FrameGeneration::ToString(nextMode));
            showNotification("Frame generation: " +
                             std::string(FrameGeneration::ToString(nextMode)));
            return true;
        }
        case DisplayOutput::CKeyEvent::KEY_H:
            CElectricSheep::HandleOneEvent(spEvent);
            showNotification("Shuffle");
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

  private:
    std::string getSpeedStr()
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f fps", g_Player().GetPerceptualFPS());
        return buf;
    }

    void showNotification(const std::string& msg, float secs = 2.0f)
    {
        m_HudManager->Add("notification",
                          std::make_shared<CTextNotification>(msg),
                          static_cast<double>(secs));
    }
};

#endif // CLIENT_H_INCLUDED
