#include "DisplayDX11.h"
#include "Log.h"
#include "Player.h"

namespace DisplayOutput {

CDisplayDX11::CDisplayDX11() : CDisplayOutput(), m_WindowHandle(nullptr) {
    g_Log->Info("CDisplayDX11()");
}

CDisplayDX11::~CDisplayDX11() {
    g_Log->Info("~CDisplayDX11()");
}

LRESULT CALLBACK CDisplayDX11::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYUP: {
        auto spEvent = std::make_shared<CKeyEvent>();
        spEvent->m_bPressed = true;
        
        switch (wParam) {
            case VK_ESCAPE: spEvent->m_Code = CKeyEvent::KEY_Esc; break;
            case VK_SPACE: spEvent->m_Code = CKeyEvent::KEY_SPACE; break;
        }
        
        if (spEvent->m_Code != CKeyEvent::KEY_NONE) {
            g_Player().Display()->AppendEvent(spEvent);
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

HWND CDisplayDX11::CreateDisplayWindow(uint32_t w, uint32_t h, bool fullscreen) {
    HMODULE hInstance = GetModuleHandle(NULL);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"EDreamDX11Class";

    if (!RegisterClassEx(&wc)) {
        return nullptr;
    }

    DWORD style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT rc = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&rc, style, FALSE);

    return CreateWindow(wc.lpszClassName, L"E-Dream",
                       style, 
                       CW_USEDEFAULT, CW_USEDEFAULT,
                       rc.right - rc.left, rc.bottom - rc.top,
                       nullptr, nullptr, hInstance, nullptr);
}

bool CDisplayDX11::CreateDeviceAndSwapChain() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = m_Width;
    sd.BufferDesc.Height = m_Height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_WindowHandle;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = !m_bFullScreen;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevels, 1,
        D3D11_SDK_VERSION, &sd,
        &m_swapChain, &m_device, nullptr, &m_context);

    if (FAILED(hr)) {
        g_Log->Error("Failed to create device and swap chain");
        return false;
    }

    // Create render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr)) return false;

    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_Width;
    vp.Height = (float)m_Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

HWND CDisplayDX11::Initialize(uint32_t width, uint32_t height, bool fullscreen) {
    m_Width = width;
    m_Height = height;
    m_bFullScreen = fullscreen;

    m_WindowHandle = CreateDisplayWindow(width, height, fullscreen);
    if (!m_WindowHandle) return nullptr;

    if (!CreateDeviceAndSwapChain()) {
        return nullptr;
    }

    ShowWindow(m_WindowHandle, SW_SHOW);
    ShowCursor(!fullscreen);

    return m_WindowHandle;
}

bool CDisplayDX11::Initialize() {
    return true;
}

void CDisplayDX11::Title(const std::string& title) {
    SetWindowTextA(m_WindowHandle, title.c_str());
}

void CDisplayDX11::Update() {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void CDisplayDX11::SwapBuffers() {
    m_swapChain->Present(1, 0);
}

} // namespace DisplayOutput