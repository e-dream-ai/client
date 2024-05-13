#pragma once
#include "TextureFlat.h"
#include <d3d12.h>
#include <wrl.h>
#include "D3D12Helpers.h"

using Microsoft::WRL::ComPtr;
 
namespace DisplayOutput
{

class CTextureFlatD3D12 : public CTextureFlat
{
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_resource;

    //	Internal to keep track if size or format changed.
    DisplayOutput::eImageFormat m_Format;

  public:
    Base::Math::CRect m_Size;

    CTextureFlatD3D12(ComPtr<ID3D12Device> _m_device, const uint32_t _flags = 0);
    virtual ~CTextureFlatD3D12();

    virtual bool Upload(spCImage _spImage);
    virtual bool Bind(const uint32_t _index);
    virtual bool Unbind(const uint32_t _index);
    bool BindFrame(ContentDecoder::spCVideoFrame _pFrame);
};

} // namespace DisplayOutput 
