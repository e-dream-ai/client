#include "TextureFlatD3D12.h"

#include "ResourceUploadBatch.h"

#include "Log.h"


namespace DisplayOutput
{

    CTextureFlatD3D12::CTextureFlatD3D12(std::unique_ptr<DeviceResources> _m_deviceResources, const uint32_t _flags)
    {
        if (_m_deviceResources != nullptr)
        {
            m_deviceResources = _m_deviceResources;
        }
        else
        {
            g_Log->Error("CTextureFlatD3D12: Device is nullptr");
        }
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

        ComPtr<ID3D12Resource> texture;
        ThrowIfFailed(
            m_deviceResources->GetD3DDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &txtDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())));

        // Make our subresource fro
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = m_spImage->GetData(1);
        textureData.RowPitch = m_spImage->GetWidth() * 4;
        textureData.SlicePitch = textureData.RowPitch * m_spImage->GetHeight();

        DirectX::ResourceUploadBatch resourceUpload(
            m_deviceResources->GetD3DDevice());

        resourceUpload.Begin();

        resourceUpload.Upload(texture.Get(), 0, &textureData, 1);

        resourceUpload.Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        auto uploadResourcesFinished =
            resourceUpload.End(m_deviceResources->GetCommandQueue());

        uploadResourcesFinished.wait();




        return true;
    }

    bool CTextureFlatD3D12::Bind(uint32_t _slot)
    {
        return true;
    }

    bool CTextureFlatD3D12::Unbind(uint32_t _slot)
    {
        return true;
    }

    bool CTextureFlatD3D12::BindFrame(ContentDecoder::spCVideoFrame _pFrame)
    {

        return true;
    }

} // namespace DisplayOutput

