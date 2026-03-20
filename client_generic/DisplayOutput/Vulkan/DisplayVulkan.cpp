#ifndef WIN32

#include "DisplayVulkan.h"
#include "PlatformUtils_Internal.h"
#include "Log.h"

#include <X11/Xutil.h>
#include <cstdio>
#include <cstring>

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// Motif window-decoration hints (for borderless windows)
// ---------------------------------------------------------------------------
typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long          input_mode;
    unsigned long status;
} MotifWmHints;
#define MWM_HINTS_DECORATIONS (1L << 1)

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
CDisplayVulkan::CDisplayVulkan() : CDisplayOutput() {}

CDisplayVulkan::~CDisplayVulkan()
{
    if (m_surface  != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_instance, nullptr);

    if (!m_bScreensaver && m_Window && m_pDisplay)
    {
        XUnmapWindow(m_pDisplay, m_Window);
        XDestroyWindow(m_pDisplay, m_Window);
    }
    if (m_pDisplay)
        XCloseDisplay(m_pDisplay);
}

// ---------------------------------------------------------------------------
// createVulkanInstance
// ---------------------------------------------------------------------------
bool CDisplayVulkan::createVulkanInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "infinidream";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "infinidream";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = 2;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.enabledLayerCount       = 0;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS)
    {
        g_Log->Error("vkCreateInstance failed: %d", (int)result);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Initialize — create X11 window + Vulkan surface
// ---------------------------------------------------------------------------
bool CDisplayVulkan::Initialize(const uint32_t _width, const uint32_t _height,
                                const bool _bFullscreen)
{
    m_Width  = _width;
    m_Height = _height;

    // Open X11 display
    m_pDisplay = XOpenDisplay(nullptr);
    if (!m_pDisplay)
    {
        g_Log->Error("CDisplayVulkan: XOpenDisplay failed");
        return false;
    }

    int screen = DefaultScreen(m_pDisplay);
    m_WidthFS  = static_cast<uint32_t>(DisplayWidth (m_pDisplay, screen));
    m_HeightFS = static_cast<uint32_t>(DisplayHeight(m_pDisplay, screen));

    // Cache useful atoms
    m_wmDeleteWindow  = XInternAtom(m_pDisplay, "WM_DELETE_WINDOW", False);
    m_netWmState      = XInternAtom(m_pDisplay, "_NET_WM_STATE",    False);
    m_netWmFullscreen = XInternAtom(m_pDisplay, "_NET_WM_STATE_FULLSCREEN", False);

    // Check for XScreensaver mode
    const char* xssId = getenv("XSCREENSAVER_WINDOW");
    if (xssId && *xssId)
    {
        unsigned long id = 0;
        sscanf(xssId, " 0x%lx", &id);
        m_Window = static_cast<Window>(id);

        XWindowAttributes attr;
        XGetWindowAttributes(m_pDisplay, m_Window, &attr);
        m_Width  = static_cast<uint32_t>(attr.width);
        m_Height = static_cast<uint32_t>(attr.height);
        m_bScreensaver = true;
        g_Log->Info("CDisplayVulkan: XScreensaver mode, window 0x%lx (%ux%u)",
                    id, m_Width, m_Height);
    }
    else
    {
        uint32_t winW = _bFullscreen ? m_WidthFS  : m_Width;
        uint32_t winH = _bFullscreen ? m_HeightFS : m_Height;

        XSetWindowAttributes winAttribs{};
        winAttribs.event_mask = StructureNotifyMask | KeyPressMask |
                                KeyReleaseMask | ButtonPressMask |
                                PointerMotionMask | ClientMessage;
        winAttribs.override_redirect = False;

        m_Window = XCreateSimpleWindow(
            m_pDisplay,
            RootWindow(m_pDisplay, screen),
            0, 0, winW, winH, 0,
            BlackPixel(m_pDisplay, screen),
            BlackPixel(m_pDisplay, screen));

        if (!m_Window)
        {
            g_Log->Error("CDisplayVulkan: XCreateSimpleWindow failed");
            return false;
        }

        XSelectInput(m_pDisplay, m_Window,
                     StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | PointerMotionMask);

        XSetWMProtocols(m_pDisplay, m_Window, &m_wmDeleteWindow, 1);

        setWindowDecorations(!_bFullscreen);
        if (_bFullscreen)
            setFullScreen(true);

        XMapRaised(m_pDisplay, m_Window);
        XFlush(m_pDisplay);

        // Wait for the window to be mapped
        XEvent e;
        do { XNextEvent(m_pDisplay, &e); }
        while (e.type != MapNotify || e.xmap.window != m_Window);
    }

    applyInvisibleCursor();

    // Create Vulkan instance
    if (!createVulkanInstance())
        return false;

    // Create Xlib surface
    VkXlibSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.dpy    = m_pDisplay;
    surfaceInfo.window = m_Window;

    VkResult result = vkCreateXlibSurfaceKHR(m_instance, &surfaceInfo,
                                              nullptr, &m_surface);
    if (result != VK_SUCCESS)
    {
        g_Log->Error("vkCreateXlibSurfaceKHR failed: %d", (int)result);
        return false;
    }

    g_Log->Info("CDisplayVulkan: initialized %ux%u (fullscreen=%d)",
                m_Width, m_Height, (int)_bFullscreen);
    return true;
}

// ---------------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------------
void CDisplayVulkan::Title(const std::string& _title)
{
    if (!m_pDisplay || !m_Window) return;
    XStoreName(m_pDisplay, m_Window, _title.c_str());
}

// ---------------------------------------------------------------------------
// Update — pump X11 events
// ---------------------------------------------------------------------------
void CDisplayVulkan::Update()
{
    PlatformUtils_DrainMainThreadQueue();
    checkEvents();
}

// ---------------------------------------------------------------------------
// checkEvents — translate X11 events into CDisplayOutput events
// ---------------------------------------------------------------------------
void CDisplayVulkan::checkEvents()
{
    XEvent xEvent;

    while (XPending(m_pDisplay))
    {
        XNextEvent(m_pDisplay, &xEvent);

        // Window close
        if (xEvent.type == ClientMessage)
        {
            if (static_cast<Atom>(xEvent.xclient.data.l[0]) == m_wmDeleteWindow)
            {
                m_bClosed = true;
                return;
            }
        }

        // Keyboard
        if (xEvent.type == KeyPress || xEvent.type == KeyRelease)
        {
            auto spEvent = std::make_shared<CKeyEvent>();
            spEvent->m_bPressed = (xEvent.type == KeyPress);

            int nSyms = 0;
            KeySym* syms = XGetKeyboardMapping(m_pDisplay,
                                               xEvent.xkey.keycode, 1, &nSyms);
            if (syms)
            {
                switch (syms[0])
                {
                case XK_F1:     spEvent->m_Code = CKeyEvent::KEY_F1;    break;
                case XK_F2:     spEvent->m_Code = CKeyEvent::KEY_F2;    break;
                case XK_F3:     spEvent->m_Code = CKeyEvent::KEY_F3;    break;
                case XK_F4:     spEvent->m_Code = CKeyEvent::KEY_F4;    break;
                case XK_F8:     spEvent->m_Code = CKeyEvent::KEY_F8;    break;
                case XK_f:      spEvent->m_Code = CKeyEvent::KEY_F;     break;
                case XK_s:      spEvent->m_Code = CKeyEvent::KEY_S;     break;
                case XK_space:  spEvent->m_Code = CKeyEvent::KEY_SPACE; break;
                case XK_Left:   spEvent->m_Code = CKeyEvent::KEY_LEFT;  break;
                case XK_Right:  spEvent->m_Code = CKeyEvent::KEY_RIGHT; break;
                case XK_Up:     spEvent->m_Code = CKeyEvent::KEY_UP;    break;
                case XK_Down:   spEvent->m_Code = CKeyEvent::KEY_DOWN;  break;
                case XK_Escape: spEvent->m_Code = CKeyEvent::KEY_Esc;   break;
                default:        spEvent->m_Code = CKeyEvent::KEY_NONE;  break;
                }
                XFree(syms);
            }

            m_EventQueue.push(std::static_pointer_cast<CEvent>(spEvent));
        }

        // Mouse move
        if (xEvent.type == MotionNotify)
        {
            auto& cb = PlatformUtils_GetMouseCallback();
            if (cb)
                cb(xEvent.xmotion.x, xEvent.xmotion.y);
        }
    }
}

// ---------------------------------------------------------------------------
// Fullscreen (EWMH)
// ---------------------------------------------------------------------------
void CDisplayVulkan::setFullScreen(bool enabled)
{
    m_bFullScreen = enabled;
    setWindowDecorations(!enabled);

    if (!m_pDisplay || !m_Window) return;

    Atom netWmStateAdd = XInternAtom(m_pDisplay, "_NET_WM_STATE_ADD",    False);
    Atom netWmStateRem = XInternAtom(m_pDisplay, "_NET_WM_STATE_REMOVE", False);

    XEvent e{};
    e.type                 = ClientMessage;
    e.xclient.window       = m_Window;
    e.xclient.message_type = m_netWmState;
    e.xclient.format       = 32;
    e.xclient.data.l[0]    = enabled ? netWmStateAdd : netWmStateRem;
    e.xclient.data.l[1]    = static_cast<long>(m_netWmFullscreen);
    e.xclient.data.l[3]    = 0;

    XSendEvent(m_pDisplay, DefaultRootWindow(m_pDisplay), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &e);
    XFlush(m_pDisplay);
}

// ---------------------------------------------------------------------------
// Window decorations (Motif hints)
// ---------------------------------------------------------------------------
void CDisplayVulkan::setWindowDecorations(bool enabled)
{
    if (!m_pDisplay || !m_Window) return;

    Atom motifAtom = XInternAtom(m_pDisplay, "_MOTIF_WM_HINTS", True);
    if (motifAtom == None) return;

    MotifWmHints hints{};
    hints.flags       = MWM_HINTS_DECORATIONS;
    hints.decorations = enabled ? 1 : 0;

    XChangeProperty(m_pDisplay, m_Window, motifAtom, motifAtom, 32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(&hints),
                    sizeof(MotifWmHints) / sizeof(long));
}

// ---------------------------------------------------------------------------
// Always-on-top (WIN layer)
// ---------------------------------------------------------------------------
void CDisplayVulkan::alwaysOnTop()
{
    if (!m_pDisplay || !m_Window) return;
    Atom layerAtom = XInternAtom(m_pDisplay, "_WIN_LAYER", False);
    long val = 12;
    XChangeProperty(m_pDisplay, m_Window, layerAtom, XA_CARDINAL, 32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(&val), 1);
    XRaiseWindow(m_pDisplay, m_Window);
}

// ---------------------------------------------------------------------------
// Invisible cursor
// ---------------------------------------------------------------------------
void CDisplayVulkan::applyInvisibleCursor()
{
    if (!m_pDisplay || !m_Window) return;

    static char noData[8] = {};
    XColor black{};
    Pixmap bm = XCreateBitmapFromData(m_pDisplay, m_Window, noData, 8, 8);
    Cursor cur = XCreatePixmapCursor(m_pDisplay, bm, bm, &black, &black, 0, 0);
    XDefineCursor(m_pDisplay, m_Window, cur);
    XFreeCursor(m_pDisplay, cur);
    XFreePixmap(m_pDisplay, bm);
}

} // namespace DisplayOutput

#endif // !WIN32
