// DisplayDX11.h
#pragma once

#include "DisplayOutput.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <dxgi.h>

using Microsoft::WRL::ComPtr;

namespace DisplayOutput {

class CDisplayDX11 : public CDisplayOutput {
private:
    HWND m_WindowHandle;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    
    HWND CreateDisplayWindow(uint32_t w, uint32_t h, bool fullscreen);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool CreateDeviceAndSwapChain();
    void ResetDevice(); // Handle device lost scenarios
    bool m_deviceValid;

  public:
    CDisplayDX11();
    virtual ~CDisplayDX11();

    static const char* Description() { return "Direct3D 11 display"; }

    virtual bool Initialize() /* override */;
    virtual HWND Initialize(uint32_t width, uint32_t height, bool fullscreen) override;
    virtual HWND GetWindowHandle() override { return m_WindowHandle; }

    virtual void Title(const std::string& title) override;
    virtual void Update() override;
    virtual bool HasShaders() override { return true; }
    virtual void SwapBuffers() override;

    IDXGISwapChain* GetSwapChain() const { return m_swapChain.Get(); }

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
};

}