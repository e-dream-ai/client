#include "DisplayDX11.h"
#include "Log.h"
#include "Player.h"
#include "RendererDX11.h"

#ifdef WIN32
#include "FirstTimeSetupWin32.h"
#include "SettingsDialogWin32.h"
extern void ESShowPreferences();
#endif

namespace DisplayOutput {

namespace {

// Avoid duplicate F1 enqueue when WM_HELP and WM_KEYUP arrive for the same press.
static ULONGLONG g_lastF1EnqueueTickMs = 0;
constexpr ULONGLONG kF1DuplicateWindowMs = 250;

static void AppendKeyEvent(CKeyEvent::eKeyCode code, bool dedupeF1)
{
    if (code == CKeyEvent::KEY_NONE)
        return;

    if (dedupeF1 && code == CKeyEvent::KEY_F1)
    {
        const ULONGLONG now = GetTickCount64();
        if (now - g_lastF1EnqueueTickMs <= kF1DuplicateWindowMs)
            return;
        g_lastF1EnqueueTickMs = now;
    }

    auto spEvent = std::make_shared<CKeyEvent>();
    spEvent->m_bPressed = true;
    spEvent->m_Code = code;
    if (auto spD = g_Player().Display())
        spD->AppendEvent(spEvent);
}

} // namespace

CDisplayDX11::CDisplayDX11() : CDisplayOutput(), m_WindowHandle(nullptr) {
    g_Log->Info("CDisplayDX11()");
}

CDisplayDX11::~CDisplayDX11() {
    g_Log->Info("~CDisplayDX11()");
}

LRESULT CALLBACK CDisplayDX11::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        if (cs && cs->lpCreateParams)
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }

    CDisplayDX11* self = reinterpret_cast<CDisplayDX11*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

#ifdef WIN32
    LRESULT imguiHandled = 0;
    if (SettingsDialogWin32_TryConsumeWndProc(hWnd, msg, wParam, lParam, &imguiHandled))
        return imguiHandled;
    if (FirstTimeSetupWin32_TryConsumeWndProc(hWnd, msg, wParam, lParam, &imguiHandled))
        return imguiHandled;
#endif

    switch (msg)
    {
#ifdef WIN32
    case WM_PAINT:
    {
        // We render via Direct3D and draw DX11 "text HUD" via GDI after Present.
        // Prevent default background erase that can cause HUD flicker.
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
#endif

    case WM_HELP:
        // Fallback path: some Windows setups surface F1 as help message.
        AppendKeyEvent(CKeyEvent::KEY_F1, true);
        return 0;

    case WM_SIZE:
        if (self && wParam != SIZE_MINIMIZED)
        {
            const UINT nw = static_cast<UINT>(LOWORD(lParam));
            const UINT nh = static_cast<UINT>(HIWORD(lParam));
            if (nw > 0 && nh > 0)
                self->ResizeSwapChain(nw, nh);
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    case WM_KEYUP: {
        if (wParam == VK_OEM_COMMA && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            ESShowPreferences();
            return 0;
        }

        CKeyEvent::eKeyCode code = CKeyEvent::KEY_NONE;

        switch (wParam) {
            case VK_ESCAPE: code = CKeyEvent::KEY_Esc; break;
            case VK_SPACE: code = CKeyEvent::KEY_SPACE; break;
            case VK_TAB: code = CKeyEvent::KEY_TAB; break;
            case VK_LEFT: code = CKeyEvent::KEY_LEFT; break;
            case VK_RIGHT: code = CKeyEvent::KEY_RIGHT; break;
            case VK_UP: code = CKeyEvent::KEY_UP; break;
            case VK_DOWN: code = CKeyEvent::KEY_DOWN; break;
            case VK_MENU: code = CKeyEvent::KEY_MENU; break;
            case VK_LMENU: code = CKeyEvent::KEY_LALT; break;
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
                code = CKeyEvent::KEY_CTRL;
                break;
            case VK_SHIFT:
            case VK_LSHIFT:
            case VK_RSHIFT:
                code = CKeyEvent::KEY_SHIFT;
                break;
            case VK_BACK: code = CKeyEvent::KEY_BACKSPACE; break;
            case VK_RETURN: code = CKeyEvent::KEY_ENTER; break;
            case VK_CAPITAL: code = CKeyEvent::KEY_CAPSLOCK; break;
            case VK_DELETE: code = CKeyEvent::KEY_DELETE; break;
            case VK_END: code = CKeyEvent::KEY_END; break;
            case VK_HOME: code = CKeyEvent::KEY_HOME; break;
            case VK_INSERT: code = CKeyEvent::KEY_INSERT; break;
            case VK_PRIOR: code = CKeyEvent::KEY_PAGEUP; break;
            case VK_NEXT: code = CKeyEvent::KEY_PAGEDOWN; break;
            case VK_F1: code = CKeyEvent::KEY_F1; break;
            case VK_F2: code = CKeyEvent::KEY_F2; break;
            case VK_F3: code = CKeyEvent::KEY_F3; break;
            case VK_F4: code = CKeyEvent::KEY_F4; break;
            case VK_F5: code = CKeyEvent::KEY_F5; break;
            case VK_F6: code = CKeyEvent::KEY_F6; break;
            case VK_F7: code = CKeyEvent::KEY_F7; break;
            case VK_F8: code = CKeyEvent::KEY_F8; break;
            case VK_F9: code = CKeyEvent::KEY_F9; break;
            case VK_F10: code = CKeyEvent::KEY_F10; break;
            case VK_F11: code = CKeyEvent::KEY_F11; break;
            case VK_F12: code = CKeyEvent::KEY_F12; break;
            case VK_OEM_COMMA: code = CKeyEvent::KEY_Comma; break;
            case VK_OEM_PERIOD: code = CKeyEvent::KEY_Period; break;
            default:
                if (wParam >= 'A' && wParam <= 'Z') {
                    code = static_cast<CKeyEvent::eKeyCode>(wParam);
                } else if (wParam >= '0' && wParam <= '9') {
                    code = static_cast<CKeyEvent::eKeyCode>(wParam);
                }
                break;
        }

        AppendKeyEvent(code, true);
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

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
    HICON appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = appIcon ? appIcon : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"EDreamDX11Class";

    if (!RegisterClassEx(&wc)) {
        return nullptr;
    }

    DWORD style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT rc = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&rc, style, FALSE);

    return CreateWindowW(wc.lpszClassName, L"E-Dream", style, CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, this);
}

bool CDisplayDX11::ResizeSwapChain(uint32_t width, uint32_t height)
{
    if (!m_swapChain || !m_device || !m_context || width == 0 || height == 0)
        return false;
    if (width == m_Width && height == m_Height)
        return true;

    m_context->ClearState();
    m_context->Flush();

    if (auto spR = g_Player().Renderer())
    {
        if (auto* r11 = dynamic_cast<CRendererDX11*>(spR.get()))
            r11->PrepareForSwapChainResize();
    }

    m_renderTargetView.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        g_Log->Error("ResizeBuffers failed: %08X", hr);
        return false;
    }

    m_Width = width;
    m_Height = height;

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        g_Log->Error("GetBuffer after resize failed: %08X", hr);
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView);
    if (FAILED(hr))
    {
        g_Log->Error("CreateRenderTargetView after resize failed: %08X", hr);
        return false;
    }

    m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(m_Width);
    vp.Height = static_cast<float>(m_Height);
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    m_context->RSSetViewports(1, &vp);

    if (auto spR = g_Player().Renderer())
    {
        if (auto* r11 = dynamic_cast<CRendererDX11*>(spR.get()))
        {
            if (!r11->RecreateRenderTargetsAfterResize())
                g_Log->Warning("Renderer RTs failed to recreate after resize");
        }
    }

    return true;
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
        if (msg.message == WM_QUIT) {
            m_bClosed = true;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void CDisplayDX11::SwapBuffers()
{
    HRESULT hr = m_swapChain->Present(1, 0); // Use vsync
    if (FAILED(hr))
    {
        g_Log->Error("Present failed: %08X", hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG)
        {
            g_Log->Warning("Device lost during present");
            // Handle device lost
        }
    }
}


} // namespace DisplayOutput