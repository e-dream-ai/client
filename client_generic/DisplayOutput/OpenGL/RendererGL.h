#ifndef _RENDERERGL_H_
#define _RENDERERGL_H_

#include "FontGL.h"
#include "Image.h"
#include "Renderer.h"
#include "SmartPtr.h"
#include "TextureFlat.h"
#include "base.h"
#include <string>
#ifdef MAC
#undef Random
#include <OpenGL/CGLMacro.h>
#endif

namespace DisplayOutput
{

/*
        CRendererGL().

*/
class CRendererGL : public CRenderer
{
#ifdef WIN32
    HWND m_WindowHandle;
    HDC m_DeviceContext;
    HGLRC m_RenderContext;
#else
#ifdef MAC
    CGLContextObj cgl_ctx;
#endif
#endif

    spCTextureFlat m_spSoftCorner;

    spCImage m_spTextImage;

    spCTextureFlat m_spTextTexture;

    spCFontGL m_glFont;

    Base::Math::CRect m_textRect;

  public:
    CRendererGL();
    virtual ~CRendererGL();

    virtual eRenderType Type(void) const { return eGL; };
    virtual const std::string Description(void) const { return "OpenGL"; }

    //
    bool Initialize(spCDisplayOutput _spDisplay);

    //
    void Defaults();

    //
    bool BeginFrame(void);
    bool EndFrame(bool drawn = true);

    //
    void Apply();
    void Reset(const uint32 _flags);

    //
    spCTextureFlat NewTextureFlat(const uint32 flags = 0);
    spCTextureFlat NewTextureFlat(spCImage _spImage, const uint32 flags = 0);

    eTextureTargetType GetTextureTargetType(void);

    //
    spCBaseFont NewFont(CFontDescription &_desc);
    void Text(spCBaseFont _spFont, const std::string &_text,
              const Base::Math::CVector4 &_color,
              const Base::Math::CRect &_rect, uint32 _flags);
    spCBaseText NewText(spCBaseFont /*_font*/, const std::string & /*_text*/)
    {
        return NULL;
    }
    Base::Math::CVector2 GetTextExtent(spCBaseFont _spFont,
                                       const std::string &_text);

    //
    spCShader
    NewShader(const char *_pVertexShader, const char *_pFragmentShader,
              std::vector<std::pair<std::string, eUniformType>> _uniforms = {});

    //
    void DrawText(spCBaseText /*_text*/,
                  const Base::Math::CVector4 & /*_color*/,
                  const Base::Math::CRect & /*_rect*/)
    {
    }
    void DrawQuad(const Base::Math::CRect &_rect,
                  const Base::Math::CVector4 &_color);
    void DrawQuad(const Base::Math::CRect &_rect,
                  const Base::Math::CVector4 &_color,
                  const Base::Math::CRect &_uvRect);
    void DrawSoftQuad(const Base::Math::CRect &_rect,
                      const Base::Math::CVector4 &_color, const fp4 _width);

    static void Verify(const char *_file, const int32 _line);

    void SetCurrentGLContext();
};

#define VERIFYGL CRendererGL::Verify(__FILE__, __LINE__)

MakeSmartPointers(CRendererGL);

} // namespace DisplayOutput

#endif
