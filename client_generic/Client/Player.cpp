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
#include <thread>

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

// Async destruction of a clip. The destructor may be delayed by a
// catch up streaming mechanism for caching
void destroyClipAsync(ContentDecoder::spCClip clip) {
    std::thread([clip = std::move(clip)]() mutable {
        // This should call the destructor now
        clip = nullptr;
    }).detach();
}

// MARK: - Setup & lifecycle
CPlayer::CPlayer() : m_isFirstPlay(true), m_offlineMode(false)
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

void CPlayer::SetOfflineMode(bool offline)
{
    m_offlineMode = offline;
    if (m_playlistManager)
    {
        m_playlistManager->setOfflineMode(offline);
    }
    g_Log->Info("Player offline mode set to: %s", offline ? "true" : "false");
}

bool CPlayer::IsOfflineMode() const
{
    return m_offlineMode;
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
        g_Settings()->Get("settings.app.InstallDir", PlatformUtils::GetWorkingDir()) +
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

        // Make sure this is reset before we start the thread
        // This matters on changing accounts and disconnected starts
        m_shutdownFlag = false;
        
        // We do this async, so client rendering loop doesn't lock
        m_startupThread = std::make_shared<std::thread>([this]{
            if (m_shutdownFlag) return;

            std::string lastPlayedUUID = g_Settings()->Get(
                "settings.content.last_played_uuid", std::string{});
            
            auto clientPlaylistId = g_Settings()->Get("settings.content.current_playlist_uuid", std::string(""));

            if (EDreamClient::IsLoggedIn()) {
                if (m_shutdownFlag) return;
                auto serverPlaylistId = EDreamClient::GetCurrentServerPlaylist();
                
                // Override if there's a mismatch, and don't try to resume previous file as
                // it may not be part of the new playlist
                if (m_shutdownFlag) return;
                if (serverPlaylistId != clientPlaylistId) {
                    g_Settings()->Set("settings.content.current_playlist_uuid", serverPlaylistId);
                    lastPlayedUUID = "";
                }

                // Make sure we remove the current clip before enqueuing the new playlist
                m_currentClip = nullptr;

                // Start the playlist and playback at the start, or at a given position
                if (m_shutdownFlag) return;
                if (lastPlayedUUID.empty()) {
                    SetPlaylist(serverPlaylistId);
                } else {
                    SetPlaylistAtDream(serverPlaylistId, lastPlayedUUID);
                }
                
                m_hasStarted = true;
            } else {
                if (m_shutdownFlag) return;

                // Make sure we remove the current clip before enqueuing the new playlist

                m_currentClip = nullptr;

                if (lastPlayedUUID.empty()) {
                    SetPlaylist(clientPlaylistId);
                } else {
                    SetPlaylistAtDream(clientPlaylistId, lastPlayedUUID);
                }

                m_hasStarted = true;
            }
        });
    }
}

void CPlayer::Stop()
{
    m_shutdownFlag = true;
    
    if (m_startupThread && m_startupThread->joinable()) {
        m_startupThread->join();
    }
    
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
    
    // Signal any current clips to abort immediately
    if (m_currentClip) {
        writer_lock l(m_UpdateMutex);
        if (m_currentClip->GetClipMetadata().path.substr(0, 4) == "http") {
            // Get the decoder and signal shutdown
            m_currentClip->GetDecoder()->signalShutdown();
        }
    }

    if (m_nextClip) {
        writer_lock l(m_UpdateMutex);
        if (m_nextClip->GetClipMetadata().path.substr(0, 4) == "http") {
            m_nextClip->GetDecoder()->signalShutdown();
        }
    }

    m_displayUnits.clear();

    m_bStarted = false;
    m_shutdownFlag = true;  

    return true;
}

CPlayer::~CPlayer()
{
    m_playlistManager = nullptr;
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

/*    if (m_currentClip) {
        m_currentClip->Update(m_TimelineTime);
    }

    if (m_nextClip) {
        if (!m_nextClip->HasFinished()) {
            m_nextClip->Update(m_TimelineTime);
        }
    }*/
    
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

// MARK: Update function
//bool CPlayer::Update(uint32_t displayUnit, bool& bPlayNoSheepIntro)
bool CPlayer::Update(uint32_t displayUnit)
{
    // todo: need to check this
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

    if (m_currentClip) {
        m_currentClip->Update(m_TimelineTime);

        // Check if we need to prepare for transition
        if (!m_isTransitioning && !m_nextDreamDecision && shouldPrepareTransition(m_currentClip)) {
            // Get the next dream decision
            m_nextDreamDecision = m_playlistManager->preflightNextDream();
            
            if (m_nextDreamDecision) {
                if (m_nextDreamDecision->transition == PlaylistManager::TransitionType::Seamless) {
                    prepareSeamlessTransition();
                }
            }
        }
        
        if (m_isTransitioning) {
            UpdateTransition(m_TimelineTime);
        }
        
        if (m_nextDreamDecision) {
            if (m_nextDreamDecision->transition == PlaylistManager::TransitionType::Seamless) {
                if (m_currentClip && m_currentClip->HasFinished()) {
                    g_Log->Info("PND : Launching on finished current");
                    PlayNextDream();
                }
            } else if (m_nextDreamDecision->transition == PlaylistManager::TransitionType::StandardCrossfade) {
                //
                if (m_currentClip && m_currentClip->IsFadingOut()) {
                    g_Log->Info("PND : Standard crossfading");
                    PlayNextDream();
                }
            }
        }
    }
    
    if (m_nextClip) {
        if (!m_nextClip->HasFinished()) {
            m_nextClip->Update(m_TimelineTime);
        }
    }
    
    // Make sure we are :
    // - either logged in, or in offline mode
    // - have a playlist ready before going in
    if ((EDreamClient::IsLoggedIn() /*|| g_Client->IsMultipleInstancesMode()*/ ) && m_hasStarted) {
        if (!m_currentClip && m_isFirstPlay) {
            g_Log->Info("PND : First play");
            PlayNextDream();
        }
    }
    
    if (m_currentClip && m_currentClip->m_Alpha == 0.0f && m_currentClip->m_FadeInSeconds == 0.f) {
        g_Log->Info("Fixing null alpha");
        m_currentClip->m_Alpha = 1.0f;
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
        // TODO: tmplog
        /*g_Log->Info("render current frame %d of %s", m_currentClip->m_CurrentFrameMetadata.frameIdx, m_currentClip->m_ClipMetadata.dreamData.uuid.c_str());
         */
        m_currentClip->DrawFrame(renderer, currentAlpha);

        // Render next clip
        // Somehow sometimes we reach here with no m_nextClip, not 100% clear why
        if (m_nextClip) {
            // TODO: tmplog
            /*g_Log->Info("render next frame %d of %s", m_nextClip->m_CurrentFrameMetadata.frameIdx, m_nextClip->m_ClipMetadata.dreamData.uuid.c_str());*/
           m_nextClip->DrawFrame(renderer, nextAlpha);
        } else {
            g_Log->Error("Render frame has null nextClip despite checking for it earlier");
        }
    } else if (m_currentClip) {
        if (m_currentClip->IsBuffering()) {
            // We're still buffering, show appropriate UI
            /*g_Log->Info("Buffering clip %s, frame queue: %d",
                m_currentClip->GetClipMetadata().dreamData.uuid.c_str(),
                m_currentClip->GetDecoder()->QueueLength());*/
            
            // Still call DrawFrame which will handle buffering visualization
            m_currentClip->DrawFrame(renderer);

            // TODO : we need to impl that 
            // renderer->DrawBufferingIndicator();
        } else {
            // Normal playback
            /*g_Log->Info("render frame %d of %s",
                m_currentClip->m_CurrentFrameMetadata.frameIdx,
                m_currentClip->m_ClipMetadata.dreamData.uuid.c_str());
            */
            m_currentClip->DrawFrame(renderer);

            if (m_currentClip->m_CurrentFrameMetadata.frameIdx == 1) {
                // TMP breakpoint
            }
        }
    }
}

bool CPlayer::PlayClip(const Cache::Dream* dream, double _startTime, int64_t _seekFrame, bool isTransition)
{
    if (!dream) {
        g_Log->Error("Attempted to play a null dream");
        return false;
    }
    
    auto du = m_displayUnits[0];
    int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
    
    auto path = dream->getCachedPath();

    if (path.empty()) {
        // Try streamingUrl that's been prefetched
        path = dream->getStreamingUrl();

        if (path.empty()) {
            // Last resort blocking call this should never get called ideally
            // but keeping it for resiliancy
            path = EDreamClient::GetDreamDownloadLink(dream->uuid);
        }
    }

    if (path.empty()) {
        g_Log->Error("Failed to get path for dream: %s", dream->uuid.c_str());
        return false;
    }
    
    auto newClip = std::make_shared<ContentDecoder::CClip>(
        ContentDecoder::sClipMetadata{path, m_PerceptualFPS / dream->activityLevel, *dream},
        du->spRenderer, displayMode, du->spDisplay->Width(),
        du->spDisplay->Height());
    
    // Update internal decoder fps counter
    m_DecoderFps = m_PerceptualFPS / dream->activityLevel;

    if (!newClip->Start(_seekFrame))
        return false;
    
    newClip->SetStartTime(_startTime);
    
    if (m_isFirstPlay || !isTransition) {
        // Start the asynchronous destruction of the current clip
        if (m_currentClip) {
            destroyClipAsync(std::move(m_currentClip));
        }
        
        m_currentClip = newClip;
        m_isTransitioning = false;
    } else if (isTransition) {
        if (m_nextClip) {
            destroyClipAsync(std::move(m_nextClip));
        }
        m_nextClip = newClip;
    }

    m_isFirstPlay = false;

    // Verify what this does
    // m_PlayCond.notify_all();
    return true;
}


void CPlayer::PlayNextDream(bool quickFade) {
    if (!m_playlistManager) return;
    
    // For user-forced transitions, we don't use the preflight decision
    if (quickFade) {
        // Force a quick transition
        StartTransition();
        if (m_currentClip) {
            m_currentClip->SetTransitionLength(5.0f, 1.0f);
        }
        
        auto nextDecision = m_playlistManager->preflightNextDream();
        if (nextDecision) {
            PlayClip(nextDecision->dream, m_TimelineTime, -1, true);
            m_playlistManager->moveToNextDream(*nextDecision);
            
            if (m_nextClip) {
                m_nextClip->SetTransitionLength(1.0f, 5.0f);
            }
        }
        return;
    }
    
    // Use existing preflight decision if we have one
    if (m_nextDreamDecision) {
        if (m_nextDreamDecision->transition == PlaylistManager::TransitionType::Seamless) {
            // For seamless, next clip should already be prepared
            if (m_nextClip) {
                g_Log->Info("Executing seamless transition now");
                destroyClipAsync(std::move(m_currentClip));

                m_currentClip = m_nextClip;
                m_nextClip = nullptr;

                m_currentClip->SetTransitionLength(0.0, 0.0);

                m_playlistManager->moveToNextDream(*m_nextDreamDecision);
                m_nextDreamDecision = std::nullopt;
            }
        } else {
            // Standard transition
            g_Log->Info("Regular transitionning");

            StartTransition();
            PlayClip(m_nextDreamDecision->dream, m_TimelineTime, -1, true);
            m_playlistManager->moveToNextDream(*m_nextDreamDecision);
        }
        m_nextDreamDecision = std::nullopt;
    } else {
        // No preflight decision, get one now (fallback case)
        auto nextDecision = m_playlistManager->preflightNextDream();
        if (nextDecision) {
            StartTransition();
            PlayClip(nextDecision->dream, m_TimelineTime, -1, true);
            m_playlistManager->moveToNextDream(*nextDecision);
        }
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

            // Check if the dream is in the current playlist and update position
            m_playlistManager->getDreamByUUID(std::string(_uuid));
            
            m_transitionDuration = 1.0f;
            StartTransition();
            PlayClip(dream, m_TimelineTime, frameNumber, true);
            m_nextClip->SetTransitionLength(1.0f, 5.0f);
        } else {
            std::thread([this, frameNumber, dream = dream]() {
                // Fetch URL first
                auto path = EDreamClient::GetDreamDownloadLink(dream->uuid);
                dream->setStreamingUrl(path);
                
                // Check if the dream is in the current playlist and update position
                m_playlistManager->getDreamByUUID(dream->uuid);

                // Prepare the clip outside the lock
                auto du = m_displayUnits[0];
                int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
                
                auto newClip = std::make_shared<ContentDecoder::CClip>(
                    ContentDecoder::sClipMetadata{path, m_PerceptualFPS / dream->activityLevel, *dream},
                    du->spRenderer, displayMode, du->spDisplay->Width(),
                    du->spDisplay->Height());
                
                // Start the clip before taking the lock, this replaces PlayClip
                if (newClip->Start(frameNumber)) {
                    // Only take the lock once everything is ready
                    writer_lock l(m_UpdateMutex);
                    m_transitionDuration = 1.0f;
                    StartTransition();
                    
                    // Set the start time and store the clip
                    newClip->SetStartTime(m_TimelineTime);
                    m_nextClip = newClip;
                    m_nextClip->SetTransitionLength(1.0f, 5.0f);
                }
            }).detach();
        }
    } else {
        std::thread([uuid = std::string(_uuid), &cm, this, frameNumber]() {
            EDreamClient::FetchDreamMetadata(uuid);
            cm.reloadMetadata(uuid);
            
            auto dream = cm.getDream(uuid);
            if (!dream) {
                g_Log->Error("Can't get dream metadata, aborting PlayDreamNow");
                return;
            }
            
            auto path = EDreamClient::GetDreamDownloadLink(dream->uuid);
            dream->setStreamingUrl(path);
            
            // Prepare clip outside lock, this replaces PlayClip
            auto du = m_displayUnits[0];
            int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
            
            auto newClip = std::make_shared<ContentDecoder::CClip>(
                                                                   ContentDecoder::sClipMetadata{path, m_PerceptualFPS / dream->activityLevel, *dream},
                                                                   du->spRenderer, displayMode, du->spDisplay->Width(),
                                                                   du->spDisplay->Height());
            
            if (newClip->Start(frameNumber)) {
                writer_lock l(m_UpdateMutex);
                m_transitionDuration = 1.0f;
                StartTransition();
                
                newClip->SetStartTime(m_TimelineTime);
                m_nextClip = newClip;
                m_nextClip->SetTransitionLength(1.0f, 5.0f);
            }
        }).detach();
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
    
    // Ensure m_hasStarted is set
    m_hasStarted = true;
    
    // Start playing the first dream if not already playing
    if (!m_currentClip) {
        m_isFirstPlay = true;  // Reset the first play flag when setting a new playlist
        
        auto nextDecision = m_playlistManager->preflightNextDream();
        if (nextDecision) {
            // Load the clip directly without transition
            if (PlayClip(nextDecision->dream, m_TimelineTime, -1, false)) {
                m_playlistManager->moveToNextDream(*nextDecision);
                
                // Explicitly set the fade-in and fade-out lengths
                if (m_currentClip) {
                    m_currentClip->SetTransitionLength(5.0, 5.0);
                }
                return true;
            }
        }
        
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
        g_Log->Error("Dream with UUID %s not found in playlist %s defaulting to the first dream", dreamUUID.c_str(), playlistUUID.c_str());
        
        if (!m_currentClip) {
            m_isFirstPlay = true;  // Reset the first play flag when setting a new playlist
            
            // Get the first dream directly instead of using PlayNextDream()
            auto nextDecision = m_playlistManager->preflightNextDream();
            if (nextDecision) {
                // Load the clip directly without transition
                if (PlayClip(nextDecision->dream, m_TimelineTime, -1, false)) {
                    m_playlistManager->moveToNextDream(*nextDecision);
                    
                    // Explicitly set the fade-in and fade-out lengths
                    if (m_currentClip) {
                        m_currentClip->SetTransitionLength(5.0, 5.0);
                    }
                    return true;
                }
            }

            // Fallback (maybe not needed)
            PlayNextDream();
        }
        return true;
    }

    m_isFirstPlay = true;  // Treat this as a first play to avoid transition

    int64_t seekFrame;
    seekFrame = (int64_t)g_Settings()->Get(
        "settings.content.last_played_frame", uint64_t{});
    
    // If we've reached here, the playlist is set and positioned at the correct dream
    // Now we can start playing this dream
    StartTransition();
    if (PlayClip(optionalDream->dream, m_TimelineTime, seekFrame)) {
        if (m_currentClip) {
            m_currentClip->SetResumeTime(m_TimelineTime);
            m_currentClip->SetTransitionLength(5.0, 5.0);
        }
        return true;
    }
    return false;
   
}


void CPlayer::ResetPlaylist() {
    //writer_lock l(m_UpdateMutex);

    // Grab the default playlist again & set it
    g_Log->Info("PreReset");
    std::thread([this]{
        SetPlaylist("");
        SetTransitionDuration(1.0f);
        StartTransition();
    }).detach();
    g_Log->Info("PostReset");
}

// MARK: - Transition
void CPlayer::StartTransition()
{
    if (m_isFirstPlay) {
        m_isFirstPlay = false;
        return;  // Don't start a transition for the first play
    }

    if (m_nextDreamDecision &&
           m_nextDreamDecision->transition == PlaylistManager::TransitionType::Seamless) {
       g_Log->Info("Skipping crossfade for seamless transition");
       return;
    }

    m_isTransitioning = true;
    m_transitionStartTime = m_TimelineTime;
    
    if (m_nextClip && m_nextClip != m_currentClip) {
        destroyClipAsync(std::move(m_nextClip));
    }
    m_nextClip = nullptr;  // We'll set this when we have the next clip ready
}

void CPlayer::UpdateTransition(double currentTime)
{
    if (!m_isTransitioning) return;

    double transitionProgress = (currentTime - m_transitionStartTime) / m_transitionDuration;

    bool nextClipBuffering = (m_nextClip && m_nextClip->IsBuffering());
        
    if (nextClipBuffering) {
        g_Log->Info("Next clip still buffering during transition (progress: %.2f)",
                    transitionProgress);
    }
    
    // If we have preflight decision and it's seamless, but we're transitioning,
    // that means it was interrupted - convert to quick fade
    if (m_nextDreamDecision &&
        m_nextDreamDecision->transition == PlaylistManager::TransitionType::Seamless) {
        g_Log->Info("Seamless transition was interrupted, converting to quick fade");
        m_transitionDuration = 1.0f;
        m_nextDreamDecision = std::nullopt;
    }
    

    if (transitionProgress >= 1.0) {
        // Transition complete
        m_isTransitioning = false;
        
        // Start the asynchronous destruction of the current clip
        if (m_currentClip) {
            destroyClipAsync(std::move(m_currentClip));
        }
        
        m_currentClip = m_nextClip;
        
        m_nextClip = nullptr;
        m_transitionDuration = 5.0f;
        m_nextDreamDecision = std::nullopt;  // Clear any pending decision
    } else if (!m_nextClip) {
        // If we don't have a next clip yet, try to get one
        auto nextDecision = m_playlistManager->preflightNextDream();
        if (nextDecision) {
            // We're in mid-transition without a next clip, must be user/network forced
            // Always use quick crossfade
            PlayClip(nextDecision->dream, currentTime, -1, true);
            if (m_nextClip) {
                // Use quick fade for interrupted transitions
                m_nextClip->SetTransitionLength(1.0f, 5.0f);
                m_playlistManager->moveToNextDream(*nextDecision);
            }
        }
        // tmp
        /*PlayClip(nextDecision->dream, currentTime, -1, true);
        if (m_nextClip) {
            // Use quick fade for interrupted transitions
            m_nextClip->SetTransitionLength(1.0f, 5.0f);
            m_playlistManager->moveToNextDream(*nextDecision);
        }
        if (nextDecision) {
            if (!nextDream->uuid.empty()) {
                // We may need to prefetch the url, if so, do it all in a thread
                if (!nextDream->isCached() && nextDream->getStreamingUrl().empty()) {
                    std::thread([this, &currentTime, nextDream]{
                        auto path = EDreamClient::GetDreamDownloadLink(nextDream->uuid);
                        nextDream->setStreamingUrl(path);

                        PlayClip(nextDream, currentTime, -1, true);  // The true flag indicates this is for transition
                        if (m_nextClip) {
                            m_nextClip->SetTransitionLength(m_transitionDuration, 5.0f);
                        }
                    }).detach();
                } else {
                    PlayClip(nextDream, currentTime, -1, true);  // The true flag indicates this is for transition
                    if (m_nextClip) {
                        m_nextClip->SetTransitionLength(m_transitionDuration, 5.0f);
                    }
                }
            }
        }*/
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
    g_Log->Info("Next");
    m_transitionDuration = 1.0f;
    PlayNextDream(true);
}

void CPlayer::ReturnToPrevious()
{
    m_transitionDuration = 1.0f;
    auto previousDream = m_playlistManager->getPreviousDream();
    
    StartTransition();
    PlayClip(previousDream, m_TimelineTime, -1, true);
    m_nextClip->SetTransitionLength(1.0f, 5.0f);
}

void CPlayer::RepeatClip()
{
    m_transitionDuration = 1.0f;
    auto currentDream = m_playlistManager->getCurrentDream();
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

// MARK: - Transitions
bool CPlayer::shouldPrepareTransition(const ContentDecoder::spCClip& clip) const {
    if (!clip) return false;
    
    const auto& metadata = clip->GetCurrentFrameMetadata();
    
    // Make sure the clip is properly loaded before we look for transition
    if (metadata.maxFrameIdx <= 0) {
        return false;
    }
    
    uint32_t framesRemaining = metadata.maxFrameIdx - metadata.frameIdx;
    double timeRemaining = framesRemaining / clip->GetClipMetadata().decodeFps;
    
    // Start preparing transition when we're within 6 seconds of the end
    return timeRemaining <= 6.0;
}

void CPlayer::prepareSeamlessTransition() {
    if (!m_playlistManager || !m_nextDreamDecision) return;
    
    // We only want to prepare once
    if (m_nextClip) return;

    // Make sure current clip won't transition
    m_currentClip->SetTransitionLength(0.0, 0.0);

    // Start loading the next clip but don't start transition yet
    PreloadClip(m_nextDreamDecision->dream);
    
    if (m_nextClip) {
        m_nextClip->StartPlayback(0);
        g_Log->Info("Prepared seamless transition to: %s", m_nextDreamDecision->dream->uuid.c_str());
    }
}

bool CPlayer::PreloadClip(const Cache::Dream* dream) {
    if (!dream) {
        g_Log->Error("Attempted to preload a null dream");
        return false;
    }
    
    auto du = m_displayUnits[0];
    int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
    
    auto path = dream->getCachedPath();
    if (path.empty()) {
        // Try streamingUrl that's been prefetched
        path = dream->getStreamingUrl();
        if (path.empty()) {
            // Last resort blocking call
            path = EDreamClient::GetDreamDownloadLink(dream->uuid);
        }
    }
    
    if (path.empty()) {
        g_Log->Error("Failed to get path for dream: %s", dream->uuid.c_str());
        return false;
    }
    
    auto newClip = std::make_shared<ContentDecoder::CClip>(
        ContentDecoder::sClipMetadata{path, m_PerceptualFPS / dream->activityLevel, *dream},
        du->spRenderer, displayMode, du->spDisplay->Width(),
        du->spDisplay->Height());
    
    // Update internal decoder fps counter
    m_DecoderFps = m_PerceptualFPS / dream->activityLevel;

    // Load the clip but don't start playback yet - this is key
    if (!newClip->Preload()) {
        return false;
    }
    
    // Store the clip but don't set its start time yet
    m_nextClip = newClip;
    
    return true;
}
