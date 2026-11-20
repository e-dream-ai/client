#pragma once

#if !defined(WIN32) && !defined(MAC)

#include <cstdint>

/// Register the ESSetShowFirstTimeSetupCallback so the auth layer can trigger the wizard.
void FirstTimeSetupVulkan_Register();

/// True while the wizard overlay is visible. Used to block game key-bindings.
bool FirstTimeSetupVulkan_IsWizardVisible();

/// Called from RendererVulkan::EndFrame() just before ImGui::Render() — emits wizard ImGui
/// calls into the current frame if the wizard is visible.
void FirstTimeSetupVulkan_DrawIfNeeded();

/// Feed a key event (Wayland or X11) into ImGui while the wizard is visible. `keysym` is
/// layout-aware, `utf8` the text the key types (null or empty for none). Returns true if
/// the wizard consumed the event (caller should skip pushing it onto the normal event queue).
bool FirstTimeSetupVulkan_FeedKey(uint32_t keysym, bool pressed, bool ctrl, bool shift,
                                   const char* utf8);

/// Feed pointer (mouse) position from Wayland callbacks.
void FirstTimeSetupVulkan_FeedMousePos(int x, int y);

/// Feed pointer button from Wayland callbacks (button: 272 = left BTN_LEFT, 273 = right).
void FirstTimeSetupVulkan_FeedMouseButton(uint32_t button, bool pressed);

#endif // !WIN32 && !MAC
