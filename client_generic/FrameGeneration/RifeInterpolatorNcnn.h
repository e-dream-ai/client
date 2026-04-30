#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "IFrameInterpolator.h"

namespace FrameGeneration
{

class CRifeInterpolatorNcnn : public IFrameInterpolator
{
  public:
    CRifeInterpolatorNcnn();
    ~CRifeInterpolatorNcnn() override;

    const char* Name() const override { return "rife_2x"; }
    bool IsGpuBacked() const override { return true; }
    bool IsAvailable(std::string* reason = nullptr) const override;
    ContentDecoder::spCVideoFrame Interpolate(
        const ContentDecoder::spCVideoFrame& previous,
        const ContentDecoder::spCVideoFrame& next,
        float t) override;

    static bool BuiltWithSupport();
    static const char* RuntimeModelName();

    std::string ModelDirectory() const;
    std::string GpuLabel() const;
    std::string UnavailabilityReason() const;
    double LastInferenceMs() const;
    double AverageInferenceMs() const;
    uint64_t SuccessfulFrameCount() const;
    uint64_t FailedInferenceCount() const;
    bool IsFallingBack() const;

  private:
    class Impl;

    bool initialize(std::string* reason = nullptr) const;
    void setUnavailableLocked(const std::string& reason) const;

    mutable std::once_flag m_initializeOnce;
    mutable std::mutex m_mutex;
    mutable bool m_available = false;
    mutable bool m_runtimeDisabled = false;
    mutable std::string m_unavailabilityReason;
    mutable std::string m_modelDirectory;
    mutable std::string m_gpuLabel;
    mutable bool m_loggedUnsupportedT = false;
    mutable double m_lastInferenceMs = 0.0;
    mutable double m_totalInferenceMs = 0.0;
    mutable uint64_t m_successfulFrames = 0;
    mutable uint64_t m_failedFrames = 0;
    mutable bool m_loggedFirstGeneratedFrame = false;
    mutable bool m_runtimeFallbackActive = false;
    mutable std::unique_ptr<Impl> m_impl;
    mutable std::vector<unsigned char> m_prevRgbBuf;
    mutable std::vector<unsigned char> m_nextRgbBuf;
    mutable std::vector<unsigned char> m_outRgbBuf;
};

} // namespace FrameGeneration
