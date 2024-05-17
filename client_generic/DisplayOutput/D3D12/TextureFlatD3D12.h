#pragma once
#include "TextureFlat.h"
#include <d3d12.h>
#include <d3dx12.h>
#include <windows.h>
#include <wrl.h>
#include "D3D12Helpers.h"
#include "DisplayOutput.h"
#include "RendererD3D12.h"
#include "DeviceResources.h"

using Microsoft::WRL::ComPtr;
 
namespace DisplayOutput
{

class CTextureFlatD3D12 : public CTextureFlat
{
    int textureIndex;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    std::shared_ptr<DescriptorHeap> heap;

    ComPtr<ID3D12Resource> m_texture; 


    // Our texture resource
    ComPtr<ID3D12Resource> m_resource;

    //	Internal to keep track if size or format changed.
    DisplayOutput::eImageFormat m_Format;

  public:
    Base::Math::CRect m_Size;
    static int hasUploaded;

    CTextureFlatD3D12(ComPtr<ID3D12Device> _device,
                      ComPtr<ID3D12CommandQueue> _commandQueue,
                      std::shared_ptr<DescriptorHeap> _heap, 
                      int _textureIndex,
                      const uint32_t _flags = 0);
    virtual ~CTextureFlatD3D12();

    virtual bool Upload(spCImage _spImage);
    virtual bool Bind(const uint32_t _index);
    virtual bool Unbind(const uint32_t _index);
    bool BindFrame(ContentDecoder::spCVideoFrame _pFrame);
};

} // namespace DisplayOutput 
