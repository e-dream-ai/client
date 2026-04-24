//
//  Clip.cpp
//  e-dream
//
//  Created by Tibi Hencz on 5.1.2024.
//

#include <memory>
#include <numeric>

#include "Clip.h"
#include "CubicFrameDisplay.h"
#include "LinearFrameDisplay.h"
#include "FrameGeneration/BlendFrameInterpolator.h"
#include "FrameGeneration/FrameGenerationMode.h"
#include "FrameGeneration/RifeInterpolatorNcnn.h"

namespace ContentDecoder
{

static double SelectFrameGenerationTargetFps(double sourceFps,
                                             double requestedOutputFps,
                                             double displayRefreshFps)
{
    if (sourceFps <= 0.0)
        return requestedOutputFps;

    const double cappedRequested = std::min(requestedOutputFps, sourceFps * 2.0);
    if (displayRefreshFps <= 0.0)
        return cappedRequested;

    double best = sourceFps;
    for (int divisor = 1; divisor <= 8; ++divisor)
    {
        const double candidate = displayRefreshFps / static_cast<double>(divisor);
        if (candidate <= cappedRequested + 0.001 && candidate > best + 0.001)
            best = candidate;
    }

    return best;
}

CClip::CClip(const sClipMetadata& _metadata, spCRenderer _spRenderer,
             int32_t _displayMode, uint32_t _displayWidth,
             uint32_t _displayHeight)
    : m_ClipMetadata(_metadata), m_spRenderer(_spRenderer),
m_CurrentFrameMetadata{}, m_HasFinished(false), m_IsFadingOut(false)
{
    //    Create frame display.
    if (_displayMode == 2)
    {
        g_Log->Info("Using piecewise cubic video display...");
        m_spFrameDisplay = std::make_shared<CCubicFrameDisplay>(m_spRenderer);
    }
    else if (_displayMode == 1)
    {
        g_Log->Info("Using piecewise linear video display...");
        m_spFrameDisplay = std::make_shared<CLinearFrameDisplay>(m_spRenderer);
        g_Settings()->Set("settings.player.DisplayMode", 1);
    }

    if (m_spFrameDisplay && !m_spFrameDisplay->Valid())
    {
        g_Log->Warning("FrameDisplay failed, falling back to normal");
        g_Settings()->Set("settings.player.DisplayMode", 0);
        m_spFrameDisplay = nullptr;
    }

    //    Fallback to normal.
    if (m_spFrameDisplay == nullptr)
    {
        g_Log->Info("Using normal video display...");
        m_spFrameDisplay = std::make_shared<CFrameDisplay>(m_spRenderer);
        g_Settings()->Set("settings.player.DisplayMode", 0);
    }

    m_spFrameDisplay->SetDisplaySize(_displayWidth, _displayHeight);

#ifndef LINUX_GNU
#if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
    AVPixelFormat pf = AV_PIX_FMT_RGBA;
#else
    AVPixelFormat pf = AV_PIX_FMT_RGB32;
#endif

    // On PowerPC machines we need to use different pixel format!
#if defined(MAC) && defined(__BIG_ENDIAN__)
    pf = AV_PIX_FMT_BGR32_1;
#endif

#else

    AVPixelFormat pf = AV_PIX_FMT_RGBA;
#if defined(__BIG_ENDIAN__)
    pf = AV_PIX_FMT_RGBA;  // RGBA is byte-order agnostic for our purposes
#endif

#endif
    m_spDecoder = std::make_shared<CContentDecoder>(
        (uint32_t)abs(g_Settings()->Get("settings.player.BufferLength", 25)),
        pf);
    m_spImageRef = std::make_shared<DisplayOutput::CImage>();
    m_spFrameGeneration = std::make_unique<FrameGeneration::CFrameGenerationScheduler>();
    m_PresentationFps = m_ClipMetadata.decodeFps;

#ifdef LINUX_GNU
    m_FrameGenerationMode = FrameGeneration::FromSetting(
        g_Settings()->Get("settings.player.frame_generation.mode",
                          FrameGeneration::ToSetting(FrameGeneration::EFrameGenerationMode::Off)));
    const double requestedOutputFps =
        g_Settings()->Get("settings.player.frame_generation.output_fps", 40.0);
    const double displayRefreshFps =
        g_Settings()->Get("settings.player.display_fps", 60.0);
    const double selectedOutputFps = SelectFrameGenerationTargetFps(
        m_ClipMetadata.decodeFps, requestedOutputFps, displayRefreshFps);

    FrameGeneration::spIFrameInterpolator interpolator;
    if (m_FrameGenerationMode != FrameGeneration::EFrameGenerationMode::Off)
    {
        if (m_FrameGenerationMode == FrameGeneration::EFrameGenerationMode::RIFE)
        {
            g_Log->Info("Clip %s requested RIFE backend (compiled=%s model=%s)",
                        m_ClipMetadata.dreamData.uuid.c_str(),
                        FrameGeneration::CRifeInterpolatorNcnn::BuiltWithSupport() ? "yes" : "no",
                        FrameGeneration::CRifeInterpolatorNcnn::RuntimeModelName());
        }

        switch (m_FrameGenerationMode)
        {
        case FrameGeneration::EFrameGenerationMode::Blend2X:
            interpolator = std::make_shared<FrameGeneration::CBlendFrameInterpolator>();
            break;
        case FrameGeneration::EFrameGenerationMode::RIFE:
            interpolator = std::make_shared<FrameGeneration::CRifeInterpolatorNcnn>();
            break;
        case FrameGeneration::EFrameGenerationMode::Off:
            break;
        }
    }

    if (interpolator)
    {
        std::string reason;
        if (interpolator->IsAvailable(&reason))
        {
            m_spFrameGeneration->Configure(true, m_ClipMetadata.decodeFps,
                                           selectedOutputFps, interpolator);
            m_PresentationFps = m_spFrameGeneration->PresentationFps();
            g_Log->Info("Frame generation enabled for %s using %s (source=%.2f requested=%.2f display=%.2f selected=%.2f fps)",
                        m_ClipMetadata.dreamData.uuid.c_str(),
                        interpolator->Name(),
                        m_ClipMetadata.decodeFps,
                        requestedOutputFps,
                        displayRefreshFps,
                        m_PresentationFps);
        }
        else
        {
            g_Log->Info("Frame generation unavailable for %s using %s: %s",
                        m_ClipMetadata.dreamData.uuid.c_str(),
                        interpolator->Name(),
                        reason.c_str());

            if (m_FrameGenerationMode == FrameGeneration::EFrameGenerationMode::RIFE)
            {
                auto fallbackInterpolator = std::make_shared<FrameGeneration::CBlendFrameInterpolator>();
                std::string fallbackReason;
                if (fallbackInterpolator->IsAvailable(&fallbackReason))
                {
                    m_spFrameGeneration->Configure(true, m_ClipMetadata.decodeFps,
                                                   selectedOutputFps, fallbackInterpolator);
                    m_PresentationFps = m_spFrameGeneration->PresentationFps();
                    g_Log->Info("Falling back to %s for %s (source=%.2f requested=%.2f display=%.2f selected=%.2f fps)",
                                fallbackInterpolator->Name(),
                                m_ClipMetadata.dreamData.uuid.c_str(),
                                m_ClipMetadata.decodeFps,
                                requestedOutputFps,
                                displayRefreshFps,
                                m_PresentationFps);
                }
            }
        }
    }

    g_Log->Info("Clip %s frame generation state: enabled=%s mode=%s decode=%.2f presentation=%.2f",
                m_ClipMetadata.dreamData.uuid.c_str(),
                IsFrameGenerationEnabled() ? "true" : "false",
                GetFrameGenerationMode().c_str(),
                m_ClipMetadata.decodeFps,
                GetPresentationFps());
#endif
}

bool CClip::Start(int64_t _seekFrame)
{
/*    m_DecoderClock = {};
    return m_spDecoder->Start(m_ClipMetadata, _seekFrame);*/
    
    // Reset buffering state and timing values
    m_BufferingState = BufferingState::Buffering;
    m_RequestedStartTime = m_StartTime;
    m_ActualStartTime = 0.0;
    m_TotalBufferingTime = 0.0;
    m_HasStartedPlaying = false;
    
    g_Log->Info("Starting clip %s in buffering mode (seek: %d), waiting for frames...",
                m_ClipMetadata.dreamData.uuid.c_str(), _seekFrame);

    // First preload the clip
    if (!Preload(_seekFrame)) {
        return false;
    }
    
    // Then start playback
    //return StartPlayback(_seekFrame);
    
    // Just initialize the decoder and start filling the frame queue
    return m_spDecoder->Start(m_ClipMetadata, _seekFrame);
}

void CClip::Stop() { m_spDecoder->Stop(); }

bool CClip::Preload(int64_t _seekFrame)
{
    g_Log->Info("Starting preloading %s at frame %d", m_ClipMetadata.path.c_str(), _seekFrame);
    g_Log->Info("Dream: %s by %s", m_ClipMetadata.dreamData.name.c_str(), m_ClipMetadata.dreamData.artist.c_str());
 
    // Reset flags
    m_HasFinished.exchange(false);
    m_IsFadingOut.exchange(false);
    m_IsPreloaded = false;
    
    // Initialize the decoder without starting playback
    if (!m_spDecoder->Start(m_ClipMetadata, _seekFrame)) {
        g_Log->Error("Failed to initialize decoder for %s", m_ClipMetadata.path.c_str());
        return false;
    }
    
    m_IsPreloaded = true;
    return true;
}

bool CClip::IsPreloadComplete() const {
    if (!m_IsPreloaded || !m_spDecoder) {
        return false;
    }
    
    // Require a minimum number of frames to consider preloading complete
    uint32_t queueLength = m_spDecoder->QueueLength();
    uint32_t minFramesRequired = 10;  // Require at least 10 frames
    
    bool complete = queueLength >= minFramesRequired;
    
    /*if (complete && (queueLength < 25)) {
        g_Log->Info("Clip %s preload complete with %d frames",
                  m_ClipMetadata.dreamData.uuid.c_str(), queueLength);
    }*/
    
    return complete;
}

bool CClip::StartPlayback(int64_t _seekFrame)
{
    if (!m_IsPreloaded) {
        g_Log->Error("Cannot start playback - clip not preloaded");
        return false;
    }
    
    m_DecoderClock = {};
    m_DecoderClock.started = false;
    ResetFrameGeneration();

/*    // Initialize clock to actual start time to avoid frame skipping
    m_DecoderClock.clock = m_ActualStartTime;
    m_DecoderClock.started = true;  // Mark as started so first frame doesn't skip timing*/
    m_DecoderClock.acc = 0.0;
    m_DecoderClock.interframeDelta = 0.0;
    g_Log->Info("Start playback is reseting decoder clock");
    m_HasStartedPlaying = true;

    return true;
    //return m_spDecoder->Start(m_ClipMetadata, _seekFrame);
}



//    Calculate how many frames we need to advance based on elapsed time
//    Returns: 0 = no new frame needed, 1+ = number of frames to advance
int CClip::GetFramesToAdvance(double _timelineTime,
                              DecoderClock* _decoderClock) const
{
    double deltaTime = _timelineTime - _decoderClock->clock;
    
    // Detect large time gaps (like clip transitions) and reset timing
    if (deltaTime > 1.0 && _decoderClock->started) {
        g_Log->Info("Large time gap detected (%.6f seconds), resetting decoder clock", deltaTime);
        _decoderClock->started = false;
        _decoderClock->acc = 0.0;
        deltaTime = 0.0;
    }
    
    _decoderClock->clock = _timelineTime;
    if (!_decoderClock->started)
    {
        _decoderClock->started = true;
        _decoderClock->acc = 0.0;  // Initialize accumulator
        g_Log->Info("First frame timing - reinit acc and force grab");
        return 1;  // Need first frame
    }
    _decoderClock->acc += deltaTime;

    const double fps = (m_PresentationFps > 0.0) ? m_PresentationFps : m_ClipMetadata.decodeFps;
    const double dt = 1.0 / fps;
    
    // Calculate how many complete frame intervals have passed
    int framesToAdvance = static_cast<int>(_decoderClock->acc / dt);
    
    // Keep only the fractional remainder
    _decoderClock->acc = std::fmod(_decoderClock->acc, dt);

    // This is our inter-frame delta, > 0 < 1
    _decoderClock->interframeDelta = _decoderClock->acc / dt;

    if (framesToAdvance > 1) {
        g_Log->Info("Frame timing catch-up: advancing %d frames (deltaTime: %.4f, dt: %.4f)",
                    framesToAdvance, deltaTime, dt);
    }

    return framesToAdvance;
}

void CClip::DiscardFrames(int count)
{
    for (int i = 0; i < count; ++i) {
        spCVideoFrame frame = m_spDecoder->PopVideoFrame();
        if (!frame) {
            // No more frames in queue, stop discarding
            g_Log->Info("DiscardFrames: only discarded %d of %d requested (queue empty)", i, count);
            break;
        }
        // Frame is automatically released when shared_ptr goes out of scope
    }
}

bool CClip::Update(double _timelineTime, bool isPaused)
{
    m_Alpha = m_LastCalculatedAlpha;
    
    // Check buffering state
    if (m_BufferingState != BufferingState::NotBuffering) {
        uint32_t queueLength = m_spDecoder->QueueLength();
        bool decoderEnded = m_spDecoder->DecoderThreadEnded();  // Check if decoder thread finished, not if all frames consumed
        
        // Check if we have enough frames to start/resume playback
        // OR if the decoder has ended (meaning no more frames will arrive)
        if (m_BufferingState == BufferingState::Buffering && (queueLength >= 10 || (decoderEnded && queueLength > 0))) {
            // Initial buffer filled, start playback
            if (decoderEnded && queueLength < 10) {
                g_Log->Info("Decoder ended with only %d frames (less than target 10), starting playback anyway for %s",
                            queueLength, m_ClipMetadata.dreamData.uuid.c_str());
            } else {
                g_Log->Info("Initial buffer filled (%d frames), starting playback for %s",
                            queueLength, m_ClipMetadata.dreamData.uuid.c_str());
            }
            m_BufferingState = BufferingState::NotBuffering;
            
            // Set the actual start time when playback begins
            if (!m_HasStartedPlaying) {
                m_ActualStartTime = _timelineTime;
                m_HasStartedPlaying = true;
                
                // Update the clip's timeline position
                SetStartTime(m_ActualStartTime);
                
                // Start actual playback
                StartPlayback(m_spDecoder->GetVideoInfo()->m_SeekTargetFrame);
            }
        }
        else if (m_BufferingState == BufferingState::Rebuffering && (queueLength >= 5 || (decoderEnded && queueLength > 0))) {
            // Rebuffer filled, resume playback
            if (decoderEnded && queueLength < 5) {
                g_Log->Info("Decoder ended with only %d frames (less than target 5), resuming playback anyway for %s",
                            queueLength, m_ClipMetadata.dreamData.uuid.c_str());
            } else {
                g_Log->Info("Buffer refilled (%d frames), resuming playback for %s",
                            queueLength, m_ClipMetadata.dreamData.uuid.c_str());
            }
            
            // Track how long we were buffering
            double bufferingDuration = _timelineTime - m_RebufferingStartTime;
            m_TotalBufferingTime += bufferingDuration;
            
            m_BufferingState = BufferingState::NotBuffering;
        }
        else if (decoderEnded && queueLength == 0) {
            // Decoder ended and no frames available - clip is finished
            g_Log->Info("Decoder ended with no frames available for %s, marking as finished",
                        m_ClipMetadata.dreamData.uuid.c_str());
            m_HasFinished.exchange(true);
            return false;
        }
        else {
            // Still buffering, don't update the frame
            return true;    // Return true so player knows we're still active
        }
    }
    
    // Check if we need to rebuffer (unless we're near the end or decoder has ended)
    bool nearEnd = IsNearEnd();
    bool decoderEnded = m_spDecoder->DecoderThreadEnded();
    
    // Only check for rebuffering if decoder is still running
    // If decoder has ended, we just play whatever frames we have left
    if (!decoderEnded && m_spDecoder->QueueLength() < 2) {
        // Log state for debugging
        /*g_Log->Info("Buffer low check: nearEnd=%d, queue=%d, for %s",
                  nearEnd ? 1 : 0,
                  m_spDecoder->QueueLength(), m_ClipMetadata.dreamData.uuid.c_str());
        */
        // During transitions, we're more conservative about what we consider "near end"
        if (!nearEnd) {
            g_Log->Info("Buffer too low (%d frames), entering rebuffering state for %s",
                      m_spDecoder->QueueLength(), m_ClipMetadata.dreamData.uuid.c_str());
            m_BufferingState = BufferingState::Rebuffering;
            m_RebufferingStartTime = _timelineTime;
            return true;
        }
    }

    if (_timelineTime < m_StartTime) {
        return false;
    }
    
    int framesToAdvance = GetFramesToAdvance(_timelineTime, &m_DecoderClock);
    
    if (framesToAdvance > 0)
    {
        if (m_spFrameGeneration && m_spFrameGeneration->Enabled())
        {
            for (int step = 0; step < framesToAdvance; ++step)
            {
                if (!AdvanceFrameGenerationPlayback())
                {
                    if (m_LastValidFrame && IsNearEnd())
                    {
                        m_spFrameData = m_LastValidFrame;
                        break;
                    }
                    return false;
                }
            }
        }
        else
        {
            // If we need to advance multiple frames, discard the intermediate ones
            // Only grab (and process/upload to GPU) the last frame we need
            if (framesToAdvance > 1) {
                int framesToDiscard = framesToAdvance - 1;
                
                // Don't discard more frames than we have in the queue
                uint32_t queueLength = m_spDecoder->QueueLength();
                if (framesToDiscard >= static_cast<int>(queueLength)) {
                    // We're very far behind - discard all but one frame
                    framesToDiscard = std::max(0, static_cast<int>(queueLength) - 1);
                }
                
                if (framesToDiscard > 0) {
                    DiscardFrames(framesToDiscard);
                }
            }
            
            // Now grab the actual frame we want to display
            if (!GrabVideoFrame())
            {
                // Check if we're at the last frame and should mark as finished
                if (m_CurrentFrameMetadata.maxFrameIdx > 0 &&
                    m_CurrentFrameMetadata.frameIdx >= m_CurrentFrameMetadata.maxFrameIdx)
                {
                    g_Log->Info("marking dream %s as finished", m_ClipMetadata.dreamData.uuid.c_str());
                    
                    if (m_FadeOutSeconds == 0.f)
                        m_Alpha = 1.f;
                    
                    m_HasFinished.exchange(true);
                    m_IsFadingOut.exchange(false);
                    
                    return false;
                }
                // If we're near the end, don't fail as we used to (this may no longer be needed)
                else if (m_LastValidFrame && IsNearEnd())
                {
                    g_Log->Info("Reusing last valid, faking increment count");
                    // Just keep using the last valid frame
                    m_spFrameData = m_LastValidFrame;
                    m_CurrentFrameMetadata.frameIdx++;
                }
                else
                {
                    return false;
                }
            }
            m_DisplayFramePhase = 0.0;
        }
    }
    
    // temporarily needed by basic per frame renderer at startup, we should avoid this path in the future
    // Cubic/linear don't need this
    if (m_spFrameData == nullptr)
        return false;

    // Triple check that we have a frame to display. Seems like an issue on WIN32 somehow ?
    if (m_spFrameData == nullptr)
	{
        g_Log->Error("GrabVideoFrame() returned true but no frame data available?");
		return false;
	}

    uint32_t idx = m_spFrameData->GetMetaData().frameIdx;
    uint32_t maxIdx = m_spFrameData->GetMetaData().maxFrameIdx;
    const double displayOffset = m_DisplayFramePhase / m_ClipMetadata.decodeFps;
    double delta = m_DecoderClock.interframeDelta / ((m_PresentationFps > 0.0) ? m_PresentationFps : m_ClipMetadata.decodeFps);
    
    // Calculate secondsIn based on timeline for resume cases
    double secondsIn;
    if (m_IsResume) {
        secondsIn = _timelineTime - m_ResumeStartTime;
    } else {
        // We no longer take into account StartAtFrame from FrameDisplay
        secondsIn = idx / m_ClipMetadata.decodeFps + displayOffset + delta;
    }
    
    // Calculate remaining time based on actual video frames
    // This is more accurate than using m_EndTime which may have been set
    // before the decoder knew the correct frame count
    double secondsOut = (maxIdx - idx) / m_ClipMetadata.decodeFps - displayOffset - delta;
    secondsOut = std::fmin(secondsOut, (m_EndTime - _timelineTime));

    if (m_FadeOutSeconds > 0 && secondsOut > 0 && secondsOut < m_FadeOutSeconds) {
        if (!m_IsFadingOut.load()) {
            g_Log->Info("Fade out started for %s: %.1f seconds remaining, idx=%u/%u",
                        m_ClipMetadata.dreamData.uuid.c_str(), secondsOut, idx, maxIdx);
        }
        m_IsFadingOut.exchange(true);
    }

    if (isPaused) {
        // If we're paused, use the last calculated alpha value
        m_Alpha = m_LastCalculatedAlpha;
        return true;
    }
    
    if (m_FadeOutSeconds > 0)
    {
        float fadeInFactor = (float)std::fmin(secondsIn / m_FadeInSeconds, 1.f);
        float fadeOutFactor = (float)std::fmin(secondsOut / m_FadeOutSeconds, 1.f);
        float alpha = fadeInFactor * fadeOutFactor;

        if (secondsOut <= 0)
        {
            m_HasFinished.exchange(true);
            return false;
        }
        m_Alpha = alpha;
        m_LastCalculatedAlpha = m_Alpha;  // Store for pause state
    } else {
        m_Alpha = 1.f;
        m_LastCalculatedAlpha = m_Alpha;  // Store for pause state
    }

    return true;
}

bool CClip::DrawFrame(spCRenderer _spRenderer, float alpha) {
    // Use passed alpha if valid (>= 0), otherwise fall back to internal m_Alpha
    float effectiveAlpha = (alpha >= 0.0f) ? alpha : m_Alpha;
    
    if (m_BufferingState == BufferingState::Buffering) {
        // Could display a loading indicator here
        //g_Log->Info("Buffering, nothing to display yet (ql: %d)", m_spDecoder->QueueLength());
        return false; // Nothing to draw yet
    }
    
    // If we're buffering, draw the last valid frame again
    if (IsBuffering()) {
        if (m_LastValidFrame) {
            // Use the last valid frame while buffering
            m_spFrameData = m_LastValidFrame;
            return m_spFrameDisplay->Draw(_spRenderer, effectiveAlpha, m_DecoderClock.interframeDelta);
        }
        return false; // No frame yet
    }
    
    if (!m_spFrameData)
        return false;
    return m_spFrameDisplay->Draw(_spRenderer, effectiveAlpha, m_DecoderClock.interframeDelta);
}

void CClip::SetDisplaySize(uint32_t _displayWidth, uint32_t _displayHeight)
{
    m_spFrameDisplay->SetDisplaySize(_displayWidth, _displayHeight);
}

bool CClip::GrabVideoFrame()
{
    spCVideoFrame frame;
    if (!PopDecoderFrame(frame))
        return false;

    m_DisplayFramePhase = 0.0;
    return UploadFrameToTexture(frame);
}

bool CClip::IsNearEnd() const
{
    if (!m_CurrentFrameMetadata.maxFrameIdx) return false;
    
    uint32_t framesRemaining = m_CurrentFrameMetadata.maxFrameIdx - m_CurrentFrameMetadata.frameIdx;
    return framesRemaining < 50; // We might adjust that, good start point. might need push to 25+
}

uint32_t CClip::GetCurrentFrameIdx() const
{
    std::shared_lock<std::shared_mutex> lock(m_CurrentFrameMetadataLock);
    return m_CurrentFrameMetadata.frameIdx;
}

const sFrameMetadata& CClip::GetCurrentFrameMetadata() const
{
    std::shared_lock<std::shared_mutex> lock(m_CurrentFrameMetadataLock);
    return m_CurrentFrameMetadata;
}

uint32_t CClip::GetFrameCount() const
{
    return m_spDecoder->GetVideoInfo()->m_TotalFrameCount;
}

void CClip::SetStartTime(double _startTime)
{
    m_StartTime = _startTime;
    m_EndTime = _startTime + GetLength();
}

void CClip::FadeOut(double _currentTimelineTime)
{
    m_EndTime = _currentTimelineTime + m_FadeOutSeconds;
}

void CClip::UpdatePlaybackRate(double _newFps, double _currentTimelineTime)
{
    m_ClipMetadata.decodeFps = _newFps;
    m_PresentationFps = (m_spFrameGeneration && m_spFrameGeneration->Enabled())
        ? _newFps * 2.0
        : _newFps;

    // If the clip is already fading out, FadeOut() has deliberately pinned
    // m_EndTime to a "stop N seconds from now" deadline; don't un-end it.
    if (m_IsFadingOut.load())
        return;

    uint32_t idx = 0;
    uint32_t maxIdx = 0;
    {
        std::shared_lock<std::shared_mutex> lock(m_CurrentFrameMetadataLock);
        idx = m_CurrentFrameMetadata.frameIdx;
        maxIdx = m_CurrentFrameMetadata.maxFrameIdx;
    }

    // Decoder hasn't populated frame metadata yet — SetStartTime will handle
    // m_EndTime once the clip is actually started.
    if (maxIdx == 0 || _newFps <= 0.0)
        return;

    double remainingFrames = (maxIdx > idx) ? static_cast<double>(maxIdx - idx) : 0.0;
    m_EndTime = _currentTimelineTime + remainingFrames / _newFps;
}

void CClip::SkipTime(float _secondsForward)
{
    // Pass the displayed frame index to the decoder so it uses the correct base
    // for calculating the seek target, rather than its internal decoder index
    int64_t displayedFrame = static_cast<int64_t>(m_CurrentFrameMetadata.frameIdx);
    
    g_Log->Info("SkipTime: displayed frame %lld, skip %f seconds in clip %s",
                displayedFrame, _secondsForward, m_ClipMetadata.dreamData.uuid.c_str());
    
    m_spDecoder->SkipTime(_secondsForward, displayedFrame);
    m_DecoderClock.started = false;
    ResetFrameGeneration();
    
    // If we were in buffering state, reset it to ensure proper rebuffering
    // This matters on successive skips
    if (m_BufferingState == BufferingState::Rebuffering) {
        m_BufferingState = BufferingState::NotBuffering;
    }
}

bool CClip::PopDecoderFrame(spCVideoFrame& frame)
{
    frame = m_spDecoder->PopVideoFrame();
    if (!frame) {
        g_Log->Info("GrabVideoFrame() - No frame available, returning false");
        return false;
    }

    return true;
}

bool CClip::UploadFrameToTexture(const spCVideoFrame& frame)
{
    if (!frame)
        return false;

    m_spFrameData = frame;
    m_LastValidFrame = frame;

    {
        std::unique_lock<std::shared_mutex> lock(m_CurrentFrameMetadataLock);
        m_CurrentFrameMetadata = m_spFrameData->GetMetaData();
    }

#if !USE_HW_ACCELERATION || defined(WIN32)
    if (m_spImageRef->GetWidth() != m_spFrameData->Width() ||
        m_spImageRef->GetHeight() != m_spFrameData->Height())
    {
        m_spImageRef->Create(m_spFrameData->Width(),
                             m_spFrameData->Height(),
                             DisplayOutput::eImage_RGBA8, false, true);
    }
#endif

    spCTextureFlat& currentTexture = m_spFrameDisplay->RequestTargetTexture();
    if (!currentTexture)
        currentTexture = m_spRenderer->NewTextureFlat();

    if (!currentTexture)
        return false;

    if (!m_spFrameData->Frame())
        return false;

#if USE_HW_ACCELERATION && !defined(WIN32)
    currentTexture->BindFrame(m_spFrameData);
#else
    m_spImageRef->SetStorageBuffer(m_spFrameData->StorageBuffer());
    currentTexture->Upload(m_spImageRef);
#endif

    return true;
}

bool CClip::AdvanceFrameGenerationPlayback()
{
    if (!m_spFrameGeneration || !m_spFrameGeneration->Enabled())
        return GrabVideoFrame();

    const bool advanced = m_spFrameGeneration->Advance(
        [this]() -> spCVideoFrame {
            spCVideoFrame decoded;
            if (!PopDecoderFrame(decoded))
                return nullptr;
            return decoded;
        });

    if (!advanced)
    {
        if (m_CurrentFrameMetadata.maxFrameIdx > 0 &&
            m_CurrentFrameMetadata.frameIdx >= m_CurrentFrameMetadata.maxFrameIdx)
        {
            m_HasFinished.exchange(true);
        }
        return false;
    }

    m_DisplayFramePhase = m_spFrameGeneration->CurrentDisplayPhase();
    if (m_DisplayFramePhase > 0.0 && (m_spFrameGeneration->GeneratedFrameCount() == 1 ||
        (m_spFrameGeneration->GeneratedFrameCount() % 300) == 0))
    {
        g_Log->Info("Frame generation active for %s: generated=%llu real=%llu output=%.2f fps mode=%s avg=%.2fms last=%.2fms failures=%llu",
                    m_ClipMetadata.dreamData.uuid.c_str(),
                    static_cast<unsigned long long>(m_spFrameGeneration->GeneratedFrameCount()),
                    static_cast<unsigned long long>(m_spFrameGeneration->RealFrameCount()),
                    GetPresentationFps(),
                    GetFrameGenerationMode().c_str(),
                    m_spFrameGeneration->InterpolatorAverageTimeMs(),
                    m_spFrameGeneration->InterpolatorLastTimeMs(),
                    static_cast<unsigned long long>(m_spFrameGeneration->InterpolatorFailureCount()));
    }
    return UploadFrameToTexture(m_spFrameGeneration->CurrentDisplayFrame());
}

void CClip::ResetFrameGeneration()
{
    m_DisplayFramePhase = 0.0;
    if (m_spFrameGeneration)
        m_spFrameGeneration->Reset();
}

bool CClip::IsFrameGenerationEnabled() const
{
    return m_spFrameGeneration && m_spFrameGeneration->Enabled();
}

std::string CClip::GetFrameGenerationMode() const
{
    if (m_FrameGenerationMode == FrameGeneration::EFrameGenerationMode::RIFE &&
        m_spFrameGeneration && m_spFrameGeneration->Enabled() &&
        (m_spFrameGeneration->ModeName() == "blend_2x" ||
         m_spFrameGeneration->InterpolatorFallingBack()))
    {
        return "RIFE (Blend_2X fallback)";
    }
    return FrameGeneration::ToString(m_FrameGenerationMode);
}

uint64_t CClip::GetGeneratedFrameCount() const
{
    if (!m_spFrameGeneration)
        return 0;
    return m_spFrameGeneration->GeneratedFrameCount();
}

uint64_t CClip::GetPresentedRealFrameCount() const
{
    if (!m_spFrameGeneration)
        return 0;
    return m_spFrameGeneration->RealFrameCount();
}

double CClip::GetFrameGenerationLastTimeMs() const
{
    if (!m_spFrameGeneration)
        return 0.0;
    return m_spFrameGeneration->InterpolatorLastTimeMs();
}

double CClip::GetFrameGenerationAverageTimeMs() const
{
    if (!m_spFrameGeneration)
        return 0.0;
    return m_spFrameGeneration->InterpolatorAverageTimeMs();
}

} // namespace ContentDecoder
