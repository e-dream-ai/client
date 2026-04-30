#include "DisplayDX11.h"
#include "Log.h"
#include "PlatformUtils.h"
#include "Player.h"
#include "RendererDX11.h"
#include "Settings.h"
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>

#if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
#include <d3d11_4.h>
#endif

#ifdef WIN32
#include "AboutDialogWin32.h"
#include "FirstTimeSetupWin32.h"
#include "SettingsDialogWin32.h"
#include <dwmapi.h>
#include <ShellScalingApi.h>
#include <windowsx.h>
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "dwmapi.lib")
extern void ESShowPreferences();
#endif

namespace DisplayOutput {

namespace {

#ifdef WIN32
// Shared D3D11 device/context across all DX11 windows so decoded textures
// can be rendered into multiple swapchains (one per monitor window).
static std::mutex g_sharedD3DInitMutex;
static ComPtr<ID3D11Device> g_sharedDevice;
static ComPtr<ID3D11DeviceContext> g_sharedContext;

enum DX11MenuCmd : UINT
{
    ID_FILE_PREFERENCES = 0xE100,
    ID_FILE_EXIT,
    ID_VIEW_FULLSCREEN,
    ID_TOOLS_REMOTE,
    ID_TOOLS_PLAYLISTS,
    ID_HELP_ONLINE,
    ID_HELP_CHECK_UPDATES,
    ID_HELP_ABOUT,
};

static void OpenInfinidreamWebUrl(int which);
static void AppendKeyEvent(CKeyEvent::eKeyCode code, bool dedupeF1);

static int GetSystemTitlebarHeightPx()
{
    const int caption = GetSystemMetrics(SM_CYCAPTION);
    const int frame = GetSystemMetrics(SM_CYSIZEFRAME);
    const int padded = GetSystemMetrics(SM_CXPADDEDBORDER);
    const int h = caption + frame + padded;
    return (h > 0) ? h : 0;
}

static int GetResizeBorderThicknessPx()
{
    const int frame = GetSystemMetrics(SM_CXSIZEFRAME);
    const int padded = GetSystemMetrics(SM_CXPADDEDBORDER);
    const int t = frame + padded;
    return (t > 0) ? t : 1;
}

static bool PtInRectClient(const RECT& rc, const POINT& ptClient)
{
    return ptClient.x >= rc.left && ptClient.x < rc.right && ptClient.y >= rc.top && ptClient.y < rc.bottom;
}

static DWORD GetDefaultWindowedStyle(bool useCustomChrome)
{
    return useCustomChrome ? (WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
                           : WS_OVERLAPPEDWINDOW;
}

static void ComputeCaptionButtonRects(HWND hWnd, int titlebarHeightPx, RECT& outMin, RECT& outMax, RECT& outClose)
{
    RECT client{};
    GetClientRect(hWnd, &client);
    const int w = client.right - client.left;

    const int btnH = (titlebarHeightPx > 0) ? titlebarHeightPx : (std::max)(GetSystemMetrics(SM_CYSIZE), 20);
    const int baseW = (std::max)(GetSystemMetrics(SM_CXSIZE), 30);
    const int baseH = (std::max)(GetSystemMetrics(SM_CYSIZE), 20);
    // Scale width proportionally so buttons stay "Explorer-like" when titlebar height changes.
    int btnW = (baseH > 0) ? MulDiv(baseW, btnH, baseH) : baseW;
    btnW = (std::max)(btnW, (std::max)(40, baseW));
    const int topPad = 0;

    // Right-aligned: [min][max][close]
    outClose = {w - btnW, topPad, w, topPad + btnH};
    outMax = {w - (2 * btnW), topPad, w - btnW, topPad + btnH};
    outMin = {w - (3 * btnW), topPad, w - (2 * btnW), topPad + btnH};
}

static RECT ComputeTitlebarLogoRect(int titlebarHeightPx)
{
    // Must match the DX titlebar overlay's logo placement.
    constexpr int kLogoPx = 18;
    constexpr int kLogoPadPx = 10;
    const int h = (titlebarHeightPx > 0) ? titlebarHeightPx : kLogoPx;
    const int y = (std::max)(0, (h - kLogoPx) / 2);
    RECT r{};
    r.left = kLogoPadPx;
    r.top = y;
    r.right = kLogoPadPx + kLogoPx;
    r.bottom = y + kLogoPx;
    return r;
}

static void ComputeTitlebarLeftButtonRects(int titlebarHeightPx, const RECT& logoRect,
                                           RECT& outGear, RECT& outFullscreen, RECT& outMenu)
{
    // Must match the DX titlebar overlay's left-button placement.
    constexpr int kLogoPadPx = 10;   // right padding of logo (must equal left padding)
    constexpr int kSplitterPx = 1;   // vertical separator width
    constexpr int kGapPx = 8;        // gap after splitter before first button
    const int btnH = (titlebarHeightPx > 0) ? titlebarHeightPx : 24;
    // Make left buttons a bit narrower than tall (closer to Windows caption affordances).
    int btnW = static_cast<int>(std::round(static_cast<double>(btnH) * 0.62));
    btnW = (std::max)(btnW, 24);
    int x = logoRect.right + kLogoPadPx + kSplitterPx + kGapPx;

    outGear = {x, 0, x + btnW, btnH};
    x += btnW;
    outFullscreen = {x, 0, x + btnW, btnH};
    x += btnW;
    outMenu = {x, 0, x + btnW, btnH};
}

static void ShowSystemMenuAtScreenPoint(HWND hWnd, POINT ptScreen)
{
    if (!hWnd || !IsWindow(hWnd))
        return;
    HMENU sys = GetSystemMenu(hWnd, FALSE);
    if (!sys)
        return;

    // Required quirk: make the window foreground before TrackPopupMenu, otherwise the menu can dismiss immediately.
    SetForegroundWindow(hWnd);
    const UINT cmd = TrackPopupMenuEx(sys,
                                     TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                     ptScreen.x, ptScreen.y,
                                     hWnd, nullptr);
    if (cmd != 0)
        SendMessageW(hWnd, WM_SYSCOMMAND, cmd, 0);
}

static void ShowTitlebarOverflowMenu(HWND hWnd, POINT ptScreen)
{
    if (!hWnd || !IsWindow(hWnd))
        return;

    HMENU popup = CreatePopupMenu();
    if (!popup)
        return;

    AppendMenuW(popup, MF_STRING, ID_HELP_ABOUT, L"&About infinidream");
    AppendMenuW(popup, MF_STRING, ID_TOOLS_REMOTE, L"&Remote Control\tCtrl+R");
    AppendMenuW(popup, MF_STRING, ID_TOOLS_PLAYLISTS, L"&Browse Playlists\tCtrl+B");

    SetForegroundWindow(hWnd);
    const UINT cmd = TrackPopupMenuEx(popup,
                                     TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                     ptScreen.x, ptScreen.y,
                                     hWnd, nullptr);
    DestroyMenu(popup);
    if (cmd != 0)
        SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

static void PopulateSystemMenu(HWND hWnd)
{
    if (!hWnd || !IsWindow(hWnd))
        return;

    HMENU sys = GetSystemMenu(hWnd, FALSE);
    if (!sys)
        return;

    // Avoid duplicate inserts if window/menu gets recreated.
    if (GetMenuState(sys, ID_FILE_PREFERENCES, MF_BYCOMMAND) != static_cast<UINT>(-1))
        return;

    // Insert before Close for a standard placement.
    InsertMenuW(sys, SC_CLOSE, MF_BYCOMMAND | MF_STRING, ID_HELP_ABOUT, L"&About infinidream");
    InsertMenuW(sys, SC_CLOSE, MF_BYCOMMAND | MF_STRING, ID_TOOLS_PLAYLISTS, L"&Browse Playlists\tCtrl+B");
    InsertMenuW(sys, SC_CLOSE, MF_BYCOMMAND | MF_STRING, ID_TOOLS_REMOTE, L"&Remote Control\tCtrl+R");
    InsertMenuW(sys, SC_CLOSE, MF_BYCOMMAND | MF_STRING, ID_FILE_PREFERENCES, L"&Settings\tCtrl+,");
    InsertMenuW(sys, SC_CLOSE, MF_BYCOMMAND | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(sys, SC_CLOSE, MF_BYCOMMAND | MF_STRING, ID_VIEW_FULLSCREEN, L"&Full screen\tF11");
}

static bool HandleAppCommand(HWND hWnd, CDisplayDX11* self, UINT cmd)
{
    (void)self;
    switch (cmd)
    {
    case ID_FILE_PREFERENCES:
        ESShowPreferences();
        return true;
    case ID_FILE_EXIT:
        DestroyWindow(hWnd);
        return true;
    case ID_VIEW_FULLSCREEN:
        // Keep behavior consistent with the existing hotkey path.
        AppendKeyEvent(CKeyEvent::KEY_F11, false);
        return true;
    case ID_TOOLS_REMOTE:
        OpenInfinidreamWebUrl(0);
        return true;
    case ID_TOOLS_PLAYLISTS:
        OpenInfinidreamWebUrl(1);
        return true;
    case ID_HELP_ONLINE:
        OpenInfinidreamWebUrl(2);
        return true;
    case ID_HELP_CHECK_UPDATES:
        return true;
    case ID_HELP_ABOUT:
        AboutDialogWin32_RequestShow();
        return true;
    default:
        return false;
    }
}

static void OpenInfinidreamWebUrl(int which)
{
    // 0 = remote, 1 = playlists, 2 = help (matches Mac ESWindow URLs)
#ifdef STAGE
    constexpr const char* kRemote = "https://stage.infinidream.ai/rc";
    constexpr const char* kPlaylists = "https://stage.infinidream.ai/playlists";
    constexpr const char* kHelp = "https://stage.infinidream.ai/help";
#else
    constexpr const char* kRemote = "https://alpha.infinidream.ai/rc";
    constexpr const char* kPlaylists = "https://alpha.infinidream.ai/playlists";
    constexpr const char* kHelp = "https://alpha.infinidream.ai/help";
#endif
    const char* url = kRemote;
    if (which == 1)
        url = kPlaylists;
    else if (which == 2)
        url = kHelp;
    PlatformUtils::OpenURLExternally(url);
}
#endif // WIN32

#ifdef WIN32
static bool SetDwmImmersiveDarkMode(HWND hWnd, bool enabled)
{
    if (!hWnd)
        return false;
    const BOOL use = enabled ? TRUE : FALSE;

    // Windows 10 has used both 19 and 20 across builds; try both.
    const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_20 = 20;
    const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_19 = 19;

    HRESULT hr = DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_20, &use, sizeof(use));
    if (SUCCEEDED(hr))
        return true;
    hr = DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_19, &use, sizeof(use));
    return SUCCEEDED(hr);
}
#endif

// Avoid duplicate F1 enqueue when WM_HELP and WM_KEYDOWN arrive for the same press.
static ULONGLONG g_lastF1EnqueueTickMs = 0;
constexpr ULONGLONG kF1DuplicateWindowMs = 250;

// Timer IDs used in WndProc.
constexpr UINT_PTR kPreviewTimerId    = 1; // 500 ms one-shot for screensaver preview init
constexpr UINT_PTR kMenuLoopTimerId   = 2; // ~60 fps keep-alive while menu bar is open
constexpr float kTargetClientAspect16By9 = 16.0f / 9.0f;

static void AppendMouseEvent(CMouseEvent::eMouseCode code, LPARAM lParam)
{
    if (code == CMouseEvent::Mouse_MOVE)
    {
        const POINTS pt = MAKEPOINTS(lParam);
        PlatformUtils::NotifyMouseMoved(static_cast<int>(pt.x),
                                        static_cast<int>(pt.y));
    }
    auto spEvent = std::make_shared<CMouseEvent>();
    spEvent->m_Code = code;
    spEvent->m_X = MAKEPOINTS(lParam).x;
    spEvent->m_Y = MAKEPOINTS(lParam).y;
    if (auto spD = g_Player().Display())
        spD->AppendEvent(spEvent);
}

static void LayoutFullscreenSaverWindow(HWND hwnd, IDXGISwapChain* swapChain)
{
    if (!hwnd)
        return;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(mon, &mi))
        return;

    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOPMOST) == 0)
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex | WS_EX_TOPMOST | WS_EX_APPWINDOW);

    SetWindowPos(hwnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    if (swapChain)
    {
        const HRESULT hr = swapChain->SetFullscreenState(TRUE, nullptr);
        if (FAILED(hr))
            g_Log->Warning("SetFullscreenState(TRUE) after layout failed: %08X", hr);
    }
}

struct MonitorInfoByIndex
{
    uint32_t idx = 0;
    uint32_t target = 0;
    bool found = false;
    RECT rc = {};
};

static BOOL CALLBACK EnumMonitorForIndex(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
{
    auto* st = reinterpret_cast<MonitorInfoByIndex*>(lp);
    if (!st)
        return TRUE;

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hMon, &mi))
        return TRUE;

    if (st->idx == st->target)
    {
        st->found = true;
        st->rc = mi.rcMonitor;
        return FALSE; // stop enumeration
    }

    st->idx++;
    return TRUE;
}

static bool GetMonitorRectByIndex(uint32_t index, RECT& outRc)
{
    MonitorInfoByIndex st;
    st.target = index;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorForIndex, reinterpret_cast<LPARAM>(&st));
    if (!st.found)
        return false;
    outRc = st.rc;
    return true;
}

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

static bool IsPreserveAspectEnabled()
{
    if (!g_Settings())
        return false;
    return g_Settings()->Get("settings.player.preserve_AR", false);
}

static void EnforceSizingAspect16By9(HWND hWnd, WPARAM edge, RECT* rc)
{
    if (!rc)
        return;

    RECT currentWindow = {};
    RECT currentClient = {};
    if (!GetWindowRect(hWnd, &currentWindow) || !GetClientRect(hWnd, &currentClient))
        return;

    const LONG frameW = (currentWindow.right - currentWindow.left) -
                        (currentClient.right - currentClient.left);
    const LONG frameH = (currentWindow.bottom - currentWindow.top) -
                        (currentClient.bottom - currentClient.top);
    if (frameW < 0 || frameH < 0)
        return;

    LONG windowW = rc->right - rc->left;
    LONG windowH = rc->bottom - rc->top;
    LONG clientW = std::max<LONG>(1, windowW - frameW);
    LONG clientH = std::max<LONG>(1, windowH - frameH);

    auto widthDriven = [&](LONG w) -> LONG {
        const LONG h = static_cast<LONG>((static_cast<double>(w) / kTargetClientAspect16By9) + 0.5);
        return std::max<LONG>(1, h);
    };
    auto heightDriven = [&](LONG h) -> LONG {
        const LONG w = static_cast<LONG>((static_cast<double>(h) * kTargetClientAspect16By9) + 0.5);
        return std::max<LONG>(1, w);
    };

    LONG targetClientW = clientW;
    LONG targetClientH = clientH;
    switch (edge)
    {
    case WMSZ_LEFT:
    case WMSZ_RIGHT:
        targetClientH = widthDriven(clientW);
        break;
    case WMSZ_TOP:
    case WMSZ_BOTTOM:
        targetClientW = heightDriven(clientH);
        break;
    default:
    {
        const LONG byWidthH = widthDriven(clientW);
        const LONG byHeightW = heightDriven(clientH);
        const LONG errWidth = std::abs(byWidthH - clientH);
        const LONG errHeight = std::abs(byHeightW - clientW);
        if (errWidth <= errHeight)
            targetClientH = byWidthH;
        else
            targetClientW = byHeightW;
        break;
    }
    }

    const LONG targetWindowW = targetClientW + frameW;
    const LONG targetWindowH = targetClientH + frameH;

    switch (edge)
    {
    case WMSZ_LEFT:
        rc->left = rc->right - targetWindowW;
        rc->bottom = rc->top + targetWindowH;
        break;
    case WMSZ_RIGHT:
        rc->right = rc->left + targetWindowW;
        rc->bottom = rc->top + targetWindowH;
        break;
    case WMSZ_TOP:
        rc->top = rc->bottom - targetWindowH;
        rc->right = rc->left + targetWindowW;
        break;
    case WMSZ_BOTTOM:
        rc->bottom = rc->top + targetWindowH;
        rc->right = rc->left + targetWindowW;
        break;
    case WMSZ_TOPLEFT:
        rc->left = rc->right - targetWindowW;
        rc->top = rc->bottom - targetWindowH;
        break;
    case WMSZ_TOPRIGHT:
        rc->right = rc->left + targetWindowW;
        rc->top = rc->bottom - targetWindowH;
        break;
    case WMSZ_BOTTOMLEFT:
        rc->left = rc->right - targetWindowW;
        rc->bottom = rc->top + targetWindowH;
        break;
    case WMSZ_BOTTOMRIGHT:
        rc->right = rc->left + targetWindowW;
        rc->bottom = rc->top + targetWindowH;
        break;
    default:
        rc->right = rc->left + targetWindowW;
        rc->bottom = rc->top + targetWindowH;
        break;
    }
}

} // namespace

CDisplayDX11::CDisplayDX11()
    : CDisplayOutput(), m_WindowHandle(nullptr),
      m_windowedStyle(0), m_windowedExStyle(0), m_hasWindowedRect(false),
      m_bWaitForPreviewPump(false), m_bEmbeddedSaverPreview(false),
      m_bMenuRenderInProgress(false) {
    m_windowedRect = {0, 0, 0, 0};
    g_Log->Info("CDisplayDX11()");

#ifdef WIN32
    m_customTitlebarHeightPx = GetSystemTitlebarHeightPx();
#endif
}

bool CDisplayDX11::EnsureWindowClassRegistered(HINSTANCE hInstance)
{
    WNDCLASSEXW wcProbe = {};
    if (GetClassInfoExW(hInstance, L"EDreamDX11Class", &wcProbe))
        return true;

    HICON appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = appIcon ? appIcon : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"EDreamDX11Class";

    if (RegisterClassExW(&wc))
        return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
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
    if (AboutDialogWin32_TryConsumeWndProc(hWnd, msg, wParam, lParam, &imguiHandled))
        return imguiHandled;
    if (FirstTimeSetupWin32_TryConsumeWndProc(hWnd, msg, wParam, lParam, &imguiHandled))
        return imguiHandled;
#endif

    switch (msg)
    {
#ifdef WIN32
    case WM_NCCALCSIZE:
    {
        // Eliminate the standard non-client frame so our custom titlebar strip can sit
        // flush at the very top of the window (no tiny gap from WS_THICKFRAME).
        if (!self || self->m_bFullScreen || self->m_bEmbeddedSaverPreview || !self->m_useCustomWindowChrome)
            break;

        const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
        const bool hasNativeCaption = (style & WS_CAPTION) != 0;
        if (hasNativeCaption)
            break;

        if (wParam)
        {
            // When maximized, keep a small inset so the window doesn't overlap the monitor edges/taskbar.
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (params && IsZoomed(hWnd))
            {
                const int inset = GetResizeBorderThicknessPx();
                params->rgrc[0].left += inset;
                params->rgrc[0].top += inset;
                params->rgrc[0].right -= inset;
                params->rgrc[0].bottom -= inset;
            }
            return 0;
        }

        return 0;
    }

    case WM_NCHITTEST:
    {
        // For our captionless windowed style we must provide:
        // - resize border hit tests
        // - draggable region hit test (HTCAPTION) excluding custom caption buttons
        if (!self || self->m_bFullScreen || self->m_bEmbeddedSaverPreview)
            break;

        const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
        const bool hasNativeCaption = (style & WS_CAPTION) != 0;
        if (hasNativeCaption || !self->m_useCustomWindowChrome)
            break;

        const POINT ptScreen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        POINT ptClient = ptScreen;
        ScreenToClient(hWnd, &ptClient);

        RECT rc{};
        GetClientRect(hWnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        // When maximized, the client rect may already be inset via WM_NCCALCSIZE. Treat the
        // top/edges as client so caption buttons remain clickable.
        const int border = IsZoomed(hWnd) ? 0 : GetResizeBorderThicknessPx();

        const bool left = ptClient.x < border;
        const bool right = ptClient.x >= (w - border);
        const bool top = ptClient.y < border;
        const bool bottom = ptClient.y >= (h - border);

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        const int titlebarH = (self->m_customTitlebarHeightPx > 0) ? self->m_customTitlebarHeightPx
                                                                   : GetSystemTitlebarHeightPx();
        RECT minRc{}, maxRc{}, closeRc{};
        ComputeCaptionButtonRects(hWnd, titlebarH, minRc, maxRc, closeRc);
        self->m_captionBtnMinRect = minRc;
        self->m_captionBtnMaxRect = maxRc;
        self->m_captionBtnCloseRect = closeRc;
        self->m_titlebarLogoRect = ComputeTitlebarLogoRect(titlebarH);
        ComputeTitlebarLeftButtonRects(titlebarH, self->m_titlebarLogoRect,
                                       self->m_titlebarGearRect, self->m_titlebarFullscreenRect, self->m_titlebarMenuRect);
        self->m_customTitlebarHeightPx = titlebarH;

        if (ptClient.y >= 0 && ptClient.y < titlebarH)
        {
            if (PtInRectClient(self->m_titlebarLogoRect, ptClient) ||
                PtInRectClient(self->m_titlebarGearRect, ptClient) ||
                PtInRectClient(self->m_titlebarFullscreenRect, ptClient) ||
                PtInRectClient(self->m_titlebarMenuRect, ptClient) ||
                PtInRectClient(minRc, ptClient) || PtInRectClient(maxRc, ptClient) || PtInRectClient(closeRc, ptClient))
                return HTCLIENT;
            return HTCAPTION;
        }

        return HTCLIENT;
    }

    case WM_SETCURSOR:
        // In fullscreen screensaver mode, the OS may repeatedly reset the cursor
        // to the class cursor (IDC_ARROW). Keep it hidden deterministically.
        if (self && self->m_bFullScreen && !self->m_bEmbeddedSaverPreview)
        {
            SetCursor(nullptr);
            return TRUE;
        }
        // Windowed mode: defer to DefWindowProc so it picks the right cursor based on the
        // WM_NCHITTEST result — IDC_SIZEWE / IDC_SIZENS / etc. on the resize borders.
        // Returning 0 (the default switch fall-through) leaves the arrow cursor in place
        // and the resize handles become invisible, even though clicking the border still
        // initiates the resize via DefWindowProc's WM_NCLBUTTONDOWN handling.
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_PAINT:
    {
        // D3D11 presents continuously; custom titlebar is drawn as a DX overlay (not GDI)
        // to avoid compositor flashing. Keep WM_PAINT lightweight.
        PAINTSTRUCT ps{};
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_COMMAND:
    {
        if (HIWORD(wParam) != 0)
            break;
        if (HandleAppCommand(hWnd, self, static_cast<UINT>(LOWORD(wParam))))
            return 0;
        break;
    }

    case WM_SYSCOMMAND:
    {
        const UINT cmd = static_cast<UINT>(wParam & 0xFFFFu);
        if (HandleAppCommand(hWnd, self, cmd))
            return 0;
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
#endif

    case WM_HELP:
        // Fallback path: some Windows setups surface F1 as help message.
        AppendKeyEvent(CKeyEvent::KEY_F1, true);
        return 0;

    case WM_USER:
        // Let the screen-saver control panel finish painting; same pattern as
        // legacy D3D9 (DisplayDX).
        SetTimer(hWnd, kPreviewTimerId, 500, NULL);
        g_Log->Info("Starting 500ms preview timer");
        return 0;

    case WM_TIMER:
        if (wParam == kPreviewTimerId)
        {
            if (self)
                self->m_bWaitForPreviewPump = false;
            KillTimer(hWnd, kPreviewTimerId);
            g_Log->Info("500ms preview timer done");
            return 0;
        }
        if (wParam == kMenuLoopTimerId)
        {
            if (self && self->m_menuLoopRenderCb && !self->m_bMenuRenderInProgress)
            {
                self->m_bMenuRenderInProgress = true;
                self->m_menuLoopRenderCb();
                self->m_bMenuRenderInProgress = false;
            }
            return 0;
        }
        break;

    case WM_ENTERMENULOOP:
        SetTimer(hWnd, kMenuLoopTimerId, 16, nullptr);
        return 0;

    case WM_EXITMENULOOP:
        KillTimer(hWnd, kMenuLoopTimerId);
        return 0;

    case WM_SIZE:
        if (self && wParam != SIZE_MINIMIZED)
        {
            const UINT nw = static_cast<UINT>(LOWORD(lParam));
            const UINT nh = static_cast<UINT>(HIWORD(lParam));
            if (nw > 0 && nh > 0)
                self->ResizeSwapChain(nw, nh);
        }
        // Aero snap (Win+Left/Right) and other programmatic SetWindowPos paths bypass WM_SIZING,
        // so the aspect handler never sees them. If preserve_AR is on, defer a snap-to-16:9 via
        // the message pump — SnapWindowTo16By9IfNeeded early-returns when already 16:9, so the
        // deferred handler is idempotent and re-posting on every WM_SIZE is safe.
        if (wParam != SIZE_MINIMIZED && wParam != SIZE_MAXIMIZED && IsPreserveAspectEnabled())
            PostMessageW(hWnd, kSettingsDialogWin32SnapAfterCloseMsg, 0, 0);
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_SIZING:
        if (self && IsPreserveAspectEnabled())
        {
            EnforceSizingAspect16By9(hWnd, wParam, reinterpret_cast<RECT*>(lParam));
            return TRUE;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_KEYDOWN: {
        // Match macOS keyDown: handle hotkeys on press, not release (WM_KEYUP felt delayed).
        // lParam bit 30: previous key state (1 = autorepeat); ignore repeats.
        if ((lParam >> 30) & 1)
            break;

        const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

#ifdef WIN32
        if (ctrlDown && (SettingsDialogWin32_HasPendingOrVisible() || FirstTimeSetupWin32_IsWizardVisible()))
            return 0;
#endif

        if (wParam == VK_OEM_COMMA && ctrlDown)
        {
            ESShowPreferences();
            return 0;
        }

        // Match Mac menu: Command-R / Command-B (help text: Control-R / Control-B on Windows).
        if (ctrlDown && (wParam == 'R' || wParam == 'B'))
        {
            OpenInfinidreamWebUrl(wParam == 'R' ? 0 : 1);
            return 0;
        }

        // Match Mac Quit (Cmd+Q): Ctrl+Q exits (same as File → Exit).
        if (ctrlDown && wParam == 'Q')
        {
            DestroyWindow(hWnd);
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

    case WM_LBUTTONUP:
    {
        // If using custom chrome, map clicks on the top-right caption button rects to actions.
        if (self && !self->m_bFullScreen && !self->m_bEmbeddedSaverPreview && self->m_useCustomWindowChrome)
        {
            const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
            const bool hasNativeCaption = (style & WS_CAPTION) != 0;
            if (!hasNativeCaption)
            {
                // Update rects for the current client size.
                const int titlebarH = (self->m_customTitlebarHeightPx > 0) ? self->m_customTitlebarHeightPx
                                                                           : GetSystemTitlebarHeightPx();
                RECT minRc{}, maxRc{}, closeRc{};
                ComputeCaptionButtonRects(hWnd, titlebarH, minRc, maxRc, closeRc);
                self->m_captionBtnMinRect = minRc;
                self->m_captionBtnMaxRect = maxRc;
                self->m_captionBtnCloseRect = closeRc;
                self->m_titlebarLogoRect = ComputeTitlebarLogoRect(titlebarH);
                ComputeTitlebarLeftButtonRects(titlebarH, self->m_titlebarLogoRect,
                                               self->m_titlebarGearRect, self->m_titlebarFullscreenRect, self->m_titlebarMenuRect);

                const POINT ptClient{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int pressed = self->m_captionBtnPressed;
                self->m_captionBtnPressed = 0;
                if (GetCapture() == hWnd)
                    ReleaseCapture();

                if (PtInRectClient(self->m_titlebarLogoRect, ptClient))
                {
                    // Classic behavior: system menu appears anchored under the icon,
                    // aligned to the icon's bottom-left (not the click point).
                    POINT anchorClient{self->m_titlebarLogoRect.left, self->m_titlebarLogoRect.bottom};
                    POINT anchorScreen = anchorClient;
                    ClientToScreen(hWnd, &anchorScreen);
                    ShowSystemMenuAtScreenPoint(hWnd, anchorScreen);
                    return 0;
                }

                if (PtInRectClient(self->m_titlebarGearRect, ptClient))
                {
                    if (pressed == 4)
                        SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_FILE_PREFERENCES, 0), 0);
                    return 0;
                }
                if (PtInRectClient(self->m_titlebarFullscreenRect, ptClient))
                {
                    if (pressed == 5)
                        SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_VIEW_FULLSCREEN, 0), 0);
                    return 0;
                }
                if (PtInRectClient(self->m_titlebarMenuRect, ptClient))
                {
                    if (pressed == 6)
                    {
                        POINT anchorClient{self->m_titlebarMenuRect.left, self->m_titlebarMenuRect.bottom};
                        POINT anchorScreen = anchorClient;
                        ClientToScreen(hWnd, &anchorScreen);
                        ShowTitlebarOverflowMenu(hWnd, anchorScreen);
                    }
                    return 0;
                }

                if (PtInRectClient(self->m_captionBtnCloseRect, ptClient))
                {
                    if (pressed == 3)
                        self->WindowClose();
                    return 0;
                }
                if (PtInRectClient(self->m_captionBtnMaxRect, ptClient))
                {
                    if (pressed == 2)
                        self->WindowToggleMaximizeRestore();
                    return 0;
                }
                if (PtInRectClient(self->m_captionBtnMinRect, ptClient))
                {
                    if (pressed == 1)
                        self->WindowMinimize();
                    return 0;
                }
            }
        }

        AppendMouseEvent(CMouseEvent::Mouse_LEFT, lParam);
        return 0;
    }

    case WM_RBUTTONUP:
        AppendMouseEvent(CMouseEvent::Mouse_RIGHT, lParam);
        return 0;

    case WM_MOUSEMOVE:
    {
        if (self && !self->m_bFullScreen && !self->m_bEmbeddedSaverPreview && self->m_useCustomWindowChrome)
        {
            const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
            const bool hasNativeCaption = (style & WS_CAPTION) != 0;
            if (!hasNativeCaption)
            {
                const int titlebarH = (self->m_customTitlebarHeightPx > 0) ? self->m_customTitlebarHeightPx
                                                                           : GetSystemTitlebarHeightPx();
                RECT minRc{}, maxRc{}, closeRc{};
                ComputeCaptionButtonRects(hWnd, titlebarH, minRc, maxRc, closeRc);
                self->m_captionBtnMinRect = minRc;
                self->m_captionBtnMaxRect = maxRc;
                self->m_captionBtnCloseRect = closeRc;
                self->m_titlebarLogoRect = ComputeTitlebarLogoRect(titlebarH);
                ComputeTitlebarLeftButtonRects(titlebarH, self->m_titlebarLogoRect,
                                               self->m_titlebarGearRect, self->m_titlebarFullscreenRect, self->m_titlebarMenuRect);

                const POINT ptClient{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                int hover = 0;
                if (PtInRectClient(self->m_titlebarGearRect, ptClient))
                    hover = 4;
                else if (PtInRectClient(self->m_titlebarFullscreenRect, ptClient))
                    hover = 5;
                else if (PtInRectClient(self->m_titlebarMenuRect, ptClient))
                    hover = 6;
                else if (PtInRectClient(self->m_captionBtnMinRect, ptClient))
                    hover = 1;
                else if (PtInRectClient(self->m_captionBtnMaxRect, ptClient))
                    hover = 2;
                else if (PtInRectClient(self->m_captionBtnCloseRect, ptClient))
                    hover = 3;

                if (hover != self->m_captionBtnHover)
                {
                    self->m_captionBtnHover = hover;
                }
            }
        }

        AppendMouseEvent(CMouseEvent::Mouse_MOVE, lParam);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (self && !self->m_bFullScreen && !self->m_bEmbeddedSaverPreview && self->m_useCustomWindowChrome)
        {
            const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
            const bool hasNativeCaption = (style & WS_CAPTION) != 0;
            if (!hasNativeCaption)
            {
                const int titlebarH = (self->m_customTitlebarHeightPx > 0) ? self->m_customTitlebarHeightPx
                                                                           : GetSystemTitlebarHeightPx();
                RECT minRc{}, maxRc{}, closeRc{};
                ComputeCaptionButtonRects(hWnd, titlebarH, minRc, maxRc, closeRc);
                self->m_captionBtnMinRect = minRc;
                self->m_captionBtnMaxRect = maxRc;
                self->m_captionBtnCloseRect = closeRc;
                self->m_titlebarLogoRect = ComputeTitlebarLogoRect(titlebarH);
                ComputeTitlebarLeftButtonRects(titlebarH, self->m_titlebarLogoRect,
                                               self->m_titlebarGearRect, self->m_titlebarFullscreenRect, self->m_titlebarMenuRect);

                const POINT ptClient{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                int pressed = 0;
                if (PtInRectClient(self->m_titlebarGearRect, ptClient))
                    pressed = 4;
                else if (PtInRectClient(self->m_titlebarFullscreenRect, ptClient))
                    pressed = 5;
                else if (PtInRectClient(self->m_titlebarMenuRect, ptClient))
                    pressed = 6;
                else if (PtInRectClient(self->m_captionBtnMinRect, ptClient))
                    pressed = 1;
                else if (PtInRectClient(self->m_captionBtnMaxRect, ptClient))
                    pressed = 2;
                else if (PtInRectClient(self->m_captionBtnCloseRect, ptClient))
                    pressed = 3;
                self->m_captionBtnPressed = pressed;
                if (pressed != 0)
                {
                    SetCapture(hWnd);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_CAPTURECHANGED:
        if (self)
            self->m_captionBtnPressed = 0;
        break;

    case WM_POWERBROADCAST:
        switch (LOWORD(wParam))
        {
        case PBT_APMBATTERYLOW:
        case PBT_APMSUSPEND:
        {
            auto spEvent = std::make_shared<CPowerEvent>();
            if (auto spD = g_Player().Display())
                spD->AppendEvent(spEvent);
        }
        break;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        if (msg == kSettingsDialogWin32SnapAfterCloseMsg)
        {
            SettingsDialogWin32_HandleDeferredSnap();
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

#ifdef WIN32
HWND CDisplayDX11::CreateDisplayWindow(uint32_t w, uint32_t h, bool fullscreen,
                                       HMENU hMenu) {
    HMODULE hInstance = GetModuleHandle(NULL);
    if (!EnsureWindowClassRegistered(hInstance))
        return nullptr;

    DWORD style = fullscreen ? WS_POPUP : GetDefaultWindowedStyle(m_useCustomWindowChrome);
    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;

    if (!fullscreen)
    {
        // Manifest is system-DPI-aware (electricsheep.vcxproj), so CreateWindow takes
        // physical pixels and DX11 swap chains aren't bitmap-virtualized — meaning a
        // 1920x1080 window stays 1920 physical pixels even on a high-DPI external monitor
        // (1/4 of a 4K screen at 200%). Pick the cursor's monitor, query its effective DPI,
        // scale the requested size accordingly, and centre the window on that monitor so
        // CW_USEDEFAULT doesn't drop it onto the primary at a different scale.
        POINT pt{};
        HMONITOR mon = nullptr;
        if (GetCursorPos(&pt))
            mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (!mon)
            mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

        UINT dpiX = 96;
        UINT dpiY = 96;
        if (mon)
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (dpiX > 0 && dpiY > 0)
        {
            w = MulDiv(w, dpiX, 96);
            h = MulDiv(h, dpiY, 96);
        }

        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfo(mon, &mi))
        {
            const LONG waW = mi.rcWork.right - mi.rcWork.left;
            const LONG waH = mi.rcWork.bottom - mi.rcWork.top;
            // Compute chrome thickness once (AdjustWindowRect with a zero rect returns just
            // the borders/caption). Uniform-scale the client size so the design aspect ratio
            // (16:9 for 1920x1080) is preserved when the work area can't fit it. Independent
            // per-axis shrinking would skew the window — and since SettingsDialogWin32 snaps
            // back to 16:9 on close (preserve_AR), a non-16:9 startup window triggers a
            // SetWindowPos at close time, with subtle re-entrancy hazards.
            RECT chrome = {0, 0, 0, 0};
            AdjustWindowRect(&chrome, style, hMenu ? TRUE : FALSE);
            const LONG chromeW = chrome.right - chrome.left;
            const LONG chromeH = chrome.bottom - chrome.top;
            const LONG maxClientW = (waW > chromeW) ? (waW - chromeW) : 1;
            const LONG maxClientH = (waH > chromeH) ? (waH - chromeH) : 1;
            double scale = 1.0;
            if (static_cast<LONG>(w) > maxClientW)
                scale = (std::min)(scale, static_cast<double>(maxClientW) / static_cast<double>(w));
            if (static_cast<LONG>(h) > maxClientH)
                scale = (std::min)(scale, static_cast<double>(maxClientH) / static_cast<double>(h));
            if (scale < 1.0)
            {
                w = static_cast<uint32_t>(static_cast<double>(w) * scale);
                h = static_cast<uint32_t>(static_cast<double>(h) * scale);
            }
            RECT outer = {0, 0, (LONG)w, (LONG)h};
            AdjustWindowRect(&outer, style, hMenu ? TRUE : FALSE);
            posX = mi.rcWork.left + (waW - (outer.right - outer.left)) / 2;
            posY = mi.rcWork.top + (waH - (outer.bottom - outer.top)) / 2;
        }
    }

    // Reflect the (possibly scaled and clamped) client size so the swap chain matches.
    m_Width = w;
    m_Height = h;

    if (!fullscreen)
        m_customTitlebarHeightPx = GetSystemTitlebarHeightPx();

    RECT rc = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&rc, style, hMenu ? TRUE : FALSE);

    const DWORD exStyle =
        fullscreen ? (WS_EX_APPWINDOW | WS_EX_TOPMOST) : 0UL;

    int x = posX;
    int y = posY;
    if (fullscreen && m_hasTargetMonitorRect)
    {
        x = m_targetMonitorRect.left;
        y = m_targetMonitorRect.top;
    }

    return CreateWindowExW(exStyle, L"EDreamDX11Class", L"infinidream", style,
                           x, y, rc.right - rc.left,
                           rc.bottom - rc.top, nullptr, hMenu, hInstance, this);
}

void CDisplayDX11::ApplyDarkTitlebarIfSupported()
{
    if (!m_WindowHandle || !IsWindow(m_WindowHandle))
        return;
    SetDwmImmersiveDarkMode(m_WindowHandle, true);
}
#endif

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

    const bool wantExclusiveFullscreen = m_bFullScreen && !m_disableExclusiveFullscreen;
    sd.Windowed = !wantExclusiveFullscreen;

    // D3D11_CREATE_DEVICE_VIDEO_SUPPORT is REQUIRED because this same ID3D11Device
    // is shared with FFmpeg's D3D11VA decoder (see CContentDecoder::Open).  Without
    // it, the driver's ID3D11VideoDevice/ID3D11VideoContext interfaces sit on
    // partially-initialized state and the decode command ring eventually corrupts,
    // causing DXGI_ERROR_DEVICE_RESET (0x887A0006) after minutes of sustained use.
    //
    // D3D11_CREATE_DEVICE_BGRA_SUPPORT is harmless and commonly paired with video.
    UINT createDeviceFlags =
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = S_OK;

    {
        std::lock_guard<std::mutex> lock(g_sharedD3DInitMutex);
        if (!g_sharedDevice || !g_sharedContext)
        {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                createDeviceFlags, featureLevels, 1,
                D3D11_SDK_VERSION, &g_sharedDevice, nullptr, &g_sharedContext);

            if (FAILED(hr) || !g_sharedDevice || !g_sharedContext)
            {
                g_Log->Error("Failed to create shared D3D11 device/context");
                return false;
            }

#if defined(WIN32) || defined(_WIN32) || defined(_WIN64)
            // Enable device-level multithread protection once on the shared context.
            {
                ComPtr<ID3D11Multithread> mt;
                if (SUCCEEDED(g_sharedContext.As(&mt)) && mt)
                {
                    mt->SetMultithreadProtected(TRUE);
                }
                else
                {
                    g_Log->Warning("ID3D11Multithread unavailable; decode/render races possible");
                }
            }
#endif
        }
    }

    m_device = g_sharedDevice;
    m_context = g_sharedContext;

    // Create a swapchain for this window using the shared device.
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = m_device.As(&dxgiDevice);
        if (FAILED(hr) || !dxgiDevice)
        {
            g_Log->Error("Failed to query IDXGIDevice from D3D11 device: %08X", hr);
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr) || !adapter)
        {
            g_Log->Error("Failed to get DXGI adapter: %08X", hr);
            return false;
        }

        ComPtr<IDXGIFactory> factory;
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory)
        {
            g_Log->Error("Failed to get DXGI factory: %08X", hr);
            return false;
        }

        hr = factory->CreateSwapChain(m_device.Get(), &sd, &m_swapChain);
        if (FAILED(hr) || !m_swapChain)
        {
            g_Log->Error("Failed to create swap chain for window: %08X", hr);
            return false;
        }
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
    m_bEmbeddedSaverPreview = false;
    m_bFullScreen = fullscreen;

    m_WindowHandle = nullptr;
#ifdef WIN32
    m_hasTargetMonitorRect = false;
    if (fullscreen && m_hasTargetMonitorIndex)
    {
        RECT rc = {};
        if (GetMonitorRectByIndex(m_targetMonitorIndex, rc))
        {
            m_targetMonitorRect = rc;
            m_hasTargetMonitorRect = true;
            width = static_cast<uint32_t>(rc.right - rc.left);
            height = static_cast<uint32_t>(rc.bottom - rc.top);
        }
        else
        {
            g_Log->Warning("Failed to resolve monitor rect for index %u; falling back to default placement",
                           static_cast<unsigned>(m_targetMonitorIndex));
        }
    }

    m_Width = width;
    m_Height = height;

    m_WindowHandle = CreateDisplayWindow(width, height, fullscreen, nullptr);
    // Restore the default system menu (no custom app commands).
    if (m_WindowHandle)
        GetSystemMenu(m_WindowHandle, TRUE);
#endif
    if (!m_WindowHandle) return nullptr;

#ifdef WIN32
    ApplyDarkTitlebarIfSupported();
#endif

    if (!CreateDeviceAndSwapChain()) {
        return nullptr;
    }

    ShowWindow(m_WindowHandle, SW_SHOW);
    ShowCursor(!fullscreen);

#ifdef WIN32
    // Ensure the custom titlebar strip is painted at least once on show.
    if (m_WindowHandle && !fullscreen && m_useCustomWindowChrome)
    {
        const LONG_PTR style = GetWindowLongPtr(m_WindowHandle, GWL_STYLE);
        const bool hasNativeCaption = (style & WS_CAPTION) != 0;
        if (!hasNativeCaption)
        {
            RECT client{};
            GetClientRect(m_WindowHandle, &client);
            RECT strip = client;
            const int titlebarH = (m_customTitlebarHeightPx > 0) ? m_customTitlebarHeightPx
                                                                 : GetSystemTitlebarHeightPx();
            strip.bottom = (std::min)(strip.bottom, strip.top + titlebarH);
            InvalidateRect(m_WindowHandle, &strip, FALSE);
        }
    }
#endif

    if (fullscreen)
    {
        // Screensaver mode prefers borderless windowed fullscreen to avoid
        // DXGI exclusive fullscreen issues on multi-monitor setups.
        if (m_disableExclusiveFullscreen)
            LayoutFullscreenSaverWindow(m_WindowHandle, nullptr);
        else
            LayoutFullscreenSaverWindow(m_WindowHandle, m_swapChain.Get());
        SetForegroundWindow(m_WindowHandle);
        SetFocus(m_WindowHandle);
    }

    return m_WindowHandle;
}

HWND CDisplayDX11::Initialize(HWND parentHwnd, bool preview)
{
    if (!parentHwnd || !preview)
        return nullptr;

    m_bEmbeddedSaverPreview = true;

    HMODULE hInstance = GetModuleHandleW(NULL);
    if (!EnsureWindowClassRegistered(hInstance))
        return nullptr;

    RECT rc = {};
    if (!GetClientRect(parentHwnd, &rc))
        return nullptr;

    const LONG cx = rc.right - rc.left;
    const LONG cy = rc.bottom - rc.top;
    if (cx <= 0 || cy <= 0)
        return nullptr;

    g_Log->Info("CDisplayDX11::Initialize preview client %ldx%ld", cx, cy);

    DWORD dwStyle = WS_VISIBLE | WS_CHILD;
    AdjustWindowRect(&rc, dwStyle, FALSE);

    m_WindowHandle = CreateWindowW(L"EDreamDX11Class", L"Preview", dwStyle, rc.left,
                                   rc.top, rc.right - rc.left, rc.bottom - rc.top,
                                   parentHwnd, nullptr, hInstance, this);
    if (!m_WindowHandle)
    {
        g_Log->Error("CDisplayDX11::Initialize preview CreateWindow failed");
        return nullptr;
    }

    m_Width = static_cast<uint32_t>(cx);
    m_Height = static_cast<uint32_t>(cy);
    m_bFullScreen = false;

    g_Log->Info("Screensaver preview D3D11 (%ux%u)", m_Width, m_Height);

    m_bWaitForPreviewPump = true;
    PostMessageW(m_WindowHandle, WM_USER, 0, 0);
    InvalidateRect(GetParent(m_WindowHandle), nullptr, FALSE);
    UpdateWindow(GetParent(m_WindowHandle));
    UpdateWindow(GetParent(m_WindowHandle));

    MSG msg = {};
    while (m_bWaitForPreviewPump)
    {
        if (!GetMessageW(&msg, m_WindowHandle, 0, 0))
        {
            PostQuitMessage(0);
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (!CreateDeviceAndSwapChain())
    {
        DestroyWindow(m_WindowHandle);
        m_WindowHandle = nullptr;
        return nullptr;
    }

    ShowWindow(m_WindowHandle, SW_SHOW);
    ShowCursor(TRUE);
    SetFocus(parentHwnd);

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
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_bClosed = true;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void CDisplayDX11::SwapBuffers()
{
    if (m_deviceLost)
        return;

    // /p preview child: avoid vsync Present(1) stalling on the tiny HWND.
    const UINT syncInterval = m_bEmbeddedSaverPreview ? 0u : 1u;
    HRESULT hr = m_swapChain->Present(syncInterval, 0);
    if (FAILED(hr))
    {
        g_Log->Error("Present failed: %08X", hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG)
        {
            HRESULT reason = m_device ? m_device->GetDeviceRemovedReason() : hr;
            g_Log->Warning("Device lost during present (removal reason: %08X); "
                           "halting rendering until device is recreated", reason);
            m_deviceLost = true;
        }
        return;
    }

    // Custom titlebar is drawn in the DX11 renderer overlay (stable, no GDI flashing).
}

bool CDisplayDX11::SetFullscreen(const bool fullscreen)
{
    if (!m_WindowHandle || !m_swapChain)
        return false;
    if (m_bFullScreen == fullscreen)
        return true;

    if (fullscreen)
    {
        m_windowedStyle = static_cast<DWORD>(GetWindowLongPtr(m_WindowHandle, GWL_STYLE));
        m_windowedExStyle = static_cast<DWORD>(GetWindowLongPtr(m_WindowHandle, GWL_EXSTYLE));
        if (GetWindowRect(m_WindowHandle, &m_windowedRect))
            m_hasWindowedRect = true;

        if (!m_disableExclusiveFullscreen)
        {
            const HRESULT fsHr = m_swapChain->SetFullscreenState(TRUE, nullptr);
            if (FAILED(fsHr))
                g_Log->Warning("SetFullscreenState(TRUE) failed: %08X", fsHr);
        }

        HMONITOR monitor = MonitorFromWindow(m_WindowHandle, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(monitor, &mi))
        {
            SetWindowLongPtr(m_WindowHandle, GWL_STYLE, static_cast<LONG_PTR>(WS_POPUP | WS_VISIBLE));
            SetWindowPos(m_WindowHandle, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        }
        ShowCursor(FALSE);
        m_bFullScreen = true;
        return true;
    }

    if (!m_disableExclusiveFullscreen)
    {
        const HRESULT wsHr = m_swapChain->SetFullscreenState(FALSE, nullptr);
        if (FAILED(wsHr))
            g_Log->Warning("SetFullscreenState(FALSE) failed: %08X", wsHr);
    }

    DWORD restoreStyle = m_windowedStyle ? m_windowedStyle : GetDefaultWindowedStyle(m_useCustomWindowChrome);
    SetWindowLongPtr(m_WindowHandle, GWL_STYLE, static_cast<LONG_PTR>(restoreStyle));
    SetWindowLongPtr(m_WindowHandle, GWL_EXSTYLE, static_cast<LONG_PTR>(m_windowedExStyle));

    RECT windowRect = m_windowedRect;
    if (!m_hasWindowedRect)
    {
        windowRect.left = CW_USEDEFAULT;
        windowRect.top = CW_USEDEFAULT;
        windowRect.right = windowRect.left + static_cast<LONG>(m_Width);
        windowRect.bottom = windowRect.top + static_cast<LONG>(m_Height);
    }
    SetWindowPos(m_WindowHandle, HWND_NOTOPMOST, windowRect.left, windowRect.top,
                 windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    ShowCursor(TRUE);
    m_bFullScreen = false;
    return true;
}

bool CDisplayDX11::ToggleFullscreen()
{
    return SetFullscreen(!m_bFullScreen);
}

#ifdef WIN32
void CDisplayDX11::WindowMinimize()
{
    if (m_WindowHandle && IsWindow(m_WindowHandle))
        ShowWindow(m_WindowHandle, SW_MINIMIZE);
}

void CDisplayDX11::WindowToggleMaximizeRestore()
{
    if (!m_WindowHandle || !IsWindow(m_WindowHandle))
        return;
    if (IsZoomed(m_WindowHandle))
        ShowWindow(m_WindowHandle, SW_RESTORE);
    else
        ShowWindow(m_WindowHandle, SW_MAXIMIZE);
}

void CDisplayDX11::WindowClose()
{
    if (m_WindowHandle && IsWindow(m_WindowHandle))
        PostMessageW(m_WindowHandle, WM_CLOSE, 0, 0);
}
#endif


} // namespace DisplayOutput
