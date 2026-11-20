#pragma once

#if !defined(WIN32) && !defined(MAC)

#include <cstdint>

/// Register the ESSetShowPreferencesCallback so Ctrl+, can trigger the dialog.
void SettingsDialogVulkan_Register();

/// True while the settings dialog is visible.
bool SettingsDialogVulkan_IsVisible();

/// Toggle: open if closed, close (saving) if open.
void SettingsDialogVulkan_Toggle();

/// Called from RendererVulkan inside the active ImGui frame — emits dialog
/// draw calls if visible.
void SettingsDialogVulkan_DrawIfNeeded();

/// Feed a key event (Wayland or X11) into ImGui while the dialog is visible.
/// See FirstTimeSetupVulkan_FeedKey for the arguments. Returns true if consumed.
bool SettingsDialogVulkan_FeedKey(uint32_t keysym, bool pressed, bool ctrl, bool shift,
                                   const char* utf8);

void SettingsDialogVulkan_FeedMousePos(int x, int y);
void SettingsDialogVulkan_FeedMouseButton(uint32_t button, bool pressed);

#endif // !WIN32 && !MAC
