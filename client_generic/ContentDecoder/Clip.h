//
//  Clip.h
//  e-dream
//
//  Created by Tibi Hencz on 5.1.2024.
//

#pragma once

#include <string>
#include <string_view>
#include <tuple>
#include <boost/atomic.hpp>

#include "SmartPtr.h"
#include "FrameDisplay.h"
#include "ContentDecoder.h"
#include "CacheManager.h"

namespace ContentDecoder
{


class CClip
{
    using spCTextureFlat = DisplayOutput::spCTextureFlat;
    using spCRenderer = DisplayOutput::spCRenderer;

  public:
    enum eClipFlags
    {
        None = 0,
        Discarded = 1,
        ReverseHistory = 2
    };

    enum class BufferingState {
        NotBuffering,  // Normal playback
        Buffering,     // Initial buffering before playback starts
        Rebuffering    // Paused playback to rebuild buffer
    };

    
  private:
    struct DecoderClock
    {
        double clock;
        double acc;
        double interframeDelta;
        bool started;
    };

public: // tmp public for debug
    sClipMetadata m_ClipMetadata;
    DecoderClock m_DecoderClock;
private: //tmp
    spCFrameDisplay m_spFrameDisplay;
    spCRenderer m_spRenderer;
    spCContentDecoder m_spDecoder;
    ContentDecoder::spCVideoFrame m_spFrameData;
    DisplayOutput::spCImage m_spImageRef;
public: // tmp public for debug
    sFrameMetadata m_CurrentFrameMetadata;
private: // tmp
    mutable std::shared_mutex m_CurrentFrameMetadataLock;
    double m_StartTime;
    double m_EndTime;
    boost::atomic<bool> m_HasFinished;
    boost::atomic<bool> m_IsFadingOut;
public:
    float m_FadeInSeconds = 5.f;
    float m_FadeOutSeconds = 5.f;
    float m_Alpha = 0.f;
private:
    eClipFlags m_ClipFlags = eClipFlags::None;

    // We need these to handle resumes
    double m_ResumeStartTime = 0.0;
    bool m_IsResume = false;
    
    bool m_IsPreloaded = false;
 
    // Buffering-related fields
    BufferingState m_BufferingState = BufferingState::NotBuffering;
    double m_RequestedStartTime = 0.0;  // When clip was requested to start
    double m_ActualStartTime = 0.0;     // When clip actually started playing
    double m_RebufferingStartTime = 0.0; // When rebuffering began
    double m_TotalBufferingTime = 0.0;   // Accumulated buffering time
    bool m_HasStartedPlaying = false;    // Whether playback has actually begun
    float m_LastCalculatedAlpha = 0.0f;  // Store last calculated alpha (needed when buffering)

    
  private:
    bool NeedsNewFrame(double _timelineTime, DecoderClock* _decoderClock) const;
    ///    Grab a frame from the decoder and use it as a texture.
    bool GrabVideoFrame();
    spCVideoFrame m_LastValidFrame;  // Store the last successfully decoded frame

public:
    CClip(const sClipMetadata& _metadata, spCRenderer _spRenderer,
          int32_t _displayMode, uint32_t _displayWidth,
          uint32_t _displayHeight);
    bool Start(int64_t _seekFrame = -1);
    void Stop();
    
    bool Preload(int64_t _seekFrame = -1);
    bool IsPreloadComplete() const;

    bool StartPlayback(int64_t _seekFrame = -1);
    
    bool Update(double _timelineTime, bool isPaused = false);
    bool DrawFrame(spCRenderer _spRenderer, float alpha = 1.0f);
    void SetDisplaySize(uint32_t _displayWidth, uint32_t _displayHeight);

    spCContentDecoder GetDecoder() const { return m_spDecoder; }

    const sClipMetadata& GetClipMetadata() const { return m_ClipMetadata; }
    void SetClipMetadata(const sClipMetadata& _metadata)
    {
        m_ClipMetadata = _metadata;
    }
    /// Gets the frame index of the last frame read from the encoder. Thread safe function.
    uint32_t GetCurrentFrameIdx() const;
    const sFrameMetadata& GetCurrentFrameMetadata() const;
    uint32_t GetFrameCount() const;
    void SetStartTime(double _startTime);
    double GetStartTime() const { return m_StartTime; }
    double GetLength() const
    {
        return GetFrameCount() / m_ClipMetadata.decodeFps;
    }
    double GetLength(float _atFps) const { return GetFrameCount() / _atFps; }
    bool HasFinished() const { return m_HasFinished.load(); }
    void ResetFinished() {
        m_HasFinished.exchange(false);
    }
    bool IsFadingOut() const { return m_IsFadingOut.load(); }

    void SetTransitionLength(float _fadeInSeconds, float _fadeOutSeconds)
    {
        m_FadeInSeconds = _fadeInSeconds;
        m_FadeOutSeconds = _fadeOutSeconds;
    }
    std::tuple<float, float> GetTransitionLength() const
    {
        return {m_FadeInSeconds, m_FadeOutSeconds};
    }
    void FadeOut(double _currentTimelineTime);
    double GetEndTime() const { return m_EndTime; }
    void SetFlags(eClipFlags _flags) { m_ClipFlags = _flags; }
    eClipFlags GetFlags() const { return m_ClipFlags; }
    void SkipTime(float _secondsForward);
    void SetFps(double _fps) { m_ClipMetadata.decodeFps = _fps; }
    
    void SetResumeTime(double startTime) {
        m_ResumeStartTime = startTime;
        m_IsResume = true;
    }
    bool IsNearEnd() const;

    bool IsBuffering() const { return m_BufferingState != BufferingState::NotBuffering; }
    bool IsRebuffering() const { return m_BufferingState == BufferingState::Rebuffering; }
    double GetActualStartTime() const { return m_ActualStartTime; }
    double GetTotalBufferingTime() const { return m_TotalBufferingTime; }
    bool HasStartedPlaying() const { return m_HasStartedPlaying; }
   
};
MakeSmartPointers(CClip);
} // namespace ContentDecoder
