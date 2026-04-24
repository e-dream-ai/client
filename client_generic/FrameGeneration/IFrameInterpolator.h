#pragma once

#include <memory>
#include <string>

#include "ContentDecoder.h"

namespace FrameGeneration
{

class IFrameInterpolator
{
  public:
    virtual ~IFrameInterpolator() = default;

    virtual const char* Name() const = 0;
    virtual bool IsGpuBacked() const = 0;
    virtual bool IsAvailable(std::string* reason = nullptr) const = 0;
    virtual ContentDecoder::spCVideoFrame Interpolate(
        const ContentDecoder::spCVideoFrame& previous,
        const ContentDecoder::spCVideoFrame& next,
        float t) = 0;
};

using spIFrameInterpolator = std::shared_ptr<IFrameInterpolator>;

} // namespace FrameGeneration
