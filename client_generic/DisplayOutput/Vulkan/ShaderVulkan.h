#ifndef _SHADERVULKAN_H_
#define _SHADERVULKAN_H_

#include "Shader.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <utility>

namespace DisplayOutput
{

/*
    CShaderUniformVulkan — stub; uniforms for Vulkan are push constants
    managed by RendererVulkan directly.
*/
class CShaderUniformVulkan : public CShaderUniform
{
    std::vector<uint8_t> m_data;

  public:
    CShaderUniformVulkan(const std::string& name, eUniformType type)
        : CShaderUniform(name, type)
    {
        m_data.resize(UniformTypeSizes[type], 0);
    }

    bool SetData(void* _pData, const uint32_t _size) override
    {
        size_t sz = std::min(static_cast<size_t>(_size), m_data.size());
        memcpy(m_data.data(), _pData, sz);
        m_bDirty = true;
        return true;
    }

    void Apply() override { m_bDirty = false; }

    const void* Data()   const { return m_data.data(); }
    size_t       Size()   const { return m_data.size(); }
};

MakeSmartPointers(CShaderUniformVulkan);

/*
    CShaderVulkan — wraps a Vulkan pipeline handle.
    For this renderer pipeline creation is centralised in CRendererVulkan;
    this class is a thin handle so the base-class API is satisfied.
*/
class CShaderVulkan : public CShader
{
    VkDevice   m_device   = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

  public:
    CShaderVulkan() {}
    virtual ~CShaderVulkan() {}

    bool Bind()   override { return true; }
    bool Unbind() override { return true; }
    bool Apply()  override { return true; }

    bool Build(const char* /*_pVert*/, const char* /*_pFrag*/) override
    {
        // Pipeline building is handled by CRendererVulkan::NewShader()
        return true;
    }

    VkPipeline Pipeline() const { return m_pipeline; }
    void SetPipeline(VkPipeline p) { m_pipeline = p; }
};

MakeSmartPointers(CShaderVulkan);

} // namespace DisplayOutput

#endif
