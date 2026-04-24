#pragma once

#include <cstdint>
#include <string>

namespace FrameGeneration
{

enum class EFrameGenerationMode : int32_t
{
    Off = 0,
    Blend2X = 1,
    RIFE = 2,
};

inline const char* ToString(EFrameGenerationMode mode)
{
    switch (mode)
    {
    case EFrameGenerationMode::Off:
        return "Off";
    case EFrameGenerationMode::Blend2X:
        return "Blend_2X";
    case EFrameGenerationMode::RIFE:
        return "RIFE";
    }
    return "Off";
}

inline EFrameGenerationMode FromSetting(int32_t value)
{
    switch (value)
    {
    case 1:
        return EFrameGenerationMode::Blend2X;
    case 2:
        return EFrameGenerationMode::RIFE;
    default:
        return EFrameGenerationMode::Off;
    }
}

inline int32_t ToSetting(EFrameGenerationMode mode)
{
    return static_cast<int32_t>(mode);
}

inline EFrameGenerationMode NextMode(EFrameGenerationMode mode)
{
    switch (mode)
    {
    case EFrameGenerationMode::Off:
        return EFrameGenerationMode::Blend2X;
    case EFrameGenerationMode::Blend2X:
        return EFrameGenerationMode::RIFE;
    case EFrameGenerationMode::RIFE:
        return EFrameGenerationMode::Off;
    }
    return EFrameGenerationMode::Off;
}

} // namespace FrameGeneration
