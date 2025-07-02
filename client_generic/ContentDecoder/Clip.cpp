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

namespace ContentDecoder
{

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
    AVPixelFormat pf = AV_PIX_FMT_RGB32;

    // On PowerPC machines we need to use different pixel format!
#if defined(MAC) && defined(__BIG_ENDIAN__)
    pf = AV_PIX_FMT_BGR32_1;
#endif

#else

    AVPixelFormat pf = AV_PIX_FMT_BGR32;
#if defined(__BIG_ENDIAN__)
    pf = AV_PIX_FMT_RGB32_1;
#endif

#endif
    m_spDecoder = std::make_shared<CContentDecoder>(
        (uint32_t)abs(g_Settings()->Get("settings.player.BufferLength", 25)),
        pf);
    m_spImageRef = std::make_shared<DisplayOutput::CImage>();
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
    g_Log->Info("Starting preloading %s at %d", m_ClipMetadata.path.c_str(), _seekFrame);
 
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



//    Do some math to figure out the delta between frames...
bool CClip::NeedsNewFrame(double _timelineTime,
                          DecoderClock* _decoderClock) const
{
   /* g_Log->Info("Needs new frame : %d %d",m_CurrentFrameMetadata.frameIdx, m_spFrameDisplay->StartAtFrame());
    
    if (m_CurrentFrameMetadata.frameIdx < m_spFrameDisplay->StartAtFrame())
        return true;
*/
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
        //deltaTime = 0.0167;
        return true;
    }
    _decoderClock->acc += deltaTime;

    const double dt = 1.0 / m_ClipMetadata.decodeFps;
    bool bCrossedFrame = false;

    //    Accumulated time is longer than the requested framerate, we
    // crossed over
    // to the next frame
    if (_decoderClock->acc >= dt)
        bCrossedFrame = true;

    _decoderClock->acc = std::fmod(_decoderClock->acc, dt);

    //    This is our inter-frame delta, > 0 < 1 <
    _decoderClock->interframeDelta = _decoderClock->acc / dt;

    /*g_Log->Info("Frame timing - deltaTime: %.6f, acc: %.6f, dt: %.6f (%.1ffps), crossedFrame: %s",
                deltaTime, _decoderClock->acc, dt, m_ClipMetadata.decodeFps, bCrossedFrame ? "YES" : "NO");*/

    return bCrossedFrame;
}

bool CClip::Update(double _timelineTime, bool isPaused)
{
    //g_Log->Info("Update for %s", m_ClipMetadata.dreamData.uuid.c_str());
    m_Alpha = m_LastCalculatedAlpha;
    
    // Check buffering state
    if (m_BufferingState != BufferingState::NotBuffering) {
        uint32_t queueLength = m_spDecoder->QueueLength();
        
        // Check if we have enough frames to start/resume playback
        if (m_BufferingState == BufferingState::Buffering && queueLength >= 10) {
            // Initial buffer filled, start playback
            g_Log->Info("Initial buffer filled (%d frames), starting playback for %s",
                        queueLength, m_ClipMetadata.dreamData.uuid.c_str());
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
        else if (m_BufferingState == BufferingState::Rebuffering && queueLength >= 5) {
            // Rebuffer filled, resume playback
            g_Log->Info("Buffer refilled (%d frames), resuming playback for %s",
                        queueLength, m_ClipMetadata.dreamData.uuid.c_str());
            
            // Track how long we were buffering
            double bufferingDuration = _timelineTime - m_RebufferingStartTime;
            m_TotalBufferingTime += bufferingDuration;
            
            m_BufferingState = BufferingState::NotBuffering;
        }
        else {
            // Still buffering, don't update the frame
            return true;    // Return true so player knows we're still active
        }
    }
    
    // Check if we need to rebuffer (unless we're near the end)
    bool nearEnd = IsNearEnd();
    
    if (m_spDecoder->QueueLength() < 2) {
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
    
    bool needsFrame = NeedsNewFrame(_timelineTime, &m_DecoderClock);
    /*g_Log->Info("Update() - NeedsNewFrame returned: %s, current frame: %d",
                needsFrame ? "YES" : "NO", m_CurrentFrameMetadata.frameIdx); */
    if (needsFrame)
    {
        //g_Log->Info("About to call GrabVideoFrame()");
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
    }
    
    // temporarily needed by basic per frame renderer at startup, we should avoid this path in the future
    // Cubic/linear don't need this
    if (m_spFrameData == nullptr)
        return false;

    uint32_t idx = m_spFrameData->GetMetaData().frameIdx;
    uint32_t maxIdx = m_spFrameData->GetMetaData().maxFrameIdx;
    double delta = m_DecoderClock.interframeDelta / m_ClipMetadata.decodeFps;
    
    // Calculate secondsIn based on timeline for resume cases
    double secondsIn;
    if (m_IsResume) {
        secondsIn = _timelineTime - m_ResumeStartTime;
    } else {
        // We no longer take into account StartAtFrame from FrameDisplay
        secondsIn = idx / m_ClipMetadata.decodeFps + delta;
    }
    
    double secondsOut = (maxIdx - idx) / m_ClipMetadata.decodeFps - delta;
    secondsOut = std::fmin(secondsOut, (m_EndTime - _timelineTime));
    
    if (m_FadeOutSeconds > 0 && secondsOut > 0 && secondsOut < m_FadeOutSeconds) {
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
            return m_spFrameDisplay->Draw(_spRenderer, m_Alpha, m_DecoderClock.interframeDelta);
        }
        return false; // No frame yet
    }
    
    if (!m_spFrameData)
        return false;
    return m_spFrameDisplay->Draw(_spRenderer, m_Alpha, m_DecoderClock.interframeDelta);
}

void CClip::SetDisplaySize(uint32_t _displayWidth, uint32_t _displayHeight)
{
    m_spFrameDisplay->SetDisplaySize(_displayWidth, _displayHeight);
}

bool CClip::GrabVideoFrame()
{
    /*g_Log->Info("GrabVideoFrame() - Attempting to pop frame from queue (size: %d)",
                m_spDecoder->QueueLength());
    */
    spCVideoFrame frame = m_spDecoder->PopVideoFrame();
    if (!frame) {
        g_Log->Info("GrabVideoFrame() - No frame available, returning false");
        return false;
    }
    
    if (frame)
    {
        m_spFrameData = frame;

        // Store this as our last valid frame
        m_LastValidFrame = frame;
        
        {
            std::unique_lock<std::shared_mutex> lock(
                m_CurrentFrameMetadataLock);
            m_CurrentFrameMetadata = m_spFrameData->GetMetaData();
            
            /*g_Log->Info("GrabVideoFrame() - Successfully grabbed frame %d",
                        m_CurrentFrameMetadata.frameIdx);*/
        }
#if !USE_HW_ACCELERATION
        if (m_spImageRef->GetWidth() != m_spFrameData->Width() ||
            m_spImageRef->GetHeight() != m_spFrameData->Height())
        {
            //    Frame differs in size, recreate ref image.
            m_spImageRef->Create(m_spFrameData->Width(),
                                 m_spFrameData->Height(),
                                 DisplayOutput::eImage_RGBA8, false, true);
        }
#endif

        spCTextureFlat& currentTexture =
            m_spFrameDisplay->RequestTargetTexture();
        if (!currentTexture)
            currentTexture = m_spRenderer->NewTextureFlat();

        if (!currentTexture)
            return false;
        if (m_spFrameData->Frame())
        {
            if (USE_HW_ACCELERATION)
            {
                //g_Log->Info("BindFrame %d", m_CurrentFrameMetadata.frameIdx);
                currentTexture->BindFrame(m_spFrameData);
            }
            else
            {
                //    Set image texturedata and upload to texture.
                m_spImageRef->SetStorageBuffer(m_spFrameData->StorageBuffer());
                currentTexture->Upload(m_spImageRef);
            }
        }
    }

    return true;
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

void CClip::SkipTime(float _secondsForward)
{
    m_spDecoder->SkipTime(_secondsForward);
    m_DecoderClock.started = false;
    
    // If we were in buffering state, reset it to ensure proper rebuffering
    // This matters on successive skips
    if (m_BufferingState == BufferingState::Rebuffering) {
        m_BufferingState = BufferingState::NotBuffering;
    }

    g_Log->Info("Skipped %f seconds in clip %s", _secondsForward,
                m_ClipMetadata.dreamData.uuid.c_str());
}

} // namespace ContentDecoder
