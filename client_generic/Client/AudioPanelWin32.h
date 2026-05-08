#pragma once
#ifdef WIN32
#include <atomic>
#include <d3d11.h>

/// Called from RendererDX11::EndFrame — renders ImGui audio sliders when F3 is open.
bool AudioPanelWin32_RenderIfNeeded(ID3D11Device* device,
                                    ID3D11DeviceContext* ctx,
                                    ID3D11RenderTargetView* rtv,
                                    float viewportW, float viewportH);

/// Called from client.h when F3 is toggled.
void AudioPanelWin32_SetVisible(bool visible);
bool AudioPanelWin32_IsVisible();
bool AudioPanelWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam, LRESULT* outResult);
#endif