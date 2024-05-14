#include "DisplayD3D12.h"

#include "client.h"
#include "Log.h"
#include "Player.h"
#include <d3d12.h>

namespace DisplayOutput
{
    using namespace DirectX;

    CDisplayD3D12::CDisplayD3D12() : CDisplayOutput()
    {
        g_Log->Info("CDisplayD3D12()");
        m_deviceResources = std::make_unique<DeviceResources>();
        // TODO: Provide parameters for swapchain format, depth/stencil format, and backbuffer count.
        //   Add DX::DeviceResources::c_AllowTearing to opt-in to variable rate displays.
        //   Add DX::DeviceResources::c_EnableHDR for HDR10 display.
        //   Add DX::DeviceResources::c_ReverseDepth to optimize depth buffer clears for 0 instead of 1.
        m_deviceResources->RegisterDeviceNotify(this);

    }

    CDisplayD3D12::~CDisplayD3D12() { g_Log->Info("~CDisplayD3D12()"); }

    // IDeviceNotify
    void CDisplayD3D12::OnDeviceLost()
    {
        // TODO: Add Direct3D resource cleanup here.

        // If using the DirectX Tool Kit for DX12, uncomment this line:
        // m_graphicsMemory.reset();
    }

    void CDisplayD3D12::OnDeviceRestored()
    {
        //CreateDeviceDependentResources();
        //CreateWindowSizeDependentResources();
    }

/*
    wndProc().
    Handle all events, push them onto the eventqueue.
*/
LRESULT CALLBACK CDisplayD3D12::wndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam)
{
    PAINTSTRUCT ps;

    switch (msg)
    {
    case WM_USER:
        //	All initialization messages have gone through.  Allow 500ms of
        // idle
        // time, then proceed with initialization.
        SetTimer(hWnd, 1, 500, NULL);
        g_Log->Info("Starting 500ms preview timer");
        break;

    case WM_TIMER:
        //	Initial idle time is done, proceed with initialization.
        //m_bWaitForInputIdle = false;
        g_Log->Info("500ms preview timer done");
        KillTimer(hWnd, 1);
        break;

    case WM_PAINT:
    {
        if (BeginPaint(hWnd, &ps) != NULL)
        {
            /*if( g_bPreview )
            {
                    RECT rc;
                    GetClientRect( hWnd,&rc );
                    FillRect( ps.hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH)
            );
            }*/

            EndPaint(hWnd, &ps);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_KEYUP:
    {
        spCKeyEvent spEvent = std::make_shared<CKeyEvent>();
        spEvent->m_bPressed = true;

        switch (wParam)
        {
        case VK_TAB:
            spEvent->m_Code = CKeyEvent::KEY_TAB;
            break;
        case VK_LWIN:
            spEvent->m_Code = CKeyEvent::KEY_LALT;
            break;
        case VK_MENU:
            spEvent->m_Code = CKeyEvent::KEY_MENU;
            break;
        case VK_LEFT:
            spEvent->m_Code = CKeyEvent::KEY_LEFT;
            break;
        case VK_RIGHT:
            spEvent->m_Code = CKeyEvent::KEY_RIGHT;
            break;
        case VK_UP:
            spEvent->m_Code = CKeyEvent::KEY_UP;
            break;
        case VK_DOWN:
            spEvent->m_Code = CKeyEvent::KEY_DOWN;
            break;
        case VK_SPACE:
            spEvent->m_Code = CKeyEvent::KEY_SPACE;
            break;
        case 0x46:
            spEvent->m_Code = CKeyEvent::KEY_F;
            break;
        case VK_CONTROL:
            spEvent->m_Code = CKeyEvent::KEY_CTRL;
            break;
        case VK_F1:
            spEvent->m_Code = CKeyEvent::KEY_F1;
            break;
        case VK_F2:
            spEvent->m_Code = CKeyEvent::KEY_F2;
            break;
        case VK_F3:
            spEvent->m_Code = CKeyEvent::KEY_F3;
            break;
        case VK_F4:
            spEvent->m_Code = CKeyEvent::KEY_F4;
            break;
        case VK_F8:
            spEvent->m_Code = CKeyEvent::KEY_F8;
            break;
        case VK_ESCAPE:
            spEvent->m_Code = CKeyEvent::KEY_Esc;
            break;
        }
        g_Player().Display()->AppendEvent(spEvent);
    }
    break;

    case WM_LBUTTONUP:
    {
        auto spEvent = std::make_shared<CMouseEvent>();
        spEvent->m_Code = CMouseEvent::Mouse_LEFT;
        spEvent->m_X = MAKEPOINTS(lParam).x;
        spEvent->m_Y = MAKEPOINTS(lParam).y;
        g_Player().Display()->AppendEvent(spEvent);
    }
    break;

    case WM_RBUTTONUP:
    {
        auto spEvent = std::make_shared<CMouseEvent>();
        spEvent->m_Code = CMouseEvent::Mouse_RIGHT;
        spEvent->m_X = MAKEPOINTS(lParam).x;
        spEvent->m_Y = MAKEPOINTS(lParam).y;
        g_Player().Display()->AppendEvent(spEvent);
    }
    break;

    case WM_MOUSEMOVE:
    {
        auto spEvent = std::make_shared<CMouseEvent>();
        spEvent->m_Code = CMouseEvent::Mouse_MOVE;

        spEvent->m_X = MAKEPOINTS(lParam).x;
        spEvent->m_Y = MAKEPOINTS(lParam).y;
        g_Player().Display()->AppendEvent(spEvent);
    }
    break;

    case WM_POWERBROADCAST:
        switch (LOWORD(wParam))
        {
        case PBT_APMBATTERYLOW:
        case PBT_APMSUSPEND:
        {
            auto spEvent = std::make_shared<CPowerEvent>();
            g_Player().Display()->AppendEvent(spEvent);
        }
        }
        break;

    default:
    {
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    }
    return 0;
}


/*
    CreateDisplayWindow().
    Creates a new window, _w * _h in size, optionally fullscreen.
*/
HWND CDisplayD3D12::CreateDisplayWindow(uint32_t _w, uint32_t _h, const bool _bFullscreen)
{
    m_bFullScreen = _bFullscreen;
    HMODULE hInstance = GetModuleHandle(NULL);

    // EnumMonitors();

    WNDCLASS wndclass = {0};
    RECT windowRect;
    SetRect(&windowRect, 0, 0, _w, _h);
    g_Log->Info("CDisplayD3D12::CreateDisplayWindow x=%u y=%u w=%u h=%u", 0, 0, _w, _h);
    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = (WNDPROC)CDisplayD3D12::wndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
    wndclass.hCursor = NULL; // LoadCursor (NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = L"EdreamWndClass";

    static bool wndclassAlreadyRegistered = false;
    if (wndclassAlreadyRegistered == false && !RegisterClass(&wndclass))
    {
        return 0;
    }
    wndclassAlreadyRegistered = true;

    unsigned long exStyle;
    unsigned long style;
    if (_bFullscreen)
    {
        exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;
        style = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }
    else
    {
        exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
        style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    AdjustWindowRectEx(&windowRect, style, false, exStyle);
    int ww = windowRect.right - windowRect.left;
    int hh = windowRect.bottom - windowRect.top;
    int xx = 0;
    int yy = 0;
    g_Log->Info(
        "CDisplayD3D12::CreateDisplayWindow AdjustWindowRectEx x=%u y=%u w=%u h=%u", 0, 0,
        ww, hh);
    /*
    MONITORINFO monitorInfo;

    for (DWORD iMonitor = 0; iMonitor < m_dwNumMonitors; iMonitor++)
    {
        if (iMonitor == m_DesiredScreenID && _bFullscreen)
        {
            MonitorInfo* pMonitorInfo = &m_Monitors[iMonitor];
            monitorInfo.cbSize = sizeof(MONITORINFO);
            GetMonitorInfo(pMonitorInfo->hMonitor, &monitorInfo);
            pMonitorInfo->rcScreen = monitorInfo.rcMonitor;
            // SetWindowPos( hWnd, HWND_TOPMOST, monitorInfo.rcMonitor.left,
            // monitorInfo.rcMonitor.top, monitorInfo.rcMonitor.right -
            // monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.bottom -
            // monitorInfo.rcMonitor.top, SWP_NOACTIVATE );
            xx = monitorInfo.rcMonitor.left;
            yy = monitorInfo.rcMonitor.top;
            ww = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
            hh = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
            g_Log->Info(
                "CDisplayDX::createwindow SetWindowPos x=%u y=%u w=%u h=%u",
                monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
        }
    }*/

    HWND hWnd =
        CreateWindowEx(exStyle, L"EdreamWndClass", L"e-dream", style, xx,
                       yy, ww, hh, NULL, NULL, hInstance, NULL);
    //BlankUnusedMonitors(wndclass, hWnd, hInstance);
    return hWnd;
}

// This is called in client mode, not in screensaver mode
// We then call CreateDisplayWindow from here and return the HWND
HWND CDisplayD3D12::Initialize(const uint32_t _width, const uint32_t _height,
                               const bool _bFullscreen)
{
    m_WindowHandle = CreateDisplayWindow(_width, _height, _bFullscreen);
    if (m_WindowHandle == 0)
    {
        g_Log->Error("CDisplayD3D12::Initialize CreateDisplayWindow returned 0");
        return 0;
    }

    //	Show or Hide cursor.
    ShowCursor(!_bFullscreen);

    //	Get window dimensions.
    RECT rect;
    GetClientRect(m_WindowHandle, &rect);
    m_Width = rect.right - rect.left;
    m_Height = rect.bottom - rect.top;

    ShowWindow(m_WindowHandle, SW_SHOW);
    /* if (gl_hFocusWindow == NULL)
    {
        SetForegroundWindow(m_WindowHandle);
        SetFocus(m_WindowHandle);
    }*/

    // Initialize Direct3D
    m_deviceResources->SetWindow(m_WindowHandle, _width, _height);

    m_deviceResources->CreateDeviceResources();
    //CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    //CreateWindowSizeDependentResources();

    // TODO: Change the timer settings if you want something other than the default variable timestep mode.
    // e.g. for 60 FPS fixed timestep update logic, call:
    /*
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(1.0 / 60);
    */

    return m_WindowHandle;
}



// ?? Not sure why we need this 
bool CDisplayD3D12::Initialize()
{
	return true;
}


void CDisplayD3D12::Title(const std::string& _title)
{
    SetWindowTextA(m_WindowHandle, _title.c_str());
}

void CDisplayD3D12::Update()
{
    MSG msg;
    while (PeekMessage(&msg, m_WindowHandle, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void CDisplayD3D12::SwapBuffers() {
    // Likely no longer used, not used either on Metal
}
	
} // namespace DisplayOutput
