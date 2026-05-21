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
    CShaderUniformVulkan — stores a single uniform value in CPU memory.
    The Vulkan renderer reads these values back when building push constants.
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

    const void* Data() const { return m_data.data(); }
    size_t       Size() const { return m_data.size(); }
};

MakeSmartPointers(CShaderUniformVulkan);

/*
    CShaderVulkan — thin handle satisfying the CShader interface.
    Carries the blend mode and uniform values that CRendererVulkan reads in DrawQuad.
*/
class CShaderVulkan : public CShader
{
  public:
    enum BlendMode { kNone, kLinear, kCubic };

  private:
    BlendMode m_blendMode = kNone;

  public:
    CShaderVulkan() {}
    virtual ~CShaderVulkan() {}

    bool Bind()   override { return true; }
    bool Unbind() override { return true; }
    bool Apply()  override { return true; }

    bool Build(const char* /*_pVert*/, const char* /*_pFrag*/) override
    {
        return true;
    }

    void SetBlendMode(BlendMode m) { m_blendMode = m; }
    BlendMode GetBlendMode() const { return m_blendMode; }

    // Register a named uniform so CShader::Set() can store its value.
    void AddUniform(const std::string& name, eUniformType type)
    {
        m_Uniforms[name] = std::make_shared<CShaderUniformVulkan>(name, type);
    }

    // Read a float uniform value (e.g. "delta").
    float GetFloat(const std::string& name) const
    {
        auto it = m_Uniforms.find(name);
        if (it != m_Uniforms.end())
        {
            auto u = std::dynamic_pointer_cast<CShaderUniformVulkan>(it->second);
            if (u && u->Size() >= sizeof(float))
                return *static_cast<const float*>(u->Data());
        }
        return 0.f;
    }

    // Read a float4 uniform value (e.g. "weights") into a 4-element array.
    void GetFloat4(const std::string& name, float out[4]) const
    {
        auto it = m_Uniforms.find(name);
        if (it != m_Uniforms.end())
        {
            auto u = std::dynamic_pointer_cast<CShaderUniformVulkan>(it->second);
            if (u && u->Size() >= 4 * sizeof(float))
                memcpy(out, u->Data(), 4 * sizeof(float));
        }
    }
};

MakeSmartPointers(CShaderVulkan);

} // namespace DisplayOutput

#endif
