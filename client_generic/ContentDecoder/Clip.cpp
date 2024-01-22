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
             uint32_t _displayHeight, float _decodeFps)
    : m_ClipMetadata(_metadata), m_spRenderer(_spRenderer),
      m_CurrentFrameMetadata{}, m_DecodeFps(_decodeFps), m_HasFinished(false)
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
    m_DecoderClock = {};
    return m_spDecoder->Start(m_ClipMetadata.path, _seekFrame);
}

void CClip::Stop()
{
    m_spDecoder->Stop();
}

//    Do some math to figure out the delta between frames...
bool CClip::NeedsNewFrame(DecoderClock* _decoderClock) const
{
    if (m_CurrentFrameMetadata.frameIdx < m_spFrameDisplay->StartAtFrame())
        return true;

    double newTime = m_Timer.Time();
    double deltaTime = newTime - _decoderClock->clock;
    _decoderClock->clock = newTime;
    if (!_decoderClock->started)
    {
        _decoderClock->started = true;
        return true;
    }
    _decoderClock->acc += deltaTime;

    const double dt = 1.0 / m_DecodeFps;
    bool bCrossedFrame = false;

    //    Accumulated time is longer than the requested framerate, we
    // crossed over
    // to the next frame
    if (_decoderClock->acc >= dt)
        bCrossedFrame = true;

    _decoderClock->acc = std::fmod(_decoderClock->acc, dt);

    //    This is our inter-frame delta, > 0 < 1 <
    _decoderClock->interframeDelta = _decoderClock->acc / dt;

    return bCrossedFrame;
}

bool CClip::Update(double _timelineTime)
{
    m_Alpha = 0.f;
    if (_timelineTime > m_EndTime)
    {
        m_HasFinished.exchange(true);
        return false;
    }
    if (_timelineTime < m_StartTime)
        return false;
    if (NeedsNewFrame(&m_DecoderClock))
    {
        if (!GrabVideoFrame())
        {
            m_HasFinished.exchange(true);
            return false;
        }
    }

    uint32_t idx = m_spFrameData->GetMetaData().frameIdx;
    uint32_t maxIdx = m_spFrameData->GetMetaData().maxFrameIdx;
    double delta = m_DecoderClock.interframeDelta / m_DecodeFps;
    double secondsIn =
        std::fmax((int)idx - (int)m_spFrameDisplay->StartAtFrame(), 0) /
            m_DecodeFps +
        delta;
    double secondsOut = (maxIdx - idx) / m_DecodeFps - delta;
    secondsOut = std::fmin(secondsOut, (m_EndTime - _timelineTime));
    float alpha = (float)std::fmin(secondsIn / m_FadeInSeconds, 1.f) *
                  (float)std::fmin(secondsOut / m_FadeOutSeconds, 1.f);
    if (secondsOut <= 0)
    {
        m_HasFinished.exchange(true);
        return false;
    }
    m_Alpha = alpha;
    return true;
}

bool CClip::DrawFrame(spCRenderer _spRenderer)
{
    if (!m_spFrameData)
        return false;
    return m_spFrameDisplay->Draw(_spRenderer, m_Alpha,
                                  m_DecoderClock.interframeDelta);
}

void CClip::SetDisplaySize(uint32_t _displayWidth, uint32_t _displayHeight)
{
    m_spFrameDisplay->SetDisplaySize(_displayWidth, _displayHeight);
}

bool CClip::GrabVideoFrame()
{
    spCVideoFrame frame = m_spDecoder->PopVideoFrame();
    if (frame)
    {
        m_spFrameData = frame;
        {
            boost::unique_lock<boost::shared_mutex> lock(
                m_CurrentFrameMetadataLock);
            m_CurrentFrameMetadata = m_spFrameData->GetMetaData();
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
    else
    {
        g_Log->Warning("failed to get frame...");
        return false;
    }

    return true;
}

uint32_t CClip::GetCurrentFrameIdx() const
{
    boost::shared_lock<boost::shared_mutex> lock(m_CurrentFrameMetadataLock);
    return m_CurrentFrameMetadata.frameIdx;
}

const sFrameMetadata& CClip::GetCurrentFrameMetadata() const
{
    boost::shared_lock<boost::shared_mutex> lock(m_CurrentFrameMetadataLock);
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
}
} // namespace ContentDecoder
