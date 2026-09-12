#pragma once

#if !defined(WIN32) && !defined(MAC)

#include <cstdint>

#ifdef HAVE_WAYLAND
#include <xkbcommon/xkbcommon.h>
#endif

/// Register the ESSetShowPreferencesCallback so Ctrl+, can trigger the dialog.
void SettingsDialogVulkan_Register();

/// True while the settings dialog is visible.
bool SettingsDialogVulkan_IsVisible();

/// Toggle: open if closed, close (saving) if open.
void SettingsDialogVulkan_Toggle();

/// Called from RendererVulkan inside the active ImGui frame — emits dialog
/// draw calls if visible.
void SettingsDialogVulkan_DrawIfNeeded();

/// Feed a Wayland keyboard event into ImGui while the dialog is visible.
/// Returns true if the event was consumed.
#ifdef HAVE_WAYLAND
bool SettingsDialogVulkan_FeedKey(uint32_t evdev_key, xkb_keysym_t keysym, bool pressed,
                                  struct xkb_state* state);
#endif

void SettingsDialogVulkan_FeedMousePos(int x, int y);
void SettingsDialogVulkan_FeedMouseButton(uint32_t button, bool pressed);

#endif // !WIN32 && !MAC
