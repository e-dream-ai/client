#ifndef WIN32

#include "DisplayVulkan.h"
#include "PlatformUtils_Internal.h"
#include "Log.h"

#include <X11/Xutil.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// Motif window-decoration hints (borderless windows, X11 only)
// ---------------------------------------------------------------------------
typedef struct {
    unsigned long flags, functions, decorations;
    long          input_mode;
    unsigned long status;
} MotifWmHints;
#define MWM_HINTS_DECORATIONS (1L << 1)

// ===========================================================================
// Wayland listener callbacks
// ===========================================================================
#ifdef HAVE_WAYLAND

void CDisplayVulkan::onRegistryGlobal(void* data, wl_registry* registry,
                                       uint32_t name, const char* interface,
                                       uint32_t /*version*/)
{
    auto* self = static_cast<CDisplayVulkan*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        self->m_pWlCompositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        self->m_pXdgWmBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        static const xdg_wm_base_listener s_wmbListener = { onXdgWmBasePing };
        xdg_wm_base_add_listener(self->m_pXdgWmBase, &s_wmbListener, nullptr);
    }
}

void CDisplayVulkan::onRegistryGlobalRemove(void*, wl_registry*, uint32_t) {}

void CDisplayVulkan::onXdgWmBasePing(void*, xdg_wm_base* base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}

void CDisplayVulkan::onXdgSurfaceConfigure(void* data, xdg_surface* surface,
                                            uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
    wl_surface_commit(static_cast<CDisplayVulkan*>(data)->m_pWlSurface);
}

void CDisplayVulkan::onXdgToplevelConfigure(void* data, xdg_toplevel*,
                                             int32_t width, int32_t height,
                                             wl_array*)
{
    // width/height == 0 means "compositor defers to us — keep requested size"
    if (width <= 0 || height <= 0) return;
    auto* self = static_cast<CDisplayVulkan*>(data);
    self->m_Width  = static_cast<uint32_t>(width);
    self->m_Height = static_cast<uint32_t>(height);
}

void CDisplayVulkan::onXdgToplevelClose(void* data, xdg_toplevel*)
{
    static_cast<CDisplayVulkan*>(data)->m_bClosed = true;
}

bool CDisplayVulkan::initWayland(uint32_t w, uint32_t h, bool bFullscreen)
{
    m_pWlDisplay = wl_display_connect(nullptr);
    if (!m_pWlDisplay)
    {
        g_Log->Error("CDisplayVulkan: wl_display_connect failed");
        return false;
    }

    wl_registry* registry = wl_display_get_registry(m_pWlDisplay);
    static const wl_registry_listener s_regListener = {
        onRegistryGlobal,
        onRegistryGlobalRemove,
    };
    wl_registry_add_listener(registry, &s_regListener, this);
    wl_display_roundtrip(m_pWlDisplay);  // bind wl_compositor + xdg_wm_base

    if (!m_pWlCompositor || !m_pXdgWmBase)
    {
        g_Log->Error("CDisplayVulkan: Wayland compositor or xdg_wm_base unavailable");
        return false;
    }

    m_pWlSurface = wl_compositor_create_surface(m_pWlCompositor);

    static const xdg_surface_listener s_xdgSurfaceListener = {
        onXdgSurfaceConfigure,
    };
    m_pXdgSurface = xdg_wm_base_get_xdg_surface(m_pXdgWmBase, m_pWlSurface);
    xdg_surface_add_listener(m_pXdgSurface, &s_xdgSurfaceListener, this);

    static const xdg_toplevel_listener s_xdgToplevelListener = {
        onXdgToplevelConfigure,
        onXdgToplevelClose,
    };
    m_pXdgToplevel = xdg_surface_get_toplevel(m_pXdgSurface);
    xdg_toplevel_add_listener(m_pXdgToplevel, &s_xdgToplevelListener, this);

    xdg_toplevel_set_title(m_pXdgToplevel, "infinidream");
    xdg_toplevel_set_app_id(m_pXdgToplevel, "infinidream");

    if (bFullscreen)
        xdg_toplevel_set_fullscreen(m_pXdgToplevel, nullptr);

    wl_surface_commit(m_pWlSurface);
    wl_display_roundtrip(m_pWlDisplay);  // receive initial xdg_surface::configure

    // If compositor sent 0,0 it wants us to choose — keep requested dimensions
    if (m_Width == 0)  m_Width  = w;
    if (m_Height == 0) m_Height = h;

    g_Log->Info("CDisplayVulkan: Wayland initialized %ux%u (fullscreen=%d)",
                m_Width, m_Height, (int)bFullscreen);
    return true;
}

void CDisplayVulkan::destroyWayland()
{
    if (m_pXdgToplevel)  { xdg_toplevel_destroy(m_pXdgToplevel);    m_pXdgToplevel  = nullptr; }
    if (m_pXdgSurface)   { xdg_surface_destroy(m_pXdgSurface);      m_pXdgSurface   = nullptr; }
    if (m_pWlSurface)    { wl_surface_destroy(m_pWlSurface);         m_pWlSurface    = nullptr; }
    if (m_pXdgWmBase)    { xdg_wm_base_destroy(m_pXdgWmBase);        m_pXdgWmBase    = nullptr; }
    if (m_pWlCompositor) { wl_compositor_destroy(m_pWlCompositor);   m_pWlCompositor = nullptr; }
    if (m_pWlDisplay)    { wl_display_disconnect(m_pWlDisplay);       m_pWlDisplay    = nullptr; }
}

#endif // HAVE_WAYLAND

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

CDisplayVulkan::CDisplayVulkan() : CDisplayOutput() {}

CDisplayVulkan::~CDisplayVulkan()
{
    // Vulkan objects must be destroyed before the underlying window/surface
    if (m_surface  != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);

#ifdef HAVE_WAYLAND
    if (m_bWayland)
    {
        destroyWayland();
        return;
    }
#endif

    if (!m_bScreensaver && m_Window && m_pDisplay)
    {
        XUnmapWindow(m_pDisplay, m_Window);
        XDestroyWindow(m_pDisplay, m_Window);
    }
    if (m_pDisplay)
        XCloseDisplay(m_pDisplay);
}

// ===========================================================================
// createVulkanInstance — selects surface extension based on display server
// ===========================================================================

bool CDisplayVulkan::createVulkanInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "infinidream";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "infinidream";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

#ifdef HAVE_WAYLAND
    const char* waylandExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };
#endif
    const char* xlibExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo ci{};
    ci.sType             = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo  = &appInfo;
    ci.enabledLayerCount = 0;
#ifdef HAVE_WAYLAND
    if (m_bWayland)
    {
        ci.enabledExtensionCount   = 2;
        ci.ppEnabledExtensionNames = waylandExts;
    }
    else
#endif
    {
        ci.enabledExtensionCount   = 2;
        ci.ppEnabledExtensionNames = xlibExts;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, &m_instance);
    if (r != VK_SUCCESS) { g_Log->Error("vkCreateInstance failed: %d", (int)r); return false; }
    return true;
}

// ===========================================================================
// Initialize — create window + VkSurfaceKHR
// ===========================================================================

bool CDisplayVulkan::Initialize(const uint32_t _width, const uint32_t _height,
                                const bool _bFullscreen)
{
    m_Width  = _width;
    m_Height = _height;

    const char* xssId = getenv("XSCREENSAVER_WINDOW");

#ifdef HAVE_WAYLAND
    // Use native Wayland unless this is an XScreensaver embed (inherently X11)
    if (!xssId && getenv("WAYLAND_DISPLAY"))
    {
        m_bWayland = true;
        if (!initWayland(_width, _height, _bFullscreen)) return false;
        if (!createVulkanInstance()) return false;

        VkWaylandSurfaceCreateInfoKHR si{};
        si.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        si.display = m_pWlDisplay;
        si.surface = m_pWlSurface;
        VkResult r = vkCreateWaylandSurfaceKHR(m_instance, &si, nullptr, &m_surface);
        if (r != VK_SUCCESS)
        {
            g_Log->Error("vkCreateWaylandSurfaceKHR failed: %d", (int)r);
            return false;
        }
        return true;
    }
#endif

    // -------------------------------------------------------------------
    // X11 path — also covers XWayland sessions and XScreensaver embed
    // -------------------------------------------------------------------
    m_pDisplay = XOpenDisplay(nullptr);
    if (!m_pDisplay)
    {
        g_Log->Error("CDisplayVulkan: XOpenDisplay failed");
        return false;
    }

    int screen = DefaultScreen(m_pDisplay);
    m_WidthFS  = static_cast<uint32_t>(DisplayWidth (m_pDisplay, screen));
    m_HeightFS = static_cast<uint32_t>(DisplayHeight(m_pDisplay, screen));

    m_wmDeleteWindow  = XInternAtom(m_pDisplay, "WM_DELETE_WINDOW",         False);
    m_netWmState      = XInternAtom(m_pDisplay, "_NET_WM_STATE",            False);
    m_netWmFullscreen = XInternAtom(m_pDisplay, "_NET_WM_STATE_FULLSCREEN", False);

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

        m_Window = XCreateSimpleWindow(
            m_pDisplay, RootWindow(m_pDisplay, screen),
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
        if (_bFullscreen) setFullScreen(true);

        XMapRaised(m_pDisplay, m_Window);
        XFlush(m_pDisplay);

        XEvent e;
        do { XNextEvent(m_pDisplay, &e); }
        while (e.type != MapNotify || e.xmap.window != m_Window);
    }

    applyInvisibleCursor();

    if (!createVulkanInstance()) return false;

    VkXlibSurfaceCreateInfoKHR si{};
    si.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    si.dpy    = m_pDisplay;
    si.window = m_Window;
    VkResult r = vkCreateXlibSurfaceKHR(m_instance, &si, nullptr, &m_surface);
    if (r != VK_SUCCESS)
    {
        g_Log->Error("vkCreateXlibSurfaceKHR failed: %d", (int)r);
        return false;
    }

    g_Log->Info("CDisplayVulkan: X11 initialized %ux%u (fullscreen=%d)",
                m_Width, m_Height, (int)_bFullscreen);
    return true;
}

// ===========================================================================
// Title
// ===========================================================================

void CDisplayVulkan::Title(const std::string& _title)
{
#ifdef HAVE_WAYLAND
    if (m_bWayland)
    {
        if (m_pXdgToplevel) xdg_toplevel_set_title(m_pXdgToplevel, _title.c_str());
        return;
    }
#endif
    if (m_pDisplay && m_Window)
        XStoreName(m_pDisplay, m_Window, _title.c_str());
}

// ===========================================================================
// Update — pump display events each frame
// ===========================================================================

void CDisplayVulkan::Update()
{
    PlatformUtils_DrainMainThreadQueue();
    checkEvents();
}

void CDisplayVulkan::checkEvents()
{
#ifdef HAVE_WAYLAND
    if (m_bWayland)
    {
        wl_display_flush(m_pWlDisplay);
        wl_display_dispatch_pending(m_pWlDisplay);
        return;
    }
#endif

    XEvent xEvent;
    while (XPending(m_pDisplay))
    {
        XNextEvent(m_pDisplay, &xEvent);

        if (xEvent.type == ClientMessage)
        {
            if (static_cast<Atom>(xEvent.xclient.data.l[0]) == m_wmDeleteWindow)
            {
                m_bClosed = true;
                return;
            }
        }

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

        if (xEvent.type == MotionNotify)
        {
            auto& cb = PlatformUtils_GetMouseCallback();
            if (cb) cb(xEvent.xmotion.x, xEvent.xmotion.y);
        }
    }
}

// ===========================================================================
// Fullscreen
// ===========================================================================

void CDisplayVulkan::setFullScreen(bool enabled)
{
    m_bFullScreen = enabled;

#ifdef HAVE_WAYLAND
    if (m_bWayland)
    {
        if (!m_pXdgToplevel) return;
        if (enabled)
            xdg_toplevel_set_fullscreen(m_pXdgToplevel, nullptr);
        else
            xdg_toplevel_unset_fullscreen(m_pXdgToplevel);
        wl_surface_commit(m_pWlSurface);
        wl_display_flush(m_pWlDisplay);
        return;
    }
#endif

    setWindowDecorations(!enabled);
    if (!m_pDisplay || !m_Window) return;

    Atom add = XInternAtom(m_pDisplay, "_NET_WM_STATE_ADD",    False);
    Atom rem = XInternAtom(m_pDisplay, "_NET_WM_STATE_REMOVE", False);

    XEvent e{};
    e.type                 = ClientMessage;
    e.xclient.window       = m_Window;
    e.xclient.message_type = m_netWmState;
    e.xclient.format       = 32;
    e.xclient.data.l[0]    = enabled ? add : rem;
    e.xclient.data.l[1]    = static_cast<long>(m_netWmFullscreen);
    e.xclient.data.l[3]    = 0;
    XSendEvent(m_pDisplay, DefaultRootWindow(m_pDisplay), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &e);
    XFlush(m_pDisplay);
}

// ---------------------------------------------------------------------------
// Window decorations (Motif hints — X11 only)
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
// Invisible cursor (X11 only — Wayland compositors handle this natively)
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
