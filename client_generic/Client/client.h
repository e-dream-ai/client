#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <iomanip>
#include <sstream>
#include <string>

#include "Exception.h"
#include "Log.h"
#include "Networking.h"
#include "PlayCounter.h"
#include "Player.h"
#include "Clip.h"
#include "Settings.h"
#include "base.h"
#include "clientversion.h"
#include "storage.h"

#include "ContentDownloader.h"
#include "CrossFade.h"
#include "EDreamClient.h"
#include "Hud.h"
#include "Matrix.h"
#include "ServerMessage.h"
#include "Shepherd.h"
#include "Splash.h"
#include "StartupScreen.h"
#include "StatsConsole.h"
#include "TextureFlat.h"
#include "Timer.h"
#include "Voting.h"
#include "PlatformUtils.h"
#include "StringFormat.h"

#if defined(WIN32) && defined(_MSC_VER)
#include "../msvc/cpu_usage_win32.h"
#include "../msvc/msvc_fix.h"
#else
#if defined(LINUX_GNU)
#include "cpu_usage_linux.h"
#include <limits.h>
#else
#include "cpu_usage_mac.h"
#endif
#endif

#ifdef MAC
#define FULLSCREEN_MODIFIER_KEY "Cmd"
#else
#define FULLSCREEN_MODIFIER_KEY "Control"
#endif

typedef void (*ShowPreferencesCallback_t)();
extern void ESSetShowPreferencesCallback(ShowPreferencesCallback_t);
extern void ESShowPreferences();

/*
        CElectricSheep().
        Prime mover for the client, used from main.cpp...
*/
class CElectricSheep
{
  protected:
    ESCpuUsage m_CpuUsage;
    double m_LastCPUCheckTime;
    int m_CpuUsageTotal;
    int m_CpuUsageES;
    int m_HighCpuUsageCounter;
    int m_CpuUsageThreshold;
    bool m_MultipleInstancesMode;
    bool m_bConfigMode;
    bool m_SeamlessPlayback;
    bool m_bPaused;
    bool m_bFullScreen;
    Base::CTimer m_Timer;
    Base::CTimer m_F1F4Timer;

    Hud::spCHudManager m_HudManager;
    Hud::spCStartupScreen m_StartupScreen;

    double m_PNGDelayTimer;

    //	The base framerate(from config).
    double m_PlayerFps;

    //	Potentially adjusted framerate(from <- and -> keys)
    double m_CurrentFps;

    double m_OriginalFps;

    //	Voting object.
    CVote* m_pVoter;

    //	Default root directory, ie application data.
    std::string m_AppData;

    //	Default application working directory.
    std::string m_WorkingDir;

    //	Splash images.
    Hud::spCSplash m_spSplashPos;
    Hud::spCSplash m_spSplashNeg;

    // Splash PNG
    Hud::spCSplashImage m_spSplashPNG;
    Base::CTimer m_SplashPNGDelayTimer;
    int m_nSplashes;
    std::string m_SplashFilename;

    Hud::spCCrossFade m_spCrossFade;

    std::deque<std::string> m_ConnectionErrors;

#ifdef DO_THREAD_UPDATE
    boost::barrier* m_pUpdateBarrier;

    boost::thread_group* m_pUpdateThreads;

    std::mutex m_BarrierMutex;
#endif

    //	Init tuplestorage.
    bool InitStorage(bool _bReadOnly = false)
    {
        g_Log->Info("InitStorage()");
#ifndef LINUX_GNU
        if (g_Settings()->Init(m_AppData, m_WorkingDir, _bReadOnly) == false)
#else
        char appdata[PATH_MAX];
        snprintf(appdata, PATH_MAX, "%s/.electricsheep/", getenv("HOME"));
        if (g_Settings()->Init(appdata, SHAREDIR) == false)
#endif
            return false;

            //	Trigger this to exist in the settings.
#ifndef LINUX_GNU
        g_Settings()->Set("settings.app.InstallDir", m_WorkingDir);
#else
        g_Settings()->Set("settings.app.InstallDir", SHAREDIR);
#endif
        return true;
    }

    //	Attach log.
    void AttachLog()
    {
        TupleStorage::IStorageInterface::CreateFullDirectory(m_AppData +
                                                             "Logs/");
        if (g_Settings()->Get("settings.app.log", true))
            g_Log->Attach(m_AppData + "Logs/");
        g_Log->Info("AttachLog()");

        g_Log->Info("******************* %s (Built %s / %s)...\n",
                    CLIENT_VERSION_PRETTY, __DATE__, __TIME__);
    }

  public:
    CElectricSheep()
    {
        PlatformUtils::SetThreadName("Main");
        m_CpuUsageTotal = -1;
        m_CpuUsageES = -1;
        m_CpuUsageThreshold = 50;
        m_HighCpuUsageCounter = 0;
        m_bConfigMode = false;
        m_MultipleInstancesMode = false;
        printf("CElectricSheep()\n");

        m_pVoter = nullptr;
#ifndef LINUX_GNU
        m_AppData = "./.ElectricSheep/";
        m_WorkingDir = "./";
#else
        m_AppData = std::string(getenv("HOME")) + "/.electricsheep/";
        m_WorkingDir = SHAREDIR;
#endif
#ifdef DO_THREAD_UPDATE
        m_pUpdateBarrier = nullptr;
        m_pUpdateThreads = nullptr;
#endif
    }

    virtual ~CElectricSheep() {}
    
    Hud::spCHudManager GetHudManager() const { return m_HudManager; }

    //
    virtual bool Startup()
    {
        m_CpuUsageThreshold =
            g_Settings()->Get("settings.player.cpuusagethreshold", 50);

        if (m_MultipleInstancesMode == false)
        {
            g_NetworkManager->Startup();

            //	Set proxy info.
            if (g_Settings()->Get("settings.content.use_proxy", false))
            {
                g_Log->Info("Using proxy server...");
                g_NetworkManager->Proxy(
                    g_Settings()->Get("settings.content.proxy",
                                      std::string("")),
                    g_Settings()->Get("settings.content.proxy_username",
                                      std::string("")),
                    g_Settings()->Get("settings.content.proxy_password",
                                      std::string("")));
            }

            m_pVoter = new CVote();
        }

        g_Player().SetMultiDisplayMode(
            (CPlayer::MultiDisplayMode)g_Settings()->Get(
                "settings.player.MultiDisplayMode", 0));

        //	Init the display and create decoder.
        if (!g_Player().Startup())
            return false;

#ifdef DO_THREAD_UPDATE
        CreateUpdateThreads();
#endif

        //	Set framerate.
        m_PlayerFps = g_Settings()->Get("settings.player.player_fps", 20.);
        if (m_PlayerFps < 0.1)
            m_PlayerFps = 1.;
        m_OriginalFps = m_PlayerFps;

        g_Player().SetFramerate(m_PlayerFps);

        //	Get hud font size.
        std::string hudFontName = g_Settings()->Get(
            "settings.player.hudFontName", std::string("Lato"));
        uint32_t hudFontSize = static_cast<uint32_t>(
            g_Settings()->Get("settings.player.hudFontSize", 24));

        m_PNGDelayTimer =
            g_Settings()->Get("settings.player.pngdelaytimer", 600);

        m_HudManager = std::make_shared<Hud::CHudManager>();

        m_HudManager->Add("dreamcredits", std::make_shared<Hud::CStatsConsole>(
                                              Base::Math::CRect(1, 1),
                                              hudFontName, hudFontSize));
        Hud::spCStatsConsole spStats =
            std::dynamic_pointer_cast<Hud::CStatsConsole>(
                m_HudManager->Get("dreamcredits"));
        m_HudManager->Hide("dreamcredits");
        spStats->Add(new Hud::CStringStat("credits", "", "Title - Artist"));

        m_HudManager->Add("helpmessage", std::make_shared<Hud::CStatsConsole>(
                                             Base::Math::CRect(1, 1),
                                             hudFontName, hudFontSize));
        m_HudManager->Hide("helpmessage");
        Hud::spCStatsConsole spHelpMessage =
            std::dynamic_pointer_cast<Hud::CStatsConsole>(
                m_HudManager->Get("helpmessage"));
        spHelpMessage->Add(new Hud::CStringStat(
            "message",
            "e-dream\n\n"
            "A platform for generative AI visuals, see e-dream.ai to learn "
            "more.\n\n"
            "Keyboard Commands:\n"
            "Up-arrow: like this dream\n"
            "Down-arrow: dislike this dream and delete it\n"
            "Left-arrow: go back to play previous dream\n"
            "Right-arrow: go forward through history\n"
            "A: playback slower\nD: playback faster\n"
            "C: show credit\n"
            "L: skip 10 seconds forward\n"
            "J: skip 10 seconds back\n"
            "V: open web source                       "
            "\n" FULLSCREEN_MODIFIER_KEY "-F: toggle full screen\n"
            "F1: help (this page)\nF2: status overlay",
            ""));

        std::string ver = GetVersion();

        spHelpMessage->Add(
            new Hud::CStringStat("version", "\nVersion: ",
                                 ver + " " + PlatformUtils::GetGitRevision() +
                                     " " + PlatformUtils::GetBuildDate()));

        m_SeamlessPlayback =
            g_Settings()->Get("settings.player.SeamlessPlayback", false);

        //	Add some server stats.
        m_HudManager->Add("dreamstats", std::make_shared<Hud::CStatsConsole>(
                                            Base::Math::CRect(1, 1),
                                            hudFontName, hudFontSize));
        m_HudManager->Hide("dreamstats");
        spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
            m_HudManager->Get("dreamstats"));
        spStats->Add(new Hud::CStringStat("loginstatus", "", "Not logged in"));
        spStats->Add(
            new Hud::CStringStat("all", "Content cache: ", "unknown..."));
        spStats->Add(new Hud::CStringStat("transfers", "", ""));
        spStats->Add(new Hud::CStringStat("deleted", "", ""));
        if (m_MultipleInstancesMode == true)
            spStats->Add(new Hud::CTimeCountDownStat(
                "svstat", "", "Downloading disabled, read-only mode"));
        else if (g_Settings()->Get("settings.content.download_mode", true) ==
                 false)
            spStats->Add(new Hud::CTimeCountDownStat("svstat", "",
                                                     "Downloading disabled"));
        else
            spStats->Add(new Hud::CTimeCountDownStat(
                "svstat", "", "Preparing downloader..."));

        spStats->Add(new Hud::CStringStat("zzacpu", "CPU usage: ", "Unknown"));
        spStats->Add(
            new Hud::CStringStat("uptime", "\nClient uptime: ", "...."));
        spStats->Add(
            new Hud::CStringStat("decodefps", "Decoding video at ", "? fps"));
        spStats->Add(
            new Hud::CStringStat("activityLevel", "Activity level: ", "1.00"));
        spStats->Add(new Hud::CStringStat("playHead", "", "00m00s/00m00s"));

        int32_t displayMode =
            g_Settings()->Get("settings.player.DisplayMode", 2);
        if (displayMode == 2)
            spStats->Add(
                new Hud::CAverageCounter("displayfps",
                                         g_Player().Renderer()->Description() +
                                             " piecewise cubic display at ",
                                         " fps", 1.0));
        else if (displayMode == 1)
            spStats->Add(
                new Hud::CAverageCounter("displayfps",
                                         g_Player().Renderer()->Description() +
                                             " piecewise linear display at ",
                                         " fps", 1.0));
        else
            spStats->Add(new Hud::CAverageCounter(
                "displayfps",
                g_Player().Renderer()->Description() + " display at ", " fps",
                1.0));
        spStats->Add(new Hud::CStringStat(std::string("zconnerror"), "", ""));

#ifndef LINUX_GNU
        std::string defaultDir = std::string(".\\");
#else
        std::string defaultDir = std::string("");
#endif
        //	Vote splash.
        m_spSplashPos = std::make_shared<Hud::CSplash>(
            0.2f, g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                      "vote-up.png");
        m_spSplashNeg = std::make_shared<Hud::CSplash>(
            0.2f, g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                      "vote-down.png");

        // PNG splash
        m_SplashFilename = g_Settings()->Get(
            "settings.player.attrpngfilename",
            g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                "e-dream-attr.png");

        bool splashFound = false;

        const char* percent;

        if (m_SplashFilename.empty() == false)
        {
            if ((percent = strchr(m_SplashFilename.c_str(), '%')))
            {
                if (percent[1] == 'd')
                {
                    FILE* test;
                    while (1)
                    {
                        char fNameFormatted[FILENAME_MAX];
                        snprintf(fNameFormatted, FILENAME_MAX,
                                 m_SplashFilename.c_str(), m_nSplashes);
                        if ((test = fopen(fNameFormatted, "r")))
                        {
                            splashFound = true;
                            fclose(test);
                            m_nSplashes++;
                        }
                        else
                        {
                            break;
                        }
                    }
                }
            }
            else
            {
                FILE* test = fopen(m_SplashFilename.c_str(), "r");

                if (test != nullptr)
                {
                    splashFound = true;
                    fclose(test);
                }
            }
        }

        if (!splashFound)
        {
            m_SplashFilename =
                g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                "e-dream-attr.png";
            g_Settings()->Set("settings.player.attrpngfilename",
                              m_SplashFilename);
        }

        // if multiple splashes are found then they are loaded when the timer
        // goes off, not here
        if (m_SplashFilename.empty() == false &&
            g_Settings()->Get("settings.app.attributionpng", false) == true)
            m_spSplashPNG = std::make_shared<Hud::CSplashImage>(
                0.2f, m_SplashFilename.c_str(),
                float(g_Settings()->Get("settings.app.pngfadein", 10)),
                float(g_Settings()->Get("settings.app.pnghold", 10)),
                float(g_Settings()->Get("settings.app.pngfadeout", 10)));

        m_spCrossFade = std::make_shared<Hud::CCrossFade>(
            g_Player().Display()->Width(), g_Player().Display()->Height(),
            true);

        //	For testing...
        /*ContentDownloader::Shepherd::addMessageText(
            "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do "
            "eiusmod tempor incididunt ut labore et dolore magna aliqua",
            180);*/

        bool internetReachable = PlatformUtils::IsInternetReachable();
        if (!internetReachable)
        {
            ContentDownloader::Shepherd::addMessageText(
                "No Internet Connection.", 180);
        }
        //    Start downloader.
        g_Log->Info("Starting downloader...");

        g_ContentDownloader().Startup(false, m_MultipleInstancesMode ||
                                                 !internetReachable);

        // call static method to fill sheep counts
        ContentDownloader::Shepherd::GetFlockSizeMBsRecount(0);
        ContentDownloader::Shepherd::GetFlockSizeMBsRecount(1);
        spCDelayedDispatch hideCursorDispatch =
            std::make_shared<CDelayedDispatch>(
                [&, this]() -> void
                {
                    if (m_bFullScreen)
                        PlatformUtils::SetCursorHidden(true);
                });
        hideCursorDispatch->DispatchAfter(5);
        PlatformUtils::SetOnMouseMovedCallback(
            [=](int, int) -> void
            {
                PlatformUtils::SetCursorHidden(false);
                hideCursorDispatch->Cancel();
                hideCursorDispatch->DispatchAfter(5);
            });

        //	And we're off.
        m_SplashPNGDelayTimer.Reset();
        m_Timer.Reset();
        g_Player().Start();
        m_F1F4Timer.Reset();
        m_LastCPUCheckTime = m_Timer.Time();
        return true;
    }

    //
    virtual void Shutdown()
    {
        printf("CElectricSheep::Shutdown()\n");

#ifdef DO_THREAD_UPDATE
        DestroyUpdateThreads();
#endif

        m_spSplashPos = nullptr;
        m_spSplashNeg = nullptr;
        m_spSplashPNG = nullptr;
        m_nSplashes = 0;
        m_SplashFilename = std::string();
        m_spCrossFade = nullptr;
        m_StartupScreen = nullptr;
        m_HudManager = nullptr;
        m_bPaused = false;

        if (!m_bConfigMode)
        {
            g_ContentDownloader().Shutdown();

            //	This stuff was never started in config mode.
            if (m_MultipleInstancesMode == false)
            {
                SAFE_DELETE(m_pVoter);

                g_NetworkManager->Shutdown();
            }
            g_Player().Shutdown();
            g_Settings()->Shutdown();
        }
        EDreamClient::DeinitializeClient();
    }

    void SetIsFullScreen(bool _bFullScreen) { m_bFullScreen = _bFullScreen; }

    bool Run()
    {
        while (true)
        {
            // g_Player().Renderer()->BeginFrame();

            // if( !Update() )
            {
                g_Player().Renderer()->EndFrame();
                return false;
            }
        }

        return true;
    }

    static std::string FormatTimeDiff(uint64_t timediff, bool showseconds)
    {
        std::stringstream ss;

        //	Prettify uptime.
        uint32_t seconds = (timediff % 60);
        uint32_t minutes = (timediff / 60) % 60;
        uint32_t hours = (timediff / 3600) % 24;
        uint32_t days = uint32_t((timediff / (60 * 60 * 24)));
        const char* space = "";
        if (days > 0)
        {
            ss << days << " day" << ((days != 1) ? "s" : "") << ",";
            space = " ";
        }

        if (!showseconds)
        {
            if (days > 0)
            {
                if (hours > 0)
                    ss << space << hours << " hour"
                       << ((hours != 1) ? "s" : "");
                else if (minutes > 0)
                    ss << space << minutes << " minute"
                       << ((minutes != 1) ? "s" : "");
                else if (seconds > 0)
                    ss << space << seconds << " second"
                       << ((seconds != 1) ? "s" : "");
            }
            else
            {
                ss << space;

                if (hours > 0)
                    ss << space << std::setfill('0') << std::setw(2) << hours
                       << "h";

                ss << std::setfill('0') << std::setw(2) << minutes << "m"
                   << std::setfill('0') << std::setw(2) << seconds << "s";
            }
        }
        else
        {
            ss << space;

            if (hours > 0)
                ss << space << std::setfill('0') << std::setw(2) << hours
                   << "h";

            ss << std::setfill('0') << std::setw(2) << minutes << "m"
               << std::setfill('0') << std::setw(2) << seconds << "s";
        }

        return ss.str();
    }

    static std::string FrameNumberToMinutesAndSecondsString(int64_t _frames,
                                                            float _fps)
    {
        double seconds = _frames / _fps;
        uint64_t timeDiff = (uint64_t)seconds;
        return FormatTimeDiff(timeDiff, true);
    }

#ifdef DO_THREAD_UPDATE
    //
    virtual void CreateUpdateThreads()
    {
        int displayCnt = g_Player().GetDisplayCount();

        m_pUpdateBarrier = new boost::barrier(displayCnt + 1);

        m_pUpdateThreads = new boost::thread_group;

        for (int i = 0; i < displayCnt; i++)
        {
            boost::thread* th =
                new boost::thread(&CElectricSheep::UpdateFrameThread, this, i);
#ifdef WIN32
            SetThreadPriority((HANDLE)th->native_handle(),
                              THREAD_PRIORITY_HIGHEST);
            SetThreadPriorityBoost((HANDLE)th->native_handle(), FALSE);
#else
            struct sched_param sp;
            sp.sched_priority = sched_get_priority_max(
                SCHED_RR); // HIGH_PRIORITY_CLASS - THREAD_PRIORITY_NORMAL
            pthread_setschedparam((pthread_t)th->native_handle(), SCHED_RR,
                                  &sp);
#endif
            m_pUpdateThreads->add_thread(th);
        }
    }

    //
    virtual void DestroyUpdateThreads()
    {
        if (m_pUpdateThreads != nullptr)
        {
            m_pUpdateThreads->interrupt_all();
            m_pUpdateThreads->join_all();

            SAFE_DELETE(m_pUpdateThreads);
        }

        SAFE_DELETE(m_pUpdateBarrier);
    }
#endif

    //
    virtual bool Update(int _displayIdx, boost::barrier& _beginFrameBarrier,
                        boost::barrier& _endFrameBarrier)
    {
        g_Player().BeginFrameUpdate();

#ifdef DO_THREAD_UPDATE
        {
            std::scoped_lock lock(m_BarrierMutex);

            m_pUpdateBarrier->wait();

            m_pUpdateBarrier->wait();
        }
#else
        uint32_t displayCnt = g_Player().GetDisplayCount();

        _beginFrameBarrier.wait();
        bool ret = DoRealFrameUpdate(_displayIdx);
        _endFrameBarrier.wait();
#endif

        g_Player().EndFrameUpdate();

        return ret;
    }

#ifdef DO_THREAD_UPDATE
    virtual void UpdateFrameThread(int32_t displayUnit)
    {
        try
        {
            while (true)
            {
                m_pUpdateBarrier->wait();

                DoRealFrameUpdate(displayUnit);

                m_pUpdateBarrier->wait();
            }
        }
        catch (boost::thread_interrupted const&)
        {
        }
    }
#endif

    virtual bool DoRealFrameUpdate(uint32_t displayUnit)
    {
        if (g_Player().BeginDisplayFrame(displayUnit))
        {

            // g_Player().Renderer()->BeginFrame();

            if (g_Player().Closed())
            {
                g_Log->Info("Player closed...");
                g_Player().Renderer()->EndFrame();
                return false;
            }

            bool drawNoSheepIntro = false;
            bool drawn = g_Player().Update(displayUnit, drawNoSheepIntro);

            if ((drawNoSheepIntro && displayUnit == 0) ||
                (drawn && displayUnit == 0))
            {
                if (drawNoSheepIntro)
                {
                    if (!m_StartupScreen)
                        m_StartupScreen = std::make_shared<Hud::CStartupScreen>(
                            Base::Math::CRect(0, 0, 1., 1.), "Lato", 24);
                    m_StartupScreen->Render(0., g_Player().Renderer());
                }
                //	Process any server messages.
                std::string msg;
                double duration;

                if (m_spSplashPNG)
                {
                    if (m_SplashPNGDelayTimer.Time() > m_PNGDelayTimer)
                    {

                        // update m_spSplashPNG here, so every time it is shown,
                        // it is randomized among our shuffle group.

                        if (m_nSplashes > 0)
                        {
                            char fNameFormatted[FILENAME_MAX];
                            snprintf(fNameFormatted, FILENAME_MAX,
                                     m_SplashFilename.c_str(),
                                     rand() % m_nSplashes);
                            if (m_SplashFilename.empty() == false &&
                                g_Settings()->Get("settings.app.attributionpng",
                                                  false) == true)
                                m_spSplashPNG =
                                    std::make_shared<Hud::CSplashImage>(
                                        0.2f, fNameFormatted,
                                        float(g_Settings()->Get(
                                            "settings.app.pngfadein", 10)),
                                        float(g_Settings()->Get(
                                            "settings.app.pnghold", 10)),
                                        float(g_Settings()->Get(
                                            "settings.app.pngfadeout", 10)));
                        }

                        m_HudManager->Add(
                            "splash_png", m_spSplashPNG,
                            float(
                                g_Settings()->Get("settings.app.pngfadein",
                                                  10) +
                                g_Settings()->Get("settings.app.pnghold", 10) +
                                g_Settings()->Get("settings.app.pngfadeout",
                                                  10)));
                        m_SplashPNGDelayTimer.Reset();
                    }
                }

                if (ContentDownloader::Shepherd::PopMessage(msg, duration))
                {
                    bool addtohud = true;
                    time_t lt = time(nullptr);

                    if ((msg == "error connecting to server") ||
                        (msg == "server request failed, using free one"))
                    {
                        std::string temptime = ctime(&lt);
                        msg = temptime.substr(0, temptime.length() -
                                                     strlen("\n")) +
                              " " + msg;
                        g_Log->Error(msg.c_str());
                        if (m_ConnectionErrors.size() > 20)
                            m_ConnectionErrors.pop_front();
                        m_ConnectionErrors.push_back(msg);
                        if (g_Settings()->Get("settings.player.quiet_mode",
                                              true) == true)
                            addtohud = false;
                    }

                    if (addtohud == true)
                    {
                        m_HudManager->Add("servermessage",
                                          std::make_shared<Hud::CServerMessage>(
                                              msg, Base::Math::CRect(1, 1), 24),
                                          duration);
                    }
                }
                if (m_F1F4Timer.Time() > 20 * 60)
                {
                    m_F1F4Timer.Reset();
                    m_HudManager->Hide("helpmessage");
                    m_HudManager->Hide("dreamstats");
                }

                std::string batteryStatus = "Unknown";
                bool blockRendering = false;
                if (m_Timer.Time() > m_LastCPUCheckTime + 3.)
                {
                    m_LastCPUCheckTime = m_Timer.Time();
                    if (m_CpuUsage.GetCpuUsage(m_CpuUsageTotal, m_CpuUsageES))
                        if (m_CpuUsageTotal != -1 && m_CpuUsageES != -1 &&
                            m_CpuUsageTotal > m_CpuUsageES)
                        {
                            if ((m_CpuUsageTotal - m_CpuUsageES) >
                                m_CpuUsageThreshold)
                            {
                                ++m_HighCpuUsageCounter;
                                if (m_HighCpuUsageCounter > 10)
                                {
                                    m_HighCpuUsageCounter = 5;
                                    blockRendering = true;
                                }
                            }
                            else
                            {
                                if (m_HighCpuUsageCounter > 0)
                                    --m_HighCpuUsageCounter;
                            }
                        }
                }
                if (m_HighCpuUsageCounter > 0 &&
                    ContentDownloader::Shepherd::RenderingAllowed() == false)
                    blockRendering = true;

                switch (GetACLineStatus())
                {
                case 1:
                {
                    batteryStatus = "Power adapter";
                    /*m_PlayerFps = m_OriginalFps;
                    m_CurrentFps = m_PlayerFps;
                    g_Player().Framerate( m_CurrentFps );*/
                    break;
                }
                case 0:
                {
                    batteryStatus = "Battery";
                    /*m_PlayerFps = m_OriginalFps/2; // half speed on battery
                    power m_CurrentFps = m_PlayerFps; g_Player().Framerate(
                    m_CurrentFps );*/
                    blockRendering = true;
                    break;
                }
                case 255:
                {
                    batteryStatus = "Unknown1";
                    break;
                }
                default:
                {
                    batteryStatus = "Unknown2";
                    break;
                }
                }

                if (blockRendering)
                    ContentDownloader::Shepherd::SetRenderingAllowed(false);
                else
                    ContentDownloader::Shepherd::SetRenderingAllowed(true);

                //	Update some stats.
                Hud::spCStatsConsole spStats =
                    std::dynamic_pointer_cast<Hud::CStatsConsole>(
                        m_HudManager->Get("dreamstats"));
                float activityLevel = 1.f;
                double realFps = 20;
                const ContentDecoder::sClipMetadata* clipMetadata =
                    g_Player().GetCurrentPlayingClipMetadata();
                if (clipMetadata)
                {
                    activityLevel = clipMetadata->dreamData.activityLevel;
                    realFps = clipMetadata->decodeFps;
                }

                const ContentDecoder::sFrameMetadata* frameMetadata =
                    g_Player().GetCurrentFrameMetadata();
                if (frameMetadata)
                {
                    ((Hud::CStringStat*)spStats->Get("playHead"))
                        ->SetSample(
                            string_format("%s/%s",
                                          FrameNumberToMinutesAndSecondsString(
                                              frameMetadata->frameIdx, 20)
                                              .data(),
                                          FrameNumberToMinutesAndSecondsString(
                                              frameMetadata->maxFrameIdx, 20)
                                              .data()));
                }
                ((Hud::CStringStat*)spStats->Get("decodefps"))
                    ->SetSample(string_format(" %.2f fps", realFps));
                ((Hud::CStringStat*)spStats->Get("activityLevel"))
                    ->SetSample(string_format(" %.2f", activityLevel));
                ((Hud::CIntCounter*)spStats->Get("displayfps"))->AddSample(1);

                spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                    m_HudManager->Get("dreamcredits"));
                if (clipMetadata)
                {
                    ((Hud::CStringStat*)spStats->Get("credits"))
                        ->SetSample(
                            string_format("%s - %s",
                                          clipMetadata->dreamData.name.data(),
                                          clipMetadata->dreamData.author.data())
                                .data());
                }
                //	Serverstats.
                spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                    m_HudManager->Get("dreamstats"));

                //    Prettify uptime.
                uint64_t uptime = (uint64_t)m_Timer.Time();

                ((Hud::CStringStat*)spStats->Get("uptime"))
                    ->SetSample(FormatTimeDiff(uptime, true));
                if (m_CpuUsageTotal != -1 && m_CpuUsageES != -1)
                {
                    ((Hud::CStringStat*)spStats->Get("zzacpu"))
                        ->SetSample(string_format("%i\%/%i\%", m_CpuUsageES,
                                                  m_CpuUsageTotal));
                    EDreamClient::SetCPUUsage(m_CpuUsageES);
                }

                std::stringstream tmpstr;
                uint64_t flockcount =
                    ContentDownloader::Shepherd::getClientFlockCount(0);
                uint64_t flockmbs =
                    ContentDownloader::Shepherd::getClientFlockMBs(0);
                tmpstr << flockcount << " dream" << (flockcount > 1 ? "s" : "")
                       << ", " << flockmbs << "MB";
                ((Hud::CStringStat*)spStats->Get("all"))
                    ->SetSample(tmpstr.str());

                Hud::CStringStat* pTmp =
                    (Hud::CStringStat*)spStats->Get("transfers");
                if (pTmp)
                {
                    std::string serverStatus = g_NetworkManager->Status();
                    if (serverStatus == "")
                        pTmp->Visible(false);
                    else
                    {
                        pTmp->SetSample(serverStatus);
                        pTmp->Visible(true);
                    }
                }

                pTmp = (Hud::CStringStat*)spStats->Get("loginstatus");
                if (pTmp)
                {
                    bool visible = true;

                    if (EDreamClient::IsLoggedIn())
                    {
                        std::stringstream loginstatusstr;
                        loginstatusstr
                            << "Logged in as "
                            << ContentDownloader::Shepherd::GetNickName();
                        pTmp->SetSample(loginstatusstr.str());
                    }
                    else
                    {
                        pTmp->SetSample("Not logged in");
                    }
                    pTmp->Visible(visible);
                }

                pTmp = (Hud::CStringStat*)spStats->Get("deleted");
                if (pTmp)
                {
                    std::string deleted;
                    if (ContentDownloader::Shepherd::PopOverflowMessage(
                            deleted) &&
                        (deleted != ""))
                    {
                        pTmp->SetSample(deleted);
                        pTmp->Visible(true);
                    }
                    else
                        pTmp->Visible(false);
                }

                pTmp = (Hud::CStringStat*)spStats->Get("zconnerror");
                if (pTmp)
                {
                    if (m_ConnectionErrors.size() > 0)
                    {
                        std::string allConnectionErrors;
                        for (size_t ii = 0; ii < m_ConnectionErrors.size();
                             ++ii)
                            allConnectionErrors +=
                                m_ConnectionErrors.at(ii) + "\n";
                        if (allConnectionErrors.size() > 0)
                            allConnectionErrors.erase(
                                allConnectionErrors.size() - 1);
                        pTmp->SetSample(allConnectionErrors);
                        pTmp->Visible(true);
                    }
                    else
                        pTmp->Visible(false);
                }

                Hud::CTimeCountDownStat* pTcd =
                    (Hud::CTimeCountDownStat*)spStats->Get("svstat");
                if (pTcd)
                {
                    bool isnew = false;

                    std::string dlState =
                        ContentDownloader::Shepherd::downloadState(isnew);

                    if (isnew)
                    {
                        pTcd->SetSample(dlState);
                        pTcd->Visible(true);
                    }
                }

                //	Finally render hud.
                m_HudManager->Render(g_Player().Renderer());

                //	Update display events.
                g_Player().Display()->Update();
            }

            g_Player().EndDisplayFrame(displayUnit, drawn);
        }

        return true;
    }

    virtual bool HandleOneEvent(DisplayOutput::spCEvent& _event)
    {
        static const float voteDelaySeconds = 1;
        if (_event->Type() == DisplayOutput::CEvent::Event_KEY)
        {
            DisplayOutput::spCKeyEvent spKey =
                std::dynamic_pointer_cast<DisplayOutput::CKeyEvent>(_event);
            const ContentDecoder::sClipMetadata* data =
                g_Player().GetCurrentPlayingClipMetadata();
            switch (spKey->m_Code)
            {
                //	Vote for sheep.
            case DisplayOutput::CKeyEvent::KEY_UP:
                if (m_pVoter != nullptr &&
                    m_pVoter->Vote(0, true, voteDelaySeconds))
                    m_HudManager->Add("splash_pos", m_spSplashPos,
                                      voteDelaySeconds * 0.9f);
                return true;
            case DisplayOutput::CKeyEvent::KEY_DOWN:
                if (m_pVoter != nullptr &&
                    m_pVoter->Vote(0, false, voteDelaySeconds))
                {
                    if (g_Settings()->Get("settings.content.negvotedeletes",
                                          true))
                    {
                        // g_Player().Stop();
                        g_Player().SkipToNext();
                        m_spCrossFade->Reset();
                        m_HudManager->Add("fade", m_spCrossFade, 1.5);
                    }

                    m_HudManager->Add("splash_pos", m_spSplashNeg,
                                      voteDelaySeconds * 0.9f);
                }
                return true;
                //	Repeat current sheep
            case DisplayOutput::CKeyEvent::KEY_LEFT:
                g_Player().ReturnToPrevious();
                return true;
                //  Force Next Sheep
            case DisplayOutput::CKeyEvent::KEY_RIGHT:
                g_Player().SkipToNext();
                return true;
                //	Repeat sheep
            case DisplayOutput::CKeyEvent::KEY_R:
                g_Player().RepeatClip();
                return true;
            case DisplayOutput::CKeyEvent::KEY_A:
                m_F1F4Timer.Reset();
                g_Player().MultiplyFramerate(1.f / 1.1f);
                return true;
            case DisplayOutput::CKeyEvent::KEY_D:
                m_F1F4Timer.Reset();
                g_Player().MultiplyFramerate(1.1f);
                return true;
                //	OSD info.
            case DisplayOutput::CKeyEvent::KEY_F1:
                m_F1F4Timer.Reset();
                m_HudManager->Toggle("helpmessage");
                return true;
            case DisplayOutput::CKeyEvent::KEY_F2:
                m_F1F4Timer.Reset();
                m_HudManager->Toggle("dreamstats");
                return true;
            case DisplayOutput::CKeyEvent::KEY_J:
                g_Player().SkipForward(-15);
                return true;
            case DisplayOutput::CKeyEvent::KEY_L:
                g_Player().SkipForward(15);
                return true;
            case DisplayOutput::CKeyEvent::KEY_K:
                g_Player().SetPaused(m_bPaused = !m_bPaused);
                return true;
            case DisplayOutput::CKeyEvent::KEY_C:
                m_HudManager->Toggle("dreamcredits");
                return true;
            case DisplayOutput::CKeyEvent::KEY_V:
                if (data && !data->dreamData.frontendUrl.empty())
                {
                    PlatformUtils::OpenURLExternally(
                        data->dreamData.frontendUrl);
                }
                return true;
            case DisplayOutput::CKeyEvent::KEY_W:
                g_Player().Renderer()->SetBrightness(
                    g_Player().Renderer()->GetBrightness() + 0.05f);
                return true;
            case DisplayOutput::CKeyEvent::KEY_S:
                g_Player().Renderer()->SetBrightness(
                    g_Player().Renderer()->GetBrightness() - 0.05f);
                return true;
            //	All other keys needs to be ignored, they are handled somewhere
            // else...
            default:
                g_Log->Info("Key event, ignoring");
                return false;
            }
        }
        return false;
    }

    virtual bool HandleEvents()
    {
        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();

        //	Handle events.
        DisplayOutput::spCEvent spEvent;
        while (spDisplay->GetEvent(spEvent))
        {
            if (HandleOneEvent(spEvent) == false)
                return false;
        }

        return true;
    }

    std::string GetVersion() { return PlatformUtils::GetAppVersion(); }

    virtual int GetACLineStatus() { return -1; }

    void SetUpdateAvailable(const std::string& verinfo)
    {
        std::string message("New e-drean ");

        message += verinfo;
        message += " is available.";
#ifdef MAC
        message +=
            " Relaunch e-dream application or preference pane to update.";
#endif

        ContentDownloader::Shepherd::QueueMessage(message, 30.0);
    }
};

#endif // CLIENT_H_INCLUDED
