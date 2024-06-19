#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <queue>

#ifdef WIN32
#include <d3d9.h>
#include <d3dx9.h>
#endif
#include "ContentDecoder.h"
#include "DisplayOutput.h"
#include "FrameDisplay.h"
#include "Renderer.h"
#include "Playlist.h"
#include "DreamPlaylist.h"
#include "Settings.h"
#include "Singleton.h"
#include "Timer.h"
#include "Clip.h"

/**
        CPlayer.
        Singleton class to display decoded video to display.
*/
class CPlayer : public Base::CSingleton<CPlayer>
{
    using sOpenVideoInfo = ContentDecoder::sOpenVideoInfo;
    using sClipMetadata = ContentDecoder::sClipMetadata;

  public:
    typedef enum
    {
        kMDSharedMode,
        kMDIndividualMode,
        kMDSingleScreen
    } MultiDisplayMode;
    ContentDecoder::spCDreamPlaylist m_spPlaylist;

  private:
    typedef struct
    {
        DisplayOutput::spCDisplayOutput spDisplay;
        DisplayOutput::spCRenderer spRenderer;
        std::vector<ContentDecoder::spCClip> spClips;
    } DisplayUnit;

    typedef std::vector<std::shared_ptr<DisplayUnit>> DisplayUnitList;
    typedef DisplayUnitList::iterator DisplayUnitIterator;

    std::mutex m_displayListMutex;

    friend class Base::CSingleton<CPlayer>;

    //	Private constructor accessible only to CSingleton.
    CPlayer();

    //	No copy constructor or assignment operator.
    NO_CLASS_STANDARDS(CPlayer);

    DisplayUnitList m_displayUnits;
    Base::CTimer m_Timer;
    double m_DecoderFps;
    double m_PerceptualFPS;
    double m_DisplayFps;
    double m_TimelineTime;
    double m_LastFrameRealTime;
    bool m_bFullscreen;
    bool m_InitPlayCounts;
    bool m_bPaused;
    Base::CBlockingQueue<std::string> m_NextClipInfoQueue;
    Base::CBlockingQueue<std::string> m_ClipInfoHistoryQueue;
    std::vector<ContentDecoder::spCClip> m_CurrentClips;
    
    std::vector<std::string> m_evictedUUIDs;    // List of dreams that have been disliked this session
    
    std::mutex m_CurrentClipsMutex;
    boost::thread* m_pNextClipThread;
    boost::thread* m_pPlayQueuedClipsThread;

    MultiDisplayMode m_MultiDisplayMode;

    boost::atomic<bool> m_bStarted;

    //	Used to keep track of elapsed time since last frame.
    double m_CapClock;

    mutable std::shared_mutex m_UpdateMutex;
    boost::condition m_PlayCond;

    // Last uuid reported to server
    std::string lastReportedUUID;
#ifdef WIN32
    HWND m_hWnd;

  public:
    //	When running as a screensaver, we need to pass this along, as it's
    // already created for us.
    void SetHWND(HWND _hWnd);
    HWND GetHWND(void) { return m_hWnd; }

  private:
#endif

    void FpsCap(const double _cap);

  public:
    bool Startup();
    bool Shutdown(void);
    virtual ~CPlayer();

    const char* Description() { return ("Player"); };

    bool Closed(void)
    {
        DisplayOutput::spCDisplayOutput spDisplay = Display();

        if (spDisplay == nullptr)
        {
            g_Log->Warning("m_spDisplay is nullptr");
            return true;
        }

        return spDisplay->Closed();
    }

    bool BeginFrameUpdate();
    bool EndFrameUpdate();
    bool BeginDisplayFrame(uint32_t displayUnit);
    bool EndDisplayFrame(uint32_t displayUnit, bool drawn = true);
    bool Update(uint32_t displayUnit, bool& bPlayNoSheepIntro);
    void Start();
    void Stop();
    bool NextClipForPlaying(int32_t _forceNext);
    void CalculateNextClipThread();
    void PlayQueuedClipsThread();
    sOpenVideoInfo* GetNextClipInfo();

    int AddDisplay(uint32_t screen, CGraphicsContext _grapicsContext,
                   bool _blank = false);

    inline void PlayCountsInitOff() { m_InitPlayCounts = false; };

    void MultiplyPerceptualFPS(const double _multiplier);
    void SetPerceptualFPS(const double _fps);
    double GetPerceptualFPS();
    double GetDecoderFPS();
    inline void Fullscreen(const bool _bState) { m_bFullscreen = _bState; };
    inline bool Stopped() { return !m_bStarted; };

    inline DisplayOutput::spCDisplayOutput Display(uint32_t du = 0)
    {
        std::scoped_lock lockthis(m_displayListMutex);

        if (du >= m_displayUnits.size())
            return nullptr;

        return m_displayUnits[du]->spDisplay;
    }

    inline DisplayOutput::spCRenderer Renderer()
    {
        std::scoped_lock lockthis(m_displayListMutex);

        return m_displayUnits.empty() ? nullptr : m_displayUnits[0]->spRenderer;
    }

    const ContentDecoder::sClipMetadata* GetCurrentPlayingClipMetadata() const;
    const ContentDecoder::sFrameMetadata* GetCurrentFrameMetadata() const;

    inline void Add(const std::string& _fileName)
    {
        if (m_spPlaylist)
            m_spPlaylist->Add(_fileName);
    }
    inline void Delete(std::string_view _uuid)
    {
        if (m_spPlaylist)
            m_spPlaylist->Delete(_uuid);
    };

    void PlayDreamNow(std::string_view _uuid);
    void ResetPlaylist();
    
    void MarkForDeletion(std::string_view _uuid);
    void SkipToNext();
    void ReturnToPrevious();
    void SkipForward(float _seconds);
    void RepeatClip();
    /// Sets up a clip for playing, that will start playing at startTimelineTime
    /// Returns true if the file was loaded successfully
    bool PlayClip(std::string_view _clipPath, double _startTimelineTime,
                  int64_t _seekFrame = -1);
    void SetMultiDisplayMode(MultiDisplayMode mode)
    {
        m_MultiDisplayMode = mode;
    }
    uint32_t GetDisplayCount()
    {
        return static_cast<uint32_t>(m_displayUnits.size());
    }
    void ForceWidthAndHeight(uint32_t du, uint32_t _w, uint32_t _h);
    void SetPaused(bool _bPaused) { m_bPaused = _bPaused; }
};

/*
        Helper for less typing...

*/
inline CPlayer& g_Player(void) { return (CPlayer::Instance()); }

#endif
