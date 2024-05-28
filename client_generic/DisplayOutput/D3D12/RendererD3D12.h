#pragma once
#include "Renderer.h"
#include "Image.h"
#include "SmartPtr.h"
#include "TextureFlat.h"
#include "Vector4.h"
#include "base.h"

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXColors.h>

// From DirectX Tool Kit
#include <directxtk12/GraphicsMemory.h>
#include "directxtk12/DescriptorHeap.h"
#include <directxtk12/VertexTypes.h>
#include <directxtk12/PrimitiveBatch.h>
#include <directxtk12/Effects.h>
#include <directxtk12/CommonStates.h>
#include "directxtk12/ResourceUploadBatch.h"
#include "directxtk12/WICTextureLoader.h"

#include "DeviceResources.h"
#include <windows.h>
#include <wrl.h>
#include <pix.h>

using namespace Microsoft::WRL;

namespace DisplayOutput
{
    using namespace DirectX;

class CRendererD3D12 : public CRenderer, IDeviceNotify
{
	HWND m_WindowHandle;

	// Device resources. This contains everuthing D3D12 needs to know about the device


    std::unique_ptr<PrimitiveBatch<VertexPositionColor>> primitiveBatch;
    std::unique_ptr<PrimitiveBatch<VertexPositionTexture>> texturedBatch;

    //std::unique_ptr<BasicEffect> m_textureEffect;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_texture; // tmp

    std::unique_ptr<BasicEffect> m_lineEffect;
    std::unique_ptr<PrimitiveBatch<VertexPositionColor>> m_batch;

    // IDeviceNotify
    // This is called when the Win32 window is created (or re-created).
    void OnDeviceLost() override;
    void OnDeviceRestored() override;


    void XM_CALLCONV DrawGrid(FXMVECTOR xAxis, FXMVECTOR yAxis,
                                              FXMVECTOR origin, size_t xdivs,
                                              size_t ydivs, GXMVECTOR color);

	spCTextureFlat m_spSoftCorner;
    
    int m_currentTextureIndex = 0; 

  public:
    // Device resources. This contains everuthing D3D12 needs to know about the device
    //std::unique_ptr<DeviceResources> m_deviceResources;
    std::unique_ptr<DeviceResources> m_deviceResources;
    std::unique_ptr<GraphicsMemory> m_graphicsMemory;
    std::shared_ptr<DescriptorHeap> m_resourceDescriptors;
    std::unique_ptr<CommonStates> m_states;
    
    CRendererD3D12();
	virtual ~CRendererD3D12();

    static int textureIndex;



    virtual ComPtr<ID3D12Device> GetDevice() { return m_deviceResources->GetD3DDevice(); };
	virtual ComPtr<ID3D12GraphicsCommandList> GetCommandList() { return m_deviceResources->GetCommandList(); };
	virtual ComPtr<ID3D12CommandQueue> GetCommandQueue() { return m_deviceResources->GetCommandQueue(); };
    virtual std::shared_ptr<DescriptorHeap> GetResourceDescriptors() { return m_resourceDescriptors; };

    void BindTexture(int index);

	virtual eRenderType Type(void) const { return eDX9; };
	virtual const std::string Description(void) const { return "DirectX 12"; };

	void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

	//
	bool Initialize(spCDisplayOutput _spDisplay);

	//
	void Defaults();

	void Clear();
	//
	bool BeginFrame(void);
	bool EndFrame(bool drawn);

	//
	void Apply();
	void Reset(const uint32_t _flags);

	bool TestResetDevice();

	//
    spCTextureFlat NewTextureFlat(const uint32_t flags = 0);
    spCTextureFlat NewTextureFlat(spCImage _spImage,
                                    const uint32_t flags = 0);

    // Font functions.
    spCBaseFont NewFont(CFontDescription& _desc);
    void Text(spCBaseFont _spFont, const std::string& _text,
                const Base::Math::CVector4& _color,
                const Base::Math::CRect& _rect, uint32_t _flags);
    Base::Math::CVector2 GetTextExtent(spCBaseFont _spFont,
                                        const std::string& _text);

    // Virtual functions added for Metal, we don't use them here yet but will likely be needed
    spCBaseFont GetFont(CFontDescription& _desc) { return nullptr; };
    spCBaseText NewText(spCBaseFont _font, const std::string& _text)
    {
        g_Log->Info("*** %s ***", _text.c_str());
        return nullptr;
    };

    void DrawText(spCBaseText _text, const Base::Math::CVector4& _color)
    {
        return;
    };

    // Shaders.
    spCShader NewShader(const char* _pVertexShader,
                        const char* _pFragmentShader);

    // Virtual shared functions added for Metal, we don't use them here yet but will likely be needed
    spCShader
    NewShader(const char* _pVertexShader, const char* _pFragmentShader,
              std::vector<std::pair<std::string, eUniformType>> _uniforms = {});

	//
	void SetWindowHandle(HWND _hWnd) { m_WindowHandle = _hWnd; };
	HWND GetWindowHandle() { return m_WindowHandle; };

    virtual void DrawQuad(const Base::Math::CRect& /*_rect*/,
                          const Base::Math::CVector4& /*_color*/);
    virtual void DrawQuad(const Base::Math::CRect& /*_rect*/,
                          const Base::Math::CVector4& /*_color*/,
                          const Base::Math::CRect& /*_uvRect*/);
    virtual void DrawSoftQuad(const Base::Math::CRect& /*_rect*/,
                              const Base::Math::CVector4& /*_color*/,
                              const float /*_width*/);



	////
	//void SetPresentationParams(D3DPRESENT_PARAMETERS _params) { m_PresentationParams = _params; };
	//D3DPRESENT_PARAMETERS GetPresentationParams() { return m_PresentationParams; };

	////
	//void SetDevice(IDirect3DDevice9* _pDevice) { m_pDevice = _pDevice; };
	//IDirect3DDevice9* GetDevice() { return m_pDevice; };

	//
	////
	//void SetSprite(ID3DXSprite* _pSprite) { m_pSprite = _pSprite; };
	//ID3DXSprite* GetSprite() { return m_pSprite; };

	////
	//void SetFonts(std::vector<ID3DXFont*> _fonts) { m_Fonts = _fonts; };
	//std::vector<ID3DXFont*> GetFonts() { return m_Fonts; };

	// ??
	void SetSoftCorner(spCTextureFlat _spSoftCorner) { m_spSoftCorner = _spSoftCorner; };
	spCTextureFlat GetSoftCorner() { return m_spSoftCorner; };

    // Descriptors
    enum Descriptors
    {
        Texture,
        Font,
        Count = 25600
    };
};

}
