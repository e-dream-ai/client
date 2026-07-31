#pragma once

#if !defined(WIN32) && !defined(MAC)

#include <cstdint>

#ifdef HAVE_WAYLAND
#include <xkbcommon/xkbcommon.h>
#endif

/// Register the ESSetShowFirstTimeSetupCallback so the auth layer can trigger the wizard.
void FirstTimeSetupVulkan_Register();

/// True while the wizard overlay is visible. Used to block game key-bindings.
bool FirstTimeSetupVulkan_IsWizardVisible();

/// Called from RendererVulkan::EndFrame() just before ImGui::Render() — emits wizard ImGui
/// calls into the current frame if the wizard is visible.
void FirstTimeSetupVulkan_DrawIfNeeded();

/// Feed a Wayland keyboard event into ImGui. `evdev_key` is the raw Wayland key code (before
/// the +8 xkb offset). Returns true if the wizard consumed the event (caller should skip
/// pushing it onto the normal event queue).
#ifdef HAVE_WAYLAND
bool FirstTimeSetupVulkan_FeedKey(uint32_t evdev_key, xkb_keysym_t keysym, bool pressed,
                                  struct xkb_state* state);
#endif

/// Feed pointer (mouse) position from Wayland callbacks.
void FirstTimeSetupVulkan_FeedMousePos(int x, int y);

/// Feed pointer button from Wayland callbacks (button: 272 = left BTN_LEFT, 273 = right).
void FirstTimeSetupVulkan_FeedMouseButton(uint32_t button, bool pressed);

#endif // !WIN32 && !MAC
