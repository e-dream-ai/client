#include "TextureFlatD3D12.h"

#include "directxtk12/ResourceUploadBatch.h"

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

        // We create our texture effect here 
        const RenderTargetState rtState(
            m_renderer->m_deviceResources->GetBackBufferFormat(),
            m_renderer->m_deviceResources->GetDepthBufferFormat());

        EffectPipelineStateDescription pd(
            &VertexPositionTexture::InputLayout, CommonStates::AlphaBlend,
            CommonStates::DepthNone, CommonStates::CullNone, rtState);

        auto l_Device = m_renderer->m_deviceResources->GetD3DDevice();
        // Allocate it here
        m_textureEffect =
            std::make_unique<BasicEffect>(l_Device, EffectFlags::Texture, pd);
    }

    CTextureFlatD3D12::~CTextureFlatD3D12() { 
        g_Log->Info("CTextureFlatD3D12::~CTextureFlatD3D12 %d", textureIndex);
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
        //m_renderer->BindTexture(textureIndex);

        auto commandList = m_renderer->m_deviceResources->GetCommandList();

        // Set the descriptor heaps
        ID3D12DescriptorHeap* heaps[] = {
            m_renderer->m_resourceDescriptors->Heap(),
            m_renderer->m_states->Heap()};
        commandList->SetDescriptorHeaps(_countof(heaps), heaps);

        m_textureEffect->SetTexture(
            m_renderer->m_resourceDescriptors->GetGpuHandle(textureIndex),
            m_renderer->m_states->LinearWrap());

        m_textureEffect->Apply(commandList);

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

