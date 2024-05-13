#include "TextureFlatD3D12.h"
#include "Log.h"

namespace DisplayOutput
{

	CTextureFlatD3D12::CTextureFlatD3D12(ComPtr<ID3D12Device> _m_device,
                                   const uint32_t _flags)
	{
        if (_m_device != nullptr)
        {
			m_device = _m_device;
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


		//// Create the texture
		//D3D12_RESOURCE_DESC textureDesc = {};
		//textureDesc.MipLevels = 1;
		//textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  //      textureDesc.Width = m_Size.iWidth();
  //      textureDesc.Height = m_Size.iHeight();
		//textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		//textureDesc.DepthOrArraySize = 1;
		//textureDesc.SampleDesc.Count = 1;
		//textureDesc.SampleDesc.Quality = 0;
		//textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		//// Create the texture resource
		//ThrowIfFailed(m_device->CreateCommittedResource(
		//	&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		//	D3D12_HEAP_FLAG_NONE,
		//	&textureDesc,
		//	D3D12_RESOURCE_STATE_COPY_DEST,
		//	nullptr,
		//	IID_PPV_ARGS(&m_texture)));

		//const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

		//// Create the GPU upload buffer.
		//ThrowIfFailed(m_device->CreateCommittedResource(
		//	&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		//	D3D12_HEAP_FLAG_NONE,
		//	&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		//	D3D12_RESOURCE_STATE_GENERIC_READ,
		//	nullptr,
		//	IID_PPV_ARGS(&m_textureUploadHeap)));

		//// Copy data to the intermediate upload heap and then schedule a copy from the upload heap to the Texture2D.
		//std::vector<UINT8> texture = GenerateTextureData();

		//D3D12_SUBRESOURCE_DATA textureData = {};
		//textureData.pData = &texture[0];
		//textureData.RowPitch = m_width * 4;
		//textureData.SlicePitch = textureData.RowPitch * m_height;

		//UpdateSubresources(m_commandList.Get(), m_texture.Get(), m_textureUploadHeap.Get(), 0, 0, 1, &textureData);
		//m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		//// Describe and create a SRV for the texture.
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};


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