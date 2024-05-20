#include "TextureFlatD3D12.h"

#include "ResourceUploadBatch.h"

#include "Log.h"


namespace DisplayOutput
{

    CTextureFlatD3D12::CTextureFlatD3D12(ComPtr<ID3D12Device> _device,
                                     ComPtr<ID3D12CommandQueue> _commandQueue,
                                     std::shared_ptr<DescriptorHeap> _heap, 
                                     CRendererD3D12* _m_renderer,
                                     int _textureIndex,
                                     const uint32_t _flags)
    {
        m_renderer = _m_renderer;
        device = _device;
        commandQueue = _commandQueue;
        heap = std::move(_heap);
        textureIndex = _textureIndex;
    }

    CTextureFlatD3D12::~CTextureFlatD3D12()
    {
    }

    bool CTextureFlatD3D12::Upload(spCImage _spImage)
    {
        
        m_spImage = _spImage;

        CImageFormat format = m_spImage->GetFormat();

        D3D12_RESOURCE_DESC txtDesc = {};
        txtDesc.MipLevels = txtDesc.DepthOrArraySize = 1;
        txtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        txtDesc.Width = m_spImage->GetWidth();
        txtDesc.Height = m_spImage->GetHeight();
        txtDesc.SampleDesc.Count = 1;
        txtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

        
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &txtDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(m_texture.ReleaseAndGetAddressOf())));

        // Make our subresource fro
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = m_spImage->GetData(0);
        textureData.RowPitch = m_spImage->GetWidth() * 4;
        textureData.SlicePitch = textureData.RowPitch * m_spImage->GetHeight();

        DirectX::ResourceUploadBatch resourceUpload(
            device.Get());

        resourceUpload.Begin();

        resourceUpload.Upload(m_texture.Get(), 0, &textureData, 1);

        resourceUpload.Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        auto uploadResourcesFinished =
            resourceUpload.End(commandQueue.Get());

        uploadResourcesFinished.wait();
        device.Get()->CreateShaderResourceView(m_texture.Get(), nullptr, heap->GetCpuHandle(textureIndex));

        return true;
    }

    bool CTextureFlatD3D12::Bind(uint32_t _slot) { 
        g_Log->Info("CTextureFlatD3D12::Bind %d", textureIndex);
        m_renderer->BindTexture(textureIndex);

        return true;
    }

    bool CTextureFlatD3D12::Unbind(uint32_t _slot)
    {
        g_Log->Info("CTextureFlatD3D12::UnBind %d", textureIndex);
        return true;
    }

    bool CTextureFlatD3D12::BindFrame(ContentDecoder::spCVideoFrame _pFrame)
    {

        return true;
    }

} // namespace DisplayOutput

