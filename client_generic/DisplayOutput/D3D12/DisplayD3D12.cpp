#include "DisplayD3D12.h"

#include "client.h"
#include "Log.h"
#include "Player.h"
#include <d3d12.h>

namespace DisplayOutput
{

    using namespace DirectX;

CDisplayD3D12::CDisplayD3D12() : CDisplayOutput(), m_useWarpDevice(false)
    {
    g_Log->Info("CDisplayD3D12()");
}

CDisplayD3D12::~CDisplayD3D12() { g_Log->Info("~CDisplayD3D12()"); }


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

   

    if (!InitD3D12())
        return nullptr;
        
    return m_WindowHandle;
}

bool CDisplayD3D12::InitD3D12()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(),
                                        D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&m_device)));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(),
                                        D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&m_device)));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc,
                                               IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_Width;
    swapChainDesc.Height = m_Height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue
            .Get(), // Swap chain needs the queue so that it can force a flush on it.
        m_WindowHandle, &swapChainDesc, nullptr, nullptr,
        &swapChain));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(
        factory->MakeWindowAssociation(m_WindowHandle, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc,
                                                     IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(
                m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr,
                                             rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
	return true;
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_ void
CDisplayD3D12::GetHardwareAdapter(IDXGIFactory1* pFactory,
                                  IDXGIAdapter1** ppAdapter,
                             bool requestHighPerformanceAdapter)
{
    *ppAdapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (UINT adapterIndex = 0;
             SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                 adapterIndex,
                 requestHighPerformanceAdapter == true
                     ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                     : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                 IID_PPV_ARGS(&adapter)));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    if (adapter.Get() == nullptr)
    {
        for (UINT adapterIndex = 0;
             SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }
    g_Log->Info("CDisplayD3D12::GetHardwareAdapter() adapter.Get() = %p",
                adapter.Get());
    *ppAdapter = adapter.Detach();
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
