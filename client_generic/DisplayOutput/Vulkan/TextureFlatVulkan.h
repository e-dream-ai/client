#ifndef _TEXTUREFLATVULKAN_H_
#define _TEXTUREFLATVULKAN_H_

#include "TextureFlat.h"
#include <vulkan/vulkan.h>
#include <cstdint>

struct AVFrame;

// Forward declaration to avoid circular include
namespace DisplayOutput { class CRendererVulkan; }

namespace DisplayOutput
{

/*
    CTextureFlatVulkan.
    A flat (2-D) texture backed by an RGBA VkImage.
    Uploading data goes through a HOST_VISIBLE staging buffer. Decoded 8-bit
    4:2:0 video (NV12 / YUV420P) is uploaded as separate planes and converted
    into the RGBA image on the GPU.
*/
class CTextureFlatVulkan : public CTextureFlat
{
    // Back-pointer to renderer for device / command pool access
    CRendererVulkan* m_pRenderer = nullptr;

    // Vulkan objects
    VkImage        m_image      = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMem   = VK_NULL_HANDLE;
    VkImageView    m_imageView  = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet   = VK_NULL_HANDLE;

    // YUV planes (Y, U or interleaved UV, V) and the conversion pass target.
    // Created on the first YUV upload; plane 2 is unused for NV12.
    static const int kMaxPlanes = 3;
    VkImage         m_planeImage[kMaxPlanes]{};
    VkDeviceMemory  m_planeMem[kMaxPlanes]{};
    VkImageView     m_planeView[kMaxPlanes]{};
    VkDescriptorSet m_planeDescSet  = VK_NULL_HANDLE;
    VkFramebuffer   m_framebuffer   = VK_NULL_HANDLE;
    uint32_t        m_planeWidth    = 0;  // luma size the planes were created for
    uint32_t        m_planeHeight   = 0;
    bool            m_planesInterleaved = false;

    uint32_t m_imgWidth  = 0;
    uint32_t m_imgHeight = 0;
    bool     m_dirty     = false;

    // Staging buffer (re-used across uploads to the same texture)
    VkBuffer       m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMem    = VK_NULL_HANDLE;
    VkDeviceSize   m_stagingSize   = 0;

    // Persistent upload command buffer + fence for async uploads.
    // The fence signals when the GPU has finished consuming the staging buffer,
    // so we can safely overwrite it with new frame data on the next upload.
    VkCommandBuffer m_uploadCmdBuffer = VK_NULL_HANDLE;
    VkFence         m_copyFence       = VK_NULL_HANDLE;
    bool            m_copyPending     = false;

    bool allocStaging(VkDeviceSize size);
    bool ensureImage(uint32_t w, uint32_t h);
    bool uploadToImage(uint32_t w, uint32_t h);
    VkCommandBuffer beginUpload();
    void submitUpload();

    bool ensurePlanes(uint32_t w, uint32_t h, bool interleaved);
    void destroyPlanes();
    bool uploadYUV(const AVFrame* frame, bool interleaved);

  public:
    CTextureFlatVulkan(CRendererVulkan* renderer, const uint32_t flags);
    virtual ~CTextureFlatVulkan();

    // CTextureFlat interface
    virtual bool Upload(spCImage _spImage) override;
    virtual bool Upload(const uint8_t* data, CImageFormat fmt,
                        uint32_t w, uint32_t h,
                        uint32_t bytesPerRow, bool mipMapped,
                        uint32_t mipLevel);
    virtual bool BindFrame(ContentDecoder::spCVideoFrame _spFrame) override;
    virtual bool Bind(const uint32_t _index) override;
    virtual bool Dirty() override { return m_dirty; }

    VkDescriptorSet DescSet() const { return m_descSet; }

    // Explicitly free Vulkan resources while the device is still alive.
    // Called by the renderer destructor before vkDestroyDevice.
    // After this the destructor becomes a no-op (all handles are NULL).
    void DestroyVulkanResources();
};

MakeSmartPointers(CTextureFlatVulkan);

} // namespace DisplayOutput

#endif
