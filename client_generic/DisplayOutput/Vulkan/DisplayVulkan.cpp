#ifndef WIN32

#include "DisplayVulkan.h"
#include "PlatformUtils_Internal.h"
#include "Log.h"

#include <X11/Xutil.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/select.h>
#ifdef HAVE_WAYLAND
#include <sys/mman.h>
#include <unistd.h>
#endif

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
                                       uint32_t version)
{
    auto* self = static_cast<CDisplayVulkan*>(data);
    g_Log->Info("CDisplayVulkan: Wayland global: %s (v%u)", interface, version);
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
    else if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        self->m_pWlSeat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 4));
        static const wl_seat_listener s_seatListener = {
            onSeatCapabilities,
            onSeatName,
        };
        wl_seat_add_listener(self->m_pWlSeat, &s_seatListener, self);
    }
    else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
    {
        self->m_pDecorationManager = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
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
                                             wl_array* states)
{
    auto* self = static_cast<CDisplayVulkan*>(data);

    // Check whether the compositor considers us fullscreen right now
    bool compositorFullscreen = false;
    {
        const uint32_t* end = static_cast<uint32_t*>(
            static_cast<void*>(static_cast<char*>(states->data) + states->size));
        for (uint32_t* s = static_cast<uint32_t*>(states->data); s < end; ++s)
        {
            if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN)
            {
                compositorFullscreen = true;
                break;
            }
        }
    }

    g_Log->Info("CDisplayVulkan: onXdgToplevelConfigure %dx%d fullscreen=%d (want=%d)",
                width, height, (int)compositorFullscreen, (int)self->m_bFullScreen);

    // Ignore stale fullscreen configures after we've requested windowed mode
    if (compositorFullscreen && !self->m_bFullScreen)
        return;

    // width/height == 0 means "compositor defers to us — keep requested size"
    if (width > 0 && height > 0)
    {
        self->m_Width  = static_cast<uint32_t>(width);
        self->m_Height = static_cast<uint32_t>(height);
    }
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
    wl_display_roundtrip(m_pWlDisplay);  // bind compositor, xdg_wm_base, seat
    wl_display_roundtrip(m_pWlDisplay);  // receive seat capabilities → keyboard

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

    // Sync m_bFullScreen before the roundtrip so the configure filter works correctly
    m_bFullScreen = bFullscreen;

    // Request server-side decorations (borders + title bar) when available
    if (m_pDecorationManager)
    {
        g_Log->Info("CDisplayVulkan: xdg-decoration manager found — requesting server-side decorations");
        m_pToplevelDecoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            m_pDecorationManager, m_pXdgToplevel);
        zxdg_toplevel_decoration_v1_set_mode(m_pToplevelDecoration,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    else
    {
        g_Log->Warning("CDisplayVulkan: xdg-decoration not supported by compositor — windowed mode will lack borders");
    }

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

// ---------------------------------------------------------------------------
// Seat / keyboard callbacks
// ---------------------------------------------------------------------------

void CDisplayVulkan::onSeatName(void*, wl_seat*, const char*) {}

void CDisplayVulkan::onSeatCapabilities(void* data, wl_seat*, uint32_t caps)
{
    auto* self = static_cast<CDisplayVulkan*>(data);
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !self->m_pWlKeyboard)
    {
        self->m_pWlKeyboard = wl_seat_get_keyboard(self->m_pWlSeat);
        static const wl_keyboard_listener s_kbListener = {
            onKeyboardKeymap,
            onKeyboardEnter,
            onKeyboardLeave,
            onKeyboardKey,
            onKeyboardModifiers,
            onKeyboardRepeatInfo,
        };
        wl_keyboard_add_listener(self->m_pWlKeyboard, &s_kbListener, self);

        if (!self->m_pXkbContext)
            self->m_pXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }
}

void CDisplayVulkan::onKeyboardKeymap(void* data, wl_keyboard*,
                                       uint32_t format, int32_t fd, uint32_t size)
{
    auto* self = static_cast<CDisplayVulkan*>(data);
    if (!self->m_pXkbContext || format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close(fd);
        return;
    }

    void* map = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return; }

    xkb_keymap* keymap = xkb_keymap_new_from_string(
        self->m_pXkbContext,
        static_cast<const char*>(map),
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);

    if (!keymap) return;

    xkb_state* state = xkb_state_new(keymap);
    if (!state) { xkb_keymap_unref(keymap); return; }

    if (self->m_pXkbState)  { xkb_state_unref(self->m_pXkbState);   self->m_pXkbState  = nullptr; }
    if (self->m_pXkbKeymap) { xkb_keymap_unref(self->m_pXkbKeymap); self->m_pXkbKeymap = nullptr; }
    self->m_pXkbKeymap = keymap;
    self->m_pXkbState  = state;
}

void CDisplayVulkan::onKeyboardEnter(void*, wl_keyboard*, uint32_t,
                                      wl_surface*, wl_array*) {}

void CDisplayVulkan::onKeyboardLeave(void*, wl_keyboard*, uint32_t,
                                      wl_surface*) {}

void CDisplayVulkan::onKeyboardKey(void* data, wl_keyboard*, uint32_t,
                                    uint32_t /*time*/, uint32_t key, uint32_t state)
{
    auto* self = static_cast<CDisplayVulkan*>(data);
    if (!self->m_pXkbState) return;

    // Wayland evdev keycodes are offset by 8 from xkb keycodes.
    const xkb_keycode_t keycode = key + 8;
    const xkb_keysym_t  keysym  = xkb_state_key_get_one_sym(self->m_pXkbState, keycode);

    auto spEvent         = std::make_shared<CKeyEvent>();
    spEvent->m_bPressed  = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    spEvent->m_bCtrl     = self->m_pXkbState &&
                           xkb_state_mod_name_is_active(self->m_pXkbState,
                                                        XKB_MOD_NAME_CTRL,
                                                        XKB_STATE_MODS_EFFECTIVE) > 0;

    switch (keysym)
    {
    case XKB_KEY_F1:     spEvent->m_Code = CKeyEvent::KEY_F1;    break;
    case XKB_KEY_F2:     spEvent->m_Code = CKeyEvent::KEY_F2;    break;
    case XKB_KEY_F3:     spEvent->m_Code = CKeyEvent::KEY_F3;    break;
    case XKB_KEY_F4:     spEvent->m_Code = CKeyEvent::KEY_F4;    break;
    case XKB_KEY_F8:     spEvent->m_Code = CKeyEvent::KEY_F8;    break;
    case XKB_KEY_f:      spEvent->m_Code = CKeyEvent::KEY_F;     break;
    case XKB_KEY_s:      spEvent->m_Code = CKeyEvent::KEY_S;     break;
    case XKB_KEY_a:      spEvent->m_Code = CKeyEvent::KEY_A;     break;
    case XKB_KEY_d:      spEvent->m_Code = CKeyEvent::KEY_D;     break;
    case XKB_KEY_r:      spEvent->m_Code = CKeyEvent::KEY_R;     break;
    case XKB_KEY_h:      spEvent->m_Code = CKeyEvent::KEY_H;     break;
    case XKB_KEY_j:      spEvent->m_Code = CKeyEvent::KEY_J;     break;
    case XKB_KEY_k:      spEvent->m_Code = CKeyEvent::KEY_K;     break;
    case XKB_KEY_l:      spEvent->m_Code = CKeyEvent::KEY_L;     break;
    case XKB_KEY_c:      spEvent->m_Code = CKeyEvent::KEY_C;     break;
    case XKB_KEY_v:      spEvent->m_Code = CKeyEvent::KEY_V;     break;
    case XKB_KEY_w:      spEvent->m_Code = CKeyEvent::KEY_W;     break;
    case XKB_KEY_n:      spEvent->m_Code = CKeyEvent::KEY_N;     break;
    case XKB_KEY_b:      spEvent->m_Code = CKeyEvent::KEY_B;     break;
    case XKB_KEY_space:  spEvent->m_Code = CKeyEvent::KEY_SPACE; break;
    case XKB_KEY_Left:   spEvent->m_Code = CKeyEvent::KEY_LEFT;  break;
    case XKB_KEY_Right:  spEvent->m_Code = CKeyEvent::KEY_RIGHT; break;
    case XKB_KEY_Up:     spEvent->m_Code = CKeyEvent::KEY_UP;    break;
    case XKB_KEY_Down:   spEvent->m_Code = CKeyEvent::KEY_DOWN;  break;
    case XKB_KEY_Escape: spEvent->m_Code = CKeyEvent::KEY_Esc;   break;
    default:             spEvent->m_Code = CKeyEvent::KEY_NONE;  break;
    }

    self->m_EventQueue.push(std::static_pointer_cast<CEvent>(spEvent));
}

void CDisplayVulkan::onKeyboardModifiers(void* data, wl_keyboard*, uint32_t,
                                          uint32_t mods_depressed,
                                          uint32_t mods_latched,
                                          uint32_t mods_locked,
                                          uint32_t group)
{
    auto* self = static_cast<CDisplayVulkan*>(data);
    if (self->m_pXkbState)
        xkb_state_update_mask(self->m_pXkbState,
                              mods_depressed, mods_latched, mods_locked,
                              0, 0, group);
}

void CDisplayVulkan::onKeyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

// ---------------------------------------------------------------------------
// destroyWayland
// ---------------------------------------------------------------------------
void CDisplayVulkan::destroyWayland()
{
    if (m_pXkbState)          { xkb_state_unref(m_pXkbState);                              m_pXkbState          = nullptr; }
    if (m_pXkbKeymap)         { xkb_keymap_unref(m_pXkbKeymap);                            m_pXkbKeymap         = nullptr; }
    if (m_pXkbContext)        { xkb_context_unref(m_pXkbContext);                          m_pXkbContext        = nullptr; }
    if (m_pWlKeyboard)        { wl_keyboard_destroy(m_pWlKeyboard);                        m_pWlKeyboard        = nullptr; }
    if (m_pWlSeat)            { wl_seat_destroy(m_pWlSeat);                                m_pWlSeat            = nullptr; }
    if (m_pToplevelDecoration){ zxdg_toplevel_decoration_v1_destroy(m_pToplevelDecoration);m_pToplevelDecoration= nullptr; }
    if (m_pDecorationManager) { zxdg_decoration_manager_v1_destroy(m_pDecorationManager);  m_pDecorationManager = nullptr; }
    if (m_pXdgToplevel)       { xdg_toplevel_destroy(m_pXdgToplevel);                      m_pXdgToplevel       = nullptr; }
    if (m_pXdgSurface)        { xdg_surface_destroy(m_pXdgSurface);                        m_pXdgSurface        = nullptr; }
    if (m_pWlSurface)         { wl_surface_destroy(m_pWlSurface);                          m_pWlSurface         = nullptr; }
    if (m_pXdgWmBase)         { xdg_wm_base_destroy(m_pXdgWmBase);                         m_pXdgWmBase         = nullptr; }
    if (m_pWlCompositor)      { wl_compositor_destroy(m_pWlCompositor);                    m_pWlCompositor      = nullptr; }
    if (m_pWlDisplay)         { wl_display_disconnect(m_pWlDisplay);                       m_pWlDisplay         = nullptr; }
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
        // For EWMH fullscreen, create the window at the requested size and let
        // the WM resize it to the appropriate monitor.  Using m_WidthFS (the full
        // virtual desktop width) here would create a window that spans all monitors,
        // leaving it up to the WM to decide which ones to fill — typically resulting
        // in a surface that is still the full virtual-desktop size even though only
        // one monitor is shown, which squashes the startup logo.
        m_Window = XCreateSimpleWindow(
            m_pDisplay, RootWindow(m_pDisplay, screen),
            0, 0, m_Width, m_Height, 0,
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
        {
            // Set _NET_WM_STATE_FULLSCREEN as a window *property* before mapping.
            // The _NET_WM_STATE client message form is for toggling an already-mapped
            // window; setting the property directly is the correct EWMH approach for
            // the initial window state.  Using a client message here (before MapRaised)
            // is ignored by most WMs, leaving the window large but not in the EWMH
            // fullscreen state — meaning _NET_WM_STATE_REMOVE has nothing to act on,
            // making the F keypress to toggle fullscreen appear to do nothing.
            XChangeProperty(m_pDisplay, m_Window,
                            m_netWmState, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*>(&m_netWmFullscreen), 1);
        }

        XMapRaised(m_pDisplay, m_Window);
        XFlush(m_pDisplay);

        // Wait for MapNotify, but also capture any ConfigureNotify that the WM
        // may send *before* MapNotify when it applies the fullscreen property.
        // Discarding those events in the wait loop is the primary reason the
        // startup logo is squashed — we'd miss the WM's fullscreen resize.
        XEvent e;
        bool gotFullscreenSize = false;
        for (;;)
        {
            XNextEvent(m_pDisplay, &e);
            if (_bFullscreen &&
                e.type == ConfigureNotify &&
                e.xconfigure.window == m_Window)
            {
                const uint32_t newWidth  = static_cast<uint32_t>(e.xconfigure.width);
                const uint32_t newHeight = static_cast<uint32_t>(e.xconfigure.height);
                if (newWidth != m_Width || newHeight != m_Height)
                {
                    m_Width  = newWidth;
                    m_Height = newHeight;
                    gotFullscreenSize = true;
                }
            }
            if (e.type == MapNotify && e.xmap.window == m_Window)
                break;
        }

        if (_bFullscreen && !gotFullscreenSize)
        {
            // The WM applies the EWMH fullscreen state asynchronously: it must
            // receive our property + MapRaised, resize the window, then send
            // ConfigureNotify.  If no fullscreen ConfigureNotify arrived before
            // MapNotify, poll the X connection (up to 500 ms) so the Vulkan
            // surface and swapchain are created at the monitor's actual size.
            const int xfd = ConnectionNumber(m_pDisplay);
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (!gotFullscreenSize &&
                   std::chrono::steady_clock::now() < deadline)
            {
                XSync(m_pDisplay, False);
                while (XPending(m_pDisplay))
                {
                    XNextEvent(m_pDisplay, &e);
                    if (e.type == ConfigureNotify &&
                        e.xconfigure.window == m_Window)
                    {
                        const uint32_t newWidth  = static_cast<uint32_t>(e.xconfigure.width);
                        const uint32_t newHeight = static_cast<uint32_t>(e.xconfigure.height);
                        if (newWidth != m_Width || newHeight != m_Height)
                        {
                            m_Width  = newWidth;
                            m_Height = newHeight;
                            gotFullscreenSize = true;
                        }
                    }
                }
                if (!gotFullscreenSize)
                {
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(xfd, &fds);
                    struct timeval tv{0, 10000};  // 10 ms
                    select(xfd + 1, &fds, nullptr, nullptr, &tv);
                }
            }
        }
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
            spEvent->m_bCtrl    = (xEvent.xkey.state & ControlMask) != 0;

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
                case XK_a:      spEvent->m_Code = CKeyEvent::KEY_A;     break;
                case XK_d:      spEvent->m_Code = CKeyEvent::KEY_D;     break;
                case XK_r:      spEvent->m_Code = CKeyEvent::KEY_R;     break;
                case XK_h:      spEvent->m_Code = CKeyEvent::KEY_H;     break;
                case XK_j:      spEvent->m_Code = CKeyEvent::KEY_J;     break;
                case XK_k:      spEvent->m_Code = CKeyEvent::KEY_K;     break;
                case XK_l:      spEvent->m_Code = CKeyEvent::KEY_L;     break;
                case XK_c:      spEvent->m_Code = CKeyEvent::KEY_C;     break;
                case XK_v:      spEvent->m_Code = CKeyEvent::KEY_V;     break;
                case XK_w:      spEvent->m_Code = CKeyEvent::KEY_W;     break;
                case XK_n:      spEvent->m_Code = CKeyEvent::KEY_N;     break;
                case XK_b:      spEvent->m_Code = CKeyEvent::KEY_B;     break;
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

        if (xEvent.type == ConfigureNotify && xEvent.xconfigure.window == m_Window)
        {
            // Keep m_Width/m_Height in sync with the actual window size as reported
            // by the WM.  This matters because StatsConsole uses Display()->Width/Height()
            // to compute normalised HUD coordinates, while DrawQuad's vertex shader uses
            // m_swapExtent.  If the two disagree after a WM-driven resize (e.g. fullscreen
            // toggle), the background rect is rendered at the wrong aspect ratio and can
            // appear as an ellipse rather than a rectangle.
            m_Width  = static_cast<uint32_t>(xEvent.xconfigure.width);
            m_Height = static_cast<uint32_t>(xEvent.xconfigure.height);
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
        {
            xdg_toplevel_set_fullscreen(m_pXdgToplevel, nullptr);
        }
        else
        {
            // Switch to a sensible windowed size; the compositor will send 0,0
            // in the configure event (defers to client), so set it here.
            m_Width  = 1280;
            m_Height = 720;
            xdg_toplevel_unset_fullscreen(m_pXdgToplevel);
            // Re-assert server-side decorations — some compositors drop them during fullscreen
            if (m_pToplevelDecoration)
                zxdg_toplevel_decoration_v1_set_mode(m_pToplevelDecoration,
                    ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
        wl_surface_commit(m_pWlSurface);
        wl_display_flush(m_pWlDisplay);
        return;
    }
#endif

    setWindowDecorations(!enabled);
    if (!m_pDisplay || !m_Window) return;

    // EWMH _NET_WM_STATE client message format:
    //   data.l[0] = 0 (remove), 1 (add), or 2 (toggle)  — NOT an Atom value
    //   data.l[1] = first property atom to change
    //   data.l[2] = second property atom (0 if only one)
    //   data.l[3] = source indication: 1 = normal application
    XEvent e{};
    e.type                 = ClientMessage;
    e.xclient.window       = m_Window;
    e.xclient.message_type = m_netWmState;
    e.xclient.format       = 32;
    e.xclient.data.l[0]    = enabled ? 1L : 0L;
    e.xclient.data.l[1]    = static_cast<long>(m_netWmFullscreen);
    e.xclient.data.l[2]    = 0L;
    e.xclient.data.l[3]    = 1L;
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
