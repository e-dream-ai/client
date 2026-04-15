#pragma once

#ifdef WIN32

#include <d3d11.h>
#include <Windows.h>

void AboutDialogWin32_SetOverlayAllowed(bool allow);
bool AboutDialogWin32_HasPendingOrVisible();

/// Opens the about overlay (ImGui). Dismisses the settings overlay without saving if it is open,
/// so only one Win32 ImGui backend is active on the main window at a time.
void AboutDialogWin32_RequestShow();

/// Tear down the about overlay immediately (e.g. before opening Settings from the keyboard shortcut).
void AboutDialogWin32_DismissWithoutSaveForExternalOverlay();

/// If ImGui consumed the message, returns true and sets *outResult (return from WndProc).
bool AboutDialogWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        LRESULT* outResult);

/// Draw about UI on top of the current RTV. Returns true if something was drawn.
bool AboutDialogWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                     ID3D11RenderTargetView* rtv, float viewportW, float viewportH);

#endif
