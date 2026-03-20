#ifndef CLIENT_LINUX_H_INCLUDED
#define CLIENT_LINUX_H_INCLUDED

#ifdef WIN32
#error This file is not supposed to be used for this platform...
#endif

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

        if (!CElectricSheep::Update(0))
            return false;

        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();

        //	Update display events.
        spDisplay->Update();

        //	Handle events.
        spCEvent spEvent;
        while (spDisplay->GetEvent(spEvent))
        {
            //	Key events.
            if (spEvent->Type() == CEvent::Event_KEY)
            {
                spCKeyEvent spKey = std::static_pointer_cast<CKeyEvent>(spEvent);

                switch (spKey->m_Code)
                {
                case DisplayOutput::CKeyEvent::KEY_LEFT:
                    g_Player().ReturnToPrevious();
                    break;

                case DisplayOutput::CKeyEvent::KEY_RIGHT:
                    g_Player().SkipToNext();
                    break;

                case DisplayOutput::CKeyEvent::KEY_F1:
                    m_F1F4Timer.Reset();
                    m_HudManager->Toggle("helpmessage");
                    break;
                case DisplayOutput::CKeyEvent::KEY_F2:
                    m_F1F4Timer.Reset();
                    m_HudManager->Toggle("serverstats");
                    break;
                case DisplayOutput::CKeyEvent::KEY_F3:
                    m_F1F4Timer.Reset();
                    m_HudManager->Toggle("renderstats");
                    break;
                case DisplayOutput::CKeyEvent::KEY_F4:
                    m_F1F4Timer.Reset();
                    m_HudManager->Toggle("displaystats");
                    break;

                case CKeyEvent::KEY_Esc:
                    spDisplay->Close();
                    break;

                default:
                    break;
                }
            }
        }

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
