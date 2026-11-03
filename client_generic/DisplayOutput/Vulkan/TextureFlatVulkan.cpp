#ifndef WIN32

#include "TextureFlatVulkan.h"
#include "RendererVulkan.h"
#include "Image.h"
#include "Log.h"

extern "C" {
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
CTextureFlatVulkan::CTextureFlatVulkan(CRendererVulkan* renderer,
                                        const uint32_t flags)
    : CTextureFlat(flags), m_pRenderer(renderer)
{}

void CTextureFlatVulkan::DestroyVulkanResources()
{
    if (!m_pRenderer) return;
    VkDevice dev = m_pRenderer->GetDevice();
    if (dev == VK_NULL_HANDLE) return;

    // Wait for any in-flight upload to finish before freeing the staging buffer.
    if (m_copyPending && m_copyFence != VK_NULL_HANDLE)
    {
        vkWaitForFences(dev, 1, &m_copyFence, VK_TRUE, UINT64_MAX);
        m_copyPending = false;
    }
    if (m_uploadCmdBuffer != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(dev, m_pRenderer->GetCommandPool(),
                             1, &m_uploadCmdBuffer);
        m_uploadCmdBuffer = VK_NULL_HANDLE;
    }
    if (m_copyFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(dev, m_copyFence, nullptr);
        m_copyFence = VK_NULL_HANDLE;
    }

    destroyPlanes();

    // Return descriptor sets to the shared pool; a new texture is created for
    // every clip, so leaking them would eventually exhaust the pool.
    VkDescriptorSet sets[2];
    uint32_t setCount = 0;
    if (m_descSet != VK_NULL_HANDLE)      sets[setCount++] = m_descSet;
    if (m_planeDescSet != VK_NULL_HANDLE) sets[setCount++] = m_planeDescSet;
    if (setCount > 0)
        vkFreeDescriptorSets(dev, m_pRenderer->GetDescriptorPool(), setCount, sets);
    m_descSet      = VK_NULL_HANDLE;
    m_planeDescSet = VK_NULL_HANDLE;

    if (m_stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(dev, m_stagingBuffer, nullptr);
        vkFreeMemory(dev, m_stagingMem, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingMem    = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(dev, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(dev, m_image, nullptr);
        vkFreeMemory(dev, m_imageMem, nullptr);
        m_image    = VK_NULL_HANDLE;
        m_imageMem = VK_NULL_HANDLE;
    }
}

CTextureFlatVulkan::~CTextureFlatVulkan()
{
    DestroyVulkanResources();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Creates a single-mip 2-D image in device-local memory plus a view of it.
static bool createImage2D(CRendererVulkan* renderer, VkFormat format,
                          uint32_t w, uint32_t h, VkImageUsageFlags usage,
                          VkImage& image, VkDeviceMemory& mem, VkImageView& view)
{
    VkDevice dev = renderer->GetDevice();

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = format;
    imgInfo.extent        = {w, h, 1};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = usage;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &imgInfo, nullptr, &image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(dev, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = renderer->FindMemType(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(dev, &allocInfo, nullptr, &mem) != VK_SUCCESS)
        return false;
    vkBindImageMemory(dev, image, mem, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    return vkCreateImageView(dev, &viewInfo, nullptr, &view) == VK_SUCCESS;
}

bool CTextureFlatVulkan::allocStaging(VkDeviceSize size)
{
    VkDevice dev = m_pRenderer->GetDevice();

    if (m_stagingBuffer != VK_NULL_HANDLE && size <= m_stagingSize)
        return true; // re-use existing

    if (m_stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(dev, m_stagingBuffer, nullptr);
        vkFreeMemory(dev, m_stagingMem, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingMem    = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = size;
    bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(dev, &bufInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(dev, m_stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = m_pRenderer->FindMemType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(dev, &allocInfo, nullptr, &m_stagingMem) != VK_SUCCESS)
        return false;

    vkBindBufferMemory(dev, m_stagingBuffer, m_stagingMem, 0);
    m_stagingSize = size;
    return true;
}

// (Re-)create the RGBA image, its view and descriptor set when the size changes.
bool CTextureFlatVulkan::ensureImage(uint32_t w, uint32_t h)
{
    if (m_image != VK_NULL_HANDLE && w == m_imgWidth && h == m_imgHeight)
        return true;

    VkDevice dev = m_pRenderer->GetDevice();

    // The framebuffer references the old image view.
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(dev, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(dev, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(dev, m_image, nullptr);
        vkFreeMemory(dev, m_imageMem, nullptr);
        m_image    = VK_NULL_HANDLE;
        m_imageMem = VK_NULL_HANDLE;
    }
    m_imgWidth  = 0;
    m_imgHeight = 0;

    // COLOR_ATTACHMENT lets the YUV conversion pass render into the image.
    if (!createImage2D(m_pRenderer, VK_FORMAT_R8G8B8A8_SRGB, w, h,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                       m_image, m_imageMem, m_imageView))
        return false;

    m_imgWidth  = w;
    m_imgHeight = h;

    // Allocate / update descriptor set
    if (m_descSet == VK_NULL_HANDLE)
    {
        VkDescriptorSetAllocateInfo dsAlloc{};
        dsAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool     = m_pRenderer->GetDescriptorPool();
        dsAlloc.descriptorSetCount = 1;
        VkDescriptorSetLayout layout = m_pRenderer->GetDescriptorSetLayout();
        dsAlloc.pSetLayouts        = &layout;
        if (vkAllocateDescriptorSets(dev, &dsAlloc, &m_descSet) != VK_SUCCESS)
        {
            g_Log->Error("CTextureFlatVulkan: descriptor set allocation failed");
            m_descSet = VK_NULL_HANDLE;
            return false;
        }
    }

    VkDescriptorImageInfo imgDescInfo{};
    imgDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgDescInfo.imageView   = m_imageView;
    imgDescInfo.sampler     = m_pRenderer->GetDefaultSampler();

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descSet;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &imgDescInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    return true;
}

// Waits for the previous upload so its staging buffer can be overwritten, then
// starts recording into the persistent upload command buffer.
VkCommandBuffer CTextureFlatVulkan::beginUpload()
{
    VkDevice dev = m_pRenderer->GetDevice();

    // Lazily create the persistent upload command buffer and fence.
    if (m_uploadCmdBuffer == VK_NULL_HANDLE)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_pRenderer->GetCommandPool();
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(dev, &allocInfo, &m_uploadCmdBuffer);

        // Initially signaled so the first upload skips the wait.
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(dev, &fenceInfo, nullptr, &m_copyFence);
    }

    // In practice the GPU copy finishes long before the next video frame
    // arrives (~33 ms at 30 fps vs < 1 ms for a typical copy), so this
    // returns immediately and replaces the former vkQueueWaitIdle() stall.
    if (m_copyPending)
    {
        vkWaitForFences(dev, 1, &m_copyFence, VK_TRUE, UINT64_MAX);
        m_copyPending = false;
    }
    vkResetFences(dev, 1, &m_copyFence);

    vkResetCommandBuffer(m_uploadCmdBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_uploadCmdBuffer, &beginInfo);
    return m_uploadCmdBuffer;
}

void CTextureFlatVulkan::submitUpload()
{
    vkEndCommandBuffer(m_uploadCmdBuffer);

    // Submit without blocking the CPU.  The graphics queue executes submissions
    // in order, so the render command buffer (submitted later in EndFrame) is
    // guaranteed to start only after this upload completes — no semaphore needed.
    // The fence is checked at the start of the *next* upload to ensure the
    // staging buffer is no longer in use before we overwrite it.
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &m_uploadCmdBuffer;
    vkQueueSubmit(m_pRenderer->GetGraphicsQueue(), 1, &submitInfo, m_copyFence);
    m_copyPending = true;

    m_dirty = true;   // signal Apply() to re-call Bind() with the new descSet
}

// ---------------------------------------------------------------------------
// Upload from CImage
// ---------------------------------------------------------------------------
bool CTextureFlatVulkan::Upload(spCImage _spImage)
{
    if (!_spImage) return false;
    m_spImage = _spImage;

    uint32_t w = _spImage->GetWidth(0);
    uint32_t h = _spImage->GetHeight(0);
    const uint8_t* pixels = _spImage->GetData(0);
    if (!pixels) return false;

    VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

    // Starting the upload first waits for the previous one, so the staging
    // buffer is free to overwrite.
    VkCommandBuffer cmd = beginUpload();
    if (!allocStaging(size) || !ensureImage(w, h))
    {
        vkEndCommandBuffer(cmd);
        return false;
    }

    // Copy pixels → staging
    VkDevice dev = m_pRenderer->GetDevice();
    void* mapped;
    vkMapMemory(dev, m_stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, pixels, static_cast<size_t>(size));
    vkUnmapMemory(dev, m_stagingMem);

    return uploadToImage(w, h);
}

bool CTextureFlatVulkan::uploadToImage(uint32_t w, uint32_t h)
{
    VkCommandBuffer cmd = m_uploadCmdBuffer;

    // Transition: UNDEFINED → TRANSFER_DST, after earlier frames finish
    // sampling the image.
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = m_image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy staging → image
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TRANSFER_DST → SHADER_READ_ONLY
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    submitUpload();
    return true;
}

// ---------------------------------------------------------------------------
// YUV planes + GPU conversion
// ---------------------------------------------------------------------------
void CTextureFlatVulkan::destroyPlanes()
{
    VkDevice dev = m_pRenderer->GetDevice();
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(dev, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    for (int i = 0; i < kMaxPlanes; ++i)
    {
        if (m_planeView[i] != VK_NULL_HANDLE)
            vkDestroyImageView(dev, m_planeView[i], nullptr);
        if (m_planeImage[i] != VK_NULL_HANDLE)
            vkDestroyImage(dev, m_planeImage[i], nullptr);
        if (m_planeMem[i] != VK_NULL_HANDLE)
            vkFreeMemory(dev, m_planeMem[i], nullptr);
        m_planeView[i]  = VK_NULL_HANDLE;
        m_planeImage[i] = VK_NULL_HANDLE;
        m_planeMem[i]   = VK_NULL_HANDLE;
    }
    m_planeWidth  = 0;
    m_planeHeight = 0;
}

// (Re-)create the plane images for a w×h 4:2:0 frame, point the plane
// descriptor set at them, and create the framebuffer over the RGBA image.
// Expects ensureImage(w, h) to have been called.
bool CTextureFlatVulkan::ensurePlanes(uint32_t w, uint32_t h, bool interleaved)
{
    VkDevice dev = m_pRenderer->GetDevice();

    if (m_planeImage[0] == VK_NULL_HANDLE || w != m_planeWidth ||
        h != m_planeHeight || interleaved != m_planesInterleaved)
    {
        destroyPlanes();

        const uint32_t cw = (w + 1) / 2;
        const uint32_t ch = (h + 1) / 2;
        const VkImageUsageFlags usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        bool ok = createImage2D(m_pRenderer, VK_FORMAT_R8_UNORM, w, h, usage,
                                m_planeImage[0], m_planeMem[0], m_planeView[0]);
        if (interleaved)
            ok = ok && createImage2D(m_pRenderer, VK_FORMAT_R8G8_UNORM, cw, ch, usage,
                                     m_planeImage[1], m_planeMem[1], m_planeView[1]);
        else
            ok = ok && createImage2D(m_pRenderer, VK_FORMAT_R8_UNORM, cw, ch, usage,
                                     m_planeImage[1], m_planeMem[1], m_planeView[1])
                    && createImage2D(m_pRenderer, VK_FORMAT_R8_UNORM, cw, ch, usage,
                                     m_planeImage[2], m_planeMem[2], m_planeView[2]);
        if (!ok)
        {
            g_Log->Error("CTextureFlatVulkan: failed to create YUV plane images (%ux%u)", w, h);
            destroyPlanes();
            return false;
        }
        m_planeWidth        = w;
        m_planeHeight       = h;
        m_planesInterleaved = interleaved;

        if (m_planeDescSet == VK_NULL_HANDLE)
        {
            VkDescriptorSetAllocateInfo dsAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            dsAlloc.descriptorPool     = m_pRenderer->GetDescriptorPool();
            dsAlloc.descriptorSetCount = 1;
            VkDescriptorSetLayout layout = m_pRenderer->GetYUVSetLayout();
            dsAlloc.pSetLayouts        = &layout;
            if (vkAllocateDescriptorSets(dev, &dsAlloc, &m_planeDescSet) != VK_SUCCESS)
            {
                g_Log->Error("CTextureFlatVulkan: YUV descriptor set allocation failed");
                m_planeDescSet = VK_NULL_HANDLE;
                destroyPlanes();
                return false;
            }
        }

        // Every binding needs a valid image: for NV12 the unused V binding
        // reuses the UV plane.
        VkDescriptorImageInfo infos[kMaxPlanes]{};
        VkWriteDescriptorSet writes[kMaxPlanes]{};
        for (int i = 0; i < kMaxPlanes; ++i)
        {
            infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            infos[i].imageView   = m_planeView[i] != VK_NULL_HANDLE ? m_planeView[i]
                                                                    : m_planeView[1];
            infos[i].sampler     = m_pRenderer->GetDefaultSampler();

            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = m_planeDescSet;
            writes[i].dstBinding      = static_cast<uint32_t>(i);
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo      = &infos[i];
        }
        vkUpdateDescriptorSets(dev, kMaxPlanes, writes, 0, nullptr);
    }

    if (m_framebuffer == VK_NULL_HANDLE)
    {
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = m_pRenderer->GetYUVRenderPass();
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_imageView;
        fbci.width           = w;
        fbci.height          = h;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(dev, &fbci, nullptr, &m_framebuffer) != VK_SUCCESS)
        {
            m_framebuffer = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

// Conversion constants for the frame's colour matrix and range. Untagged
// video is treated as BT.709, matching the Metal and D3D11 renderers.
static VkPushConstantsYUV yuvConversionConstants(const AVFrame* frame,
                                                 bool interleaved)
{
    float kr = 0.2126f, kb = 0.0722f;  // BT.709
    switch (frame->colorspace)
    {
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
            kr = 0.299f;  kb = 0.114f;   // BT.601
            break;
        case AVCOL_SPC_BT2020_NCL:
            kr = 0.2627f; kb = 0.0593f;  // BT.2020
            break;
        default:
            break;
    }
    const float kg = 1.0f - kr - kb;

    VkPushConstantsYUV pc{};
    pc.matrix[0] = 2.0f * (1.0f - kr);
    pc.matrix[1] = 2.0f * kb * (1.0f - kb) / kg;
    pc.matrix[2] = 2.0f * kr * (1.0f - kr) / kg;
    pc.matrix[3] = 2.0f * (1.0f - kb);

    const bool fullRange = frame->color_range == AVCOL_RANGE_JPEG ||
                           frame->format == AV_PIX_FMT_YUVJ420P;
    pc.range[0] = fullRange ? 1.0f : 255.0f / 219.0f;
    pc.range[1] = fullRange ? 0.0f : 16.0f / 255.0f;
    pc.range[2] = fullRange ? 1.0f : 255.0f / 224.0f;
    pc.range[3] = 128.0f / 255.0f;
    pc.interleavedChroma = interleaved ? 1 : 0;
    return pc;
}

bool CTextureFlatVulkan::uploadYUV(const AVFrame* frame, bool interleaved)
{
    if (frame->width <= 0 || frame->height <= 0) return false;
    const uint32_t w  = static_cast<uint32_t>(frame->width);
    const uint32_t h  = static_cast<uint32_t>(frame->height);
    const uint32_t cw = (w + 1) / 2;
    const uint32_t ch = (h + 1) / 2;

    struct Plane { const uint8_t* data; int linesize; uint32_t width, height, texelSize; };
    Plane planes[kMaxPlanes];
    int planeCount;
    if (interleaved)
    {
        planes[0] = {frame->data[0], frame->linesize[0], w,  h,  1};
        planes[1] = {frame->data[1], frame->linesize[1], cw, ch, 2};
        planeCount = 2;
    }
    else
    {
        planes[0] = {frame->data[0], frame->linesize[0], w,  h,  1};
        planes[1] = {frame->data[1], frame->linesize[1], cw, ch, 1};
        planes[2] = {frame->data[2], frame->linesize[2], cw, ch, 1};
        planeCount = 3;
    }

    // Planes are copied with their source stride (bufferRowLength), so each
    // row length must be a whole number of texels. Offsets stay 16-byte aligned.
    VkDeviceSize offsets[kMaxPlanes];
    VkDeviceSize sizes[kMaxPlanes];
    VkDeviceSize total = 0;
    for (int i = 0; i < planeCount; ++i)
    {
        const Plane& p = planes[i];
        if (!p.data || p.linesize < static_cast<int>(p.width * p.texelSize) ||
            p.linesize % static_cast<int>(p.texelSize) != 0)
            return false;
        offsets[i] = total;
        sizes[i]   = static_cast<VkDeviceSize>(p.linesize) * (p.height - 1) +
                     p.width * p.texelSize;
        total      = (offsets[i] + sizes[i] + 15) & ~VkDeviceSize(15);
    }

    VkCommandBuffer cmd = beginUpload();
    if (!allocStaging(total) || !ensureImage(w, h) || !ensurePlanes(w, h, interleaved))
    {
        vkEndCommandBuffer(cmd);
        return false;
    }

    VkDevice dev = m_pRenderer->GetDevice();
    uint8_t* mapped;
    vkMapMemory(dev, m_stagingMem, 0, total, 0, reinterpret_cast<void**>(&mapped));
    for (int i = 0; i < planeCount; ++i)
        memcpy(mapped + offsets[i], planes[i].data, static_cast<size_t>(sizes[i]));
    vkUnmapMemory(dev, m_stagingMem);

    // Planes: wait for the previous conversion to finish reading them, then
    // copy the new data in.
    VkImageMemoryBarrier barriers[kMaxPlanes]{};
    for (int i = 0; i < planeCount; ++i)
    {
        barriers[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[i].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image               = m_planeImage[i];
        barriers[i].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers[i].srcAccessMask       = 0;
        barriers[i].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(planeCount), barriers);

    for (int i = 0; i < planeCount; ++i)
    {
        VkBufferImageCopy region{};
        region.bufferOffset      = offsets[i];
        region.bufferRowLength   = static_cast<uint32_t>(planes[i].linesize) / planes[i].texelSize;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent       = {planes[i].width, planes[i].height, 1};
        vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_planeImage[i],
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    for (int i = 0; i < planeCount; ++i)
    {
        barriers[i].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(planeCount), barriers);

    // Convert into the RGBA image. The render pass handles its layout
    // transitions and ordering against earlier/later sampling.
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = m_pRenderer->GetYUVRenderPass();
    rpbi.framebuffer       = m_framebuffer;
    rpbi.renderArea.extent = {w, h};
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pRenderer->GetYUVPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pRenderer->GetYUVPipelineLayout(), 0, 1,
                            &m_planeDescSet, 0, nullptr);

    const VkPushConstantsYUV pc = yuvConversionConstants(frame, interleaved);
    vkCmdPushConstants(cmd, m_pRenderer->GetYUVPipelineLayout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    VkViewport vp{};
    vp.width    = static_cast<float>(w);
    vp.height   = static_cast<float>(h);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {w, h}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    submitUpload();
    return true;
}

// ---------------------------------------------------------------------------
// Bind — register this texture's descriptor set at the given slot.
// Slot 0 is used for single-texture (no-blend) draws; slots 1–4 hold the
// frame textures consumed by the linear / cubic blend pipelines.
// ---------------------------------------------------------------------------
bool CTextureFlatVulkan::Bind(const uint32_t _index)
{
    if (m_descSet != VK_NULL_HANDLE)
        m_pRenderer->SetDescriptorSet(m_descSet, _index);
    m_dirty = false;
    return true;
}

// ---------------------------------------------------------------------------
// BindFrame — upload a decoded AVFrame. 8-bit 4:2:0 YUV is converted on the
// GPU; anything else is converted to RGBA with swscale first.
// ---------------------------------------------------------------------------
bool CTextureFlatVulkan::BindFrame(ContentDecoder::spCVideoFrame _spFrame)
{
    if (!_spFrame) return false;

    AVFrame* frame = _spFrame->Frame();
    if (!frame) return false;

    uint32_t w = _spFrame->Width();
    uint32_t h = _spFrame->Height();

    AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
    if (m_pRenderer->HasYUVConversion())
    {
        if (srcFmt == AV_PIX_FMT_NV12)
            return uploadYUV(frame, true);
        if (srcFmt == AV_PIX_FMT_YUV420P || srcFmt == AV_PIX_FMT_YUVJ420P)
            return uploadYUV(frame, false);
    }

    // Normalize JPEG-range YUV formats (deprecated in newer FFmpeg; sws_setColorspaceDetails aborts on them)
    if      (srcFmt == AV_PIX_FMT_YUVJ420P) srcFmt = AV_PIX_FMT_YUV420P;
    else if (srcFmt == AV_PIX_FMT_YUVJ422P) srcFmt = AV_PIX_FMT_YUV422P;
    else if (srcFmt == AV_PIX_FMT_YUVJ444P) srcFmt = AV_PIX_FMT_YUV444P;
    else if (srcFmt == AV_PIX_FMT_YUVJ440P) srcFmt = AV_PIX_FMT_YUV440P;
    else if (srcFmt == AV_PIX_FMT_YUVJ411P) srcFmt = AV_PIX_FMT_YUV411P;

    // The ContentDecoder pre-converts to RGBA on Linux (AV_PIX_FMT_RGBA).
    // If the frame is already RGBA, upload directly without an extra swscale pass.
    if (srcFmt == AV_PIX_FMT_RGBA)
    {
        auto spImg = std::make_shared<CImage>();
        spImg->Create(w, h, eImage_RGBA8);
        if (uint8_t* dst = spImg->GetData(0))
        {
            // Copy row-by-row to handle any stride padding
            const int srcStride = frame->linesize[0];
            const int dstStride = static_cast<int>(w * 4);
            for (uint32_t row = 0; row < h; ++row)
                memcpy(dst + row * dstStride,
                       frame->data[0] + row * srcStride,
                       static_cast<size_t>(dstStride));
        }
        return Upload(spImg);
    }

    // Fallback: convert to RGBA using swscale for any other format
    if (srcFmt == AV_PIX_FMT_NONE) return false;  // guard against uninitialised frames

    SwsContext* sws = sws_getContext(
        static_cast<int>(w), static_cast<int>(h), srcFmt,
        static_cast<int>(w), static_cast<int>(h), AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws) return false;

    std::vector<uint8_t> rgba(w * h * 4);
    uint8_t* dstData[4]  = {rgba.data(), nullptr, nullptr, nullptr};
    int      dstLinesize[4] = {static_cast<int>(w * 4), 0, 0, 0};

    sws_scale(sws, frame->data, frame->linesize,
              0, static_cast<int>(h), dstData, dstLinesize);
    sws_freeContext(sws);

    auto spImg = std::make_shared<CImage>();
    spImg->Create(w, h, eImage_RGBA8);
    if (uint8_t* dst = spImg->GetData(0))
        memcpy(dst, rgba.data(), rgba.size());
    return Upload(spImg);
}

// ---------------------------------------------------------------------------
// Upload from raw buffer
// ---------------------------------------------------------------------------
bool CTextureFlatVulkan::Upload(const uint8_t* data, CImageFormat fmt,
                                 uint32_t w, uint32_t h,
                                 uint32_t /*bytesPerRow*/, bool /*mipMapped*/,
                                 uint32_t /*mipLevel*/)
{
    auto spImg = std::make_shared<CImage>();
    spImg->Create(w, h, eImage_RGBA8);
    if (uint8_t* dst = spImg->GetData(0))
        memcpy(dst, data, static_cast<size_t>(w) * h * 4);
    return Upload(spImg);
}

} // namespace DisplayOutput

#endif // !WIN32
