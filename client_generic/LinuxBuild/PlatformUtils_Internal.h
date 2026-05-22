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
