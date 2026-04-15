#ifndef _TEXTUREFLATDX11_H
#define _TEXTUREFLATDX11_H

#include <d3d11.h>
#include <wrl/client.h>
#include "TextureFlat.h"

using Microsoft::WRL::ComPtr;

namespace DisplayOutput {

class CTextureFlatDX11 : public CTextureFlat {
protected:
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ContentDecoder::spCVideoFrame m_spBoundFrame;

public:
    CTextureFlatDX11(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context, const uint32_t flags = 0);
    virtual ~CTextureFlatDX11();

    bool Upload(spCImage _spImage) override;
    bool Upload(const uint8_t* _data, CImageFormat _format, uint32_t _width, 
               uint32_t _height, uint32_t _bytesPerRow, bool _mipMapped, 
               uint32_t _mipMapLevel);
    bool BindFrame(ContentDecoder::spCVideoFrame _spFrame) override;
    bool Bind(const uint32_t _index) override;
    bool Unbind(const uint32_t _index) override;

    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11SamplerState* GetSampler() const { return m_sampler.Get(); }

private:
    bool CreateSampler();
    DXGI_FORMAT GetDXGIFormat(CImageFormat format);
};

MakeSmartPointers(CTextureFlatDX11);

} // namespace DisplayOutput

#endif