#ifndef WIN32

#include "RendererVulkan.h"
#include "DisplayVulkan.h"
#include "ShaderVulkan.h"
#include "Image.h"
#include "Log.h"
#include "PlatformUtils.h"
#include "Rect.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
CRendererVulkan::CRendererVulkan()
{
    memset(m_imageAvailable, 0, sizeof(m_imageAvailable));
    memset(m_renderFinished, 0, sizeof(m_renderFinished));
    memset(m_inFlightFence,  0, sizeof(m_inFlightFence));
    memset(m_vertexBuffer,   0, sizeof(m_vertexBuffer));
    memset(m_vertexMemory,   0, sizeof(m_vertexMemory));
    memset(m_mappedVertex,   0, sizeof(m_mappedVertex));
}

CRendererVulkan::~CRendererVulkan()
{
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);

    // Shut down ImGui before any Vulkan resources are destroyed.
    if (m_imguiInitialized)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext     = nullptr;
        m_imguiInitialized = false;
    }

    // Font objects are ImGui-managed; just release the shared_ptr refs.
    m_fontPool.clear();

    // White texture
    if (m_whiteDescSet  != VK_NULL_HANDLE) { /* freed with pool */ }
    if (m_whiteView     != VK_NULL_HANDLE) vkDestroyImageView(m_device, m_whiteView, nullptr);
    if (m_whiteImage    != VK_NULL_HANDLE) vkDestroyImage(m_device, m_whiteImage, nullptr);
    if (m_whiteImageMem != VK_NULL_HANDLE) vkFreeMemory(m_device, m_whiteImageMem, nullptr);

    // Per-frame vertex buffers
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_vertexBuffer[i] != VK_NULL_HANDLE)
        {
            vkUnmapMemory(m_device, m_vertexMemory[i]);
            vkDestroyBuffer(m_device, m_vertexBuffer[i], nullptr);
            vkFreeMemory(m_device, m_vertexMemory[i], nullptr);
        }
        if (m_imageAvailable[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_imageAvailable[i], nullptr);
        if (m_renderFinished[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_renderFinished[i], nullptr);
        if (m_inFlightFence[i] != VK_NULL_HANDLE)
            vkDestroyFence(m_device, m_inFlightFence[i], nullptr);
    }

    if (m_timestampPool       != VK_NULL_HANDLE) vkDestroyQueryPool(m_device, m_timestampPool, nullptr);
    // Command buffers freed with pool
    if (m_commandPool         != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_pipeline            != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_pipelineLinear      != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipelineLinear, nullptr);
    if (m_pipelineLayoutLinear!= VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayoutLinear, nullptr);
    if (m_pipelineCubic       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipelineCubic, nullptr);
    if (m_pipelineLayoutCubic != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayoutCubic, nullptr);
    if (m_descriptorPool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    if (m_defaultSampler      != VK_NULL_HANDLE) vkDestroySampler(m_device, m_defaultSampler, nullptr);

    for (auto fb : m_framebuffers)       vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto iv : m_swapImageViews)     vkDestroyImageView(m_device, iv, nullptr);
    if (m_renderPass != VK_NULL_HANDLE)  vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_swapchain  != VK_NULL_HANDLE)  vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

    vkDestroyDevice(m_device, nullptr);
    // Null the handle so any CTextureFlatVulkan dtor that still holds a raw
    // renderer pointer sees VK_NULL_HANDLE and skips destroy calls safely.
    m_device = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Memory type helper
// ---------------------------------------------------------------------------
uint32_t CRendererVulkan::FindMemType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProp;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProp);

    for (uint32_t i = 0; i < memProp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) &&
            (memProp.memoryTypes[i].propertyFlags & props) == props)
            return i;

    g_Log->Error("CRendererVulkan: no suitable memory type found");
    return 0;
}

// ---------------------------------------------------------------------------
// Single-time command helpers
// ---------------------------------------------------------------------------
VkCommandBuffer CRendererVulkan::BeginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void CRendererVulkan::EndSingleTimeCommands(VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

// ---------------------------------------------------------------------------
// Physical device selection
// ---------------------------------------------------------------------------
bool CRendererVulkan::pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0)
    {
        g_Log->Error("CRendererVulkan: no Vulkan-capable GPU found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    // Prefer discrete GPU; fallback to first that supports our queues
    auto findQueues = [&](VkPhysicalDevice dev) -> bool {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());

        bool foundG = false, foundP = false;
        for (uint32_t i = 0; i < qCount; ++i)
        {
            if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_graphicsFamily = i;
                foundG = true;
            }
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
            if (present)
            {
                m_presentFamily = i;
                foundP = true;
            }
        }
        return foundG && foundP;
    };

    // First pass: discrete GPU
    for (auto dev : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && findQueues(dev))
        {
            m_physicalDevice = dev;
            g_Log->Info("CRendererVulkan: selected discrete GPU: %s", props.deviceName);
            return true;
        }
    }
    // Second pass: any device
    for (auto dev : devices)
    {
        if (findQueues(dev))
        {
            m_physicalDevice = dev;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            g_Log->Info("CRendererVulkan: selected GPU: %s", props.deviceName);
            return true;
        }
    }

    g_Log->Error("CRendererVulkan: no suitable GPU found");
    return false;
}

// ---------------------------------------------------------------------------
// Logical device
// ---------------------------------------------------------------------------
bool CRendererVulkan::createLogicalDevice()
{
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qInfos;
    for (uint32_t fam : {m_graphicsFamily, m_presentFamily})
    {
        // Deduplicate
        bool found = false;
        for (auto& q : qInfos) if (q.queueFamilyIndex == fam) { found = true; break; }
        if (found) continue;

        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        qInfos.push_back(qi);
    }

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(qInfos.size());
    createInfo.pQueueCreateInfos       = qInfos.data();
    createInfo.enabledExtensionCount   = 1;
    createInfo.ppEnabledExtensionNames = devExts;
    createInfo.pEnabledFeatures        = &features;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
    {
        g_Log->Error("CRendererVulkan: vkCreateDevice failed");
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily,  0, &m_presentQueue);
    return true;
}

// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------
bool CRendererVulkan::createSwapchain(VkSurfaceKHR surface,
                                       uint32_t width, uint32_t height)
{
    // Surface capabilities
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, surface, &caps);

    // Choose format: prefer B8G8R8A8_SRGB / SRGB_NONLINEAR
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosenFmt = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        { chosenFmt = f; break; }

    // FIFO (vsync) — guaranteed supported, blocks on present to throttle the render loop.
    // Mailbox would spin the CPU freely between presents, burning cycles for no benefit.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Always use the requested dimensions clamped to surface bounds.
    // Never blindly trust caps.currentExtent — on X11 it can lag behind ConfigureNotify
    // by several frames, causing the swapchain to be created at the wrong size while
    // Display()->Width/Height() already reflects the new window dimensions.
    m_swapExtent.width  = std::clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    m_swapExtent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = chosenFmt.format;
    sci.imageColorSpace  = chosenFmt.colorSpace;
    sci.imageExtent      = m_swapExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = presentMode;
    sci.clipped          = VK_TRUE;

    uint32_t queueFamilies[] = {m_graphicsFamily, m_presentFamily};
    if (m_graphicsFamily != m_presentFamily)
    {
        sci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices   = queueFamilies;
    }
    else
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain) != VK_SUCCESS)
    {
        g_Log->Error("CRendererVulkan: vkCreateSwapchainKHR failed");
        return false;
    }

    m_swapFormat = chosenFmt.format;

    // Retrieve images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapImages.data());

    // Create image views
    m_swapImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VkImageViewCreateInfo ivci{};
        ivci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image            = m_swapImages[i];
        ivci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format           = m_swapFormat;
        ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(m_device, &ivci, nullptr, &m_swapImageViews[i]);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Render pass
// ---------------------------------------------------------------------------
bool CRendererVulkan::createRenderPass()
{
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = m_swapFormat;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &colorAtt;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    return vkCreateRenderPass(m_device, &rpci, nullptr, &m_renderPass) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Framebuffers
// ---------------------------------------------------------------------------
bool CRendererVulkan::createFramebuffers()
{
    m_framebuffers.resize(m_swapImageViews.size());
    for (size_t i = 0; i < m_swapImageViews.size(); ++i)
    {
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = m_renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_swapImageViews[i];
        fbci.width           = m_swapExtent.width;
        fbci.height          = m_swapExtent.height;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbci, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Descriptor set layout (binding 0 = combined image sampler)
// ---------------------------------------------------------------------------
bool CRendererVulkan::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding            = 0;
    binding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount    = 1;
    binding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &binding;

    return vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                       &m_descriptorSetLayout) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Descriptor pool (enough for textures + white fallback)
// ---------------------------------------------------------------------------
bool CRendererVulkan::createDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1024;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = 1024;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    return vkCreateDescriptorPool(m_device, &poolInfo, nullptr,
                                  &m_descriptorPool) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Sampler
// ---------------------------------------------------------------------------
bool CRendererVulkan::createSampler()
{
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.maxLod       = 1.0f;

    return vkCreateSampler(m_device, &si, nullptr, &m_defaultSampler) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Shader loading from .spv file
// ---------------------------------------------------------------------------
VkShaderModule CRendererVulkan::loadShader(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        g_Log->Error("CRendererVulkan: cannot open shader: %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> code(size);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(m_device, &ci, nullptr, &mod);
    return mod;
}

// ---------------------------------------------------------------------------
// Graphics pipeline — shared helper that builds a VkPipeline from pre-loaded
// shader modules and a pre-created pipeline layout.
// ---------------------------------------------------------------------------
bool CRendererVulkan::buildGraphicsPipeline(VkShaderModule vert, VkShaderModule frag,
                                             VkPipelineLayout layout, VkPipeline& outPipeline)
{
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(QuadVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(QuadVertex, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset   = offsetof(QuadVertex, u);

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 1;
    vertInput.pVertexBindingDescriptions      = &binding;
    vertInput.vertexAttributeDescriptionCount = 2;
    vertInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable         = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vertInput;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState      = &vpState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState   = &ms;
    pci.pColorBlendState    = &blend;
    pci.pDynamicState       = &dynState;
    pci.layout              = layout;
    pci.renderPass          = m_renderPass;

    return vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci,
                                     nullptr, &outPipeline) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// No-blend pipeline (single texture at set=0)
// ---------------------------------------------------------------------------
bool CRendererVulkan::createPipeline()
{
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(VkPushConstants);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &m_descriptorSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        return false;

    std::string shaderDir = PlatformUtils::GetWorkingDir() + "shaders/";
    VkShaderModule vert = loadShader(shaderDir + "quad.vert.spv");
    VkShaderModule frag = loadShader(shaderDir + "quad.frag.spv");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
    {
        if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, vert, nullptr);
        if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, frag, nullptr);
        return false;
    }

    bool ok = buildGraphicsPipeline(vert, frag, m_pipelineLayout, m_pipeline);
    vkDestroyShaderModule(m_device, vert, nullptr);
    vkDestroyShaderModule(m_device, frag, nullptr);
    return ok;
}

// ---------------------------------------------------------------------------
// Linear frame-blend pipeline (2 frame textures at sets 1 and 2)
// ---------------------------------------------------------------------------
bool CRendererVulkan::createLinearBlendPipeline()
{
    // Three identical descriptor set layouts: set 0 (unused placeholder),
    // set 1 (frame1), set 2 (frame2).
    VkDescriptorSetLayout setLayouts[3] = {
        m_descriptorSetLayout, m_descriptorSetLayout, m_descriptorSetLayout
    };

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(VkPushConstantsLinear);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 3;
    plci.pSetLayouts            = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayoutLinear) != VK_SUCCESS)
        return false;

    std::string shaderDir = PlatformUtils::GetWorkingDir() + "shaders/";
    VkShaderModule vert = loadShader(shaderDir + "quad.vert.spv");
    VkShaderModule frag = loadShader(shaderDir + "blend_linear.frag.spv");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
    {
        if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, vert, nullptr);
        if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, frag, nullptr);
        return false;
    }

    bool ok = buildGraphicsPipeline(vert, frag, m_pipelineLayoutLinear, m_pipelineLinear);
    vkDestroyShaderModule(m_device, vert, nullptr);
    vkDestroyShaderModule(m_device, frag, nullptr);
    if (ok) g_Log->Info("CRendererVulkan: linear frame-blend pipeline ready");
    return ok;
}

// ---------------------------------------------------------------------------
// Cubic frame-blend pipeline (4 frame textures at sets 1–4)
// ---------------------------------------------------------------------------
bool CRendererVulkan::createCubicBlendPipeline()
{
    // Five identical descriptor set layouts: set 0 (unused), sets 1–4 (frames).
    VkDescriptorSetLayout setLayouts[5] = {
        m_descriptorSetLayout, m_descriptorSetLayout, m_descriptorSetLayout,
        m_descriptorSetLayout, m_descriptorSetLayout
    };

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(VkPushConstantsCubic);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 5;
    plci.pSetLayouts            = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayoutCubic) != VK_SUCCESS)
        return false;

    std::string shaderDir = PlatformUtils::GetWorkingDir() + "shaders/";
    VkShaderModule vert = loadShader(shaderDir + "quad.vert.spv");
    VkShaderModule frag = loadShader(shaderDir + "blend_cubic.frag.spv");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
    {
        if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, vert, nullptr);
        if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, frag, nullptr);
        return false;
    }

    bool ok = buildGraphicsPipeline(vert, frag, m_pipelineLayoutCubic, m_pipelineCubic);
    vkDestroyShaderModule(m_device, vert, nullptr);
    vkDestroyShaderModule(m_device, frag, nullptr);
    if (ok) g_Log->Info("CRendererVulkan: cubic frame-blend pipeline ready");
    return ok;
}

// ---------------------------------------------------------------------------
// Command pool + buffers
// ---------------------------------------------------------------------------
bool CRendererVulkan::createCommandPool()
{
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = m_graphicsFamily;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    return vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool) == VK_SUCCESS;
}

bool CRendererVulkan::createCommandBuffers()
{
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    return vkAllocateCommandBuffers(m_device, &ai, m_commandBuffers.data()) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Sync objects
// ---------------------------------------------------------------------------
bool CRendererVulkan::createSyncObjects()
{
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &si, nullptr, &m_renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence    (m_device, &fi, nullptr, &m_inFlightFence[i])  != VK_SUCCESS)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Vertex buffers — HOST_VISIBLE for per-frame quad data
// ---------------------------------------------------------------------------
bool CRendererVulkan::createVertexBuffers()
{
    VkDeviceSize size = sizeof(QuadVertex) * 6 * MAX_QUADS_PER_FRAME;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = size;
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bci, nullptr, &m_vertexBuffer[i]) != VK_SUCCESS)
            return false;

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device, m_vertexBuffer[i], &memReq);

        VkMemoryAllocateInfo mai{};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = memReq.size;
        mai.memoryTypeIndex = FindMemType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &mai, nullptr, &m_vertexMemory[i]) != VK_SUCCESS)
            return false;

        vkBindBufferMemory(m_device, m_vertexBuffer[i], m_vertexMemory[i], 0);
        vkMapMemory(m_device, m_vertexMemory[i], 0, size, 0, &m_mappedVertex[i]);
    }
    return true;
}

// ---------------------------------------------------------------------------
// White 1×1 texture for solid-colour quads
// ---------------------------------------------------------------------------
bool CRendererVulkan::createWhiteTexture()
{
    // Create image
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_SRGB;
    ici.extent        = {1, 1, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(m_device, &ici, nullptr, &m_whiteImage);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, m_whiteImage, &memReq);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = memReq.size;
    mai.memoryTypeIndex = FindMemType(memReq.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_device, &mai, nullptr, &m_whiteImageMem);
    vkBindImageMemory(m_device, m_whiteImage, m_whiteImageMem, 0);

    // Staging
    VkBuffer       stagBuf; VkDeviceMemory stagMem;
    const uint8_t  white[4] = {255, 255, 255, 255};
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = 4; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(m_device, &bci, nullptr, &stagBuf);
    VkMemoryRequirements bMemReq;
    vkGetBufferMemoryRequirements(m_device, stagBuf, &bMemReq);
    VkMemoryAllocateInfo bmai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bmai.allocationSize = bMemReq.size;
    bmai.memoryTypeIndex = FindMemType(bMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_device, &bmai, nullptr, &stagMem);
    vkBindBufferMemory(m_device, stagBuf, stagMem, 0);
    void* mapped; vkMapMemory(m_device, stagMem, 0, 4, 0, &mapped);
    memcpy(mapped, white, 4); vkUnmapMemory(m_device, stagMem);

    // Transition + copy
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image     = m_whiteImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, stagBuf, m_whiteImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmd);

    vkDestroyBuffer(m_device, stagBuf, nullptr);
    vkFreeMemory(m_device, stagMem, nullptr);

    // Image view
    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image            = m_whiteImage;
    ivci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format           = VK_FORMAT_R8G8B8A8_SRGB;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(m_device, &ivci, nullptr, &m_whiteView);

    // Descriptor set
    VkDescriptorSetAllocateInfo dsAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAlloc.descriptorPool     = m_descriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts        = &m_descriptorSetLayout;
    vkAllocateDescriptorSets(m_device, &dsAlloc, &m_whiteDescSet);

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView   = m_whiteView;
    imgInfo.sampler     = m_defaultSampler;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = m_whiteDescSet;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// initImGui — set up ImGui with the Vulkan backend.
// Called at the tail of Initialize(), after all Vulkan objects exist.
// ---------------------------------------------------------------------------
bool CRendererVulkan::initImGui()
{
    IMGUI_CHECKVERSION();
    m_imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini — we don't persist UI state

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion        = VK_API_VERSION_1_0;
    initInfo.Instance          = m_vulkanInstance;
    initInfo.PhysicalDevice    = m_physicalDevice;
    initInfo.Device            = m_device;
    initInfo.QueueFamily       = m_graphicsFamily;
    initInfo.Queue             = m_graphicsQueue;
    initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    initInfo.MinImageCount     = 2;
    initInfo.ImageCount        = static_cast<uint32_t>(m_swapImages.size());
    // Since imgui 1.92 (Sept 2025): RenderPass and MSAASamples live in PipelineInfoMain.
    initInfo.PipelineInfoMain.RenderPass = m_renderPass;
    // MSAASamples defaults to VK_SAMPLE_COUNT_1_BIT (zero-init = {})
    ImGui_ImplVulkan_Init(&initInfo);

    // Font atlas upload is handled automatically by ImGui_ImplVulkan_NewFrame().

    m_imguiInitialized = true;
    g_Log->Info("CRendererVulkan: ImGui initialized");
    return true;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
bool CRendererVulkan::Initialize(spCDisplayOutput _spDisplay)
{
    if (!CRenderer::Initialize(_spDisplay))
        return false;

    auto* disp = dynamic_cast<CDisplayVulkan*>(_spDisplay.get());
    if (!disp)
    {
        g_Log->Error("CRendererVulkan: display is not CDisplayVulkan");
        return false;
    }

    VkInstance   instance = disp->GetInstance();
    VkSurfaceKHR surface  = disp->GetSurface();
    uint32_t     w        = disp->Width();
    uint32_t     h        = disp->Height();
    m_surface         = surface;
    m_vulkanInstance  = instance;

    if (!pickPhysicalDevice(instance, surface))     return false;
    if (!createLogicalDevice())                      return false;
    if (!createSwapchain(surface, w, h))             return false;
    if (!createRenderPass())                         return false;
    if (!createFramebuffers())                       return false;
    if (!createDescriptorSetLayout())                return false;
    if (!createDescriptorPool())                     return false;
    if (!createSampler())                            return false;
    if (!createCommandPool())                        return false;
    if (!createCommandBuffers())                     return false;
    if (!createWhiteTexture())                       return false;
    if (!createPipeline())                           return false;
    if (!createSyncObjects())                        return false;

    // Blend pipelines are optional — failure is logged but does not abort startup.
    if (!createLinearBlendPipeline())
        g_Log->Warning("CRendererVulkan: linear blend pipeline unavailable (missing blend_linear.frag.spv?)");
    if (!createCubicBlendPipeline())
        g_Log->Warning("CRendererVulkan: cubic blend pipeline unavailable (missing blend_cubic.frag.spv?)");
    if (!createVertexBuffers())                      return false;

    m_currentDescSet = m_whiteDescSet;

    if (!initImGui())
        return false;

    // Timestamp query pool for GPU frame-time measurement.
    // Two queries per in-flight frame slot: one at start, one at end of frame.
    {
        VkPhysicalDeviceProperties devProps;
        vkGetPhysicalDeviceProperties(m_physicalDevice, &devProps);
        m_timestampPeriodNs = devProps.limits.timestampPeriod;

        if (m_timestampPeriodNs > 0.0f)
        {
            VkQueryPoolCreateInfo qpci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qpci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
            qpci.queryCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2;
            if (vkCreateQueryPool(m_device, &qpci, nullptr, &m_timestampPool) != VK_SUCCESS)
            {
                g_Log->Warning("CRendererVulkan: timestamp query pool creation failed — GPU timing unavailable");
                m_timestampPool = VK_NULL_HANDLE;
            }
        }
        else
        {
            g_Log->Warning("CRendererVulkan: GPU timestamps not supported by this device");
        }
    }

    g_Log->Info("CRendererVulkan: initialized (%ux%u)", w, h);
    return true;
}

// ---------------------------------------------------------------------------
// Defaults / Reset / Apply
// ---------------------------------------------------------------------------
void CRendererVulkan::Defaults() {}
void CRendererVulkan::Reset(const uint32_t _flags)
{
    CRenderer::Reset(_flags);
    // When textures are reset, revert to the white descriptor set so that
    // DrawSoftQuad / DrawQuad render a solid colour instead of sampling the
    // previously-bound video/startup texture (which appeared as a ghost
    // ellipse over the F1/F2 HUD background rectangles).
    if (_flags & eTexture)
    {
        m_currentDescSet = m_whiteDescSet;
        for (int i = 0; i < MAX_BLEND_TEXTURES; ++i)
            m_boundDescSets[i] = VK_NULL_HANDLE;
    }
}
void CRendererVulkan::Apply()
{
    CRenderer::Apply();
    // Force-rebind selected textures to keep m_currentDescSet pointing at the
    // correct video/content texture before the next DrawQuad call.
    for (uint32_t i = 0; i < MAX_TEXUNIT; ++i)
    {
        if (m_aspSelectedTextures[i])
            m_aspSelectedTextures[i]->Bind(i);
    }
}

// ---------------------------------------------------------------------------
// GPU utilization — read from the DRM sysfs interface exposed by amdgpu and
// some Intel drivers.  Returns 0 if the path doesn't exist (NVIDIA, VMs, etc.).
// ---------------------------------------------------------------------------
float CRendererVulkan::GetGPUUtilization()
{
    static const char* const kPaths[] = {
        "/sys/class/drm/card0/device/gpu_busy_percent",
        "/sys/class/drm/card1/device/gpu_busy_percent",
        nullptr,
    };
    for (int i = 0; kPaths[i]; ++i)
    {
        FILE* f = fopen(kPaths[i], "r");
        if (!f) continue;
        int pct = 0;
        bool ok = (fscanf(f, "%d", &pct) == 1);
        fclose(f);
        if (ok) return static_cast<float>(pct);
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Swapchain recreation (called when VK_ERROR_OUT_OF_DATE_KHR)
// ---------------------------------------------------------------------------
void CRendererVulkan::recreateSwapchain()
{
    vkDeviceWaitIdle(m_device);

    // Destroy framebuffers
    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();

    // Destroy image views
    for (auto iv : m_swapImageViews)
        vkDestroyImageView(m_device, iv, nullptr);
    m_swapImageViews.clear();

    // Destroy old swapchain
    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    // Use current display size
    uint32_t w = m_spDisplay->Width();
    uint32_t h = m_spDisplay->Height();

    g_Log->Info("CRendererVulkan: recreating swapchain (%ux%u)", w, h);
    createSwapchain(m_surface, w, h);
    createFramebuffers();
}

// BeginFrame
// ---------------------------------------------------------------------------
bool CRendererVulkan::BeginFrame()
{
    if (m_inFrame) return true;

    // Proactively recreate the swapchain if the window size changed.
    // Display()->Width/Height() is kept up-to-date by ConfigureNotify (X11) and
    // xdg_toplevel configure (Wayland), so comparing against m_swapExtent detects
    // fullscreen transitions and manual resizes exactly one frame after the WM sends
    // the resize event — no spurious recreations since m_swapExtent is set from those
    // same dimensions (never from stale caps.currentExtent).
    if (m_spDisplay->Width() != m_swapExtent.width || m_spDisplay->Height() != m_swapExtent.height)
    {
        recreateSwapchain();
        return false;
    }

    // Wait for previous frame in this slot
    vkWaitForFences(m_device, 1, &m_inFlightFence[m_currentFrame],
                    VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_imageAvailable[m_currentFrame], VK_NULL_HANDLE,
        &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return false;  // Skip this frame
    }
    // VK_SUBOPTIMAL_KHR: the image was acquired successfully and can be presented —
    // do not recreate here. On some drivers (e.g. Intel Arc on X11) this is returned
    // every single frame, causing an infinite recreation loop if acted upon.

    vkResetFences(m_device, 1, &m_inFlightFence[m_currentFrame]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);

    // GPU frame-time: read back last frame's results (fence guarantees they're ready),
    // then reset + write the start timestamp for this slot.
    if (m_timestampPool != VK_NULL_HANDLE)
    {
        if (m_timestampsValid)
        {
            uint64_t ts[2] = {};
            if (vkGetQueryPoolResults(m_device, m_timestampPool,
                    m_currentFrame * 2, 2,
                    sizeof(ts), ts, sizeof(uint64_t),
                    VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                float ns = static_cast<float>(ts[1] - ts[0]) * m_timestampPeriodNs;
                m_gpuFrameTimeMs = ns * 1e-6f;
            }
        }
        vkCmdResetQueryPool(cmd, m_timestampPool, m_currentFrame * 2, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_timestampPool, m_currentFrame * 2);
    }

    // Start an ImGui frame before the render pass so DrawText() can add to
    // the background draw list.  ImGui_ImplVulkan_NewFrame() also handles
    // automatic font-atlas uploads (new in imgui 1.92+).
    if (m_imguiInitialized)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_spDisplay->Width()),
                                static_cast<float>(m_spDisplay->Height()));
        io.DeltaTime   = 1.0f / 60.0f;  // approximation; sufficient for text rendering
        ImGui::NewFrame();
    }

    // Begin render pass
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = m_renderPass;
    rpbi.framebuffer       = m_framebuffers[m_currentImageIndex];
    rpbi.renderArea.extent = m_swapExtent;
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clearColor;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    // Bind default (no-blend) pipeline + set viewport/scissor
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    m_activePipeline = m_pipeline;

    VkViewport vp{};
    vp.width    = static_cast<float>(m_swapExtent.width);
    vp.height   = static_cast<float>(m_swapExtent.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{{0, 0}, m_swapExtent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    m_vertexCount = 0;
    m_inFrame     = true;
    return true;
}

// ---------------------------------------------------------------------------
// EndFrame
// ---------------------------------------------------------------------------
bool CRendererVulkan::EndFrame(bool /*drawn*/)
{
    if (!m_inFrame) return true;

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // Render ImGui draw data (text overlays etc.) inside the active render pass.
    if (m_imguiInitialized)
    {
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->TotalVtxCount > 0)
            ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    }

    vkCmdEndRenderPass(cmd);

    // GPU frame-time: write end timestamp outside the render pass.
    if (m_timestampPool != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            m_timestampPool, m_currentFrame * 2 + 1);
        m_timestampsValid = true;
    }

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &m_imageAvailable[m_currentFrame];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_renderFinished[m_currentFrame];
    vkQueueSubmit(m_graphicsQueue, 1, &si, m_inFlightFence[m_currentFrame]);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_renderFinished[m_currentFrame];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &m_currentImageIndex;
    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &pi);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
        recreateSwapchain();
    // VK_SUBOPTIMAL_KHR at present time is benign — the frame was displayed. Recreating
    // here would cause an infinite loop on drivers that permanently return SUBOPTIMAL
    // (e.g. Intel Arc on X11).

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_inFrame      = false;
    return true;
}

// ---------------------------------------------------------------------------
// DrawQuad helpers
// ---------------------------------------------------------------------------
static void writeQuad(void* dest, uint32_t& count,
                      float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1)
{
    QuadVertex* v = reinterpret_cast<QuadVertex*>(dest) + count;
    // Triangle 1
    v[0] = {x0, y0, u0, v0};
    v[1] = {x1, y0, u1, v0};
    v[2] = {x1, y1, u1, v1};
    // Triangle 2
    v[3] = {x0, y0, u0, v0};
    v[4] = {x1, y1, u1, v1};
    v[5] = {x0, y1, u0, v1};
    count += 6;
}

void CRendererVulkan::DrawQuad(const Base::Math::CRect& _rect,
                                const Base::Math::CVector4& _color)
{
    DrawQuad(_rect, _color,
             Base::Math::CRect(0.0f, 0.0f, 1.0f, 1.0f));
}

void CRendererVulkan::DrawQuad(const Base::Math::CRect& _rect,
                                const Base::Math::CVector4& _color,
                                const Base::Math::CRect& _uvRect)
{
    if (!m_inFrame) return;
    if (m_vertexCount + 6 > static_cast<uint32_t>(MAX_QUADS_PER_FRAME * 6)) return;

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    writeQuad(m_mappedVertex[m_currentFrame], m_vertexCount,
              _rect.m_X0, _rect.m_Y0, _rect.m_X1, _rect.m_Y1,
              _uvRect.m_X0, _uvRect.m_Y0, _uvRect.m_X1, _uvRect.m_Y1);

    // Determine blend mode from the currently active shader.
    CShaderVulkan* vkShader = dynamic_cast<CShaderVulkan*>(m_spActiveShader.get());
    CShaderVulkan::BlendMode blendMode =
        vkShader ? vkShader->GetBlendMode() : CShaderVulkan::kNone;

    // Fall back to no-blend if the requested blend pipeline is unavailable.
    if (blendMode == CShaderVulkan::kLinear && m_pipelineLinear == VK_NULL_HANDLE)
    {
        g_Log->Warning("CRendererVulkan: linear blend requested but pipeline unavailable — falling back");
        blendMode = CShaderVulkan::kNone;
    }
    if (blendMode == CShaderVulkan::kCubic  && m_pipelineCubic  == VK_NULL_HANDLE)
    {
        g_Log->Warning("CRendererVulkan: cubic blend requested but pipeline unavailable — falling back");
        blendMode = CShaderVulkan::kNone;
    }

    // Log the first draw call for each blend mode so it is easy to confirm
    // blending is active without having to eyeball the output.
    static bool s_loggedLinear = false;
    static bool s_loggedCubic  = false;
    if (blendMode == CShaderVulkan::kLinear && !s_loggedLinear)
    {
        g_Log->Info("CRendererVulkan: first linear frame-blend draw (delta=%.3f)",
                    vkShader ? vkShader->GetFloat("delta") : 0.f);
        s_loggedLinear = true;
    }
    else if (blendMode == CShaderVulkan::kCubic && !s_loggedCubic)
    {
        float w[4] = {};
        if (vkShader) vkShader->GetFloat4("weights", w);
        g_Log->Info("CRendererVulkan: first cubic frame-blend draw (weights=%.3f,%.3f,%.3f,%.3f)",
                    w[0], w[1], w[2], w[3]);
        s_loggedCubic = true;
    }

    VkPipeline       targetPipeline = m_pipeline;
    VkPipelineLayout targetLayout   = m_pipelineLayout;
    if (blendMode == CShaderVulkan::kLinear)
    {
        targetPipeline = m_pipelineLinear;
        targetLayout   = m_pipelineLayoutLinear;
    }
    else if (blendMode == CShaderVulkan::kCubic)
    {
        targetPipeline = m_pipelineCubic;
        targetLayout   = m_pipelineLayoutCubic;
    }

    if (targetPipeline != m_activePipeline)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
        m_activePipeline = targetPipeline;
    }

    const float sw = static_cast<float>(m_swapExtent.width);
    const float sh = static_cast<float>(m_swapExtent.height);

    if (blendMode == CShaderVulkan::kLinear)
    {
        VkDescriptorSet safe1 = m_boundDescSets[1] ? m_boundDescSets[1] : m_whiteDescSet;
        VkDescriptorSet safe2 = m_boundDescSets[2] ? m_boundDescSets[2] : m_whiteDescSet;
        VkDescriptorSet sets[3] = { m_whiteDescSet, safe1, safe2 };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 targetLayout, 0, 3, sets, 0, nullptr);

        VkPushConstantsLinear pc{};
        pc.screenWidth  = sw;   pc.screenHeight = sh;
        pc.r = _color.m_X;      pc.g = _color.m_Y;
        pc.b = _color.m_Z;      pc.a = _color.m_W;
        pc.delta = vkShader ? vkShader->GetFloat("delta") : 0.f;
        vkCmdPushConstants(cmd, targetLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
    }
    else if (blendMode == CShaderVulkan::kCubic)
    {
        VkDescriptorSet safe1 = m_boundDescSets[1] ? m_boundDescSets[1] : m_whiteDescSet;
        VkDescriptorSet safe2 = m_boundDescSets[2] ? m_boundDescSets[2] : m_whiteDescSet;
        VkDescriptorSet safe3 = m_boundDescSets[3] ? m_boundDescSets[3] : m_whiteDescSet;
        VkDescriptorSet safe4 = m_boundDescSets[4] ? m_boundDescSets[4] : m_whiteDescSet;
        VkDescriptorSet sets[5] = { m_whiteDescSet, safe1, safe2, safe3, safe4 };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 targetLayout, 0, 5, sets, 0, nullptr);

        float weights[4] = {0.f, 0.f, 0.f, 0.f};
        if (vkShader) vkShader->GetFloat4("weights", weights);

        VkPushConstantsCubic pc{};
        pc.screenWidth  = sw;   pc.screenHeight = sh;
        pc.r = _color.m_X;      pc.g = _color.m_Y;
        pc.b = _color.m_Z;      pc.a = _color.m_W;
        pc.w0 = weights[0];     pc.w1 = weights[1];
        pc.w2 = weights[2];     pc.w3 = weights[3];
        vkCmdPushConstants(cmd, targetLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
    }
    else
    {
        VkDescriptorSet ds = m_currentDescSet ? m_currentDescSet : m_whiteDescSet;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 targetLayout, 0, 1, &ds, 0, nullptr);

        VkPushConstants pc{};
        pc.screenWidth  = sw;   pc.screenHeight = sh;
        pc.r = _color.m_X;      pc.g = _color.m_Y;
        pc.b = _color.m_Z;      pc.a = _color.m_W;
        vkCmdPushConstants(cmd, targetLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer[m_currentFrame], &offset);
    uint32_t firstVertex = m_vertexCount - 6;
    vkCmdDraw(cmd, 6, 1, firstVertex, 0);
}

void CRendererVulkan::DrawSoftQuad(const Base::Math::CRect& _rect,
                                    const Base::Math::CVector4& _color,
                                    const float /*_width*/)
{
    DrawQuad(_rect, _color);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
spCTextureFlat CRendererVulkan::NewTextureFlat(const uint32_t flags)
{
    return std::make_shared<CTextureFlatVulkan>(this, flags);
}

spCTextureFlat CRendererVulkan::NewTextureFlat(spCImage _spImage,
                                               const uint32_t flags)
{
    auto tex = std::make_shared<CTextureFlatVulkan>(this, flags);
    if (_spImage)
        tex->Upload(_spImage);
    return tex;
}

// ---------------------------------------------------------------------------
// Fonts / Text
// ---------------------------------------------------------------------------
spCBaseFont CRendererVulkan::GetFont(CFontDescription& _desc)
{
    std::string key = _desc.TypeFace() + "_" + std::to_string(_desc.Height())
                    + "_" + std::to_string(static_cast<int>(_desc.Style()));
    auto it = m_fontPool.find(key);
    if (it != m_fontPool.end()) return it->second;

    auto font = std::make_shared<CFontImGui>();
    font->FontDescription(_desc);
    font->CreateWithRenderer(this);
    m_fontPool[key] = font;

    // Pre-create the bold variant for Normal fonts so DrawText can switch to
    // it via inline bold markers (\x01/\x02) without triggering a new font
    // registration inside an active render pass.
    if (_desc.Style() == CFontDescription::Normal)
    {
        CFontDescription boldDesc = _desc;
        boldDesc.Style(CFontDescription::Bold);
        std::string boldKey = boldDesc.TypeFace() + "_" + std::to_string(boldDesc.Height())
                            + "_" + std::to_string(static_cast<int>(boldDesc.Style()));
        if (m_fontPool.find(boldKey) == m_fontPool.end())
        {
            auto boldFont = std::make_shared<CFontImGui>();
            boldFont->FontDescription(boldDesc);
            boldFont->CreateWithRenderer(this);
            m_fontPool[boldKey] = boldFont;
        }
    }

    return font;
}

spCBaseText CRendererVulkan::NewText(spCBaseFont _font, const std::string& _text)
{
    return std::make_shared<CTextImGui>(_font, _text, this);
}

void CRendererVulkan::DrawText(spCBaseText _text,
                                const Base::Math::CVector4& _color)
{
    if (!_text || !m_inFrame || !m_imguiInitialized) return;

    auto spTV = std::dynamic_pointer_cast<CTextImGui>(_text);
    if (!spTV || !spTV->IsEnabled()) return;

    auto spFI = std::dynamic_pointer_cast<CFontImGui>(spTV->GetFont());
    if (!spFI) return;

    ImFont* normalFont = spFI->GetImFont();

    // Scale the font size so the overlay occupies the same visual proportion
    // of the screen regardless of resolution.  1080p is the reference at which
    // the baked font size (24 px) looks correct; above that we scale up, below
    // we scale down.  ImGui accepts any size in AddText regardless of the atlas
    // bake size — it rasterises at bake size and scales to display size.
    static constexpr float kHudReferenceHeight = 1080.f;
    const float scale    = static_cast<float>(m_spDisplay->Height()) / kHudReferenceHeight;
    const float fontSize = spFI->FontSize() * scale;

    // Convert normalised rect [0,1] → pixel coords (ImGui origin is top-left).
    Base::Math::CRect r = _text->GetRect();
    ImVec2 cursor{r.m_X0 * static_cast<float>(m_swapExtent.width),
                  r.m_Y0 * static_cast<float>(m_swapExtent.height)};

    ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(_color.m_X, _color.m_Y, _color.m_Z, _color.m_W));

    ImDrawList*        dl   = ImGui::GetBackgroundDrawList();
    const std::string& text = spTV->Text();

    if (text.find_first_of("\t\n") == std::string::npos)
    {
        // Fast path — plain single-line text with no special characters.
        dl->AddText(normalFont, fontSize, cursor, col, text.c_str());
        return;
    }

    // Render segment-by-segment, handling:
    //   \n — newline: reset cursor.x, advance cursor.y
    //   \t — tab: snap cursor.x to next tab stop (4 × space width)
    const float spaceW      = normalFont
        ? normalFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, " ").x
        : fontSize * 0.5f;
    const float tabInterval = spaceW * 4.0f;
    const float startX      = cursor.x;
    const char* p           = text.c_str();
    const char* segBegin    = p;

    while (*p)
    {
        if (*p == '\n' || *p == '\t')
        {
            if (p > segBegin)
            {
                dl->AddText(normalFont, fontSize, cursor, col, segBegin, p);
                cursor.x += normalFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, segBegin, p).x;
            }
            if (*p == '\n')
            {
                cursor.x = startX;
                cursor.y += fontSize;
            }
            else
            {
                float offset = cursor.x - startX;
                cursor.x = startX + (std::floor(offset / tabInterval) + 1.0f) * tabInterval;
            }
            segBegin = p + 1;
        }
        ++p;
    }
    if (p > segBegin)
        dl->AddText(normalFont, fontSize, cursor, col, segBegin, p);
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
spCShader CRendererVulkan::NewShader(
    const char* /*_pVert*/, const char* _pFrag,
    std::vector<std::pair<std::string, eUniformType>> uniforms)
{
    auto shader = std::make_shared<CShaderVulkan>();

    // Populate the uniforms map so CShader::Set() can store values that
    // DrawQuad reads back when building push constants.
    for (auto& [name, type] : uniforms)
        shader->AddUniform(name, type);

    // Choose blend mode from the symbolic fragment shader name.
    if (_pFrag)
    {
        std::string frag(_pFrag);
        if (frag == "drawDecodedFrameLinearFrameBlendFragment")
        {
            shader->SetBlendMode(CShaderVulkan::kLinear);
            g_Log->Info("CRendererVulkan: created linear blend shader");
        }
        else if (frag == "drawDecodedFrameCubicFrameBlendFragment")
        {
            shader->SetBlendMode(CShaderVulkan::kCubic);
            g_Log->Info("CRendererVulkan: created cubic blend shader");
        }
    }

    return shader;
}

} // namespace DisplayOutput

#endif // !WIN32
