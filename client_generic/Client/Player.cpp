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


using boost::filesystem::directory_iterator;
using boost::filesystem::exists;
//using boost::filesystem::extension;
using boost::filesystem::path;

using namespace DisplayOutput;

using CClip = ContentDecoder::CClip;
using spCClip = ContentDecoder::spCClip;
typedef std::shared_lock<std::shared_mutex> reader_lock;
typedef std::unique_lock<std::shared_mutex> writer_lock;

// MARK: - Setup & lifecycle
CPlayer::CPlayer() : m_isFirstPlay(true)
{
    m_DecoderFps = 23; //	http://en.wikipedia.org/wiki/23_(numerology)
    m_PerceptualFPS = 20;
    m_DisplayFps = 60;
    m_bFullscreen = true;
    m_InitPlayCounts = true;
    m_MultiDisplayMode = kMDSharedMode;
    m_bStarted = false;
    m_CapClock = 0.0;
    m_TimelineTime = 0.0;
    m_transitionDuration = 5.0;
#ifdef WIN32
    m_hWnd = nullptr;
#endif
    
    m_playlistManager = std::make_unique<PlaylistManager>();
    
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
        // Load evicted UUIDs
        Cache::CacheManager::getInstance().loadEvictedUUIDsFromJson();

        m_CapClock = 0.0;

        m_bStarted.exchange(true);

        // Prepare to start the first video here
        
        // Grab perceptual FPS
        m_PerceptualFPS = g_Settings()->Get("settings.player.perceptual_fps",
                                            m_PerceptualFPS);

        std::string lastPlayedUUID = g_Settings()->Get(
            "settings.content.last_played_uuid", std::string{});
        
        auto clientPlaylistId = g_Settings()->Get("settings.content.current_playlist_uuid", std::string(""));

        if (EDreamClient::IsLoggedIn()) {
            auto serverPlaylistId = EDreamClient::GetCurrentServerPlaylist();
            
            
            // Override if there's a mismatch, and don't try to resume previous file as
            // it may not be part of the new playlist
            if (serverPlaylistId != clientPlaylistId) {
                g_Settings()->Set("settings.content.current_playlist_uuid", serverPlaylistId);
                lastPlayedUUID = "";
            }
            
            // Start the playlist and playback at the start, or at a given position
            if (lastPlayedUUID.empty()) {
                SetPlaylist(serverPlaylistId);
            } else {
                SetPlaylistAtDream(serverPlaylistId, lastPlayedUUID);
            }
        }
    }
}

void CPlayer::Stop()
{
    writer_lock l(m_UpdateMutex);
    if (m_bStarted && m_currentClip)
    {
        // Don't save the path if it's a link
        auto uuid = m_currentClip->GetClipMetadata().dreamData.uuid;
        g_Settings()->Set("settings.content.last_played_uuid", uuid);

        g_Settings()->Set("settings.content.last_played_frame",
                          (uint64_t)m_currentClip->GetCurrentFrameIdx());

        g_Settings()->Set("settings.content.last_played_fps", m_DecoderFps);

        g_Settings()->Set("settings.player.perceptual_fps", m_PerceptualFPS);
    }

    m_bStarted = false;
    
    // Save evicted UUIDs
    Cache::CacheManager::getInstance().saveEvictedUUIDsToJson();
}

/*
 */
bool CPlayer::Shutdown(void)
{
    g_Log->Info("CPlayer::Shutdown()\n");

    Stop();

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

// MARK: - Update and rendering
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

    if (m_currentClip) {
        m_currentClip->Update(m_TimelineTime);
    }

    if (m_nextClip) {
        m_nextClip->Update(m_TimelineTime);
    }
    
    if (m_currentClip)
    {
        // Check if we need to report the clip to server
        if (lastReportedUUID != m_currentClip->GetClipMetadata().dreamData.uuid) {
            lastReportedUUID = m_currentClip->GetClipMetadata().dreamData.uuid;
            // @TODO : Will later need more context eg screen, isScreenSaver, hardware id, etc
            EDreamClient::SendPlayingDream(lastReportedUUID);
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
    
    if (EDreamClient::IsLoggedIn()) {
        if (m_isTransitioning) {
            UpdateTransition(m_TimelineTime);
        } else if (m_currentClip && m_currentClip->IsFadingOut()) {
            PlayNextDream();
        } else if (!m_currentClip && m_isFirstPlay) {
            PlayNextDream();
        }
    }

    RenderFrame(du->spRenderer);

    return true;
}

void CPlayer::RenderFrame(DisplayOutput::spCRenderer renderer) {
    if (m_isTransitioning && m_currentClip && m_nextClip) {
        double transitionProgress = (m_TimelineTime - m_transitionStartTime) / m_transitionDuration;
        float currentAlpha = 1.0f - static_cast<float>(transitionProgress);
        float nextAlpha = static_cast<float>(transitionProgress);

        // Render current clip
        m_currentClip->Update(m_TimelineTime);
        m_currentClip->DrawFrame(renderer, currentAlpha);

        // Render next clip
        m_nextClip->Update(m_TimelineTime);
        m_nextClip->DrawFrame(renderer, nextAlpha);
    } else if (m_currentClip) {
        m_currentClip->Update(m_TimelineTime);
        m_currentClip->DrawFrame(renderer);
    }
}

bool CPlayer::PlayClip(const Cache::Dream& dream, double _startTime,
                       int64_t _seekFrame, bool isTransition)
{
    auto du = m_displayUnits[0];
    int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
    
    auto path = dream.getCachedPath();

    
    // TODO : Need to make this async, this is currently locking when streaming
    // If we don't have a file, try and grab the url to stream it
    // This may get denied based on quota
    if (path.empty()) {
        // Test url for streaming
        // path = "https://sylvan.apple.com/Videos/comp_DB_D001_C005_COMP_PSNK_v12_SDR_PS_20180912_SDR_2K_AVC.mov";

        path = EDreamClient::GetDreamDownloadLink(dream.uuid);
    }

    if (path.empty())
        return false;
    
    auto newClip = std::make_shared<ContentDecoder::CClip>(
        ContentDecoder::sClipMetadata{path, m_PerceptualFPS / dream.activityLevel, dream},
        du->spRenderer, displayMode, du->spDisplay->Width(),
        du->spDisplay->Height());
    
    // Update internal decoder fps counter
    m_DecoderFps = m_PerceptualFPS / dream.activityLevel;

    if (!newClip->Start(_seekFrame))
        return false;
    
    newClip->SetStartTime(_startTime);
    
    if (m_isFirstPlay || !isTransition) {
        m_currentClip = newClip;
        m_isTransitioning = false;
    } else if (isTransition) {
        m_nextClip = newClip;
    }

    m_isFirstPlay = false;

    // Verify what this does
    // m_PlayCond.notify_all();
    return true;
}

void CPlayer::PlayNextDream(bool quickFade)
{
    Cache::Dream nextDream = m_playlistManager->getNextDream();
    if (!nextDream.uuid.empty()) {
        if (m_isFirstPlay) {
            // For the first play, start immediately without transition
            PlayClip(nextDream, m_TimelineTime);
            m_isFirstPlay = false;
        } else {
            StartTransition();
            if (quickFade)
                m_currentClip->SetTransitionLength(5.0f, 1.0f);
            
            PlayClip(nextDream, m_TimelineTime, -1, true);  // The true flag indicates this is for transition
            
            if (quickFade)
                m_nextClip->SetTransitionLength(1.0f, 5.0f);
        }
    } else {
        g_Log->Error("No next dream available to play");
    }
}

void CPlayer::MultiplyPerceptualFPS(const double _multiplier) {
    m_PerceptualFPS *= _multiplier;
    
    reader_lock l(m_UpdateMutex);
    if (m_currentClip) {
        float dreamActivityLevel = m_currentClip->GetClipMetadata().dreamData.activityLevel;
        // Update decoder speed
        m_DecoderFps = m_PerceptualFPS / dreamActivityLevel;
        m_currentClip->SetFps(m_DecoderFps);
    }
}

void CPlayer::SetPerceptualFPS(const double _fps) {
    
    m_PerceptualFPS = _fps;
    
    reader_lock l(m_UpdateMutex);

    if (m_currentClip) {
        // Grab the activity level of the dream
        float dreamActivityLevel = m_currentClip->GetClipMetadata().dreamData.activityLevel;
        // Update decoder speed
        m_DecoderFps = m_PerceptualFPS / dreamActivityLevel;
        m_currentClip->SetFps(m_DecoderFps);
    }
}

// Getters for both perceptual FPS and true decoder FPS
double CPlayer::GetPerceptualFPS() {
    return m_PerceptualFPS;
}

double CPlayer::GetDecoderFPS() {
    return m_DecoderFps;
}

void CPlayer::PlayDreamNow(std::string_view _uuid, int64_t frameNumber) {
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    // NOTE : This is the only path that currently streams 
    if (cm.hasDream(std::string(_uuid))) {
        auto dream = cm.getDream(std::string(_uuid));

        if (dream->isCached()) {
            writer_lock l(m_UpdateMutex);

            m_transitionDuration = 1.0f;
            StartTransition();
            PlayClip(*dream, m_TimelineTime, frameNumber, true);
            m_nextClip->SetTransitionLength(1.0f, 5.0f);
        } else {
            // Uncomment below to disable streaming
            //cm.cacheAndPlayImmediately(std::string(_uuid));
            writer_lock l(m_UpdateMutex);

            m_transitionDuration = 1.0f;
            StartTransition();
            PlayClip(*dream, m_TimelineTime, frameNumber, true);
            m_nextClip->SetTransitionLength(1.0f, 5.0f);
        }
    } else {
        std::async(std::launch::async, [_uuid, &cm, this, frameNumber](){
            // We need the metadata before we do anything
            EDreamClient::FetchDreamMetadata(std::string(_uuid));
            cm.reloadMetadata(std::string(_uuid));

            auto dream = cm.getDream(std::string(_uuid));
            if (!dream) {
                g_Log->Error("Can't get dream metadata, aborting PlayDreamNow");
            }
            writer_lock l(m_UpdateMutex);

            m_transitionDuration = 1.0f;
            StartTransition();
            PlayClip(*dream, m_TimelineTime, frameNumber, true);
            m_nextClip->SetTransitionLength(1.0f, 5.0f);
        });
    }
}

std::string CPlayer::GetPlaylistName() const
{
    return m_playlistManager->getPlaylistName();
}

bool CPlayer::SetPlaylist(const std::string& playlistUUID) {
    if (!m_playlistManager->initializePlaylist(playlistUUID)) {
        g_Log->Error("Failed to set playlist with UUID: %s", playlistUUID.c_str());
        return false;
    }
    
    // Start playing the first dream if not already playing
    if (!m_currentClip) {
        m_isFirstPlay = true;  // Reset the first play flag when setting a new playlist
        PlayNextDream();
    }

    return true;
}


bool CPlayer::SetPlaylistAtDream(const std::string& playlistUUID, const std::string& dreamUUID) {
    // First, set the new playlist
    if (!m_playlistManager->initializePlaylist(playlistUUID)) {
        g_Log->Error("Failed to set playlist with UUID: %s", playlistUUID.c_str());
        return false;
    }

    // Now, try to position the playlist at the specified dream
    auto optionalDream = m_playlistManager->getDreamByUUID(dreamUUID);
    if (!optionalDream) {
        g_Log->Error("Dream with UUID %s not found in playlist %s", dreamUUID.c_str(), playlistUUID.c_str());
        return false;
    }

    m_isFirstPlay = true;  // Treat this as a first play to avoid transition

    // If we've reached here, the playlist is set and positioned at the correct dream
    // Now we can start playing this dream
    StartTransition();
    return PlayClip(*optionalDream, m_TimelineTime);
}


void CPlayer::ResetPlaylist() {
    writer_lock l(m_UpdateMutex);

    
    // Grab the default playlist again & set it
    EDreamClient::FetchDefaultPlaylist();
    m_currentClip = nullptr;    // Fetch is synchronous, avoids black screen
    SetPlaylist("");
}

// MARK: - Transition
void CPlayer::StartTransition()
{
    if (m_isFirstPlay) {
        m_isFirstPlay = false;
        return;  // Don't start a transition for the first play
    }

    m_isTransitioning = true;
    m_transitionStartTime = m_TimelineTime;
    m_nextClip = nullptr;  // We'll set this when we have the next clip ready
}

void CPlayer::UpdateTransition(double currentTime)
{
    if (!m_isTransitioning) return;

    double transitionProgress = (currentTime - m_transitionStartTime) / m_transitionDuration;

    if (transitionProgress >= 1.0) {
        // Transition complete
        m_isTransitioning = false;
        m_currentClip = m_nextClip;
        m_nextClip = nullptr;
        m_transitionDuration = 5.0f;
    } else if (!m_nextClip) {
        // If we don't have a next clip yet, try to get one
        Cache::Dream nextDream = m_playlistManager->getNextDream();
        if (!nextDream.uuid.empty()) {
            PlayClip(nextDream, currentTime, -1, true);  // The true flag indicates this is for transition
            m_nextClip->SetTransitionLength(m_transitionDuration, 5.0f);
        }
    }
}

// We maintain an array of dreams that have been downvoted this session
// This is used to make sure we don't add them to our history and we don't
// mistakenly try to play a deleted dream (and crash)
void CPlayer::MarkForDeletion(std::string_view _uuid)
{
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    if (!cm.isUUIDEvicted(std::string(_uuid))) {
        g_Log->Debug("Evicting UUID: %s", _uuid);
        cm.addEvictedUUID(std::string(_uuid));
    }
}

void CPlayer::SkipToNext()
{
    m_transitionDuration = 1.0f;
    PlayNextDream(true);
}

void CPlayer::ReturnToPrevious()
{
    m_transitionDuration = 1.0f;
    Cache::Dream previousDream = m_playlistManager->getPreviousDream();
    
    StartTransition();
    PlayClip(previousDream, m_TimelineTime, -1, true);
    m_nextClip->SetTransitionLength(1.0f, 5.0f);
}

void CPlayer::RepeatClip()
{
    m_transitionDuration = 1.0f;
    Cache::Dream currentDream = m_playlistManager->getCurrentDream();
    StartTransition();
    PlayClip(currentDream, m_TimelineTime, -1, true);
    m_nextClip->SetTransitionLength(1.0f, 5.0f);
}

void CPlayer::SkipForward(float _seconds)
{
    m_currentClip->SkipTime(_seconds);
}

const ContentDecoder::sClipMetadata*
CPlayer::GetCurrentPlayingClipMetadata() const
{
    reader_lock l(m_UpdateMutex);
    if (!m_currentClip)
        return nullptr;
    return &m_currentClip->GetClipMetadata();
}
const ContentDecoder::sFrameMetadata* CPlayer::GetCurrentFrameMetadata() const
{
    reader_lock l(m_UpdateMutex);
    if (!m_currentClip)
        return nullptr;
    return &m_currentClip->GetCurrentFrameMetadata();
}
