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

  private:
    struct DecoderClock
    {
        double clock;
        double acc;
        double interframeDelta;
        bool started;
    };

    sClipMetadata m_ClipMetadata;
    DecoderClock m_DecoderClock;
    spCFrameDisplay m_spFrameDisplay;
    spCRenderer m_spRenderer;
    spCContentDecoder m_spDecoder;
    ContentDecoder::spCVideoFrame m_spFrameData;
    DisplayOutput::spCImage m_spImageRef;
    sFrameMetadata m_CurrentFrameMetadata;
    mutable std::shared_mutex m_CurrentFrameMetadataLock;
    double m_StartTime;
    double m_EndTime;
    boost::atomic<bool> m_HasFinished;
    boost::atomic<bool> m_IsFadingOut;
    float m_FadeInSeconds = 5.f;
    float m_FadeOutSeconds = 5.f;
    float m_Alpha;
    eClipFlags m_ClipFlags = eClipFlags::None;

    // We need these to handle resumes
    double m_ResumeStartTime = 0.0;
    bool m_IsResume = false;
    
  private:
    bool NeedsNewFrame(double _timelineTime, DecoderClock* _decoderClock) const;
    ///    Grab a frame from the decoder and use it as a texture.
    bool GrabVideoFrame();

  public:
    CClip(const sClipMetadata& _metadata, spCRenderer _spRenderer,
          int32_t _displayMode, uint32_t _displayWidth,
          uint32_t _displayHeight);
    bool Start(int64_t _seekFrame = -1);
    void Stop();
    bool Update(double _timelineTime);
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
};
MakeSmartPointers(CClip);
} // namespace ContentDecoder
