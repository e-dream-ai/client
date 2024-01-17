
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
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

#include "ContentDownloader.h"
#include "DreamPlaylist.h"
#include "PlayCounter.h"
#include "Settings.h"
#include "storage.h"

#include "boost/filesystem/convenience.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"

#if defined(MAC) || defined(WIN32)
#define HONOR_VBL_SYNC
#endif

const double kTransitionLengthSeconds = 1;

using boost::filesystem::directory_iterator;
using boost::filesystem::exists;
using boost::filesystem::extension;
using boost::filesystem::path;

using namespace DisplayOutput;

using CClip = ContentDecoder::CClip;
using spCClip = ContentDecoder::spCClip;
using write_lock = boost::mutex::scoped_lock;

/*
 */
CPlayer::CPlayer()
{
    m_spPlaylist = nullptr;
    m_DecoderFps = 23; //	http://en.wikipedia.org/wiki/23_(numerology)
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

bool CPlayer::AddDisplay([[maybe_unused]] uint32_t screen,
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
        return false;
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
            return false;
    }
    else if (!spDisplay->Initialize(w, h, m_bFullscreen))
        return false;
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
        return false;

#if defined(MAC)
    if (_graphicsContext != nullptr)
    {
        if (!spDisplay->Initialize(_graphicsContext, true))
            return false;

        spDisplay->ForceWidthAndHeight(w, h);
    }
#else
    if (!spDisplay->Initialize(w, h, m_bFullscreen))
        return false;
#endif //! MAC

    spRenderer = std::make_shared<CRendererMetal>();

#endif //! WIN32

    //	Start renderer & set window title.
    if (spRenderer->Initialize(spDisplay) == false)
        return false;
    spDisplay->Title("e-dream");

    {
        auto du = std::make_shared<DisplayUnit>();

        du->spRenderer = spRenderer;
        du->spDisplay = spDisplay;

        boost::mutex::scoped_lock lockthis(m_displayListMutex);

        if (g_Settings()->Get("settings.player.reversedisplays", false) == true)
            m_displayUnits.insert(m_displayUnits.begin(), du);
        else
            m_displayUnits.push_back(du);
    }

    return true;
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
    boost::mutex::scoped_lock lockthis(m_displayListMutex);

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
    if (m_bStarted)
    {
        g_Settings()->Set("settings.content.last_played_file",
                          m_CurrentClips[0]->GetClipMetadata().path);
        g_Settings()->Set("settings.content.last_played_frame",
                          (uint64_t)m_CurrentClips[0]->GetCurrentFrameIdx());
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
    double newTime = m_Timer.Time();
    if (m_LastFrameRealTime == 0.0)
        m_LastFrameRealTime = newTime;
    double delta = newTime - m_LastFrameRealTime;
    m_LastFrameRealTime = newTime;
    m_TimelineTime += delta;

    boost::mutex::scoped_lock l(m_updateMutex);

    for (spCClip clip : m_CurrentClips)
    {
        if (!clip->Update(m_TimelineTime))
        {
            //                bPlayNoSheepIntro = true;
        }
    }

    if (m_CurrentClips.size())
    {
        for (auto it = m_CurrentClips.begin(); it != m_CurrentClips.end(); ++it)
        {
            spCClip currentClip = *it;
            if (currentClip->HasFinished())
            {
                if ((currentClip->GetFlags() & CClip::eClipFlags::Discarded) ==
                    0)
                    m_ClipInfoHistoryQueue.push(
                        std::string{currentClip->GetClipMetadata().path});
                m_CurrentClips.erase(it--);
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
        boost::mutex::scoped_lock lockthis(m_displayListMutex);

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
        boost::mutex::scoped_lock lockthis(m_displayListMutex);

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
    bPlayNoSheepIntro = false;

    std::shared_ptr<DisplayUnit> du;

    {
        boost::mutex::scoped_lock lockthis(m_displayListMutex);

        if (displayUnit >= m_displayUnits.size())
            return false;

        du = m_displayUnits[displayUnit];
    }

    du->spRenderer->Reset(eEverything);
    du->spRenderer->Orthographic();
    du->spRenderer->Apply();

    boost::mutex::scoped_lock l(m_updateMutex);
    //for (auto it : du->spClips)
    for (spCClip clip : m_CurrentClips)
    {
        clip->DrawFrame(du->spRenderer);
    }

    return true;
}

bool CPlayer::NextClipForPlaying(int32_t _forceNext)
{
    if (m_spPlaylist == nullptr)
    {
        g_Log->Warning("Playlist == nullptr");
        return (false);
    }

    if (_forceNext != 0)
    {
        if (_forceNext < 0)
        {
            uint32_t _numPrevious = (uint32_t)(-_forceNext);
            if (m_ClipInfoHistoryQueue.size() > 0)
            {
                std::string name;
                for (uint32_t i = 0; i < _numPrevious; i++)
                {
                    while (m_ClipInfoHistoryQueue.pop(name, false, false))
                    {
                        m_NextClipInfoQueue.push(name, false, false);
                        break;
                    }
                }
            }
        }
    }

    //    if (m_MainVideoInfo == nullptr)
    //    {
    //        m_MainVideoInfo = GetNextSheepInfo();
    //        m_MainVideoInfo->m_DecodeFps = 0.9;
    //        if (m_MainVideoInfo == nullptr)
    //            return false;
    //    }
    //
    //    if (m_SecondVideoInfo == nullptr)
    //    {
    //        m_SecondVideoInfo = GetNextSheepInfo();
    //        m_SecondVideoInfo->m_DecodeFps = 1;
    //    }
    //
    //    if (!m_MainVideoInfo->IsOpen())
    //    {
    //        if (!Open(m_MainVideoInfo))
    //            return false;
    //    }
    //    else
    //    {
    //        // if the video was already open (m_SecondVideoInfo previously),
    //        // we need to assure seamless continuation
    //        //m_MainVideoInfo->m_NextIsSeam = true;
    //    }
    //
    //    if (m_MainVideoInfo->IsOpen())
    //    {
    //        while (m_SheepHistoryQueue.size() > 50)
    //        {
    //            std::string tmpstr;
    //
    //            m_SheepHistoryQueue.pop(tmpstr);
    //        }
    //
    //        m_SheepHistoryQueue.push(m_MainVideoInfo->m_Path);
    //
    //        m_NoSheeps = false;
    //    }
    //    else
    //        return false;
    //
    //    if (m_bCalculateTransitions && m_SecondVideoInfo != nullptr &&
    //        !m_SecondVideoInfo->IsOpen())
    //    {
    //        Open(m_SecondVideoInfo);
    //    }

    return true;
}

void CPlayer::PlayQueuedClipsThread()
{
    try
    {
        std::string lastPlayedFile = g_Settings()->Get(
            "settings.content.last_played_file", std::string{});
        if (lastPlayedFile != "")
        {
            int64_t seekFrame;
            seekFrame = (int64_t)g_Settings()->Get("settings.content.last_played_frame",
                                          uint64_t{});
            PlayClip(lastPlayedFile, m_TimelineTime, seekFrame);
        }

        while (m_bStarted)
        {
            boost::this_thread::interruption_point();
            {
                boost::mutex::scoped_lock l(m_updateMutex);
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
                    if (m_NextClipInfoQueue.pop(nextClip, true))
                    {
                        PlayClip(nextClip, startTime);
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

bool CPlayer::PlayClip(std::string_view _clipPath, double _startTime, int64_t _seekFrame)
{
    auto du = m_displayUnits[0];
    int32_t displayMode = g_Settings()->Get("settings.player.DisplayMode", 2);
    ContentDownloader::sDreamMetadata* dream = nullptr;
    m_spPlaylist->GetDreamMetadata(_clipPath, &dream);

    if (!dream)
        return false;

    spCClip clip = std::make_shared<CClip>(
        sClipMetadata{std::string{_clipPath}, *dream}, du->spRenderer,
        displayMode, du->spDisplay->Width(), du->spDisplay->Height(),
        m_DecoderFps / dream->activityLevel);

    if (!clip->Start(_seekFrame))
        return false;

    clip->SetStartTime(_startTime);
    clip->SetTransitionLength(m_CurrentClips.size() ? kTransitionLengthSeconds
                                                    : 0,
                              kTransitionLengthSeconds);
    m_CurrentClips.push_back(clip);
    return true;
}

void CPlayer::Framerate(const double _fps)
{
    m_DecoderFps = _fps;
    boost::mutex::scoped_lock l(m_updateMutex);
    if (m_CurrentClips.size())
        m_CurrentClips[0]->SetFps(_fps);
}

void CPlayer::CalculateNextClipThread()
{
    try
    {
        uint32_t curID = 0;

        bool bRebuild = true;

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
            bool enoughClips = true;
            bool playFreshClips = false;
            {
                if (m_spPlaylist->Next(spath, enoughClips, curID,
                                       playFreshClips, bRebuild, true))
                {
                    bRebuild = false;

                    if (!enoughClips)
                    {
                        while (!m_NextClipInfoQueue.empty())
                        {
                            if (!m_NextClipInfoQueue.waitForEmpty())
                            {
                                break;
                            }
                        }
                    }
                    // if (playFreshClips)
                    // m_NextClipQueue.clear(0);
                    m_NextClipInfoQueue.push(spath);
                }
                else
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

ContentDecoder::sOpenVideoInfo* CPlayer::GetNextClipInfo()
{
    std::string name;

    sOpenVideoInfo* retOVI = nullptr;

    if (m_spPlaylist->PopFreshlyDownloadedSheep(name))
    {
        retOVI = new sOpenVideoInfo;
        retOVI->m_Path.assign(name);
        return retOVI;
    }

    bool clipfound = false;

    while (!clipfound && m_NextClipInfoQueue.pop(name, true))
    {
        if (name.empty())
            break;

        clipfound = true;

        retOVI = new sOpenVideoInfo;

        retOVI->m_Path.assign(name);
    }

    return retOVI;
}

void CPlayer::SkipToNext()
{
    write_lock l(m_updateMutex);
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
    }
    m_CurrentClips[0]->FadeOut(m_TimelineTime);
    m_CurrentClips[1]->SetStartTime(m_TimelineTime);
}

void CPlayer::ReturnToPrevious()
{
    write_lock l(m_updateMutex);
    std::string clipName;
    if (m_CurrentClips.size() < 2)
        return;
    // If the current clip is fading out already, remove it immediately
    spCClip currentClip = m_CurrentClips[0];
    auto [_, out] = currentClip->GetTransitionLength();
    if (m_TimelineTime > currentClip->GetEndTime() - out)
    {
        m_NextClipInfoQueue.push(
            std::string{m_CurrentClips[1]->GetClipMetadata().path}, false,
            false);
        clipName = m_CurrentClips[0]->GetClipMetadata().path;
        m_CurrentClips[0] = m_CurrentClips[1];
        m_CurrentClips.erase(m_CurrentClips.begin() + 1);
        m_CurrentClips[0]->FadeOut(m_TimelineTime);
        m_CurrentClips[0]->SetFlags(CClip::eClipFlags::Discarded);
        PlayClip(clipName, m_TimelineTime);
        return;
    }
    if (m_ClipInfoHistoryQueue.pop(clipName, false, false))
    {
        m_NextClipInfoQueue.push(
            std::string{m_CurrentClips[1]->GetClipMetadata().path}, false,
            false);
        m_NextClipInfoQueue.push(
            std::string{m_CurrentClips[0]->GetClipMetadata().path}, false,
            false);
        m_CurrentClips[0]->SetFlags(CClip::eClipFlags::Discarded);
        m_CurrentClips[0]->FadeOut(m_TimelineTime);
        m_CurrentClips.erase(m_CurrentClips.begin() + 1);
        PlayClip(clipName, m_TimelineTime);
    }
}

void CPlayer::SkipForward(float _seconds)
{
    m_CurrentClips[0]->SkipTime(_seconds);
    m_CurrentClips[1]->SetStartTime(m_CurrentClips[1]->GetStartTime() -
                                    _seconds);
}

const CClip::sClipMetadata* CPlayer::GetCurrentPlayingClipMetadata() const
{
    if (!m_CurrentClips.size())
        return nullptr;
    return &m_CurrentClips[0]->GetClipMetadata();
}
