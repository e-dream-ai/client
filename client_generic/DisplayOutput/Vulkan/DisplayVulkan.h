#ifndef _DISPLAYVULKAN_H_
#define _DISPLAYVULKAN_H_

#ifndef LINUX_GNU
#error "DisplayVulkan.h is Linux-only"
#endif

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>

#ifdef HAVE_WAYLAND
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <vulkan/vulkan_wayland.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#ifdef HAVE_LIBDECOR
#include <libdecor.h>
#endif
#endif

#include <string>
#include "DisplayOutput.h"

namespace DisplayOutput
{

/*
    CDisplayVulkan.

    Creates a window and a VkSurfaceKHR for Linux. At runtime:
      - Native Wayland (VK_KHR_wayland_surface) when WAYLAND_DISPLAY is set
        and XSCREENSAVER_WINDOW is not — requires HAVE_WAYLAND at build time.
      - X11 (VK_KHR_xlib_surface) otherwise, including XWayland sessions and
        XScreensaver embed mode (which is inherently X11).

    CRendererVulkan owns the device, swapchain, and pipelines; this class
    owns only the window and VkSurfaceKHR.
*/
class CDisplayVulkan : public CDisplayOutput
{
    // -----------------------------------------------------------------------
    // X11 state
    // -----------------------------------------------------------------------
    Display*  m_pDisplay       = nullptr;
    Window    m_Window         = 0;
    Atom      m_wmDeleteWindow = 0;
    Atom      m_netWmState     = 0;
    Atom      m_netWmFullscreen= 0;

#ifdef HAVE_WAYLAND
    // -----------------------------------------------------------------------
    // Wayland state (populated only when m_bWayland == true)
    // -----------------------------------------------------------------------
    struct wl_display*    m_pWlDisplay    = nullptr;
    struct wl_surface*    m_pWlSurface    = nullptr;
    struct wl_surface*    m_pCursorSurface= nullptr;
    struct wl_compositor* m_pWlCompositor = nullptr;
    struct wl_shm*        m_pWlShm        = nullptr;
    struct xdg_wm_base*   m_pXdgWmBase    = nullptr;
    struct xdg_surface*   m_pXdgSurface   = nullptr;
    struct xdg_toplevel*  m_pXdgToplevel  = nullptr;
    struct wl_seat*       m_pWlSeat       = nullptr;
    struct wl_keyboard*   m_pWlKeyboard   = nullptr;
    struct wl_pointer*    m_pWlPointer    = nullptr;
    struct zxdg_decoration_manager_v1*   m_pDecorationManager = nullptr;
    struct zxdg_toplevel_decoration_v1*  m_pToplevelDecoration = nullptr;
#ifdef HAVE_LIBDECOR
    struct libdecor*      m_pLibdecorContext = nullptr;
    struct libdecor_frame* m_pLibdecorFrame  = nullptr;
    bool                  m_bUsingLibdecor   = false;
#endif

    struct xkb_context*   m_pXkbContext   = nullptr;
    struct xkb_keymap*    m_pXkbKeymap    = nullptr;
    struct xkb_state*     m_pXkbState     = nullptr;
    struct wl_cursor_theme* m_pCursorTheme = nullptr;
    struct wl_cursor*     m_pArrowCursor  = nullptr;
#ifdef HAVE_LIBDECOR
    struct wl_cursor*     m_pResizeTopCursor = nullptr;
    struct wl_cursor*     m_pResizeBottomCursor = nullptr;
    struct wl_cursor*     m_pResizeLeftCursor = nullptr;
    struct wl_cursor*     m_pResizeRightCursor = nullptr;
    struct wl_cursor*     m_pResizeTopLeftCursor = nullptr;
    struct wl_cursor*     m_pResizeTopRightCursor = nullptr;
    struct wl_cursor*     m_pResizeBottomLeftCursor = nullptr;
    struct wl_cursor*     m_pResizeBottomRightCursor = nullptr;
    uint32_t              m_lastPointerSerial = 0;
    bool                  m_bPointerInsideContent = false;
    bool                  m_bWaylandCursorHiddenApplied = false;
    int32_t               m_lastPointerX = 0;
    int32_t               m_lastPointerY = 0;
    enum libdecor_resize_edge m_hoverResizeEdge = LIBDECOR_RESIZE_EDGE_NONE;
#endif

    bool initWayland(uint32_t w, uint32_t h, bool bFullscreen);
    void destroyWayland();
    void updateWaylandCursor();
#ifdef HAVE_LIBDECOR
    enum libdecor_resize_edge getWaylandResizeEdge(int32_t x, int32_t y) const;
    struct wl_cursor* getWaylandCursorForEdge(enum libdecor_resize_edge edge) const;
#endif

    // Wayland listener callbacks (static so they match C function-pointer ABI)
    static void onRegistryGlobal(void*, wl_registry*, uint32_t, const char*, uint32_t);
    static void onRegistryGlobalRemove(void*, wl_registry*, uint32_t);
    static void onXdgWmBasePing(void*, xdg_wm_base*, uint32_t);
    static void onXdgSurfaceConfigure(void*, xdg_surface*, uint32_t);
    static void onXdgToplevelConfigure(void*, xdg_toplevel*, int32_t, int32_t, wl_array*);
    static void onXdgToplevelClose(void*, xdg_toplevel*);
    static void onSeatCapabilities(void*, wl_seat*, uint32_t);
    static void onSeatName(void*, wl_seat*, const char*);
    static void onKeyboardKeymap(void*, wl_keyboard*, uint32_t, int32_t, uint32_t);
    static void onKeyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*);
    static void onKeyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*);
    static void onKeyboardKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t);
    static void onKeyboardModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    static void onKeyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t);
    static void onPointerEnter(void*, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t);
    static void onPointerLeave(void*, wl_pointer*, uint32_t, wl_surface*);
    static void onPointerMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t);
    static void onPointerButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t);
    static void onPointerAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t);
#ifdef HAVE_LIBDECOR
    static void onLibdecorError(struct libdecor*, enum libdecor_error, const char*);
    static void onLibdecorConfigure(struct libdecor_frame*, struct libdecor_configuration*, void*);
    static void onLibdecorClose(struct libdecor_frame*, void*);
    static void onLibdecorCommit(struct libdecor_frame*, void*);
#endif
#endif

    bool      m_bWayland      = false;
    bool      m_bFullScreen   = false;
    uint32_t  m_WidthFS       = 0;
    uint32_t  m_HeightFS      = 0;
    bool      m_bScreensaver  = false;

    // Vulkan
    VkInstance   m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface  = VK_NULL_HANDLE;

    bool createVulkanInstance();
    void setFullScreen(bool enabled);
    void setWindowDecorations(bool enabled);
    void checkEvents();
    void applyInvisibleCursor();

  public:
    CDisplayVulkan();
    virtual ~CDisplayVulkan();

    static const char* Description() { return "Linux Vulkan display"; }

    virtual bool Initialize(const uint32_t _width, const uint32_t _height,
                            const bool _bFullscreen) override;

    virtual void Title(const std::string& _title) override;
    virtual void Update() override;
    virtual void ToggleFullscreen() override { setFullScreen(!m_bFullScreen); }
    virtual bool IsFullscreen() const override { return m_bFullScreen; }
    virtual void SwapBuffers() override {} // presentation managed by RendererVulkan
    virtual bool HasShaders() override { return true; }

    // Accessors for CRendererVulkan
    VkInstance   GetInstance()  const { return m_instance; }
    VkSurfaceKHR GetSurface()   const { return m_surface; }
    Display*     GetXDisplay()  const { return m_pDisplay; }
    Window       GetXWindow()   const { return m_Window; }
    bool         IsWayland()    const { return m_bWayland; }
};

} // namespace DisplayOutput

#endif
