#pragma once

#ifdef WIN32

#include <d3d11.h>
#include <Windows.h>

void FirstTimeSetupWin32_Register();
void FirstTimeSetupWin32_SetOverlayAllowed(bool allow);
bool FirstTimeSetupWin32_IsWizardVisible();

/// If ImGui consumed the message, returns true and sets *outResult (return from WndProc).
bool FirstTimeSetupWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           LRESULT* outResult);

/// Draw first-time UI on top of the current RTV. Returns true if Present should run even when
/// the main frame did not draw content.
bool FirstTimeSetupWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                       ID3D11RenderTargetView* rtv, float viewportW, float viewportH);

#endif
