#pragma once
#include "Renderer.h"
#include "Image.h"
#include "SmartPtr.h"
#include "TextureFlat.h"
#include "Vector4.h"
#include "base.h"
#include <d3d12.h>
#include "DeviceResources.h"
#include <windows.h>
#include <wrl.h>

using namespace Microsoft::WRL;

namespace DisplayOutput
{

class CRendererD3D12 : public CRenderer
{
	HWND m_WindowHandle;
	//D3DPRESENT_PARAMETERS m_PresentationParams;
    ComPtr<ID3D12Device> m_pDevice;
    std::shared_ptr<DeviceResources> m_deviceResources;

	// ID3DXLine			*m_pLine;
	//ID3DXSprite* m_pSprite;

	//std::vector<ID3DXFont*> m_Fonts;
	spCTextureFlat m_spSoftCorner;

  public:
	CRendererD3D12();
	virtual ~CRendererD3D12();

	virtual ComPtr<ID3D12Device> Device() { return m_pDevice; };

	virtual eRenderType Type(void) const { return eDX9; };
	virtual const std::string Description(void) const { return "DirectX 12"; };

	//
	bool Initialize(spCDisplayOutput _spDisplay);

	//
	void Defaults();

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
    spCShader NewShader(
        const char* _pVertexShader, const char* _pFragmentShader,
        std::vector<std::pair<std::string, eUniformType>> _uniforms = {})
    {
        return nullptr;
    };

	//
	void SetWindowHandle(HWND _hWnd) { m_WindowHandle = _hWnd; };
	HWND GetWindowHandle() { return m_WindowHandle; };

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

	//
	void SetSoftCorner(spCTextureFlat _spSoftCorner) { m_spSoftCorner = _spSoftCorner; };
	spCTextureFlat GetSoftCorner() { return m_spSoftCorner; };
};

}
