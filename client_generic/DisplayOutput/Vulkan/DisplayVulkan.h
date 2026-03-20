#ifndef _DISPLAYVULKAN_H_
#define _DISPLAYVULKAN_H_

#ifndef LINUX_GNU
#error "DisplayVulkan.h is Linux-only"
#endif

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
#include <string>

#include "DisplayOutput.h"

namespace DisplayOutput
{

/*
    CDisplayVulkan.
    X11 window + Vulkan surface for Linux.
    Mirrors CDisplayMetal's role: owns the window and surface;
    CRendererVulkan owns the device, swapchain, and pipelines.
*/
class CDisplayVulkan : public CDisplayOutput
{
    // X11
    Display*  m_pDisplay      = nullptr;
    Window    m_Window        = 0;
    bool      m_bFullScreen   = false;
    uint32_t  m_WidthFS       = 0;
    uint32_t  m_HeightFS      = 0;
    bool      m_bScreensaver  = false;

    // X11 Atoms
    Atom m_wmDeleteWindow = 0;
    Atom m_netWmState     = 0;
    Atom m_netWmFullscreen= 0;

    // Vulkan
    VkInstance   m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface  = VK_NULL_HANDLE;

    // Internal helpers
    bool createVulkanInstance();
    void setFullScreen(bool enabled);
    void setWindowDecorations(bool enabled);
    void alwaysOnTop();
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
