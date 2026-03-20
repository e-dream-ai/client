#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <atomic>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <string>
#include <mutex>
#include <queue>
#include <cstdlib>   // for exit()
#include <unistd.h>  // for _exit()

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
#include "MessageQueue.h"
#include "Splash.h"
#include "OSD.hpp"
#include "StartupScreen.h"
#include "StatsConsole.h"
#include "TextureFlat.h"
#include "Timer.h"

#include "PlatformUtils.h"
#include "StringFormat.h"
#include "CacheManager.h"

// Flag to cancel the shutdown watchdog when restarting (e.g., after wizard)
static std::atomic<bool> s_shutdownCancelled{false};

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
#define FULLSCREEN_MODIFIER_KEY "Command"
// Sparkle update availability check (Mac only)
extern bool ESScreensaver_IsUpdateAvailable(void);
#else
#define FULLSCREEN_MODIFIER_KEY "Control"
// Non-Mac platforms don't have Sparkle
inline bool ESScreensaver_IsUpdateAvailable(void) { return false; }
#endif

typedef void (*ShowPreferencesCallback_t)();
extern void ESSetShowPreferencesCallback(ShowPreferencesCallback_t);
extern void ESShowPreferences();

typedef void (*ShowFirstTimeSetupCallback_t)();
extern void ESSetShowFirstTimeSetupCallback(ShowFirstTimeSetupCallback_t);
extern void ESShowFirstTimeSetup();

extern class CElectricSheep* gClientInstance;

inline class CElectricSheep* g_Client() { return gClientInstance; }

inline double SpeedToPerceptualFPS(double speed) {
    return pow(2.0, (speed + 1) / 2);
}

inline float TapsToBrightness(double taps) {
    return pow(2.0, (taps / 40)) - 1 ;
}

/*
        CElectricSheep().
        Prime mover for the client, used from main.cpp...
*/
class CElectricSheep
{
  private:
    Cache::MessageQueue m_MessageQueue;
    bool wasShowingBufferIndicator = false;
    std::atomic<bool> m_TimecodeUpdaterRunning{false};
    std::thread m_TimecodeUpdaterThread;
    std::mutex m_TimecodeHudMutex;

    // Thread-safe command queue for remote control commands.
    // WebSocket events arrive on a background thread; queueing them here
    // ensures they are executed on the main/render thread during HandleEvents,
    // avoiding data races on HudManager state and CATextLayer visibility.
    std::queue<int> m_CommandQueue;
    std::mutex m_CommandQueueMutex;

    // Network indicator state tracking
    bool m_PrevNetworkUp = true;      // Previous network state for transition detection
    bool m_PrevWebsocketUp = true;    // Previous websocket state for transition detection
    double m_NetworkIndicatorEndTime = 0.0;  // When to hide the Net indicator (0 = hidden)
    double m_RemoteIndicatorEndTime = 0.0;   // When to hide the Remote indicator (0 = hidden)
    double m_RemoteIndicatorGraceEndTime = 0.0; // Suppress Remote indicator during initial auth (seconds since startup)
    bool m_NetworkIndicatorShowGreen = false; // True when showing recovery (green) indicator
    bool m_NetworkWasDown = false;    // True if network has been down since startup (for green recovery)
    bool m_RemoteWasDown = false;     // True if remote has been down since startup (for green recovery)
    bool m_RemoteFirstConnected = false;     // True after first successful websocket connection
    bool m_RemoteInitialIndicatorShown = false;  // True after showing initial connection failure indicator

    // Update indicator state tracking
    bool m_PrevUpdateAvailable = false;      // Previous update available state for transition detection
    double m_UpdateIndicatorEndTime = 0.0;   // When to hide the update indicator (0 = hidden)

    // Busy indicator state tracking
    double m_BusyIndicatorEndTime = 0.0;     // When to hide the busy indicator (0 = hidden)

    // Disk indicator state tracking
    bool m_PrevDiskSpaceLow = false;         // Previous disk space state for transition detection
    double m_DiskIndicatorEndTime = 0.0;     // When to hide the disk indicator (0 = hidden)

  protected:
    ESCpuUsage m_CpuUsage;
    double m_LastCPUCheckTime;
    int m_CpuUsageTotal;
    int m_CpuUsageES;
    int m_GpuUsage;
    int m_HighCpuUsageCounter;
    int m_CpuUsageThreshold;
    std::string m_PreviousDlState; // Track download status
    bool m_MultipleInstancesMode;
    bool m_OfflineDueToNoInternetOnly = false;  // true when m_MultipleInstancesMode was set only because internet was down (don't show Busy in that case)
    bool m_bConfigMode;
    bool m_bIsPreview;
    bool m_SeamlessPlayback;
    bool m_bPaused;
    bool m_bFullScreen;
    Base::CTimer m_Timer;
    Base::CTimer m_F1F4Timer;
    // Track per-frame wall time to derive smooth in-frame timecode without relying on clip elapsed time.
    double m_FrameStartWallTime = 0.0;
    uint32_t m_LastFrameIdxForHud = 0;
    bool m_HaveFrameStartTime = false;

    Hud::spCHudManager m_HudManager;
    Hud::spCStartupScreen m_StartupScreen;

    double m_PNGDelayTimer;

    //	The base framerate(from config).
    double m_PlayerFps;

    //	Potentially adjusted framerate(from <- and -> keys)
    double m_CurrentFps;
    double m_OriginalFps;

    // We use perceptual FPS here which don't take into account per dream activity level
    double m_PerceptualFPS;
    
    
    // internal brightness counter
    int m_Brightness = 0;
    
    //	Default root directory, ie application data.
    std::string m_AppData;

    //	Default application working directory.
    std::string m_WorkingDir;

    // Our OSD
    Hud::spCOSD m_spOSD;

    // Splash PNG
    Hud::spCSplashImage m_spSplashPNG;
    Base::CTimer m_SplashPNGDelayTimer;
    int m_nSplashes;
    int m_StatsCodeCounter;
    std::string m_SplashFilename;

    Hud::spCCrossFade m_spCrossFade;

    std::deque<std::string> m_ConnectionErrors;
    std::string m_HudFontName;
    int m_HudFontSize;

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
        // We reset the installdir at launch here, ensuring the bundle can be moved around
#ifndef LINUX_GNU
        g_Settings()->Set("settings.app.InstallDir", m_WorkingDir);
#else
        g_Settings()->Set("settings.app.InstallDir", std::string(SHAREDIR));
#endif
        g_Settings()->Storage()->Commit();
        return true;
    }

    //	Attach log.
    void AttachLog()
    {
        TupleStorage::IStorageInterface::CreateFullDirectory(m_AppData +
                                                             "Logs/");
        if (g_Settings()->Get("settings.app.log", true))
            g_Log->Attach(m_AppData + "Logs/", m_MultipleInstancesMode);

        g_Log->Info("AttachLog()");
        g_Log->Info("******************* %s %s %s\n",
                    PlatformUtils::GetAppVersion().c_str(),
                    PlatformUtils::GetGitRevision().c_str(),
                    PlatformUtils::GetBuildDate().c_str());
    }

  public:
    CElectricSheep()
    {
        PlatformUtils::SetThreadName("Main");
        m_CpuUsageTotal = -1;
        m_CpuUsageES = -1;
        m_GpuUsage = -1;
        m_CpuUsageThreshold = 50;
        m_HighCpuUsageCounter = 0;
        m_StatsCodeCounter = 0;
        m_bConfigMode = false;
        m_bIsPreview = false;
        m_MultipleInstancesMode = false;
        printf("CElectricSheep()\n");

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
        gClientInstance = this;
    }

    virtual ~CElectricSheep() {}

    Hud::spCHudManager GetHudManager() const { return m_HudManager; }
    
    bool IsMultipleInstancesMode() {
        return m_MultipleInstancesMode;
    }
    
    void ForceMultipleInstancesMode(bool force) {
        m_MultipleInstancesMode = force;
    }
    
    virtual void SetIsPreview(bool _isPreview) {
        m_bIsPreview = _isPreview;
    }
    
    virtual bool IsPreview() const {
        return m_bIsPreview;
    }
    
    void CreateHud()
    {
        //    Get hud font size.
        m_HudFontName = g_Settings()->Get(
            "settings.player.m_HudFontName", std::string("Lato"));
        m_HudFontSize = static_cast<uint32_t>(
            g_Settings()->Get("settings.player.m_HudFontSize", 24));

        m_PNGDelayTimer =
            g_Settings()->Get("settings.player.pngdelaytimer", 600);

        m_HudManager = std::make_shared<Hud::CHudManager>();
    }
    
    void AddHelpHud()
    {
        m_HudManager->Add("helpmessage", std::make_shared<Hud::CStatsConsole>(
                                             Base::Math::CRect(1, 1),
                                             m_HudFontName, m_HudFontSize));
        m_HudManager->Hide("helpmessage");
        Hud::spCStatsConsole spHelpMessage =
            std::dynamic_pointer_cast<Hud::CStatsConsole>(
                m_HudManager->Get("helpmessage"));
        spHelpMessage->Add(new Hud::CStringStat(
            "message",
            "infinidream: visuals for your vibe\n\n"
#ifdef SCREEN_SAVER
            "Use the remote control to interact.\n\n"
#else
            "Use the remote control or keyboard to interact.\n\n"
#endif
            "Keyboard Commands:\n"
            "A: slower playback\t\t\t\t\tUp: like this dream\n"
            "D: faster playback\t\t\t\t\tDown: dislike and delete\n"
            "J: skip 10 seconds back\t\t\tLeft: previous dream\n"
            "L: skip 10 seconds forward\tRight: next dream\n"
            "R: repeat current dream\t\t\tH: shuffle mode\n"
            "C: show credit\t\t\t\t\t\tB: report this dream\n"
            "V: open web source\t\t\t\t" FULLSCREEN_MODIFIER_KEY "-F: toggle full screen\n"
            "F1: help (this page)\t\t\t\tF2: status overlay\n\n"

            FULLSCREEN_MODIFIER_KEY "-R: open remote control\n" FULLSCREEN_MODIFIER_KEY "-B: browse playlists",
            ""));

        std::string ver = GetVersion();

        spHelpMessage->Add(
            new Hud::CStringStat("version", "\nVersion: ",
                                 ver + " " + PlatformUtils::GetGitRevision() +
                                     " " + PlatformUtils::GetBuildDate()));
    }
    
    void AddDreamStatsHud()
    {
        m_HudManager->Add("dreamstats", std::make_shared<Hud::CStatsConsole>(
                                            Base::Math::CRect(1, 1),
                                            m_HudFontName, m_HudFontSize));
        m_HudManager->Hide("dreamstats");
        Hud::spCStatsConsole spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
            m_HudManager->Get("dreamstats"));
        spStats->Add(new Hud::CStringStat("loginstatus", "", "Not signed in"));
        spStats->Add(
            new Hud::CStringStat("all", "Content cache: ", "unknown..."));
        spStats->Add(new Hud::CStringStat("transfers", "", " \n "));
        if (m_MultipleInstancesMode == true)
            spStats->Add(new Hud::CTimeCountDownStat(
                "svstat", "", "Downloading disabled, busy mode"));
        else if (g_Settings()->Get("settings.content.download_mode", true) ==
                 false)
            spStats->Add(new Hud::CTimeCountDownStat("svstat", "",
                                                     "Downloading disabled"));
        else
            spStats->Add(new Hud::CTimeCountDownStat(
                "svstat", "", "Preparing downloader..."));

        spStats->Add(new Hud::CStringStat("zzacpu", "CPU: ", "measuring..."));
        spStats->Add(new Hud::CStringStat("zzagpu2", "GPU: ", "measuring..."));
        spStats->Add(
            new Hud::CStringStat("uptime", "\nClient uptime: ", "...."));
        spStats->Add(
            new Hud::CStringStat("decodefps", "Decoding video at ", "? fps"));
        spStats->Add(
            new Hud::CStringStat("perceptualfps", "Perceptual speed ", "? fps"));

        spStats->Add(new Hud::CStringStat("currentplaylist", "\nPlaylist: ", "None"));
        spStats->Add(new Hud::CStringStat("playlistposition", "Position: ", "None"));
        spStats->Add(new Hud::CStringStat("playbackmode", "Mode: ", "Normal"));

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
    }
    
    void AddCreditsHud()
    {
        auto creditsConsole = std::make_shared<Hud::CStatsConsole>(
                                              Base::Math::CRect(1, 1),
                                              m_HudFontName, m_HudFontSize);
        m_HudManager->Add("dreamcredits", creditsConsole);
        m_HudManager->Hide("dreamcredits");

        // Add left-aligned stats
        creditsConsole->Add(new Hud::CStringStat("credits-title", "", ""));
        creditsConsole->Add(new Hud::CStringStat("credits-artist", "", ""));
        creditsConsole->Add(new Hud::CStringStat("credits-playlist", "", ""));
        creditsConsole->Add(new Hud::CStringStat("credits-mode", "", ""));
        creditsConsole->Add(new Hud::CStringStat("credits-download-base", "", " "));  // Base for download line

        // Add right-aligned stats with explicit alignment
        // Move most stats down one row to avoid overlap with long titles
        creditsConsole->Add(new Hud::CStringStat("credits-track", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-title");
        creditsConsole->Add(new Hud::CStringStat("credits-time", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-artist");
        creditsConsole->Add(new Hud::CStringStat("credits-fps", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-playlist");
        // Indicator order (left to right): Busy, Net, Remote, Disk, Update
        creditsConsole->Add(new Hud::CStringStat("credits-busy", "", ""), true, Base::Math::CVector4(1, 0, 0, 1), "credits-mode");  // Red busy indicator
        creditsConsole->Add(new Hud::CStringStat("credits-net", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-mode");
        creditsConsole->Add(new Hud::CStringStat("credits-ws", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-mode");
        creditsConsole->Add(new Hud::CStringStat("credits-dsk", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-mode");
        creditsConsole->Add(new Hud::CStringStat("credits-upd", "", ""), true, Base::Math::CVector4(1, 1, 0, 1), "credits-mode");  // Yellow update indicator
        creditsConsole->Add(new Hud::CStringStat("credits-download", "", ""), true, Base::Math::CVector4(1, 1, 1, 1), "credits-download-base");
    }
    
    void AddNetworkIndicatorHud()
    {
        // Create a separate HUD for network status indicator (shown outside credits overlay)
        auto networkConsole = std::make_shared<Hud::CStatsConsole>(
                                              Base::Math::CRect(1, 1),
                                              m_HudFontName, m_HudFontSize);
        m_HudManager->Add("network-indicator", networkConsole);
        m_HudManager->Hide("network-indicator");

        // Add rows to match credits console structure for same vertical positioning
        // Credits has 5 rows: title, artist, playlist, mode, download-base
        // Indicators align to "mode" row (4th row, 2nd from bottom)
        // We need same structure so indicators appear at same Y position
        // Using transparent color (alpha=0) so placeholder text is invisible
        networkConsole->Add(new Hud::CStringStat("net-row1", "", " "), false, Base::Math::CVector4(0, 0, 0, 0));  // title row
        networkConsole->Add(new Hud::CStringStat("net-row2", "", " "), false, Base::Math::CVector4(0, 0, 0, 0));  // artist row
        networkConsole->Add(new Hud::CStringStat("net-row3", "", " "), false, Base::Math::CVector4(0, 0, 0, 0));  // playlist row
        networkConsole->Add(new Hud::CStringStat("net-row4", "", " "), false, Base::Math::CVector4(0, 0, 0, 0));  // mode row - indicators here
        networkConsole->Add(new Hud::CStringStat("net-row5", "", " "), false, Base::Math::CVector4(0, 0, 0, 0));  // download-base row

        // Add right-aligned indicators on the mode row (row4)
        // Indicator order (left to right): Disk, Busy, Net, Remote, Update
        networkConsole->Add(new Hud::CStringStat("net-indicator-dsk", "", ""), true, Base::Math::CVector4(1, 0, 0, 1), "net-row4");
        networkConsole->Add(new Hud::CStringStat("net-indicator-busy", "", ""), true, Base::Math::CVector4(1, 0, 0, 1), "net-row4");
        networkConsole->Add(new Hud::CStringStat("net-indicator-net", "", ""), true, Base::Math::CVector4(1, 0, 0, 1), "net-row4");
        networkConsole->Add(new Hud::CStringStat("net-indicator-ws", "", ""), true, Base::Math::CVector4(1, 0, 0, 1), "net-row4");
        networkConsole->Add(new Hud::CStringStat("net-indicator-upd", "", ""), true, Base::Math::CVector4(1, 1, 0, 1), "net-row4");
    }
    
    // Show Net indicator for a specific duration
    // duration_seconds: how long to show (30 for startup/down, 5 for action/recovery)
    // show_green: true for recovery (down->up transition), false for down state
    void ShowNetworkIndicator(double duration_seconds, bool show_green = false)
    {
        m_NetworkIndicatorEndTime = m_Timer.Time() + duration_seconds;
        m_NetworkIndicatorShowGreen = show_green;
        if (auto spIndicator = m_HudManager->Get("network-indicator"))
            spIndicator->Visible(true);
    }
    
    void HideNetworkIndicator()
    {
        m_NetworkIndicatorEndTime = 0.0;
        if (auto spIndicator = m_HudManager->Get("network-indicator"))
            spIndicator->Visible(false);
    }

    // Show Remote indicator for a specific duration
    void ShowRemoteIndicator(double duration_seconds)
    {
        m_RemoteIndicatorEndTime = m_Timer.Time() + duration_seconds;
        if (auto spIndicator = m_HudManager->Get("network-indicator"))
            spIndicator->Visible(true);
    }

    void HideRemoteIndicator()
    {
        m_RemoteIndicatorEndTime = 0.0;
    }

    // Show update indicator for a specific duration (similar to network indicator)
    void ShowUpdateIndicator(double duration_seconds)
    {
        m_UpdateIndicatorEndTime = m_Timer.Time() + duration_seconds;
    }

    void HideUpdateIndicator()
    {
        m_UpdateIndicatorEndTime = 0.0;
    }

    // Called when user performs an action that needs network (like, dislike, report)
    // Shows indicator for 5s if network is down
    void CheckNetworkForUserAction()
    {
        bool internetConnected = PlatformUtils::IsInternetReachable();
        bool wsConnected = internetConnected && EDreamClient::IsWebSocketConnected();
        
        if (!internetConnected)
        {
            ShowNetworkIndicator(5.0, false);  // Show red Net indicator for 5 seconds
        }
        if (!wsConnected)
        {
            ShowRemoteIndicator(5.0);  // Show red Remote indicator for 5 seconds
        }
    }
    
    void AddOSDHud()
    {
        // We pass min max values for speed and brightness
        m_spOSD = std::make_shared<Hud::COSD>(Base::Math::CRect(1, 1), 0, 9, -13, 13);
    }
    
    void AddSplashHud()
    {
#ifndef LINUX_GNU
        std::string defaultDir = std::string(".\\");
#else
        std::string defaultDir = std::string("");
#endif
        // PNG splash
        m_SplashFilename = g_Settings()->Get(
            "settings.player.attrpngfilename",
            g_Settings()->Get("settings.app.InstallDir", PlatformUtils::GetWorkingDir()) +
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
                g_Settings()->Get("settings.app.InstallDir", PlatformUtils::GetWorkingDir()) +
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
    }
    
    void AddProgressHud()
    {
        m_HudManager->Add("progress", std::make_shared<Hud::CStatsConsole>(
                                            Base::Math::CRect(1, 1),
                                            m_HudFontName, m_HudFontSize));
        Hud::spCStatsConsole spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
            m_HudManager->Get("progress"));
        if (spStats)
            spStats->Add(new Hud::CStringStat("downloadprogress", "", ""));
    }
    
    void SetupFramerate()
    {
        m_PerceptualFPS = g_Settings()->Get("settings.player.perceptual_fps", 20.);

        // We give the perceptual FPS to the player, and it is its job to adjust it with dream level
        g_Player().SetPerceptualFPS(m_PerceptualFPS);
    }
    
    void SetupProxy()
    {
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
    }

    //
    virtual bool Startup()
    {
        // Cancel any pending shutdown watchdog (e.g., when restarting after wizard)
        s_shutdownCancelled.store(true);

        // Ensure storage is read-only when in busy mode (e.g. Mac preview forced after constructor)
        InitStorage(m_MultipleInstancesMode);

        m_CpuUsageThreshold =
            g_Settings()->Get("settings.player.cpuusagethreshold", 50);

        if (m_MultipleInstancesMode == false)
        {
            g_NetworkManager->Startup();

            //	Set proxy info.
            SetupProxy();
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

        SetupFramerate();

        CreateHud();
        AddCreditsHud();
        AddNetworkIndicatorHud();
        AddHelpHud();
        AddDreamStatsHud();
        AddProgressHud();
        AddSplashHud();
        AddOSDHud();

        m_spCrossFade = std::make_shared<Hud::CCrossFade>(
            g_Player().Display()->Width(), g_Player().Display()->Height(),
            true);
        

        //	For testing...
        /*ContentDownloader::Shepherd::addMessageText(
            "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do "
            "eiusmod tempor incididunt ut labore et dolore magna aliqua",
            180);*/

        bool internetReachable = PlatformUtils::IsInternetReachable();
        
        // Initialize network state tracking
        m_PrevNetworkUp = internetReachable;
        m_PrevWebsocketUp = false;  // Websocket not connected yet at startup
        
        if (!internetReachable)
        {
            // Show network indicator for 30 seconds at startup if network is down
            ShowNetworkIndicator(30.0, false);
            // Also show remote indicator since network is down
            ShowRemoteIndicator(30.0);
            m_MultipleInstancesMode = true;  // set offline mode
            m_OfflineDueToNoInternetOnly = true;  // don't show Busy indicator (Net/Remote are enough)
        } else if (m_MultipleInstancesMode) {
            g_Log->Info("Forcing offline mode with multiple instances");
            internetReachable = false;
        }
        //    Start downloader.
        g_Log->Info("Starting downloader...");

        g_ContentDownloader().Startup(false, m_MultipleInstancesMode ||
                                                 !internetReachable);

        // Also load our local cache
        // Grab the CacheManager
        Cache::CacheManager& cm = Cache::CacheManager::getInstance();
        cm.loadCachedMetadata();
        
        // Check disk space at startup
        g_ContentDownloader().m_gDownloader.CheckDiskSpace();
        
        // Show disk indicator for 30 seconds at startup if disk space is low
        if (g_ContentDownloader().m_gDownloader.IsDiskSpaceLow()) {
            m_DiskIndicatorEndTime = m_Timer.Time() + 30.0;
            m_PrevDiskSpaceLow = true;
        }
        
        if (m_MultipleInstancesMode) {
            g_Player().SetOfflineMode(true);
            // Show busy indicator only for actual multiple instances, not when offline due to no internet
            if (!IsPreview() && !m_OfflineDueToNoInternetOnly) {
                m_BusyIndicatorEndTime = m_Timer.Time() + 30.0;
            }
        }
        
        // call static method to fill sheep counts
        //ContentDownloader::Shepherd::GetFlockSizeMBsRecount(0);
        //ContentDownloader::Shepherd::GetFlockSizeMBsRecount(1);
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
        // For the first few seconds while the initial auth refresh is in flight,
        // suppress the Remote indicator so brief startup delays don't flash errors.
        m_RemoteIndicatorGraceEndTime = m_Timer.Time() + 5.0;
        g_Player().Start();
        m_F1F4Timer.Reset();
        m_LastCPUCheckTime = m_Timer.Time();
        return true;
    }

    //
    virtual void Shutdown()
    {
        printf("CElectricSheep::Shutdown()\n");

        // Reset cancellation flag - Startup() will set it to true if this is a restart
        s_shutdownCancelled.store(false);

        // Only use aggressive shutdown tactics when not in config mode.
        // Config mode shutdown happens when transitioning from onboarding to playback,
        // so we must not abort network or force-exit in that case.
        bool useAggressiveShutdown = !m_bConfigMode;
        
        if (useAggressiveShutdown)
        {
            // Arm a force-exit watchdog to guarantee instant shutdown.
            // The watchdog checks s_shutdownCancelled before exiting, so if
            // Startup() is called (e.g., when restarting after wizard), it
            // will cancel the watchdog by setting s_shutdownCancelled = true.
            // On macOS screensaver use exit(0) so atexit/destructors run; standalone app uses _exit(0).
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (s_shutdownCancelled.load()) {
                    printf("Shutdown watchdog cancelled - app is restarting\n");
                    return;
                }
                printf("Force exiting after shutdown timeout\n");
                #ifdef SCREEN_SAVER
                exit(0);
                #else
                _exit(0);
                #endif
            }).detach();

            // FAST EXIT: If network is unreachable, exit immediately without waiting
            g_NetworkManager->Abort();
            if (!PlatformUtils::IsInternetReachable()) {
                printf("Network unreachable - forcing immediate exit\n");
                #ifdef SCREEN_SAVER
                exit(0);
                #else
                _exit(0);
                #endif
            }
        }
        else
        {
            // For screensaver or config mode: still abort network operations
            // but don't force-exit, allow normal cleanup
            g_NetworkManager->Abort();
        }

#ifdef DO_THREAD_UPDATE
        DestroyUpdateThreads();
#endif

        m_spSplashPNG = nullptr;
        m_nSplashes = 0;
        m_SplashFilename = std::string();
        m_spCrossFade = nullptr;
        m_StartupScreen = nullptr;
        m_HudManager = nullptr;
        m_bPaused = false;

        if (!m_bConfigMode)
        {
            // Stop playlist manager's periodic checking thread before player shutdown
            // This must happen before Player::Stop() which joins threads
            if (g_Player().m_playlistManager) {
                g_Player().m_playlistManager->stopPeriodicChecking();
            }
            
            g_ContentDownloader().Shutdown();

            //	This stuff was never started in config mode.
            if (m_MultipleInstancesMode == false)
            {
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

    static double FrameNumberToSeconds(int64_t _frames, double _fps)
    {
        return _frames / _fps;
    }

    // Calculate total duration from maxFrameIdx (0-based last frame index)
    // Adds 1 to convert from index to frame count
    static double MaxFrameIdxToDuration(uint32_t _maxFrameIdx, double _fps)
    {
        return FrameNumberToSeconds(_maxFrameIdx + 1, _fps);
    }

    // Calculate current and total time from frame metadata
    // Returns pair of (currentTime, totalTime)
    static std::pair<double, double> CalculateTimecode(const ContentDecoder::sFrameMetadata* frameMetadata, double baseFps)
    {
        if (!frameMetadata) return {0.0, 0.0};

        double totalTime = MaxFrameIdxToDuration(frameMetadata->maxFrameIdx, baseFps);
        double currentTime = (frameMetadata->frameIdx / (double)(frameMetadata->maxFrameIdx + 1)) * totalTime;
        if (currentTime > totalTime) currentTime = totalTime;

        return {currentTime, totalTime};
    }

    static std::string FrameNumberToMinutesAndSecondsString(int64_t _frames,
                                                            float _fps)
    {
        double seconds = FrameNumberToSeconds(_frames, _fps);
        uint64_t timeDiff = (uint64_t)seconds;
        return FormatTimeDiff(timeDiff, true);
    }

    // Format time as XX:YY.ZZ (minutes:seconds.hundredths)
    // No leading 0 for minutes if <10, but YY and ZZ always have leading zeros
    static std::string FormatTimeAsMMSSHundredths(double totalSeconds)
    {
        if (totalSeconds < 0) totalSeconds = 0;

        int minutes = (int)(totalSeconds / 60.0);
        double remainingSeconds = totalSeconds - (minutes * 60.0);
        int seconds = (int)remainingSeconds;
        int hundredths = (int)((remainingSeconds - seconds) * 100.0);

        std::stringstream ss;
        if (minutes >= 10) {
            ss << minutes;
        } else if (minutes > 0) {
            ss << minutes;
        } else {
            ss << "0";
        }
        ss << ":" << std::setfill('0') << std::setw(2) << seconds
           << "." << std::setfill('0') << std::setw(2) << hundredths;

        return ss.str();
    }

    void UpdateTimecodeHUD() {
        if (!m_HudManager)
            return;

        const ContentDecoder::sClipMetadata* clipMetadata =
            g_Player().GetCurrentPlayingClipMetadata();
        const ContentDecoder::sFrameMetadata* frameMetadata =
            g_Player().GetCurrentFrameMetadata();

        if (!clipMetadata)
            return;

        double baseFps = 1.0;
        try
        {
            baseFps = std::stod(clipMetadata->dreamData.fps);
        }
        catch (...)
        {
            baseFps = 1.0;
        }

        double currentTime = g_Player().GetCurrentClipElapsedTime(); // fallback if no frame metadata
        double totalTime = 0.0;

        if (frameMetadata)
        {
            double fps = (frameMetadata->decodeFps > 0.0f) ? frameMetadata->decodeFps : baseFps;
            if (fps <= 0.0) fps = 1.0;

            // Reset the frame start wall time when the frame index advances.
            if (!m_HaveFrameStartTime || frameMetadata->frameIdx != m_LastFrameIdxForHud)
            {
                m_LastFrameIdxForHud = frameMetadata->frameIdx;
                m_FrameStartWallTime = m_Timer.Time();
                m_HaveFrameStartTime = true;
            }

            double frameStart = FrameNumberToSeconds(frameMetadata->frameIdx, fps);
            double frameDuration = 1.0 / fps;
            double sinceFrameStart = (m_Timer.Time() - m_FrameStartWallTime) * g_Player().GetPerceptualFPS() / fps;
            
            if (sinceFrameStart < 0.0) sinceFrameStart = 0.0;
            if (sinceFrameStart > frameDuration) sinceFrameStart = frameDuration;

            currentTime = frameStart + sinceFrameStart;
            totalTime = MaxFrameIdxToDuration(frameMetadata->maxFrameIdx, fps);
        }

        if (currentTime < 0.0) currentTime = 0.0;
        if (totalTime > 0.0 && currentTime > totalTime) currentTime = totalTime;

        std::string currentTimeStr = FormatTimeAsMMSSHundredths(currentTime);
        std::string totalTimeStr = (totalTime > 0.0)
                                       ? FormatTimeAsMMSSHundredths(totalTime)
                                       : "--:--.--";

        std::lock_guard<std::mutex> lock(m_TimecodeHudMutex);

        if (auto spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                m_HudManager->Get("dreamstats")))
        {
            if (auto playHead =
                    static_cast<Hud::CStringStat*>(spStats->Get("playHead")))
            {
                playHead->SetSample(
                    string_format("%s/%s", currentTimeStr.c_str(),
                                  totalTimeStr.c_str()));
            }
        }

        if (auto spCredits = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                m_HudManager->Get("dreamcredits")))
        {
            if (auto creditsTime =
                    static_cast<Hud::CStringStat*>(spCredits->Get("credits-time")))
            {
                creditsTime->SetSample(
                    string_format("%s/%s", currentTimeStr.c_str(),
                                  totalTimeStr.c_str()));
            }
        }
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
    virtual bool Update(int _displayIdx)
    {
        g_Player().BeginFrameUpdate();

        bool ret = DoRealFrameUpdate(_displayIdx);

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
    
    void updateNextCheckTimeDisplay() {
        auto timeUntilNextCheck = g_Player().m_playlistManager->getTimeUntilNextCheck();
        int minutes = std::chrono::duration_cast<std::chrono::minutes>(timeUntilNextCheck).count();

        std::stringstream ss;
        if (minutes == 0) {
            ss << "Next playlist check in less than a minute"
            << " | (Playing video " << (g_Player().m_playlistManager->getCurrentPosition() + 1) // +1 for human-readable indexing
            << "/" << g_Player().m_playlistManager->getPlaylistSize() << ")";
        } else {
            ss << "Next playlist check in " << minutes << " minute" << (minutes != 1 ? "s" : "")
            << " | (Playing video " << (g_Player().m_playlistManager->getCurrentPosition() + 1) // +1 for human-readable indexing
            << "/" << g_Player().m_playlistManager->getPlaylistSize() << ")";
        }

        // Update the HUD with this information
        Hud::spCStatsConsole spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(m_HudManager->Get("dreamstats"));
        if (spStats) {
            auto pNextCheckStat = static_cast<Hud::CStringStat*>(spStats->Get("next_check_time"));
            if (!pNextCheckStat) {
                // If the stat doesn't exist, create it
                pNextCheckStat = new Hud::CStringStat("next_check_time", "", "");
                spStats->Add(pNextCheckStat);
            }
            pNextCheckStat->SetSample(ss.str());
        }
    }

    
// MARK: Main per frame update loop
    virtual bool DoRealFrameUpdate(uint32_t displayUnit)
    {
        if (!g_Player().BeginDisplayFrame(displayUnit))
            return true;

        {
            // g_Player().Renderer()->BeginFrame();

            if (g_Player().Closed())
            {
                g_Log->Info("Player closed...");
                g_Player().Renderer()->EndFrame();
                return false;
            }

            bool drawNoSheepIntro = false;
            bool drawn = g_Player().Update(displayUnit); // , drawNoSheepIntro);

            
            if ((!EDreamClient::IsLoggedIn() && !m_MultipleInstancesMode) || !g_Player().HasStarted()) {
                drawNoSheepIntro = true;
            }
            
            if ((drawNoSheepIntro && displayUnit == 0) ||
                (drawn && displayUnit == 0))
            {
                
                if (drawNoSheepIntro || (m_StartupScreen && !m_StartupScreen->IsFullyFaded()))
                {
                    if (!m_StartupScreen)
                        m_StartupScreen = std::make_shared<Hud::CStartupScreen>(
                            Base::Math::CRect(0, 0, 1., 1.), "Lato", 24);
                            
                    // Start fading out when player has started and we're not already fading
                    if (g_Player().HasStarted() && !m_StartupScreen->IsFadingOut()) {
                        m_StartupScreen->StartFadeOut(m_Timer.Time());
                    }
                    
                    m_StartupScreen->Render(m_Timer.Time(), g_Player().Renderer());
                }
                
                /*if (drawNoSheepIntro)
                {
                    if (!m_StartupScreen)
                        m_StartupScreen = std::make_shared<Hud::CStartupScreen>(
                            Base::Math::CRect(0, 0, 1., 1.), "Lato", 24);
                    m_StartupScreen->Render(0., g_Player().Renderer());
                    
                }*/
                
                Hud::spCStatsConsole spStats =
                    std::dynamic_pointer_cast<Hud::CStatsConsole>(
                        m_HudManager->Get("progress"));
                Hud::CStringStat* pTmp = nullptr;
                if (spStats)
                    pTmp = (Hud::CStringStat*)spStats->Get("downloadprogress");
                if (pTmp)
                {
                    std::string serverStatus = g_NetworkManager->Status();
                    if (drawNoSheepIntro)
                    {
                        // While the initial auth refresh attempt is still running,
                        // do not show any "please log in" messaging yet.
                        if (!EDreamClient::HasCompletedInitialAuth()) {
                            pTmp->SetSample("");
                            pTmp->Visible(false);
                        } else if (!EDreamClient::IsLoggedIn()) {
                            if (EDreamClient::IsAuthRetryPending()) {
                                pTmp->SetSample("");
                                pTmp->Visible(false);
                            } else {
                                if (m_MultipleInstancesMode && !IsPreview()) {
                                    pTmp->SetSample("Starting in busy mode.");
                                } else {
#ifdef SCREEN_SAVER
                                    pTmp->SetSample("Please open settings to sign in.");
#else
                                    pTmp->SetSample("Please open settings to sign in.");
#endif
                                }
                                pTmp->Visible(true);
                            }
                        } else {
                            pTmp->Visible(true);
                        }
                    }
                    else
                        pTmp->Visible(false);
                }
                //	Process any server messages.
                std::string msg;
                double duration;
/*
                // TODO : Is that some old splash screen mechanism ?
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
*/
                if (m_MessageQueue.PopMessage(msg, duration))
                {
                    bool addtohud = true;
                    time_t lt = time(nullptr);

                    if ((msg == "error connecting to server") ||
                        (msg == "server request failed, using free one"))
                    {
                        std::string temptime = ctime(&lt);
                        msg = temptime.substr(0, temptime.length() - strlen("\n")) + " " + msg;
                        g_Log->Error(msg.c_str());
                        if (m_ConnectionErrors.size() > 20)
                            m_ConnectionErrors.pop_front();
                        m_ConnectionErrors.push_back(msg);
                        if (g_Settings()->Get("settings.player.quiet_mode", true) == true)
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
                
                // Update CPU and GPU usage measurements
                // Both methods rate-limit internally to reduce jitter
                m_CpuUsage.GetAppCpuUsage(m_CpuUsageES, m_CpuUsageTotal);
                int prevGpuUsage = m_GpuUsage;
                m_GpuUsage = m_CpuUsage.GetGpuUsage();
                if (m_GpuUsage != prevGpuUsage)
                {
                    float metalMs = g_Player().Renderer()->GetGPUFrameTimeMs();
                    // g_Log->Info("GPU: system=%d%%, metal=%.2fms",
                    //             m_GpuUsage, metalMs);
                }

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
                    //blockRendering = true;
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

/*                if (blockRendering)
                    ContentDownloader::Shepherd::SetRenderingAllowed(false);
                else
                    ContentDownloader::Shepherd::SetRenderingAllowed(true);
*/
                
                // Update and display the time until next playlist check
                updateNextCheckTimeDisplay();
                
                //	Update some stats.
                spStats =
                    std::dynamic_pointer_cast<Hud::CStatsConsole>(
                        m_HudManager->Get("dreamstats"));
                float activityLevel = 1.f;
                double realFps = 20;
                bool isStreamingCurrent = false;
                const ContentDecoder::sClipMetadata* clipMetadata =
                    g_Player().GetCurrentPlayingClipMetadata();
                double baseFps = 1;
                if (clipMetadata)
                {
                    activityLevel = clipMetadata->dreamData.activityLevel;
                    realFps = clipMetadata->decodeFps;
                    isStreamingCurrent = !(clipMetadata->dreamData.isCached());
                    baseFps = std::stod(clipMetadata->dreamData.fps);
                }

                const ContentDecoder::sFrameMetadata* frameMetadata =
                    g_Player().GetCurrentFrameMetadata();
                UpdateTimecodeHUD();
                
                // Grab Perceptual FPS from player
                double pFPS = g_Player().GetPerceptualFPS();
                if (spStats) {
                if (isStreamingCurrent) {
                    ((Hud::CStringStat*)spStats->Get("decodefps"))
                        ->SetSample(string_format(" %.2f fps (streaming)", realFps));

                } else {
                    ((Hud::CStringStat*)spStats->Get("decodefps"))
                        ->SetSample(string_format(" %.2f fps", realFps));
                }

                ((Hud::CStringStat*)spStats->Get("perceptualfps"))
                    ->SetSample(string_format(" %.2f fps", pFPS));

                ((Hud::CStringStat*)spStats->Get("activityLevel"))
                    ->SetSample(string_format(" %.2f", activityLevel));
                ((Hud::CIntCounter*)spStats->Get("displayfps"))->AddSample(1);

                // Update playlist info
                static std::string lastPlaylistUUID = "";
                static size_t prevPosition = -1;
                static size_t trackedCurrentPosition = -1;

                std::string playlistUUID = g_Player().GetPlaylistManager().getPlaylistUUID();
                size_t currentPosition = g_Player().GetPlaylistManager().getCurrentPosition();
                size_t playlistSize = g_Player().GetPlaylistManager().getPlaylistSize();
                auto currentDream = g_Player().GetPlaylistManager().getCurrentDream();
                std::string dreamUUID = currentDream ? currentDream->uuid : "None";
                PlaybackMode mode = g_Player().GetPlaylistManager().getPlaybackMode();

                // If playlist changed, reset prev to -1
                if (playlistUUID != lastPlaylistUUID) {
                    prevPosition = -1;
                    lastPlaylistUUID = playlistUUID;
                    trackedCurrentPosition = currentPosition;
                }
                // If position changed (and not in Repeat mode), update prev to the old current
                else if (currentPosition != trackedCurrentPosition && mode != PlaybackMode::Repeat) {
                    prevPosition = trackedCurrentPosition;
                    trackedCurrentPosition = currentPosition;
                }

                ((Hud::CStringStat*)spStats->Get("currentplaylist"))
                    ->SetSample(playlistUUID.empty() ? "None" : playlistUUID);
                ((Hud::CStringStat*)spStats->Get("playlistposition"))
                    ->SetSample(string_format("%s [%zu/%zu] prev:%zu", dreamUUID.c_str(), currentPosition, playlistSize, prevPosition));

                // Update playback mode display
                ((Hud::CStringStat*)spStats->Get("playbackmode"))
                    ->SetSample(to_string(mode));
                }

                // Update OSD
                m_spOSD->SetFPS(pFPS);

                // Buffering and preloading checks
                bool isBuffering = g_Player().IsAnyClipBuffering();
                bool isPreloading = g_Player().IsPreloading();
                bool isCurrentClipStreaming = isStreamingCurrent; // Use existing variable that tracks if current clip is streaming
                
                // Show indicator only if current clip is buffering (not preloading next clip)
                bool shouldShowBufferIndicator = isCurrentClipStreaming && isBuffering;

                // Manage the buffering icon
                if (shouldShowBufferIndicator && !wasShowingBufferIndicator) {
                    // Buffering or preloading started on a streaming clip - show the icon
                    m_spOSD->SetType(Hud::Buffering);
                    m_HudManager->Add("osd-rebuffering", m_spOSD, 60);
                    wasShowingBufferIndicator = true;
                } else if (!shouldShowBufferIndicator && wasShowingBufferIndicator) {
                    // Buffering/preloading ended or not streaming anymore - hide the icon
                    m_HudManager->Hide("osd-rebuffering");
                    wasShowingBufferIndicator = false;
                }

                // Simple pause/unpause logic - any buffering means pause
                if (isBuffering && !g_Player().IsPausedForBuffering()) {
                    g_Log->Info("Pausing for buffering");
                    g_Player().SetPausedForBuffering(true);
                    if (!m_bPaused) {
                        g_Player().SetPaused(true);
                    }
                } else if (!isBuffering && g_Player().IsPausedForBuffering()) {
                    g_Log->Info("Buffering complete, unpausing");
                    g_Player().SetPausedForBuffering(false);
                    if (!m_bPaused || !g_Player().IsUserPaused()) {
                        g_Player().SetPaused(false);
                    }
                }
                
                // Update credits
                spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                    m_HudManager->Get("dreamcredits"));

                // Update status indicators with dynamic colors (always update, not conditional on metadata)
                bool internetConnected = PlatformUtils::IsInternetReachable();
                bool wsConnected = internetConnected && EDreamClient::IsWebSocketConnected();
                
                // Detect network state transitions for standalone indicator
                bool netWentDown = m_PrevNetworkUp && !internetConnected;
                bool netWentUp = !m_PrevNetworkUp && internetConnected;
                bool remoteWentDown = m_PrevWebsocketUp && !wsConnected;
                bool remoteWentUp = !m_PrevWebsocketUp && wsConnected;
                
                // Handle Net transitions
                if (netWentDown) {
                    m_NetworkWasDown = true;
                    ShowNetworkIndicator(30.0, false);
                } else if (netWentUp) {
                    if (m_NetworkWasDown) {
                        ShowNetworkIndicator(5.0, true);  // Green recovery
                        m_NetworkWasDown = false;
                    }
                }
                
                // Handle Remote transitions
                if (remoteWentDown) {
                    m_RemoteWasDown = true;
                    ShowRemoteIndicator(30.0);
                } else if (remoteWentUp) {
                    // Only show green recovery if this is NOT the first connection
                    if (m_RemoteFirstConnected && m_RemoteWasDown) {
                        ShowRemoteIndicator(5.0);  // Brief show on recovery
                    } else {
                        // First connection - hide any initial failure indicator
                        HideRemoteIndicator();
                    }
                    m_RemoteWasDown = false;
                    m_RemoteFirstConnected = true;  // Mark that we've connected at least once
                } else if (!m_RemoteFirstConnected && !wsConnected && !m_RemoteInitialIndicatorShown) {
                    // Initial connection not yet established - show indicator once
                    ShowRemoteIndicator(30.0);
                    m_RemoteInitialIndicatorShown = true;
                }
                
                // Update previous state for next frame
                m_PrevNetworkUp = internetConnected;
                m_PrevWebsocketUp = wsConnected;
                
                // Check if network indicator timer has expired
                if (m_NetworkIndicatorEndTime > 0.0 && m_Timer.Time() >= m_NetworkIndicatorEndTime) {
                    HideNetworkIndicator();
                }
                
                // Check if remote indicator timer has expired
                if (m_RemoteIndicatorEndTime > 0.0 && m_Timer.Time() >= m_RemoteIndicatorEndTime) {
                    HideRemoteIndicator();
                }

                // Check if update is available and detect transitions
                bool updateAvailableForIndicator = ESScreensaver_IsUpdateAvailable();

                // Detect update availability transition (from not available to available)
                if (updateAvailableForIndicator && !m_PrevUpdateAvailable) {
                    // Update just became available - show indicator for 30 seconds
                    ShowUpdateIndicator(30.0);
                }
                m_PrevUpdateAvailable = updateAvailableForIndicator;

                // Check if update indicator timer has expired
                if (m_UpdateIndicatorEndTime > 0.0 && m_Timer.Time() >= m_UpdateIndicatorEndTime) {
                    HideUpdateIndicator();
                }

                // Check disk space and detect transitions
                bool diskSpaceLow = g_ContentDownloader().m_gDownloader.IsDiskSpaceLow();
                if (diskSpaceLow && !m_PrevDiskSpaceLow) {
                    // Disk space just became low - show indicator for 30 seconds
                    m_DiskIndicatorEndTime = m_Timer.Time() + 30.0;
                }
                m_PrevDiskSpaceLow = diskSpaceLow;

                // Check if disk indicator timer has expired
                if (m_DiskIndicatorEndTime > 0.0 && m_Timer.Time() >= m_DiskIndicatorEndTime) {
                    m_DiskIndicatorEndTime = 0.0;
                }

                // During the very first auth refresh request, suppress the Remote
                // indicator for up to 5 seconds after startup so short-lived
                // startup hiccups don't show as a Remote error.
                bool inInitialAuthWindow =
                    !EDreamClient::HasCompletedInitialAuth() &&
                    (m_Timer.Time() < m_RemoteIndicatorGraceEndTime);

                // Check if credits overlay is visible
                bool creditsVisible = false;
                if (auto spCreditsHud = m_HudManager->Get("dreamcredits")) {
                    creditsVisible = spCreditsHud->Visible();
                }

                // Check if busy indicator timer has expired
                if (m_BusyIndicatorEndTime > 0.0 && m_Timer.Time() >= m_BusyIndicatorEndTime) {
                    m_BusyIndicatorEndTime = 0.0;
                }

                // Show standalone indicator HUD only when credits overlay is hidden
                // and any indicator timer is active (hide all in preview mode)
                bool showDisk = m_DiskIndicatorEndTime > 0.0 && !IsPreview();
                bool showBusy = m_BusyIndicatorEndTime > 0.0 && !IsPreview();
                bool showNet = m_NetworkIndicatorEndTime > 0.0 && !IsPreview();
                bool showRemote = m_RemoteIndicatorEndTime > 0.0 && !IsPreview() && !inInitialAuthWindow;
                bool showUpdate = m_UpdateIndicatorEndTime > 0.0;
                
                bool shouldShowIndicatorHUD = !creditsVisible &&
                                              (showDisk || showBusy || showNet || showRemote || showUpdate);
                
                // Calculate dynamic spacing based on which indicators will be shown AFTER each one
                // Order (left to right): Disk -> Busy -> Net -> Remote -> Update
                // Each indicator width (including dot and space): Disk=12, Busy=12, Net=10, Remote=17, Update=16
                const int WIDTH_DISK = 12;     // "● Disk"
                const int WIDTH_BUSY = 12;    // "● Busy"
                const int WIDTH_NET = 10;     // "● Net"
                const int WIDTH_REMOTE = 17;  // "● Remote"
                const int WIDTH_UPDATE = 16;  // "● Update"
                const int GAP = 2;           // gap between indicators
                
                // Disk needs spacing for: Busy (if shown) + Net (if shown) + Remote (if shown) + Update (if shown)
                int spacesForDiskIndicator = (showBusy ? WIDTH_BUSY + GAP : 0) + (showNet ? WIDTH_NET + GAP : 0) + (showRemote ? WIDTH_REMOTE + GAP : 0) + (showUpdate ? WIDTH_UPDATE + GAP : 0);
                // Busy needs spacing for: Net (if shown) + Remote (if shown) + Update (if shown)
                int spacesForBusyIndicator = (showNet ? WIDTH_NET + GAP : 0) + (showRemote ? WIDTH_REMOTE + GAP : 0) + (showUpdate ? WIDTH_UPDATE + GAP : 0);
                // Net needs spacing for: Remote (if shown) + Update (if shown)
                int spacesForNetIndicator = (showRemote ? WIDTH_REMOTE + GAP : 0) + (showUpdate ? WIDTH_UPDATE + GAP : 0);
                // Remote needs spacing for: Update (if shown)
                int spacesForWSIndicator = (showUpdate ? WIDTH_UPDATE + GAP : 0);
                // Update needs spacing for: nothing
                int spacesForUpdateIndicator = 0;
                
                // Update network indicator content
                if (auto spNetIndicator = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                        m_HudManager->Get("network-indicator")))
                {
                    if (shouldShowIndicatorHUD)
                    {
                        // Make sure the HUD is visible
                        spNetIndicator->Visible(true);
                        
                        // Placeholder rows are already set with transparent color in AddNetworkIndicatorHud()
                        
                        // Disk indicator - show when timer is active
                        if (showDisk) {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-dsk"))
                                ->SetSample("\u25CF Disk" + std::string(spacesForDiskIndicator, ' '));
                            spNetIndicator->SetColor("net-indicator-dsk", Base::Math::CVector4(1, 0, 0, 1));  // Red
                        } else {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-dsk"))
                                ->SetSample("");
                        }
                        
                        // Busy indicator - show when timer is active
                        if (showBusy) {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-busy"))
                                ->SetSample("\u25CF Busy" + std::string(spacesForBusyIndicator, ' '));
                            spNetIndicator->SetColor("net-indicator-busy", Base::Math::CVector4(1, 0, 0, 1));  // Red
                        } else {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-busy"))
                                ->SetSample("");
                        }
                        
                        // Net indicator - show when timer is active
                        if (showNet)
                        {
                            Base::Math::CVector4 netColor = m_NetworkIndicatorShowGreen ? 
                                Base::Math::CVector4(0, 1, 0, 1) : 
                                (internetConnected ? Base::Math::CVector4(0, 1, 0, 1) : Base::Math::CVector4(1, 0, 0, 1));
                            
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-net"))
                                ->SetSample("\u25CF Net" + std::string(spacesForNetIndicator, ' '));
                            spNetIndicator->SetColor("net-indicator-net", netColor);
                        }
                        else
                        {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-net"))
                                ->SetSample("");
                        }
                        
                        // Remote indicator - show when timer is active
                        if (showRemote)
                        {
                            Base::Math::CVector4 wsColor = wsConnected ? 
                                Base::Math::CVector4(0, 1, 0, 1) : Base::Math::CVector4(1, 0, 0, 1);
                            
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-ws"))
                                ->SetSample("\u25CF Remote" + std::string(spacesForWSIndicator, ' '));
                            spNetIndicator->SetColor("net-indicator-ws", wsColor);
                        }
                        else
                        {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-ws"))
                                ->SetSample("");
                        }
                        
                        // Update indicator - show only when timer is active
                        if (m_UpdateIndicatorEndTime > 0.0) {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-upd"))
                                ->SetSample("\u25CF Update");
                            spNetIndicator->SetColor("net-indicator-upd", Base::Math::CVector4(1, 1, 0, 1));  // Yellow
                        } else {
                            ((Hud::CStringStat*)spNetIndicator->Get("net-indicator-upd"))
                                ->SetSample("");
                        }
                    }
                    else
                    {
                        // Hide the indicator HUD when not needed
                        spNetIndicator->Visible(false);
                    }
                }

                if (spStats)
                {
                    // Disk space indicator - only show when disk space is low (hide all in preview mode)
                    bool diskSpaceLow = g_ContentDownloader().m_gDownloader.IsDiskSpaceLow();
                    bool updateAvailable = ESScreensaver_IsUpdateAvailable();

                    bool showDisk = diskSpaceLow && !IsPreview();
                    showBusy = m_MultipleInstancesMode && !m_OfflineDueToNoInternetOnly && !IsPreview();
                    showNet = !internetConnected && !IsPreview();
                    showRemote = !wsConnected && !IsPreview() && !inInitialAuthWindow;
                    showUpdate = updateAvailable;

                    // Disk needs spacing for: Busy (if shown) + Net (if shown) + Remote (if shown) + Update (if shown)
                    int spacesForDiskIndicator = (showBusy ? WIDTH_BUSY + GAP : 0) + (showNet ? WIDTH_NET + GAP : 0) + (showRemote ? WIDTH_REMOTE + GAP : 0) + (showUpdate ? WIDTH_UPDATE + GAP : 0);
                    // Busy needs spacing for: Net (if shown) + Remote (if shown) + Update (if shown)
                    spacesForBusyIndicator = (showNet ? WIDTH_NET + GAP : 0) + (showRemote ? WIDTH_REMOTE + GAP : 0) + (showUpdate ? WIDTH_UPDATE + GAP : 0);
                    // Net needs spacing for: Remote (if shown) + Update (if shown)
                    spacesForNetIndicator = (showRemote ? WIDTH_REMOTE + GAP : 0) + (showUpdate ? WIDTH_UPDATE + GAP : 0);
                    // Remote needs spacing for: Update (if shown)
                    spacesForWSIndicator = (showUpdate ? WIDTH_UPDATE + GAP : 0);
                    // Update needs spacing for: nothing
                    spacesForUpdateIndicator = 0;

                    // Busy indicator - show when in offline/multiple instances mode
                    if (showBusy) {
                        ((Hud::CStringStat*)spStats->Get("credits-busy"))
                            ->SetSample("\u25CF Busy" + std::string(spacesForBusyIndicator, ' '));
                        spStats->SetColor("credits-busy", Base::Math::CVector4(1, 0, 0, 1));  // Red
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-busy"))
                            ->SetSample("");  // Hide when not in busy mode
                    }

                    // Net and Remote indicators - only show in credits overlay when there's a problem (red)
                    if (showNet) {
                        ((Hud::CStringStat*)spStats->Get("credits-net"))
                            ->SetSample("\u25CF Net" + std::string(spacesForNetIndicator, ' '));
                        spStats->SetColor("credits-net", Base::Math::CVector4(1, 0, 0, 1));  // Red
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-net"))
                            ->SetSample("");  // Hide when connected
                    }

                    if (showRemote) {
                        ((Hud::CStringStat*)spStats->Get("credits-ws"))
                            ->SetSample("\u25CF Remote" + std::string(spacesForWSIndicator, ' '));
                        spStats->SetColor("credits-ws", Base::Math::CVector4(1, 0, 0, 1));  // Red
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-ws"))
                            ->SetSample("");  // Hide when connected
                    }
                    
                    if (showDisk) {
                        // Disk indicator with spacing (Update indicator may follow)
                        ((Hud::CStringStat*)spStats->Get("credits-dsk"))
                            ->SetSample("\u25CF Disk" + std::string(spacesForDiskIndicator, ' '));
                        spStats->SetColor("credits-dsk", Base::Math::CVector4(1, 0, 0, 1));  // Red when low
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-dsk"))
                            ->SetSample("");  // Hide when disk space is OK
                    }

                    // Update available indicator - yellow, always leftmost when shown
                    if (showUpdate) {
                        ((Hud::CStringStat*)spStats->Get("credits-upd"))
                            ->SetSample(std::string("\u25CF Update" + std::string(spacesForUpdateIndicator, ' ')));
                        spStats->SetColor("credits-upd", Base::Math::CVector4(1, 1, 0, 1));  // Yellow
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-upd"))
                            ->SetSample("");  // Hide when no update available
                    }

                    // Update download indicator
                    if (isStreamingCurrent && isBuffering) {
                        ((Hud::CStringStat*)spStats->Get("credits-download"))->SetSample("\u21BB buffering");  // ↻
                    } else if (isPreloading) {
                        ((Hud::CStringStat*)spStats->Get("credits-download"))->SetSample("\u2193 preloading");  // ↓
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-download"))->SetSample("");  // Clear
                    }
                }

                if (clipMetadata)
                {
                    // Set left-aligned title and right-aligned time info
                    ((Hud::CStringStat*)spStats->Get("credits-title"))
                        ->SetSample(string_format("title: %s", clipMetadata->dreamData.name.c_str()));

                    // Set track position right-aligned on title row
                    {
                        size_t pos = g_Player().GetPlaylistManager().getCurrentPosition();
                        size_t total = g_Player().GetPlaylistManager().getPlaylistSize();
                        ((Hud::CStringStat*)spStats->Get("credits-track"))
                            ->SetSample(string_format("%zu/%zu", pos + 1, total));
                    }

                    // Set left-aligned artist and right-aligned fps info
                    ((Hud::CStringStat*)spStats->Get("credits-artist"))
                        ->SetSample(string_format("artist: %s", clipMetadata->dreamData.artist.c_str()));
                    ((Hud::CStringStat*)spStats->Get("credits-fps"))
                        ->SetSample(string_format("%.2f fps", pFPS));

                    // Set playlist name
                    ((Hud::CStringStat*)spStats->Get("credits-playlist"))
                        ->SetSample(string_format("playlist: %s", g_Player().GetPlaylistName().c_str()));

                    // Set mode on separate line
                    PlaybackMode creditsMode = g_Player().GetPlaylistManager().getPlaybackMode();
                    auto& pm = g_Player().GetPlaylistManager();
                    int loopIter = pm.getLoopIterations();
                    int loopCount = pm.getCurrentLoopCount();
                    // Only show loop indicator if the playing clip matches the playlist position
                    // (they can be out of sync briefly during seamless transitions)
                    bool clipMatchesPlaylist = clipMetadata->dreamData.uuid == pm.getCurrentDreamUUID();
                    if (loopIter > 0 && loopCount > 0 && clipMatchesPlaylist) {
                        ((Hud::CStringStat*)spStats->Get("credits-mode"))
                            ->SetSample(string_format("mode: %s, loop %d/%d", to_string(creditsMode), loopCount, loopIter));
                    } else {
                        ((Hud::CStringStat*)spStats->Get("credits-mode"))
                            ->SetSample(string_format("mode: %s", to_string(creditsMode)));
                    }
                }
                
                //	Serverstats.
                spStats = std::dynamic_pointer_cast<Hud::CStatsConsole>(
                    m_HudManager->Get("dreamstats"));
                if (spStats) {
                //    Prettify uptime.
                uint64_t uptime = (uint64_t)m_Timer.Time();

                ((Hud::CStringStat*)spStats->Get("uptime"))
                    ->SetSample(FormatTimeDiff(uptime, true));
                // Update CPU usage display (percentage of total CPU capacity)
                if (m_CpuUsageES >= 0)
                {
                    int numCores = m_CpuUsage.GetNumCores();
                    ((Hud::CStringStat*)spStats->Get("zzacpu"))
                        ->SetSample(string_format("%i%% app, %i%% total (%d cores)", 
                                                  m_CpuUsageES, m_CpuUsageTotal, numCores));
                    EDreamClient::SetCPUUsage(m_CpuUsageES);
                }
                
                // Update GPU usage display (Metal frame time + system GPU %)
                {
                    float gpuMs = g_Player().Renderer()->GetGPUFrameTimeMs();
                    float gpuPct = g_Player().Renderer()->GetGPUUtilization();
                    std::string sysGpu = (m_GpuUsage >= 0) ? string_format(", %i%%", m_GpuUsage) : "";
                    if (gpuMs >= 0)
                        ((Hud::CStringStat*)spStats->Get("zzagpu2"))
                            ->SetSample(string_format("%.2fms (%.0f%%)%s total",
                                gpuMs, gpuPct, sysGpu.c_str()));
                    else
                        ((Hud::CStringStat*)spStats->Get("zzagpu2"))
                            ->SetSample("n/a");
                }

                Cache::CacheManager& cm = Cache::CacheManager::getInstance();
                // Make sure the cache is primed before using those
                if (cm.dreamCount() > 0) {
                    std::stringstream tmpstr;
                    auto dreamCount = cm.getCachedDreamCount();
                    
                    auto cacheSizeGB = cm.getCacheSize();
                    std::stringstream stream;
                    stream << std::fixed << std::setprecision(1) << cacheSizeGB;
                    // std::string cacheSizeString = stream.str() + " GB";
                    
                    
                    tmpstr << dreamCount << " dream" << (dreamCount > 1 ? "s" : "")
                    << ", " << stream.str() << " GB";
                    ((Hud::CStringStat*)spStats->Get("all"))
                    ->SetSample(tmpstr.str());
                }
                
                pTmp =
                    (Hud::CStringStat*)spStats->Get("transfers");
                if (pTmp)
                {
                    std::string serverStatus = g_NetworkManager->Status();
                    pTmp->SetSample(serverStatus.empty() ? " \n " : serverStatus);
                }

                pTmp = (Hud::CStringStat*)spStats->Get("loginstatus");
                if (pTmp)
                {
                    bool visible = true;

                    if (EDreamClient::IsLoggedIn())
                    {
                        std::string email = g_Settings()->Get("settings.generator.nickname", std::string(""));
                        std::stringstream loginstatusstr;
                        loginstatusstr
                        << "Signed in as " << email << "\nRemaining quota: " << cm.getRemainingQuotaAsString();
                        
                        pTmp->SetSample(loginstatusstr.str());
                    }
                    else
                    {
                        if (m_MultipleInstancesMode) {
                            pTmp->SetSample("Busy mode, please close other instances of infnidream");
                        } else {
                            pTmp->SetSample("Not signed in");
                        }
                    }
                    pTmp->Visible(visible);
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
                    }
                    else
                        pTmp->SetSample("");
                }

                Hud::CTimeCountDownStat* pTcd =
                    (Hud::CTimeCountDownStat*)spStats->Get("svstat");
                if (pTcd)
                {
                    std::string dlState =
                                        g_ContentDownloader().m_gDownloader.GetDownloadStatus();

                    if (dlState != m_PreviousDlState)
                    {
                        pTcd->SetSample(dlState);
                        pTcd->Visible(true);
                        m_PreviousDlState = dlState;
                    }
                }
                } // if (spStats) - dreamstats

                //	Finally render hud.
                m_HudManager->Render(g_Player().Renderer());

                //	Update display events.
                g_Player().Display()->Update();
            }

            g_Player().EndDisplayFrame(displayUnit, drawn);
        }

        return true;
    }

    enum eClientCommand
    {
        CLIENT_COMMAND_LIKE,
        CLIENT_COMMAND_DISLIKE,
        CLIENT_COMMAND_REPORT,
        CLIENT_COMMAND_PREVIOUS,
        CLIENT_COMMAND_NEXT,
        CLIENT_COMMAND_REPEAT,
        CLIENT_COMMAND_SHUFFLE,
        CLIENT_COMMAND_PLAYBACK_SLOWER,
        CLIENT_COMMAND_PLAYBACK_FASTER,
        CLIENT_COMMAND_F1,
        CLIENT_COMMAND_F2,
        CLIENT_COMMAND_SKIP_FW,
        CLIENT_COMMAND_SKIP_BW,
        CLIENT_COMMAND_PAUSE,
        CLIENT_COMMAND_CREDIT,
        CLIENT_COMMAND_WEBPAGE,
        CLIENT_COMMAND_BRIGHTNESS_UP,
        CLIENT_COMMAND_BRIGHTNESS_DOWN,
        CLIENT_COMMAND_SPEED_1,
        CLIENT_COMMAND_SPEED_2,
        CLIENT_COMMAND_SPEED_3,
        CLIENT_COMMAND_SPEED_4,
        CLIENT_COMMAND_SPEED_5,
        CLIENT_COMMAND_SPEED_6,
        CLIENT_COMMAND_SPEED_7,
        CLIENT_COMMAND_SPEED_8,
        CLIENT_COMMAND_SPEED_9,
        CLIENT_COMMAND_RESET_PLAYLIST
    };

    void popOSD(Hud::OSDType type) {
        if (type == Hud::Brightness) {
            m_spOSD->SetBrightness(m_Brightness);
        }
        m_spOSD->SetType(type);

        m_HudManager->Add("osd-common", m_spOSD, 1);
    }
    
    // Thread-safe: enqueue a command to be executed on the main/render thread.
    // Call this from background threads (e.g. WebSocket) instead of ExecuteCommand.
    void EnqueueCommand(eClientCommand _command)
    {
        std::lock_guard<std::mutex> lock(m_CommandQueueMutex);
        m_CommandQueue.push(static_cast<int>(_command));
    }

    // Process any queued remote-control commands (called on the main thread).
    void ProcessCommandQueue()
    {
        std::lock_guard<std::mutex> lock(m_CommandQueueMutex);
        while (!m_CommandQueue.empty())
        {
            int cmd = m_CommandQueue.front();
            m_CommandQueue.pop();
            ExecuteCommand(static_cast<eClientCommand>(cmd));
        }
    }

    virtual bool ExecuteCommand(eClientCommand _command)
    {
        g_Log->Info("ExecuteCommand called with command: %d", (int)_command);

        static const float voteDelaySeconds = 1;
        const ContentDecoder::sClipMetadata* data =
            g_Player().GetCurrentPlayingClipMetadata();

        std::string currentDreamUUID;
        if (data != nullptr) {
            currentDreamUUID = data->dreamData.uuid;
        }

        if ((int)_command && (int)_command != 5 && (int)_command != 6)
            m_StatsCodeCounter = 0;
        switch (_command)
        {
            case CLIENT_COMMAND_LIKE:
                if (m_StatsCodeCounter == 4)
                {
                    m_HudManager->Toggle("dreamstats");
                    m_StatsCodeCounter = 0;
                    return true;
                }
                if (data != nullptr) {
                    CheckNetworkForUserAction();  // Show indicator for 5s if network down
                    EDreamClient::Like(data->dreamData.uuid);
                    popOSD(Hud::Like);
                }
                return true;
            case CLIENT_COMMAND_DISLIKE:
                if (data != nullptr) {
                    CheckNetworkForUserAction();  // Show indicator for 5s if network down
                    EDreamClient::Dislike(data->dreamData.uuid);
                    
                    if (g_Settings()->Get("settings.content.negvotedeletes", true))
                    {
                        // g_Player().Stop();
                        g_Player().MarkForDeletion(currentDreamUUID);
                        if (g_Player().m_playlistManager) {
                            g_Player().m_playlistManager->removeCurrentDream();
                        }
                        g_Player().SkipToNext();
                        m_spCrossFade->Reset();
                        m_HudManager->Add("fade", m_spCrossFade, 1);
                    } else {
                        g_Player().SkipToNext();
                        m_spCrossFade->Reset();
                        m_HudManager->Add("fade", m_spCrossFade, 1);
                    }

                    popOSD(Hud::Dislike);
                }

                return true;
                //    Repeat current sheep
            case CLIENT_COMMAND_REPORT:
                if (data != nullptr) {
                    CheckNetworkForUserAction();  // Show indicator for 5s if network down
                    EDreamClient::Report(data->dreamData.uuid);
                    popOSD(Hud::Report);
                }
                return true;
            case CLIENT_COMMAND_PREVIOUS:
                popOSD(Hud::Previous);

                g_Player().ReturnToPrevious();
                EDreamClient::SendStateUpdate();
                return true;
                    
                // Speed
            case CLIENT_COMMAND_SPEED_1:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(1));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_2:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(2));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_3:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(3));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_4:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(4));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_5:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(5));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_6:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(6));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_7:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(7));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_8:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(8));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SPEED_9:
                popOSD(Hud::Speed);
                g_Player().SetPerceptualFPS(SpeedToPerceptualFPS(9));
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_RESET_PLAYLIST:
                printf("RESET PLAYLIST\n");
                g_Settings()->Set("settings.content.current_playlist_uuid", std::string(""));
                g_Player().ResetPlaylist();
                return true;
                //  Force Next Sheep
            case CLIENT_COMMAND_NEXT:
                popOSD(Hud::Next);

                /*if (g_Player().m_CurrentClips.size() > 1) {
                    auto [fadeInTime, _] =
                    g_Player().m_CurrentClips[0]->GetTransitionLength();
                    auto [__ , fadeOutTime] =
                    g_Player().m_CurrentClips[1]->GetTransitionLength();


                    g_Player().m_CurrentClips[0]->SetTransitionLength(fadeInTime, 1);
                    g_Player().m_CurrentClips[1]->SetTransitionLength(1,fadeOutTime);

                }*/

                g_Player().SkipToNext();
                EDreamClient::SendStateUpdate();
                return true;
                //    Repeat sheep
            case CLIENT_COMMAND_REPEAT:
                {
                    PlaybackMode currentMode = g_Player().GetPlaylistManager().getPlaybackMode();
                    if (currentMode == PlaybackMode::Repeat) {
                        g_Player().GetPlaylistManager().setPlaybackMode(PlaybackMode::Normal);
                        g_Log->Info("Playback mode: Normal");
                    } else {
                        popOSD(Hud::Repeat);
                        g_Player().GetPlaylistManager().setPlaybackMode(PlaybackMode::Repeat);
                        g_Log->Info("Playback mode: Repeat");
                    }
                }
                return true;
            case CLIENT_COMMAND_SHUFFLE:
                {
                    PlaybackMode currentMode = g_Player().GetPlaylistManager().getPlaybackMode();
                    if (currentMode == PlaybackMode::Shuffle) {
                        g_Player().GetPlaylistManager().setPlaybackMode(PlaybackMode::Normal);
                        g_Log->Info("Playback mode: Normal");
                    } else {
                        popOSD(Hud::Shuffle);
                        g_Player().GetPlaylistManager().setPlaybackMode(PlaybackMode::Shuffle);
                        g_Log->Info("Playback mode: Shuffle");
                    }
                }
                return true;
            case CLIENT_COMMAND_PLAYBACK_SLOWER:
                popOSD(Hud::Speed);
                g_Player().MultiplyPerceptualFPS(1.f / 1.1224f);
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_PLAYBACK_FASTER:
                popOSD(Hud::Speed);
                g_Player().MultiplyPerceptualFPS(1.1224f);
                EDreamClient::SendStateUpdate();
                return true;
                //    OSD info.
            case CLIENT_COMMAND_F1:
                m_F1F4Timer.Reset();
                m_HudManager->Toggle("helpmessage");
                g_Log->Info("HUD toggled (F1), syncing state to server");
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_F2:
                m_F1F4Timer.Reset();
                m_HudManager->Toggle("dreamstats");
                g_Log->Info("HUD toggled (F2), syncing state to server");
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SKIP_FW:
                if (!g_Player().IsJumpDisabled()) {
                    popOSD(Hud::Forward10);
                }
                g_Player().SkipForward(10);
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_SKIP_BW:
                if (!g_Player().IsJumpDisabled()) {
                    popOSD(Hud::Back10);
                }
                g_Player().SkipForward(-10);
                return true;
                EDreamClient::SendStateUpdate();
            case CLIENT_COMMAND_PAUSE:
                if (m_bPaused) {
                    popOSD(Hud::Play);
                } else {
                    popOSD(Hud::Pause);
                }
                g_Player().SetPaused(m_bPaused = !m_bPaused, true);
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_CREDIT:
                m_HudManager->Toggle("dreamcredits");
                EDreamClient::SendStateUpdate();
                return true;
            case CLIENT_COMMAND_WEBPAGE:
                if (data && !data->dreamData.frontendUrl.empty())
                {
                    PlatformUtils::OpenURLExternally(data->dreamData.frontendUrl);
                }
                return true;
            case CLIENT_COMMAND_BRIGHTNESS_UP:
                if (g_Player().Renderer()->GetBrightness() < 1) {
                    m_Brightness++;
                    g_Player().Renderer()->SetBrightness(TapsToBrightness(m_Brightness));
                }
                popOSD(Hud::Brightness);
                return true;
            case CLIENT_COMMAND_BRIGHTNESS_DOWN:
                if (g_Player().Renderer()->GetBrightness() > -1) {
                    m_Brightness--;
                    g_Player().Renderer()->SetBrightness(TapsToBrightness(m_Brightness));
                }
                popOSD(Hud::Brightness);
                return true;
        }
        return false;
    }

    virtual bool HandleOneEvent(DisplayOutput::spCEvent& _event)
    {
        if (_event->Type() == DisplayOutput::CEvent::Event_KEY)
        {
            DisplayOutput::spCKeyEvent spKey =
                std::dynamic_pointer_cast<DisplayOutput::CKeyEvent>(_event);
            switch (spKey->m_Code)
            {
                    // Vote for sheep.
                case DisplayOutput::CKeyEvent::KEY_UP:
                    return ExecuteCommand(CLIENT_COMMAND_LIKE);
                case DisplayOutput::CKeyEvent::KEY_DOWN:
                    return ExecuteCommand(CLIENT_COMMAND_DISLIKE);
                    // Report
                case DisplayOutput::CKeyEvent::KEY_B:
                    return ExecuteCommand(CLIENT_COMMAND_REPORT);

                    // Repeat current sheep
                case DisplayOutput::CKeyEvent::KEY_LEFT:
                    return ExecuteCommand(CLIENT_COMMAND_PREVIOUS);
                    // Force Next Sheep
                case DisplayOutput::CKeyEvent::KEY_RIGHT:
                    return ExecuteCommand(CLIENT_COMMAND_NEXT);
                    // Repeat sheep
                case DisplayOutput::CKeyEvent::KEY_R:
                    return ExecuteCommand(CLIENT_COMMAND_REPEAT);
                case DisplayOutput::CKeyEvent::KEY_H:
                    return ExecuteCommand(CLIENT_COMMAND_SHUFFLE);
                case DisplayOutput::CKeyEvent::KEY_A:
                    return ExecuteCommand(CLIENT_COMMAND_PLAYBACK_SLOWER);
                case DisplayOutput::CKeyEvent::KEY_D:
                    return ExecuteCommand(CLIENT_COMMAND_PLAYBACK_FASTER);
                    // Set Speed
                case DisplayOutput::CKeyEvent::KEY_0:
                    return ExecuteCommand(CLIENT_COMMAND_PAUSE);
                case DisplayOutput::CKeyEvent::KEY_1:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_1);
                case DisplayOutput::CKeyEvent::KEY_2:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_2);
                case DisplayOutput::CKeyEvent::KEY_3:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_3);
                case DisplayOutput::CKeyEvent::KEY_4:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_4);
                case DisplayOutput::CKeyEvent::KEY_5:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_5);
                case DisplayOutput::CKeyEvent::KEY_6:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_6);
                case DisplayOutput::CKeyEvent::KEY_7:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_7);
                case DisplayOutput::CKeyEvent::KEY_8:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_8);
                case DisplayOutput::CKeyEvent::KEY_9:
                    return ExecuteCommand(CLIENT_COMMAND_SPEED_9);
                    // Help and stats info.
                case DisplayOutput::CKeyEvent::KEY_F1:
                    return ExecuteCommand(CLIENT_COMMAND_F1);
                case DisplayOutput::CKeyEvent::KEY_F2:
                    return ExecuteCommand(CLIENT_COMMAND_F2);

                    // Reset playlist
                case DisplayOutput::CKeyEvent::KEY_N:
                    return ExecuteCommand(CLIENT_COMMAND_RESET_PLAYLIST);
                    // prev/next
                case DisplayOutput::CKeyEvent::KEY_J:
                    return ExecuteCommand(CLIENT_COMMAND_SKIP_BW);
                case DisplayOutput::CKeyEvent::KEY_L:
                    return ExecuteCommand(CLIENT_COMMAND_SKIP_FW);
                case DisplayOutput::CKeyEvent::KEY_K:
                    return ExecuteCommand(CLIENT_COMMAND_PAUSE);
                case DisplayOutput::CKeyEvent::KEY_C:
                    return ExecuteCommand(CLIENT_COMMAND_CREDIT);
                case DisplayOutput::CKeyEvent::KEY_V:
                    return ExecuteCommand(CLIENT_COMMAND_WEBPAGE);
                case DisplayOutput::CKeyEvent::KEY_W:
                    return ExecuteCommand(CLIENT_COMMAND_BRIGHTNESS_UP);
                case DisplayOutput::CKeyEvent::KEY_S:
                    return ExecuteCommand(CLIENT_COMMAND_BRIGHTNESS_DOWN);
                //    All other keys needs to be ignored, they are handled somewhere
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

        // Handle events.
        DisplayOutput::spCEvent spEvent;
        while (spDisplay->GetEvent(spEvent))
        {
            if (HandleOneEvent(spEvent) == false)
                return false;
        }

        // Process any commands enqueued from background threads (remote control).
        ProcessCommandQueue();

        return true;
    }

    std::string GetVersion() { return PlatformUtils::GetAppVersion(); }

    virtual int GetACLineStatus() { return -1; }

};

#endif // CLIENT_H_INCLUDED
