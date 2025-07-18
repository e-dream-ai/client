#ifndef _RENDERERDX_H_
#define _RENDERERDX_H_

#include <d3d9.h>
#include <d3dx9.h>
#include <string>

#include "../msvc/DirectX_DLL_functions.h"
#include "Image.h"
#include "Renderer.h"
#include "SmartPtr.h"
#include "TextureFlat.h"
#include "Vector4.h"
#include "base.h"

namespace DisplayOutput
{

/*
        CRendererDX().

*/
class CRendererDX : public CRenderer
{
    HWND m_WindowHandle;
    D3DPRESENT_PARAMETERS m_PresentationParams;
    IDirect3DDevice9* m_pDevice;

    // ID3DXLine			*m_pLine;
    ID3DXSprite* m_pSprite;

    std::vector<ID3DXFont*> m_Fonts;
    spCTextureFlat m_spSoftCorner;

  public:
    CRendererDX();
    virtual ~CRendererDX();

    IDirect3DDevice9* Device() { return m_pDevice; };

    virtual eRenderType Type(void) const { return eDX9; };
    virtual const std::string Description(void) const { return "DirectX 9"; };

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
    spCTextureFlat NewTextureFlat(spCImage _spImage, const uint32_t flags = 0);

    //
    spCBaseFont NewFont(CFontDescription& _desc);
    void Text(spCBaseFont _spFont, const std::string& _text,
              const Base::Math::CVector4& _color,
              const Base::Math::CRect& _rect, uint32_t _flags);
    Base::Math::CVector2 GetTextExtent(spCBaseFont _spFont,
                                       const std::string& _text);

    //
    spCShader NewShader(const char* _pVertexShader,
                        const char* _pFragmentShader);

    //	Aux functions.
    void DrawLine(const Base::Math::CVector2& _start,
                  const Base::Math::CVector2& _end,
                  const Base::Math::CVector4& _color,
                  const float _width = 1.0f);
    void DrawRect(const Base::Math::CRect& _rect,
                  const Base::Math::CVector4& _color,
                  const float _width = 1.0f);
    void DrawQuad(const Base::Math::CRect& _rect,
                  const Base::Math::CVector4& _color);
    void DrawQuad(const Base::Math::CRect& _rect,
                  const Base::Math::CVector4& _color,
                  const Base::Math::CRect& _uvRect);
    void DrawSoftQuad(const Base::Math::CRect& _rect,
                      const Base::Math::CVector4& _color, const float _width);
};

MakeSmartPointers(CRendererDX);

} // namespace DisplayOutput

#endif
