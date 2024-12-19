#ifndef _SHADERDX11_H_
#define _SHADERDX11_H_

#include <d3d11.h>
#include <wrl/client.h>
#include "Shader.h"
#include <vector>

using Microsoft::WRL::ComPtr;

namespace DisplayOutput {

class CShaderUniformDX11 : public CShaderUniform {
protected:
    ComPtr<ID3D11Buffer> m_constantBuffer;
    std::vector<uint8_t> m_data;
    ComPtr<ID3D11DeviceContext> m_context;
    uint32_t m_slot;
    size_t m_size;

public:
    CShaderUniformDX11(const std::string& name, eUniformType type, 
                       ComPtr<ID3D11Device> device,
                       ComPtr<ID3D11DeviceContext> context,
                       uint32_t slot);
    virtual ~CShaderUniformDX11() {}

    virtual bool SetData(void* _pData, const uint32_t _size) override;
    virtual void Apply() override;
};

MakeSmartPointers(CShaderUniformDX11);

class CShaderDX11 : public CShader {
protected:
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

public:
    CShaderDX11(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context);
    virtual ~CShaderDX11();

    virtual bool Build(const char* _pVertexShader, const char* _pFragmentShader) override;
    virtual bool Bind() override;
    virtual bool Unbind() override;
    virtual bool Apply() override;

    bool CreateUniform(const std::string& name, eUniformType type, uint32_t slot);
    bool CompileShader(const char* source, const char* entryPoint, const char* target, 
                      ID3DBlob** blob);

    ID3D11VertexShader* GetVertexShader() const { return m_vertexShader.Get(); }
    ID3D11PixelShader* GetPixelShader() const { return m_pixelShader.Get(); }
    ID3D11InputLayout* GetInputLayout() const { return m_inputLayout.Get(); }
};

MakeSmartPointers(CShaderDX11);

} // namespace DisplayOutput

#endif