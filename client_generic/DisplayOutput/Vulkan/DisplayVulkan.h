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
#include <vulkan/vulkan_wayland.h>
#include "xdg-shell-client-protocol.h"
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
    struct wl_compositor* m_pWlCompositor = nullptr;
    struct xdg_wm_base*   m_pXdgWmBase    = nullptr;
    struct xdg_surface*   m_pXdgSurface   = nullptr;
    struct xdg_toplevel*  m_pXdgToplevel  = nullptr;

    bool initWayland(uint32_t w, uint32_t h, bool bFullscreen);
    void destroyWayland();

    // Wayland listener callbacks (static so they match C function-pointer ABI)
    static void onRegistryGlobal(void*, wl_registry*, uint32_t, const char*, uint32_t);
    static void onRegistryGlobalRemove(void*, wl_registry*, uint32_t);
    static void onXdgWmBasePing(void*, xdg_wm_base*, uint32_t);
    static void onXdgSurfaceConfigure(void*, xdg_surface*, uint32_t);
    static void onXdgToplevelConfigure(void*, xdg_toplevel*, int32_t, int32_t, wl_array*);
    static void onXdgToplevelClose(void*, xdg_toplevel*);
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
    virtual void SwapBuffers() override {} // presentation managed by RendererVulkan
    virtual bool HasShaders() override { return true; }

    // Accessors for CRendererVulkan
    VkInstance   GetInstance()  const { return m_instance; }
    VkSurfaceKHR GetSurface()   const { return m_surface; }
    Display*     GetXDisplay()  const { return m_pDisplay; }
    Window       GetXWindow()   const { return m_Window; }
};

} // namespace DisplayOutput

#endif
