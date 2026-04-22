#ifndef _TEXTUREFLATVULKAN_H_
#define _TEXTUREFLATVULKAN_H_

#include "TextureFlat.h"
#include <vulkan/vulkan.h>
#include <cstdint>

// Forward declaration to avoid circular include
namespace DisplayOutput { class CRendererVulkan; }

namespace DisplayOutput
{

/*
    CTextureFlatVulkan.
    A flat (2-D) texture backed by a VkImage.
    Uploading data goes through a HOST_VISIBLE staging buffer.
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
    bool uploadToImage(uint32_t w, uint32_t h);

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
