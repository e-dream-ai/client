// Linux-internal PlatformUtils helpers — not part of the public API.
// Include this in DisplayVulkan.cpp and PlatformUtils_Linux.cpp.
#pragma once

#include <functional>

// Drain the main-thread dispatch queue posted via DispatchOnMainThread().
// Call once per iteration of the main loop.
void PlatformUtils_DrainMainThreadQueue();

// Returns the cursor-hidden state last set by PlatformUtils::SetCursorHidden().
bool PlatformUtils_GetCursorHidden();

// Returns the mouse-moved callback stored by SetOnMouseMovedCallback().
std::function<void(int, int)>& PlatformUtils_GetOnMouseMovedCallback();

// UI scale factor for ImGui dialogs (1.0 = 96 DPI, 2.0 = 192 DPI, etc.).
// InitUIScale() seeds from env vars (GDK_SCALE, QT_SCALE_FACTOR); display
// init then calls SetUIScale() with a more precise value if available.
void  PlatformUtils_InitUIScale();
float PlatformUtils_GetUIScale();
void  PlatformUtils_SetUIScale(float scale);
