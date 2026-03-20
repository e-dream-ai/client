#ifndef _RENDERERVULKAN_H_
#define _RENDERERVULKAN_H_

#include "Renderer.h"
#include "TextureFlatVulkan.h"
#include "FontVulkan.h"
#include <vulkan/vulkan.h>
#include <map>
#include <string>
#include <vector>

namespace DisplayOutput
{

static const int MAX_FRAMES_IN_FLIGHT = 2;
static const int MAX_QUADS_PER_FRAME  = 4096;

struct QuadVertex {
    float x, y;
    float u, v;
};

// Push constants layout — must match quad.vert / quad.frag
struct VkPushConstants {
    float screenWidth;
    float screenHeight;
    float r, g, b, a;
};

/*
    CRendererVulkan.
    Implements the CRenderer interface using Vulkan.
*/
class CRendererVulkan : public CRenderer
{
    // Physical + logical device
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          m_presentQueue   = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamily = UINT32_MAX;
    uint32_t         m_presentFamily  = UINT32_MAX;

    // Swapchain
    VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
    VkExtent2D               m_swapExtent{};
    VkFormat                 m_swapFormat = VK_FORMAT_UNDEFINED;

    // Render pass + framebuffers
    VkRenderPass               m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    // Descriptor layout + pool + sampler
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkSampler             m_defaultSampler      = VK_NULL_HANDLE;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;

    // Command pool + buffers
    VkCommandPool                m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Synchronisation
    VkSemaphore m_imageAvailable[MAX_FRAMES_IN_FLIGHT]{};
    VkSemaphore m_renderFinished[MAX_FRAMES_IN_FLIGHT]{};
    VkFence     m_inFlightFence [MAX_FRAMES_IN_FLIGHT]{};

    // Per-frame vertex buffers (HOST_VISIBLE for immediate writes)
    VkBuffer       m_vertexBuffer[MAX_FRAMES_IN_FLIGHT]{};
    VkDeviceMemory m_vertexMemory[MAX_FRAMES_IN_FLIGHT]{};
    void*          m_mappedVertex[MAX_FRAMES_IN_FLIGHT]{};

    // White 1×1 fallback texture (used for solid-colour quads)
    VkImage         m_whiteImage    = VK_NULL_HANDLE;
    VkDeviceMemory  m_whiteImageMem = VK_NULL_HANDLE;
    VkImageView     m_whiteView     = VK_NULL_HANDLE;
    VkDescriptorSet m_whiteDescSet  = VK_NULL_HANDLE;

    // Frame tracking
    uint32_t m_currentFrame      = 0;
    uint32_t m_currentImageIndex = 0;
    bool     m_inFrame           = false;
    uint32_t m_vertexCount       = 0;   // vertices written this frame

    // Currently active texture's descriptor set
    VkDescriptorSet m_currentDescSet = VK_NULL_HANDLE;

    // Font pool
    std::map<std::string, spCBaseFont> m_fontPool;

    // Initialisation helpers
    bool pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    bool createLogicalDevice();
    bool createSwapchain(VkSurfaceKHR surface, uint32_t width, uint32_t height);
    bool createRenderPass();
    bool createFramebuffers();
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createSampler();
    bool createPipeline();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createVertexBuffers();
    bool createWhiteTexture();
    VkShaderModule loadShader(const std::string& path);

  public:
    CRendererVulkan();
    virtual ~CRendererVulkan();

    // CRenderer interface
    virtual eRenderType Type(void) const override { return eMetal; } // enum reuse
    virtual const std::string Description(void) const override { return "Vulkan"; }

    virtual bool Initialize(spCDisplayOutput _spDisplay) override;
    virtual void Defaults() override;
    virtual bool BeginFrame(void) override;
    virtual bool EndFrame(bool drawn = true) override;
    virtual void Apply() override;
    virtual void Reset(const uint32_t _flags) override;

    virtual spCTextureFlat NewTextureFlat(const uint32_t flags = 0) override;
    virtual spCTextureFlat NewTextureFlat(spCImage _spImage,
                                          const uint32_t flags = 0) override;

    virtual spCBaseFont GetFont(CFontDescription& _desc) override;
    virtual spCBaseText NewText(spCBaseFont _font,
                                const std::string& _text) override;
    virtual void DrawText(spCBaseText _text,
                          const Base::Math::CVector4& _color) override;

    virtual spCShader NewShader(
        const char* _pVert, const char* _pFrag,
        std::vector<std::pair<std::string, eUniformType>> uniforms = {}) override;

    virtual void DrawQuad(const Base::Math::CRect& _rect,
                          const Base::Math::CVector4& _color) override;
    virtual void DrawQuad(const Base::Math::CRect& _rect,
                          const Base::Math::CVector4& _color,
                          const Base::Math::CRect& _uvRect) override;
    virtual void DrawSoftQuad(const Base::Math::CRect& _rect,
                               const Base::Math::CVector4& _color,
                               const float _width) override;

    // Accessors used by CTextureFlatVulkan
    VkDevice              GetDevice()             const { return m_device; }
    VkPhysicalDevice      GetPhysicalDevice()     const { return m_physicalDevice; }
    VkCommandPool         GetCommandPool()        const { return m_commandPool; }
    VkQueue               GetGraphicsQueue()      const { return m_graphicsQueue; }
    VkDescriptorPool      GetDescriptorPool()     const { return m_descriptorPool; }
    VkDescriptorSetLayout GetDescriptorSetLayout()const { return m_descriptorSetLayout; }
    VkSampler             GetDefaultSampler()     const { return m_defaultSampler; }
    uint32_t  FindMemType(uint32_t filter, VkMemoryPropertyFlags props);

    // Single-time command helpers (used by CTextureFlatVulkan for uploads)
    VkCommandBuffer BeginSingleTimeCommands();
    void            EndSingleTimeCommands(VkCommandBuffer cmd);

    // Let the active texture set its descriptor
    void SetDescriptorSet(VkDescriptorSet ds) { m_currentDescSet = ds; }
};

MakeSmartPointers(CRendererVulkan);

} // namespace DisplayOutput

#endif
