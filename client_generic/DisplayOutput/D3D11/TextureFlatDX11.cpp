#include "TextureFlatDX11.h"
#include "Log.h"

namespace DisplayOutput {

CTextureFlatDX11::CTextureFlatDX11(ComPtr<ID3D11Device> device, 
                                   ComPtr<ID3D11DeviceContext> context,
                                   const uint32_t flags)
    : CTextureFlat(flags)
    , m_device(device)
    , m_context(context) {
    CreateSampler();
}

CTextureFlatDX11::~CTextureFlatDX11() { 
    g_Log->Info("Destroying texture"); 
}

bool CTextureFlatDX11::CreateSampler() {
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&samplerDesc, &m_sampler);
    if (FAILED(hr)) {
        g_Log->Error("Failed to create sampler state: %08X", hr);
        return false;
    }
    return true;
}

DXGI_FORMAT CTextureFlatDX11::GetDXGIFormat(CImageFormat format) {
    static const DXGI_FORMAT dxgiFormats[] = {
        DXGI_FORMAT_UNKNOWN,           // eImage_None
        DXGI_FORMAT_R8_UNORM,         // eImage_I8
        DXGI_FORMAT_R8G8_UNORM,       // eImage_IA8
        DXGI_FORMAT_R8G8B8A8_UNORM,   // eImage_RGB8
        DXGI_FORMAT_R8G8B8A8_UNORM,   // eImage_RGBA8
        DXGI_FORMAT_R16_UNORM,        // eImage_I16
        DXGI_FORMAT_R16G16_UNORM,     // eImage_RG16
        DXGI_FORMAT_R16G16B16A16_UNORM, // eImage_RGB16
        DXGI_FORMAT_R16G16B16A16_UNORM, // eImage_RGBA16
        DXGI_FORMAT_R16_FLOAT,        // eImage_I16F
        DXGI_FORMAT_R16G16_FLOAT,     // eImage_RG16F
        DXGI_FORMAT_R16G16B16A16_FLOAT, // eImage_RGB16F
        DXGI_FORMAT_R16G16B16A16_FLOAT, // eImage_RGBA16F
        DXGI_FORMAT_R32_FLOAT,        // eImage_I32F
        DXGI_FORMAT_R32G32_FLOAT,     // eImage_RG32F
        DXGI_FORMAT_R32G32B32A32_FLOAT, // eImage_RGB32F
        DXGI_FORMAT_R32G32B32A32_FLOAT  // eImage_RGBA32F
    };
    DXGI_FORMAT dxgiFormat = dxgiFormats[format.getFormatEnum()];
    g_Log->Info("Converting format %d to DXGI format %d",
                format.getFormatEnum(), dxgiFormat);
    return dxgiFormat;
}

bool CTextureFlatDX11::Upload(spCImage _spImage) {
    m_spImage = _spImage;
    if (!m_spImage) return false;

    CImageFormat format = _spImage->GetFormat();
    uint32_t width = _spImage->GetWidth();
    uint32_t height = _spImage->GetHeight();
    bool mipMapped = _spImage->GetNumMipMaps() > 1;
    uint32_t bytesPerRow = _spImage->GetPitch();

    uint32_t mipMapLevel = 0;
    uint8_t* pSrc;
    while ((pSrc = _spImage->GetData(mipMapLevel)) != nullptr) {
        if (!Upload(pSrc, format, width, height, bytesPerRow, mipMapped, mipMapLevel))
            return false;
        mipMapLevel++;
    }
    return true;
}

bool CTextureFlatDX11::Upload(const uint8_t* _data, CImageFormat _format,
                              uint32_t _width, uint32_t _height,
                              uint32_t _bytesPerRow, bool _mipMapped,
                              uint32_t _mipMapLevel)
{
    try
    {
        // Create new texture only if needed
        if (!m_texture || _mipMapLevel == 0)
        {
            // Release previous resources first
            m_srv.Reset();
            m_texture.Reset();

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = _width;
            desc.Height = _height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = GetDXGIFormat(_format);
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; // Changed from DYNAMIC
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0; // No CPU access needed

            // For video frames, create a staging texture for upload
            D3D11_TEXTURE2D_DESC stagingDesc = desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            ComPtr<ID3D11Texture2D> stagingTexture;
            HRESULT hr = m_device->CreateTexture2D(&stagingDesc, nullptr,
                                                   &stagingTexture);
            if (FAILED(hr))
            {
                g_Log->Error("Failed to create staging texture: %08X", hr);
                return false;
            }

            // Create main texture
            hr = m_device->CreateTexture2D(&desc, nullptr, &m_texture);
            if (FAILED(hr))
            {
                g_Log->Error("Failed to create main texture: %08X", hr);
                return false;
            }

            // Create SRV
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            hr = m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc,
                                                    &m_srv);
            if (FAILED(hr))
            {
                g_Log->Error("Failed to create SRV: %08X", hr);
                return false;
            }

            // Upload data through staging texture
            if (_data)
            {
                D3D11_MAPPED_SUBRESOURCE mapped;
                hr = m_context->Map(stagingTexture.Get(), 0, D3D11_MAP_WRITE, 0,
                                    &mapped);
                if (SUCCEEDED(hr))
                {
                    const uint8_t* srcRow = _data;
                    uint8_t* dstRow = (uint8_t*)mapped.pData;
                    for (uint32_t y = 0; y < _height; y++)
                    {
                        memcpy(dstRow, srcRow, _bytesPerRow);
                        srcRow += _bytesPerRow;
                        dstRow += mapped.RowPitch;
                    }
                    m_context->Unmap(stagingTexture.Get(), 0);

                    // Copy from staging to main texture
                    m_context->CopyResource(m_texture.Get(),
                                            stagingTexture.Get());
                }
            }
        }

        m_bDirty = true;
        return true;
    }
    catch (...)
    {
        g_Log->Error("Exception in CTextureFlatDX11::Upload");
        return false;
    }
}

bool CTextureFlatDX11::BindFrame(ContentDecoder::spCVideoFrame _spFrame) {
    m_Flags |= eTextureFlags::TEXTURE_YUV;
    m_spBoundFrame = _spFrame;
    return true;
}

bool CTextureFlatDX11::Bind(const uint32_t _index) {
    if (m_srv) {
        m_context->PSSetShaderResources(_index, 1, m_srv.GetAddressOf());
        m_context->PSSetSamplers(_index, 1, m_sampler.GetAddressOf());
        return true;
    }
    return false;
}

bool CTextureFlatDX11::Unbind(const uint32_t _index) {
    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_context->PSSetShaderResources(_index, 1, &nullSRV);
    return true;
}


} // namespace DisplayOutput