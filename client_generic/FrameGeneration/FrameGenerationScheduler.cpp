#include "FrameGenerationScheduler.h"

#include "RifeInterpolatorNcnn.h"

namespace FrameGeneration
{

void CFrameGenerationScheduler::Configure(bool enabled, double sourceFps,
                                          double targetFps,
                                          spIFrameInterpolator interpolator)
{
    m_enabled = enabled && sourceFps > 0.0 && targetFps > sourceFps &&
                targetFps <= sourceFps * 2.0 && interpolator != nullptr;
    m_sourceFps = sourceFps;
    m_targetFps = targetFps;
    m_interpolator = std::move(interpolator);
    Reset();
}

void CFrameGenerationScheduler::Reset()
{
    m_currentRealFrame.reset();
    m_nextRealFrame.reset();
    m_generatedFrame.reset();
    m_displayFrame.reset();
    m_displayGeneratedNext = false;
    m_displayPhase = 0.0;
    m_pendingSyntheticFrames = 0.0;
    m_generatedFrameCount = 0;
    m_realFrameCount = 0;
}

double CFrameGenerationScheduler::InterpolatorLastTimeMs() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->LastInferenceMs() : 0.0;
}

double CFrameGenerationScheduler::InterpolatorAverageTimeMs() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->AverageInferenceMs() : 0.0;
}

uint64_t CFrameGenerationScheduler::InterpolatorFailureCount() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->FailedInferenceCount() : 0;
}

bool CFrameGenerationScheduler::InterpolatorFallingBack() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->IsFallingBack() : false;
}

bool CFrameGenerationScheduler::prepareNextRealFrame(const FrameProvider& frameProvider,
                                                     std::string* reason)
{
    if (m_nextRealFrame)
        return true;

    if (!frameProvider)
    {
        if (reason)
            *reason = "No frame provider configured.";
        return false;
    }

    m_nextRealFrame = frameProvider();
    if (!m_nextRealFrame)
    {
        if (reason)
            *reason = "Decoder queue is empty.";
        return false;
    }

    return true;
}

bool CFrameGenerationScheduler::canGenerateBetween(
    const ContentDecoder::spCVideoFrame& previous,
    const ContentDecoder::spCVideoFrame& next) const
{
    if (!previous || !next)
        return false;

    if (previous->Width() != next->Width() || previous->Height() != next->Height())
        return false;

    const auto& prevMeta = previous->GetMetaData();
    const auto& nextMeta = next->GetMetaData();
    if (prevMeta.isSeam || nextMeta.isSeam)
        return false;

    return true;
}

bool CFrameGenerationScheduler::Advance(const FrameProvider& frameProvider, std::string* reason)
{
    if (!Enabled())
    {
        if (reason)
            *reason = "Frame generation is disabled.";
        return false;
    }

    if (!m_currentRealFrame)
    {
        m_currentRealFrame = frameProvider ? frameProvider() : nullptr;
        if (!m_currentRealFrame)
        {
            if (reason)
                *reason = "No initial decoded frame available.";
            return false;
        }

        m_displayFrame = m_currentRealFrame;
        m_displayPhase = 0.0;
        m_displayGeneratedNext = false;
        ++m_realFrameCount;
        return true;
    }

    if (!m_displayGeneratedNext)
    {
        if (!prepareNextRealFrame(frameProvider, reason))
            return false;

        const double synthsPerGap =
            (m_sourceFps > 0.0) ? ((m_targetFps / m_sourceFps) - 1.0) : 0.0;
        m_pendingSyntheticFrames += synthsPerGap;
        bool shouldGenerateThisGap = m_pendingSyntheticFrames >= 1.0;

        if (shouldGenerateThisGap && canGenerateBetween(m_currentRealFrame, m_nextRealFrame))
        {
            m_generatedFrame = m_interpolator->Interpolate(m_currentRealFrame, m_nextRealFrame, 0.5f);
        }
        else
        {
            m_generatedFrame.reset();
        }

        if (m_generatedFrame)
        {
            m_pendingSyntheticFrames -= 1.0;
            m_displayFrame = m_generatedFrame;
            m_displayPhase = 0.5;
            m_displayGeneratedNext = true;
            ++m_generatedFrameCount;
            return true;
        }

        m_currentRealFrame = m_nextRealFrame;
        m_nextRealFrame.reset();
        m_displayFrame = m_currentRealFrame;
        m_displayPhase = 0.0;
        m_displayGeneratedNext = false;
        ++m_realFrameCount;
        return true;
    }

    if (!m_nextRealFrame)
    {
        if (reason)
            *reason = "No decoded frame available for real-frame step.";
        return false;
    }

    m_currentRealFrame = m_nextRealFrame;
    m_nextRealFrame.reset();
    m_generatedFrame.reset();
    m_displayFrame = m_currentRealFrame;
    m_displayPhase = 0.0;
    m_displayGeneratedNext = false;
    ++m_realFrameCount;
    return true;
}

} // namespace FrameGeneration
