#pragma once

#include "IFrameInterpolator.h"

namespace FrameGeneration
{

class CBlendFrameInterpolator : public IFrameInterpolator
{
  public:
    const char* Name() const override { return "blend_2x"; }
    bool IsGpuBacked() const override { return false; }
    bool IsAvailable(std::string* reason = nullptr) const override;
    ContentDecoder::spCVideoFrame Interpolate(
        const ContentDecoder::spCVideoFrame& previous,
        const ContentDecoder::spCVideoFrame& next,
        float t) override;
};

} // namespace FrameGeneration
