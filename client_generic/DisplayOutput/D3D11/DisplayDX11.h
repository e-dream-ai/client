// DisplayDX11.h
#pragma once

#include "DisplayOutput.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <dxgi.h>
#include <functional>

#ifdef WIN32
#include <Windows.h>
#endif

using Microsoft::WRL::ComPtr;

namespace DisplayOutput {

class CDisplayDX11 : public CDisplayOutput {
private:
    HWND m_WindowHandle;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    RECT m_windowedRect;
    DWORD m_windowedStyle;
    DWORD m_windowedExStyle;
    bool m_hasWindowedRect;
    
#ifdef WIN32
    HWND CreateDisplayWindow(uint32_t w, uint32_t h, bool fullscreen, HMENU hMenu);
#endif
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateDeviceAndSwapChain();
    bool ResizeSwapChain(uint32_t width, uint32_t height);
    void ResetDevice(); // Handle device lost scenarios
    bool m_bWaitForPreviewPump;
    bool m_bEmbeddedSaverPreview;
    static bool EnsureWindowClassRegistered(HINSTANCE hInstance);

    std::function<void()> m_menuLoopRenderCb;
    bool m_bMenuRenderInProgress;
    bool m_deviceLost = false;

  public:
    CDisplayDX11();
    virtual ~CDisplayDX11();

    static const char* Description() { return "Direct3D 11 display"; }

    virtual bool Initialize() /* override */;
    virtual HWND Initialize(uint32_t width, uint32_t height, bool fullscreen) override;
    virtual HWND Initialize(HWND parentHwnd, bool preview) override;
    virtual HWND GetWindowHandle() override { return m_WindowHandle; }

    virtual void Title(const std::string& title) override;
    virtual void Update() override;
    virtual bool HasShaders() override { return true; }
    virtual void SwapBuffers() override;
    virtual bool ToggleFullscreen() override;
    virtual bool SetFullscreen(const bool fullscreen) override;
    virtual bool IsFullscreen() const override { return m_bFullScreen; }

    void SetMenuLoopRenderCallback(std::function<void()> cb) { m_menuLoopRenderCb = std::move(cb); }

    IDXGISwapChain* GetSwapChain() const { return m_swapChain.Get(); }

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

    // Returns true after DXGI_ERROR_DEVICE_REMOVED/HUNG is received from Present.
    // All D3D11 operations will fail until the device is recreated.
    bool IsDeviceLost() const { return m_deviceLost; }
};

}