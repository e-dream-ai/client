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

    // Staging buffer (re-used across uploads to the same texture)
    VkBuffer       m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMem    = VK_NULL_HANDLE;
    VkDeviceSize   m_stagingSize   = 0;

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

    VkDescriptorSet DescSet() const { return m_descSet; }
};

MakeSmartPointers(CTextureFlatVulkan);

} // namespace DisplayOutput

#endif
