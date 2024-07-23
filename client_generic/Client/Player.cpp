#include <shared_mutex>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#ifdef WIN32
#include <windows.h>
#endif

#if defined(WIN32) && !defined(_MSC_VER)
#include <dirent.h>
#endif
#include <math.h>
#include <string>
#include <algorithm>

#include "Log.h"
#include "MathBase.h"
#include "Player.h"
#include "base.h"
#include "clientversion.h"

#ifdef WIN32
#include "DisplayDX.h"
#include "RendererDX.h"
#if defined(WIN32) && defined(_MSC_VER) && !defined(_WIN64)
#include "../msvc/DisplayDD.h"
#include "../msvc/RendererDD.h"
#endif
#else
#include "DisplayMetal.h"
#include "RendererMetal.h"
#endif

#include "CacheManager.h"
#include "ContentDownloader.h"
#include "DreamPlaylist.h"
#include "PlayCounter.h"
#include "Settings.h"
#include "storage.h"
#include "PlatformUtils.h"
#include "EDreamClient.h"

/*
#include "boost/filesystem/convenience.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp" */

#include <boost/filesystem.hpp>

#if defined(MAC) || defined(WIN32)
#define HONOR_VBL_SYNC
#endif

#ifdef DEBUG
#define PRINTQUEUE(x, y, z, w) PrintQueue(x, y, z, w)
#else
#define PRINTQUEUE(x, y, z, w)
#endif

class CElectricSheep* gClientInstance = nullptr;

static void
PrintQueue(std::string_view _str,
           const Base::CBlockingQueue<std::string>& _history,
           const Base::CBlockingQueue<std::string>& _next,
           const std::vector<ContentDecoder::spCClip>& _currentClips);

const double kTransitionLengthSeconds = 5;

using boost::filesystem::directory_iterator;
using boost::filesystem::exists;
//using boost::filesystem::extension;
using boost::filesystem::path;

using namespace DisplayOutput;

using CClip = ContentDecoder::CClip;
using spCClip = ContentDecoder::spCClip;
typedef std::shared_lock<std::shared_mutex> reader_lock;
typedef std::unique_lock<std::shared_mutex> writer_lock;

/*
 */
CPlayer::CPlayer()
{
    m_spPlaylist = nullptr;
    m_DecoderFps = 23; //	http://en.wikipedia.org/wiki/23_(numerology)
    m_PerceptualFPS = 20;
    m_DisplayFps = 60;
    m_bFullscreen = true;
    m_InitPlayCounts = true;
    m_MultiDisplayMode = kMDSharedMode;
    m_bStarted = false;
    m_CapClock = 0.0;
    m_TimelineTime = 0.0;
#ifdef WIN32
    m_hWnd = nullptr;
#endif
}

/*
        SetHWND().

*/
#ifdef WIN32
void CPlayer::SetHWND(HWND _hWnd)
{
    g_Log->Info("Got hwnd... (0x%x)", _hWnd);
    m_hWnd = _hWnd;
};
#endif

int CPlayer::AddDisplay([[maybe_unused]] uint32_t screen,
                        CGraphicsContext _graphicsContext,
                        [[maybe_unused]] bool _blank)
{
    DisplayOutput::spCDisplayOutput spDisplay;
    DisplayOutput::spCRenderer spRenderer;

    static bool detectgold = true;
    if (detectgold)
    {
        detectgold = false;
        std::string content = g_Settings()->Root() + "content/";
        std::string watchFolder =
            g_Settings()->Get("settings.content.sheepdir", content) + "/mp4/";
    }
    // modify aspect ratio and/or window size hint
    uint32_t w = 1280;
    uint32_t h = 720;

#ifdef WIN32
#ifndef _WIN64
    bool bDirectDraw = g_Settings()->Get("settings.player.directdraw", false);
    CDisplayDD* pDisplayDD = nullptr;
#endif
    CDisplayDX* pDisplayDX = nullptr;
#ifndef _WIN64
    if (bDirectDraw)
    {
        g_Log->Info("Attempting to open %s...", CDisplayDD::Description());
        pDisplayDD = new CDisplayDD();
    }
    else
#endif
    {
        g_Log->Info("Attempting to open %s...", CDisplayDX::Description());
        pDisplayDX = new CDisplayDX(_blank, _graphicsContext);
        pDisplayDX->SetScreen(screen);
    }

    if (pDisplayDX == nullptr
#ifndef _WIN64
        && pDisplayDD == nullptr
#endif
    )
    {
        g_Log->Error("Unable to open display");
        return -1;
    }
#ifndef _WIN64
    if (bDirectDraw)
        spDisplay = pDisplayDD;
    else
#endif
        spDisplay = pDisplayDX;
    if (m_hWnd)
    {
        if (!spDisplay->Initialize(m_hWnd, true))
            return -1;
    }
    else if (!spDisplay->Initialize(w, h, m_bFullscreen))
        return -1;
#ifndef _WIN64
    if (bDirectDraw)
        spRenderer = new CRendererDD();
    else
#endif
        spRenderer = new CRendererDX();
#else // !WIN32

    g_Log->Info("Attempting to open %s...", CDisplayMetal::Description());
    spDisplay = std::make_shared<CDisplayMetal>();

    if (spDisplay == nullptr)
        return -1;

#if defined(MAC)
    if (_graphicsContext != nullptr)
    {
        if (!spDisplay->Initialize(_graphicsContext, true))
            return -1;

        spDisplay->ForceWidthAndHeight(w, h);
    }
#else
    if (!spDisplay->Initialize(w, h, m_bFullscreen))
        return -1;
#endif //! MAC

    spRenderer = std::make_shared<CRendererMetal>();

#endif //! WIN32

    //	Start renderer & set window title.
    if (spRenderer->Initialize(spDisplay) == false)
        return -1;
    spDisplay->Title("e-dream");

    {
        auto du = std::make_shared<DisplayUnit>();

        du->spRenderer = spRenderer;
        du->spDisplay = spDisplay;

        std::scoped_lock lockthis(m_displayListMutex);

        if (g_Settings()->Get("settings.player.reversedisplays", false) == true)
            m_displayUnits.insert(m_displayUnits.begin(), du);
        else
            m_displayUnits.push_back(du);
    }

    return (int)m_displayUnits.size() - 1;
}

/*
 */
bool CPlayer::Startup()
{
    m_DisplayFps = g_Settings()->Get("settings.player.display_fps", 60.);

#ifdef HONOR_VBL_SYNC
    if (g_Settings()->Get("settings.player.vbl_sync", false))
    {
        m_DisplayFps = 0.0;
    }
#endif

    //	Grab some paths for the decoder.
    std::string content = g_Settings()->Root() + "content/";
#ifndef LINUX_GNU
    std::string scriptRoot =
        g_Settings()->Get("settings.app.InstallDir", std::string("./")) +
        "Scripts";
#else
    std::string scriptRoot =
        g_Settings()->Get("settings.app.InstallDir", std::string(SHAREDIR)) +
        "Scripts";
#endif
    std::string watchFolder =
        g_Settings()->Get("settings.content.sheepdir", content) + "/mp4/";

    if (TupleStorage::IStorageInterface::CreateFullDirectory(content.c_str()))
    {
        if (m_InitPlayCounts)
            g_PlayCounter().SetDirectory(content);
    }

    //  Tidy up the paths.
    path scriptPath = scriptRoot;
    path watchPath = watchFolder;

    //	Create playlist.
    g_Log->Info("Creating playlist...");

    m_spPlaylist =
        std::make_shared<ContentDecoder::CDreamPlaylist>(watchPath.string());

    m_NextClipInfoQueue.setMaxQueueElements(10);

    //	Create decoder last.
    g_Log->Info("Starting decoder...");

    m_bStarted = false;

    return true;
}

void CPlayer::ForceWidthAndHeight(uint32_t du, uint32_t _w, uint32_t _h)
{
    std::scoped_lock lockthis(m_displayListMutex);

    if (du >= m_displayUnits.size())
        return;

    const std::shared_ptr<DisplayUnit> duptr = m_displayUnits[du];

    if (duptr == nullptr)
        return;

#ifdef MAC
    if (duptr->spDisplay)
    {
        spCDisplayOutput disp = duptr->spDisplay;
        disp->ForceWidthAndHeight(_w, _h);
    }
#endif

    for (auto clip : duptr->spClips)
    {
        clip->SetDisplaySize((uint16_t)_w, (uint16_t)_h);
    }
}

/*

*/
void CPlayer::Start()
{
    if (!m_bStarted)
    {
        m_CapClock = 0.0;

        m_bStarted.exchange(true);

        m_pNextClipThread = new boost::thread(
            boost::bind(&CPlayer::CalculateNextClipThread, this));

        m_pPlayQueuedClipsThread = new boost::thread(
            boost::bind(&CPlayer::PlayQueuedClipsThread, this));

#ifdef WIN32
        SetThreadPriority((HANDLE)m_pNextSheepThread->native_handle(),
                          THREAD_PRIORITY_BELOW_NORMAL);
        SetThreadPriorityBoost((HANDLE)m_pNextSheepThread->native_handle(),
                               TRUE);
#else
        struct sched_param sp;
        sp.sched_priority =
            8; // Foreground NORMAL_PRIORITY_CLASS - THREAD_PRIORITY_BELOW_NORMAL
        pthread_setschedparam((pthread_t)m_pNextClipThread->native_handle(),
                              SCHED_RR, &sp);
#endif
    }
}

void CPlayer::Stop()
{
    writer_lock l(m_UpdateMutex);
    if (m_bStarted && m_CurrentClips.size())
    {
        g_Settings()->Set("settings.content.last_played_file",
                          m_CurrentClips[0]->GetClipMetadata().path);
        g_Settings()->Set("settings.content.last_played_frame",
                          (uint64_t)m_CurrentClips[0]->GetCurrentFrameIdx());

        g_Settings()->Set("settings.content.last_played_fps", m_DecoderFps);

        g_Settings()->Set("settings.player.perceptual_fps", m_PerceptualFPS);

    }

    m_bStarted = false;
}

/*
 */
bool CPlayer::Shutdown(void)
{
    g_Log->Info("CPlayer::Shutdown()\n");

    Stop();

    if (m_pNextClipThread)
    {
        m_pNextClipThread->interrupt();
        m_pNextClipThread->join();
        SAFE_DELETE(m_pNextClipThread);
    }
    if (m_pPlayQueuedClipsThread)
    {
        m_pPlayQueuedClipsThread->interrupt();
        m_pPlayQueuedClipsThread->join();
        SAFE_DELETE(m_pPlayQueuedClipsThread);
    }

    m_CurrentClips.clear();

    m_spPlaylist = nullptr;

    m_displayUnits.clear();

    m_bStarted = false;

    return true;
}

CPlayer::~CPlayer()
{
    //	Mark singleton as properly shutdown, to track unwanted access after this
    // point.
    SingletonActive(false);
}

bool CPlayer::BeginFrameUpdate()
{
    if (m_bPaused)
        return true;

    double newTime = m_Timer.Time();
    if (m_LastFrameRealTime == 0.0)
        m_LastFrameRealTime = newTime;
    double delta = newTime - m_LastFrameRealTime;
    //Avoid huge timesteps when debugging
    constexpr double SLOWEST_FRAMES_PER_SECOND = 10.0;
    delta = std::fmin(delta, 1.0 / SLOWEST_FRAMES_PER_SECOND);
    m_LastFrameRealTime = newTime;
    m_TimelineTime += delta;

    writer_lock l(m_UpdateMutex);

    for (spCClip clip : m_CurrentClips)
    {
        clip->Update(m_TimelineTime);
    }
    
    if (m_CurrentClips.size())
    {
        // Check if we need to report the clip to server
        if (lastReportedUUID != m_CurrentClips[0]->GetClipMetadata().dreamData.uuid) {
            lastReportedUUID = m_CurrentClips[0]->GetClipMetadata().dreamData.uuid;
            // @TODO : Will later need more context eg screen, isScreenSaver, hardware id, etc
            EDreamClient::SendPlayingDream(lastReportedUUID);
        }
        

        // This is imperfect but on startup this avoid too many repeats.
        // @TODO: We need to rethink having always 2 clips right now in m_CurrentClips to properly handle those cases
        if (m_CurrentClips.size() > 1) {
            if (m_CurrentClips[0]->HasFinished()) {
                if (m_CurrentClips[0]->GetClipMetadata().dreamData.uuid == m_CurrentClips[1]->GetClipMetadata().dreamData.uuid && m_spPlaylist->HasFreshlyDownloadedSheep()) {
                    m_CurrentClips[1]->FadeOut(m_TimelineTime);
                    m_NextClipInfoQueue.clear(0);
                }
            }
        }
        
        for (auto it = m_CurrentClips.begin(); it != m_CurrentClips.end(); ++it)
        {
            spCClip currentClip = *it;
            if (currentClip->HasFinished())
            {
                if ((currentClip->GetFlags() &
                     CClip::eClipFlags::ReverseHistory))
                {
                    m_NextClipInfoQueue.push(
                        std::string{currentClip->GetClipMetadata().path}, false,
                        false);
                }
                else
                {
                    // we need to make sure the clip isn't on its way to deletion
                    if ( std::find(m_evictedUUIDs.begin(), m_evictedUUIDs.end(), currentClip->GetClipMetadata().dreamData.uuid) != m_evictedUUIDs.end() ) {
                        g_Log->Debug("Clip was evicted, not adding it to history");
                    } else {
                        m_ClipInfoHistoryQueue.push(
                            std::string{currentClip->GetClipMetadata().path});
                    }
                }
             
                
                m_CurrentClips.erase(it--);
                auto next = it + (decltype(m_CurrentClips)::difference_type)1;
                next->get()->SetStartTime(
                    std::fmin(next->get()->GetStartTime(), m_TimelineTime));
                boost::thread([=]() { currentClip->Stop(); });

                if ((currentClip->GetFlags() &
                     CClip::eClipFlags::ReverseHistory) == 1)
                    PRINTQUEUE("FIN_REVERSED", m_ClipInfoHistoryQueue,
                               m_NextClipInfoQueue, m_CurrentClips);
                else
                    PRINTQUEUE("FIN_NORMAL", m_ClipInfoHistoryQueue,
                               m_NextClipInfoQueue, m_CurrentClips);
                
                
            }
        }
    }

    return true;
}

bool CPlayer::EndFrameUpdate()
{
#ifndef USE_METAL
    double capFPS = spFD->GetFps(m_PlayerFps, m_DisplayFps);
    if (spFD && capFPS > 0.000001)
        FpsCap(capFPS);
#endif

    return true;
}

bool CPlayer::BeginDisplayFrame(uint32_t displayUnit)
{
    std::shared_ptr<DisplayUnit> du;

    {
        std::scoped_lock lockthis(m_displayListMutex);

        if (displayUnit >= m_displayUnits.size())
            return false;

        du = m_displayUnits[displayUnit];
    }

    if (du->spRenderer->BeginFrame() == false)
        return false;

    return true;
}

bool CPlayer::EndDisplayFrame(uint32_t displayUnit, bool drawn)
{
    std::shared_ptr<DisplayUnit> du;

    {
        std::scoped_lock lockthis(m_displayListMutex);

        if (displayUnit >= m_displayUnits.size())
            return false;

        du = m_displayUnits[displayUnit];
    }

    return du->spRenderer->EndFrame(drawn);
}

//	Chill the remaining time to keep the framerate.
void CPlayer::FpsCap(const double _cap)
{
    double diff = 1.0 / _cap - (m_Timer.Time() - m_CapClock);
    if (diff > 0.0)
        Base::CTimer::Wait(diff);

    m_CapClock = m_Timer.Time();
}

bool CPlayer::Update(uint32_t displayUnit, bool& bPlayNoSheepIntro)
{

    std::shared_ptr<DisplayUnit> du;

    {
        std::scoped_lock lockthis(m_displayListMutex);

        if (displayUnit >= m_displayUnits.size())
            return false;

        du = m_displayUnits[displayUnit];
    }

    du->spRenderer->Reset(eEverything);
    du->spRenderer->Orthographic();
    du->spRenderer->Apply();

    writer_lock l(m_UpdateMutex);
    bPlayNoSheepIntro = !m_CurrentClips.size();
    //for (auto it : du->spClips)
    for (spCClip clip : m_CurrentClips)
    {
        clip->DrawFrame(du->spRenderer);
    }

    return true;
}

/// MARK: - Thread: PlayQueuedClips
void CPlayer::PlayQueuedClipsThread()
{
    PlatformUtils::SetThreadName("PlayQueuedClips");
    try
    {

        // Grab perceptual FPS
        m_PerceptualFPS = g_Settings()->Get("settings.player.perceptual_fps",
                                            m_PerceptualFPS);

        std::string lastPlayedFile = g_Settings()->Get(
            "settings.content.last_played_file", std::string{});
        
        auto clientPlaylistId = g_Settings()->Get("settings.content.current_playlist", 0);
        auto serverPlaylistId = EDreamClient::GetCurrentServerPlaylist();

        
        // Network error will give us a negative number. 0 = default playlist
        if (serverPlaylistId >= 0) {
            // Override if there's a mismatch, and don't try to resume previous file as
            // it may not be part of the new playlist
            if (serverPlaylistId != clientPlaylistId) {
                g_Settings()->Set("settings.content.current_playlist", serverPlaylistId);
                m_spPlaylist->SetPlaylist(serverPlaylistId);
                lastPlayedFile = "";
                ResetPlaylist();    // Don't forget to reset the playlist that was already generated
            }
        }
        
        if (lastPlayedFile != "")
        {
            int64_t seekFrame;
            seekFrame = (int64_t)g_Settings()->Get(
                "settings.content.last_played_frame", uint64_t{});
            PlayClip(lastPlayedFile, m_TimelineTime, seekFrame);
        }

        while (m_bStarted)
        {
            boost::this_thread::interruption_point();
            {
                writer_lock l(m_UpdateMutex);
                bool loadNextClip = false;
                double startTime;
                if (m_CurrentClips.size() == 0)
                {
                    loadNextClip = true;
                    startTime = m_TimelineTime;
                }
                else
                {
                    if (m_CurrentClips.size() == 1)
                    {
                        loadNextClip = true;
                        auto [_, out] =
                            m_CurrentClips[0]->GetTransitionLength();
                        startTime = m_CurrentClips[0]->GetEndTime() - out;
                    }
                }
                if (loadNextClip)
                {
                    std::string nextClip;

                    if (m_NextClipInfoQueue.pop(nextClip, false))
                    {
                        PlayClip(nextClip, startTime);
                        PRINTQUEUE("PLAYCLIPTHREAD", m_ClipInfoHistoryQueue,
                                   m_NextClipInfoQueue, m_CurrentClips);
                    }
                }
            }

            boost::thread::sleep(boost::get_system_time() +
                                 boost::posix_time::milliseconds(100));
        }
    }
    catch (boost::thread_interrupted const&)
    {
    }
}

bool CPlayer::PlayClip(std::string_view _clipPath, double _startTime,
                       int64_t _seekFrame, bool fastFade)
{
    auto du = m_displayUnits[0];
    int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
    //ContentDownloader::sDreamMetadata dream{};
    Cache::Dream dream{};
    m_spPlaylist->GetDreamMetadata(_clipPath, &dream);

    //    if (!dream)
    //        return false;

    spCClip clip = std::make_shared<CClip>(
        sClipMetadata{std::string{_clipPath},
                      m_PerceptualFPS / dream.activityLevel, dream},
        du->spRenderer, displayMode, du->spDisplay->Width(),
        du->spDisplay->Height());

    // Update internal decoder fps counter
    m_DecoderFps = m_PerceptualFPS / dream.activityLevel;

    if (!clip->Start(_seekFrame))
        return false;
    
    clip->SetStartTime(_startTime);
    if (fastFade) {
        clip->SetTransitionLength(0, 1);
    } else {
        clip->SetTransitionLength(m_CurrentClips.size() ? (kTransitionLengthSeconds / dream.activityLevel)
                                                        : 0,
                                  (kTransitionLengthSeconds / dream.activityLevel));
    }

    m_CurrentClips.push_back(clip);
    m_PlayCond.notify_all();
    return true;
}

void CPlayer::MultiplyPerceptualFPS(const double _multiplier) {
    m_PerceptualFPS *= _multiplier;
    
    reader_lock l(m_UpdateMutex);
    if (m_CurrentClips.size()) {
        // Grab the activity level of the dream
        float dreamActivityLevel = m_CurrentClips[0]->GetClipMetadata().dreamData.activityLevel;
        // Update decoder speed
        m_DecoderFps = m_PerceptualFPS / dreamActivityLevel;
        // This seems to be what actually changes the speed
        for (auto clip : m_CurrentClips) {
            clip->SetFps(m_DecoderFps);
        }
    }
}

void CPlayer::SetPerceptualFPS(const double _fps) {
    m_PerceptualFPS = _fps;
    
    reader_lock l(m_UpdateMutex);
    if (m_CurrentClips.size()) {
        // Grab the activity level of the dream
        float dreamActivityLevel = m_CurrentClips[0]->GetClipMetadata().dreamData.activityLevel;
        // Update decoder speed
        m_DecoderFps = m_PerceptualFPS / dreamActivityLevel;
        // This seems to be what actually changes the speed, we need to do it
        // for every clip here as the next clip is already hot-loaded
        for (auto clip : m_CurrentClips) {
            clip->SetFps(m_DecoderFps);
        }
    }
}

// Getters for both perceptual FPS and true decoder FPS
double CPlayer::GetPerceptualFPS() {
    return m_PerceptualFPS;
}

double CPlayer::GetDecoderFPS() {
    return m_DecoderFps;
}

/// MARK: - Thread: CalculateNextClip
void CPlayer::CalculateNextClipThread()
{
    PlatformUtils::SetThreadName("CalculateNextClip");
    try
    {
        uint32_t curID = 0;
        bool bRebuild = true;
        bool enoughClips = true;
        bool playFreshClips = false;

        while (m_bStarted)
        {
            boost::this_thread::interruption_point();

            if (m_spPlaylist == nullptr)
            {
                boost::thread::sleep(boost::get_system_time() +
                                     boost::posix_time::milliseconds(100));
                continue;
            }

            std::string spath;

            if (enoughClips)
            {
                while (!m_NextClipInfoQueue.empty())
                {
                    if (!m_NextClipInfoQueue.waitForEmpty())
                    {
                        break;
                    }
                }
            }
                
            if (m_spPlaylist->Next(spath, enoughClips, curID,
                                   playFreshClips, bRebuild, true))
            {
                bRebuild = false;
                
                if (playFreshClips) {
                    m_NextClipInfoQueue.clear(0);
                    if (m_CurrentClips.size() > 1)
                    {
                        m_CurrentClips[1]->FadeOut(m_TimelineTime);
                    }
                    m_NextClipInfoQueue.clear(0);
                } else {
                    m_NextClipInfoQueue.push(spath);
                }
            }
            else
            {
                bRebuild = true;
            }

            boost::thread::sleep(boost::get_system_time() +
                                 boost::posix_time::milliseconds(100));
            // this_thread::yield();
        }
    }
    catch (boost::thread_interrupted const&)
    {
    }
}

void CPlayer::PlayDreamNow(std::string_view _uuid) {
    writer_lock l(m_UpdateMutex);
    
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    auto path = cm.getDreamPath(std::string(_uuid));
    
    if (path != "") {
        PlayClip(path, m_TimelineTime);
        m_CurrentClips[0]->FadeOut(m_TimelineTime);
        if (m_CurrentClips.size() > 1)
            m_CurrentClips[1]->FadeOut(m_TimelineTime);
    } else {
        printf("Dream isn't cached");
    }
}

void CPlayer::ResetPlaylist() {
    writer_lock l(m_UpdateMutex);

    m_spPlaylist->Clear();
    m_NextClipInfoQueue.clear(0);
    m_CurrentClips.clear();
    m_NextClipInfoQueue.clear(0);
}

// We maintain an array of dreams that have been downvoted this session
// This is used to make sure we don't add them to our history and we don't
// mistakenly try to play a deleted dream (and crash)
void CPlayer::MarkForDeletion(std::string_view _uuid)
{
    // Make sure it's not already in the vector, then add it
    if ( std::find(m_evictedUUIDs.begin(), m_evictedUUIDs.end(), _uuid) == m_evictedUUIDs.end() ) {
        g_Log->Debug("Evicting UUID: %s", _uuid);
        m_evictedUUIDs.push_back(std::string(_uuid));
    }
}

void CPlayer::SkipToNext()
{
    writer_lock l(m_UpdateMutex);
    if (m_CurrentClips.size() < 2)
        return;
    // If the current clip is fading out already, remove it immediately
    spCClip currentClip = m_CurrentClips[0];
    auto [_, out] = currentClip->GetTransitionLength();
    if (m_TimelineTime > currentClip->GetEndTime() - out)
    {
        m_ClipInfoHistoryQueue.push(
            std::string{currentClip->GetClipMetadata().path});
        m_CurrentClips[0] = m_CurrentClips[1];
        m_CurrentClips.erase(m_CurrentClips.begin() + 1);
        std::string nextClip;
        if (m_NextClipInfoQueue.pop(nextClip, false))
        {
            PlayClip(nextClip, m_TimelineTime);
        }
        else
        {
            m_PlayCond.notify_all();
        }
        PRINTQUEUE("DOUBLESKIP", m_ClipInfoHistoryQueue, m_NextClipInfoQueue,
                   m_CurrentClips);
    }
    else
    {
        m_CurrentClips[1]->SetStartTime(m_TimelineTime);
    }
    m_CurrentClips[0]->FadeOut(m_TimelineTime);
}

void CPlayer::ReturnToPrevious()
{
    writer_lock l(m_UpdateMutex);
    std::string clipName;
    if (m_CurrentClips.size() < 2)
        return;
    // If the current clip is fading out already, empty the clips and queue both again
    spCClip currentClip = m_CurrentClips[0];
    auto [_, out] = currentClip->GetTransitionLength();
    if (m_TimelineTime > currentClip->GetEndTime() - out)
    {
        clipName = m_CurrentClips[0]->GetClipMetadata().path;
        std::string clipName2 = m_CurrentClips[1]->GetClipMetadata().path;
        m_CurrentClips.clear();
        PlayClip(clipName, m_TimelineTime);
        PlayClip(clipName2, m_TimelineTime);
        PRINTQUEUE("PREV_FADING", m_ClipInfoHistoryQueue, m_NextClipInfoQueue,
                   m_CurrentClips);
    }
    else if (m_ClipInfoHistoryQueue.pop(clipName, false, false))
    {
        m_NextClipInfoQueue.push(
            std::string{m_CurrentClips[1]->GetClipMetadata().path}, false,
            false);
        m_CurrentClips[0]->SetFlags(CClip::eClipFlags::ReverseHistory);
        m_CurrentClips[0]->FadeOut(m_TimelineTime);
        m_CurrentClips.erase(m_CurrentClips.begin() + 1);

        // force fade to 1s on current clip
        auto [fadeInTime, _] = m_CurrentClips[0]->GetTransitionLength();
        m_CurrentClips[0]->SetTransitionLength(fadeInTime, 1);
        
        // Ask PlayClip to fast fade too
        PlayClip(clipName, m_TimelineTime, -1, true);
        std::swap(m_CurrentClips[0], m_CurrentClips[1]);
        PRINTQUEUE("PREV_NORMAL", m_ClipInfoHistoryQueue, m_NextClipInfoQueue,
                   m_CurrentClips);
    }
}

void CPlayer::RepeatClip()
{
    writer_lock l(m_UpdateMutex);
    std::string clipName;
    // If the current clip is fading out already, remove it immediately
    spCClip currentClip = m_CurrentClips[0];
    auto [_, out] = currentClip->GetTransitionLength();
    if (m_TimelineTime > currentClip->GetEndTime() - out)
    {
        clipName = m_CurrentClips[0]->GetClipMetadata().path;
        std::string clipName2 = m_CurrentClips[1]->GetClipMetadata().path;
        m_CurrentClips.clear();
        PlayClip(clipName, m_TimelineTime);
        PlayClip(clipName2, m_TimelineTime);
        PRINTQUEUE("REPEAT_FADING", m_ClipInfoHistoryQueue, m_NextClipInfoQueue,
                   m_CurrentClips);
    }
    else
    {
        double clipLength = currentClip->GetLength(20);
        currentClip->SkipTime(-(float)clipLength);
        if (m_CurrentClips.size() > 1)
        {
            m_CurrentClips[1]->SetStartTime(m_TimelineTime + clipLength);
        }
        PRINTQUEUE("REPEAT_NORMAL", m_ClipInfoHistoryQueue, m_NextClipInfoQueue,
                   m_CurrentClips);
    }
}

void CPlayer::SkipForward(float _seconds)
{
    std::shared_lock<std::shared_mutex> l(m_UpdateMutex);
    while (m_CurrentClips.size() < 2 && !m_NextClipInfoQueue.empty())
    {
        l.unlock();
        std::unique_lock<std::shared_mutex> wlock(m_UpdateMutex);
        m_PlayCond.wait(m_UpdateMutex);
    }
    m_CurrentClips[0]->SkipTime(_seconds);
    if (m_CurrentClips.size() > 1)
    {
        m_CurrentClips[1]->SetStartTime(m_CurrentClips[1]->GetStartTime() -
                                        _seconds);
    }
}

const ContentDecoder::sClipMetadata*
CPlayer::GetCurrentPlayingClipMetadata() const
{
    reader_lock l(m_UpdateMutex);
    if (!m_CurrentClips.size())
        return nullptr;
    return &m_CurrentClips[0]->GetClipMetadata();
}
const ContentDecoder::sFrameMetadata* CPlayer::GetCurrentFrameMetadata() const
{
    reader_lock l(m_UpdateMutex);
    if (!m_CurrentClips.size())
        return nullptr;
    return &m_CurrentClips[0]->GetCurrentFrameMetadata();
}

static void
PrintQueue(std::string_view _str,
           const Base::CBlockingQueue<std::string>& _history,
           const Base::CBlockingQueue<std::string>& _next,
           const std::vector<ContentDecoder::spCClip>& _currentClips)
{
    g_Log->Info("\n\n\n%s history:\n", _str.data());
    for (auto t : _history)
    {
        g_Log->Info("%s\n", t.data());
    }

    g_Log->Info("%s current:\n", _str.data());
    for (auto t : _currentClips)
    {
        g_Log->Info("%s\n", t->GetClipMetadata().path.data());
    }

    g_Log->Info("%s next:\n", _str.data());
    for (auto t : _next)
    {
        g_Log->Info("%s\n", t.data());
    }
    g_Log->Info("end\n\n\n\n");
}
