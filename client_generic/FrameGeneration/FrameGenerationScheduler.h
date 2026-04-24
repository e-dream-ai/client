#pragma once

#include <functional>
#include <string>

#include "IFrameInterpolator.h"

namespace FrameGeneration
{

class CFrameGenerationScheduler
{
  public:
    using FrameProvider = std::function<ContentDecoder::spCVideoFrame()>;

    void Configure(bool enabled, double sourceFps, double targetFps,
                   spIFrameInterpolator interpolator);
    void Reset();

    bool Enabled() const { return m_enabled && m_interpolator != nullptr; }
    double SourceFps() const { return m_sourceFps; }
    double PresentationFps() const { return Enabled() ? m_targetFps : m_sourceFps; }
    std::string ModeName() const
    {
        return m_interpolator ? std::string(m_interpolator->Name()) : std::string("off");
    }
    const ContentDecoder::spCVideoFrame& CurrentDisplayFrame() const { return m_displayFrame; }
    double CurrentDisplayPhase() const { return m_displayPhase; }
    uint64_t GeneratedFrameCount() const { return m_generatedFrameCount; }
    uint64_t RealFrameCount() const { return m_realFrameCount; }
    double InterpolatorLastTimeMs() const;
    double InterpolatorAverageTimeMs() const;
    uint64_t InterpolatorFailureCount() const;
    bool InterpolatorFallingBack() const;

    bool Advance(const FrameProvider& frameProvider, std::string* reason = nullptr);

  private:
    bool prepareNextRealFrame(const FrameProvider& frameProvider, std::string* reason);
    bool canGenerateBetween(const ContentDecoder::spCVideoFrame& previous,
                            const ContentDecoder::spCVideoFrame& next) const;

    bool m_enabled = false;
    double m_sourceFps = 0.0;
    double m_targetFps = 0.0;
    spIFrameInterpolator m_interpolator;

    ContentDecoder::spCVideoFrame m_currentRealFrame;
    ContentDecoder::spCVideoFrame m_nextRealFrame;
    ContentDecoder::spCVideoFrame m_generatedFrame;
    ContentDecoder::spCVideoFrame m_displayFrame;
    bool m_displayGeneratedNext = false;
    double m_displayPhase = 0.0;
    double m_pendingSyntheticFrames = 0.0;
    uint64_t m_generatedFrameCount = 0;
    uint64_t m_realFrameCount = 0;
};

} // namespace FrameGeneration
