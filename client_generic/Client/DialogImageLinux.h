#ifndef _DIALOGIMAGELINUX_H_
#define _DIALOGIMAGELINUX_H_

#include "PlatformUtils.h"
#include "Player.h"
#include "RendererVulkan.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include <imgui.h>

// A PNG from the runtime assets, ready for ImGui::Image() at drawnSize pixels wide.
// ImTextureID_Invalid if it can't be loaded, so callers can lay out without it.
// Call while drawing a dialog.
inline ImTextureID DialogImage(const char* fileName, float drawnSize)
{
    auto renderer = std::dynamic_pointer_cast<DisplayOutput::CRendererVulkan>(g_Player().Renderer());
    if (!renderer)
        return ImTextureID_Invalid;
    VkDescriptorSet set = renderer->GetUiTexture(PlatformUtils::GetWorkingDir() + fileName,
                                                static_cast<uint32_t>(std::ceil(drawnSize)));
    return set != VK_NULL_HANDLE ? static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(set)) : ImTextureID_Invalid;
}

#endif
