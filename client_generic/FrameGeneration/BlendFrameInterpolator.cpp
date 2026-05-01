#include "BlendFrameInterpolator.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "libavutil/pixfmt.h"
}

namespace FrameGeneration
{

bool CBlendFrameInterpolator::IsAvailable(std::string* reason) const
{
    if (reason)
        *reason = "Simple midpoint blending is always available.";
    return true;
}

ContentDecoder::spCVideoFrame CBlendFrameInterpolator::Interpolate(
    const ContentDecoder::spCVideoFrame& previous,
    const ContentDecoder::spCVideoFrame& next,
    float t)
{
    if (!previous || !next)
        return nullptr;

    AVFrame* prevFrame = previous->Frame();
    AVFrame* nextFrame = next->Frame();
    if (!prevFrame || !nextFrame)
        return nullptr;

    if (previous->Width() != next->Width() || previous->Height() != next->Height())
        return nullptr;

    const bool inputIsRgb = (prevFrame->format == AV_PIX_FMT_RGB24);
    if (!inputIsRgb && (prevFrame->format != AV_PIX_FMT_RGBA || nextFrame->format != AV_PIX_FMT_RGBA))
        return nullptr;

    auto output = std::make_shared<ContentDecoder::CVideoFrame>(
        static_cast<int>(previous->Width()),
        static_cast<int>(previous->Height()),
        AV_PIX_FMT_RGBA,
        previous->GetMetaData().fileName);

    AVFrame* dstFrame = output->Frame();
    if (!dstFrame || !dstFrame->data[0])
        return nullptr;

    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    const float invT = 1.0f - clampedT;
    const uint32_t w = previous->Width();
    const uint32_t h = previous->Height();

    if (inputIsRgb)
    {
        for (uint32_t row = 0; row < h; ++row)
        {
            const uint8_t* prevRow = prevFrame->data[0] + row * prevFrame->linesize[0];
            const uint8_t* nextRow = nextFrame->data[0] + row * nextFrame->linesize[0];
            uint8_t* dstRow = dstFrame->data[0] + row * dstFrame->linesize[0];

            for (uint32_t col = 0; col < w; ++col)
            {
                dstRow[col*4+0] = static_cast<uint8_t>(std::clamp(std::lround(prevRow[col*3+0] * invT + nextRow[col*3+0] * clampedT), 0l, 255l));
                dstRow[col*4+1] = static_cast<uint8_t>(std::clamp(std::lround(prevRow[col*3+1] * invT + nextRow[col*3+1] * clampedT), 0l, 255l));
                dstRow[col*4+2] = static_cast<uint8_t>(std::clamp(std::lround(prevRow[col*3+2] * invT + nextRow[col*3+2] * clampedT), 0l, 255l));
                dstRow[col*4+3] = 255;
            }
        }
    }
    else
    {
        const int widthBytes = static_cast<int>(w * 4);
        for (uint32_t row = 0; row < h; ++row)
        {
            const uint8_t* prevRow = prevFrame->data[0] + row * prevFrame->linesize[0];
            const uint8_t* nextRow = nextFrame->data[0] + row * nextFrame->linesize[0];
            uint8_t* dstRow = dstFrame->data[0] + row * dstFrame->linesize[0];

            for (int col = 0; col < widthBytes; ++col)
            {
                const float blended = prevRow[col] * invT + nextRow[col] * clampedT;
                dstRow[col] = static_cast<uint8_t>(std::clamp(std::lround(blended), 0l, 255l));
            }
        }
    }

    const auto& meta = previous->GetMetaData();
    output->SetMetaData_FileName(meta.fileName);
    output->SetMetaData_DreamName(meta.name);
    output->SetMetaData_DreamAuthor(meta.author);
    output->SetMetaData_DecodeFps(meta.decodeFps);
    output->SetMetaData_IsSeam(false);
    output->SetMetaData_FrameIdx(meta.frameIdx);
    output->SetMetaData_MaxFrameIdx(meta.maxFrameIdx);

    return output;
}

} // namespace FrameGeneration
