#pragma once

#ifdef WIN32

#include <d3d11.h>
#include <Windows.h>

void SettingsDialogWin32_Register();
void SettingsDialogWin32_SetOverlayAllowed(bool allow);
bool SettingsDialogWin32_HasPendingOrVisible();
bool SettingsDialogWin32_IsVisible();
void SettingsDialogWin32_Toggle();

/// Close settings without saving and tear down ImGui immediately (for another overlay on the same HWND).
void SettingsDialogWin32_DismissWithoutSaveForExternalOverlay();

/// If ImGui consumed the message, returns true and sets *outResult (return from WndProc).
bool SettingsDialogWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           LRESULT* outResult);

/// Draw settings UI on top of the current RTV. Returns true if Present should run even when
/// the main frame did not draw content.
bool SettingsDialogWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                        ID3D11RenderTargetView* rtv, float viewportW, float viewportH);

/// Close-time 16:9 snap is deferred to the message pump via this WM_APP message because
/// SetWindowPos sends WM_SIZE synchronously, which would invalidate the in-flight RTV that
/// RendererDX11 passes by raw pointer to RenderIfNeeded — see SnapWindowTo16By9IfNeeded.
constexpr UINT kSettingsDialogWin32SnapAfterCloseMsg = WM_APP + 1;
void SettingsDialogWin32_HandleDeferredSnap();

#endif
