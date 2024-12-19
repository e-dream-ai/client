#include "ShaderDX11.h"
#include "Log.h"
#include <d3dcompiler.h>

namespace DisplayOutput {

CShaderUniformDX11::CShaderUniformDX11(const std::string& name, eUniformType type,
                                       ComPtr<ID3D11Device> device,
                                       ComPtr<ID3D11DeviceContext> context,
                                       uint32_t slot)
    : CShaderUniform(name, type)
    , m_context(context)
    , m_slot(slot)
    , m_size(UniformTypeSizes[type])
{
    m_data.resize(m_size);

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (UINT)m_size;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) {
        g_Log->Error("Failed to create constant buffer: %08X", hr);
    }
}

bool CShaderUniformDX11::SetData(void* _pData, const uint32_t _size) {
    if (_size != m_size) return false;
    
    memcpy(m_data.data(), _pData, _size);
    m_bDirty = true;
    return true;
}

void CShaderUniformDX11::Apply() {
    if (!m_bDirty) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 
                               0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, m_data.data(), m_size);
        m_context->Unmap(m_constantBuffer.Get(), 0);
        m_context->VSSetConstantBuffers(m_slot, 1, m_constantBuffer.GetAddressOf());
        m_context->PSSetConstantBuffers(m_slot, 1, m_constantBuffer.GetAddressOf());
        m_bDirty = false;
    }
}

CShaderDX11::CShaderDX11(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context)
    : m_device(device)
    , m_context(context) {
}

CShaderDX11::~CShaderDX11() {
}

bool CShaderDX11::CompileShader(const char* source, const char* entryPoint, 
                               const char* target, ID3DBlob** blob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                           entryPoint, target, flags, 0, blob, &errorBlob);
    
    if (FAILED(hr)) {
        if (errorBlob) {
            g_Log->Error("Shader compilation failed: %s", 
                        (char*)errorBlob->GetBufferPointer());
        }
        return false;
    }
    return true;
}

bool CShaderDX11::Build(const char* _pVertexShader, const char* _pFragmentShader) {
    ComPtr<ID3DBlob> vsBlob;
    if (!CompileShader(_pVertexShader, "main", "vs_5_0", &vsBlob)) {
        return false;
    }

    HRESULT hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                             vsBlob->GetBufferSize(), nullptr,
                                             &m_vertexShader);
    if (FAILED(hr)) {
        g_Log->Error("Failed to create vertex shader: %08X", hr);
        return false;
    }

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, 
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, 
          D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout),
                                    vsBlob->GetBufferPointer(),
                                    vsBlob->GetBufferSize(),
                                    &m_inputLayout);
    if (FAILED(hr)) {
        g_Log->Error("Failed to create input layout: %08X", hr);
        return false;
    }

    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader(_pFragmentShader, "main", "ps_5_0", &psBlob)) {
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                    psBlob->GetBufferSize(), nullptr,
                                    &m_pixelShader);
    if (FAILED(hr)) {
        g_Log->Error("Failed to create pixel shader: %08X", hr);
        return false;
    }

    return true;
}

bool CShaderDX11::CreateUniform(const std::string& name, eUniformType type, uint32_t slot) {
    spCShaderUniform uniform = std::make_shared<CShaderUniformDX11>(name, type, m_device,
                                                                    m_context, slot);
    m_Uniforms[name] = uniform;
    return true;
}

bool CShaderDX11::Bind() {
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    return true;
}

bool CShaderDX11::Unbind() {
    m_context->VSSetShader(nullptr, nullptr, 0);
    m_context->PSSetShader(nullptr, nullptr, 0);
    return true;
}

bool CShaderDX11::Apply() {
    for (auto& pair : m_Uniforms) {
        pair.second->Apply();
    }
    return true;
}

} // namespace DisplayOutput