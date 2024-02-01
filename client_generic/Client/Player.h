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

  private:
    typedef struct
    {
        DisplayOutput::spCDisplayOutput spDisplay;
        DisplayOutput::spCRenderer spRenderer;
        std::vector<ContentDecoder::spCClip> spClips;
    } DisplayUnit;

    typedef std::vector<std::shared_ptr<DisplayUnit>> DisplayUnitList;
    typedef DisplayUnitList::iterator DisplayUnitIterator;

    boost::mutex m_displayListMutex;

    friend class Base::CSingleton<CPlayer>;

    //	Private constructor accessible only to CSingleton.
    CPlayer();

    //	No copy constructor or assignment operator.
    NO_CLASS_STANDARDS(CPlayer);

    DisplayUnitList m_displayUnits;
    ContentDecoder::spCPlaylist m_spPlaylist;
    Base::CTimer m_Timer;
    double m_DecoderFps;
    double m_DisplayFps;
    double m_TimelineTime;
    double m_LastFrameRealTime;
    bool m_bFullscreen;
    bool m_InitPlayCounts;
    bool m_bPaused;
    Base::CBlockingQueue<std::string> m_NextClipInfoQueue;
    Base::CBlockingQueue<std::string> m_ClipInfoHistoryQueue;
    std::vector<ContentDecoder::spCClip> m_CurrentClips;
    boost::mutex m_CurrentClipsMutex;
    boost::thread* m_pNextClipThread;
    boost::thread* m_pPlayQueuedClipsThread;

    MultiDisplayMode m_MultiDisplayMode;

    boost::atomic<bool> m_bStarted;

    //	Used to keep track of elapsed time since last frame.
    double m_CapClock;

    mutable boost::shared_mutex m_UpdateMutex;
    boost::condition m_PlayCond;

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

    bool AddDisplay(uint32_t screen, CGraphicsContext _grapicsContext,
                    bool _blank = false);

    inline void PlayCountsInitOff() { m_InitPlayCounts = false; };
    void MultiplyFramerate(const double _multiplier);
    void SetFramerate(const double _fps) { m_DecoderFps = _fps; }
    inline void Fullscreen(const bool _bState) { m_bFullscreen = _bState; };
    inline bool Stopped() { return !m_bStarted; };

    inline DisplayOutput::spCDisplayOutput Display(uint32_t du = 0)
    {
        boost::mutex::scoped_lock lockthis(m_displayListMutex);

        if (du >= m_displayUnits.size())
            return nullptr;

        return m_displayUnits[du]->spDisplay;
    }

    inline DisplayOutput::spCRenderer Renderer()
    {
        boost::mutex::scoped_lock lockthis(m_displayListMutex);

        return m_displayUnits.empty() ? nullptr : m_displayUnits[0]->spRenderer;
    }

    const ContentDecoder::sClipMetadata* GetCurrentPlayingClipMetadata() const;
    const ContentDecoder::sFrameMetadata* GetCurrentFrameMetadata() const;

    inline void Add(const std::string& _fileName)
    {
        if (m_spPlaylist)
            m_spPlaylist->Add(_fileName);
    }
    inline void Delete(const uint32_t _id)
    {
        if (m_spPlaylist)
            m_spPlaylist->Delete(_id);
    };

    void SkipToNext();
    void ReturnToPrevious();
    void SkipForward(float _seconds);
    void RepeatClip() {}
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
