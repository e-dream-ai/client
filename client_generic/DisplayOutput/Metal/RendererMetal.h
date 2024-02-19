#ifndef _RENDERERMETAL_H_
#define _RENDERERMETAL_H_

#include <map>
#include <string>
#include <shared_mutex>

#include "Font.h"
#include "Image.h"
#include "Renderer.h"
#include "SmartPtr.h"
#include "TextureFlatMetal.h"
#include "base.h"
#include <CoreFoundation/CFBase.h>

namespace DisplayOutput
{

class CRendererMetal : public CRenderer
{
    spCTextureFlat m_spSoftCorner;
    spCImage m_spTextImage;
    spCTextureFlat m_spTextTexture;
    Base::Math::CRect m_textRect;
    CFTypeRef m_pRendererContext;
    std::shared_mutex m_textureMutex;
    std::map<std::string, spCBaseFont> m_fontPool;
#ifdef __OBJC__
    bool
    CreateMetalTextureFromDecoderFrame(CVPixelBufferRef pixelBuffer,
                                       CVMetalTextureRef* _pMetalTextureRef,
                                       uint32_t plane);
    bool GetYUVMetalTextures(spCTextureFlatMetal _texture,
                             CVMetalTextureRef* _outYTexture,
                             CVMetalTextureRef* _outUVTexture);
#endif

  public:
    CRendererMetal();
    virtual ~CRendererMetal();

    virtual eRenderType Type(void) const { return eMetal; };
    virtual const std::string Description(void) const { return "Metal"; }
    bool Initialize(spCDisplayOutput _spDisplay);
    void Defaults();
    bool BeginFrame(void);
    bool EndFrame(bool drawn = true);
    void Apply();
    void Reset(const uint32_t _flags);
    spCTextureFlat NewTextureFlat(const uint32_t flags = 0);
    spCTextureFlat NewTextureFlat(spCImage _spImage, const uint32_t flags = 0);
    spCBaseFont GetFont(CFontDescription& _desc);
    spCBaseText NewText(spCBaseFont _font, const std::string& _text);
    void DrawText(spCBaseText _text, const Base::Math::CVector4& _color);
    Base::Math::CVector2 GetTextExtent(spCBaseFont _spFont,
                                       const std::string& _text);
    spCShader
    NewShader(const char* _pVertexShader, const char* _pFragmentShader,
              std::vector<std::pair<std::string, eUniformType>> uniforms = {});
    void Clear();
    void DrawQuad(const Base::Math::CRect& _rect,
                  const Base::Math::CVector4& _color);
    void DrawQuad(const Base::Math::CRect& _rect,
                  const Base::Math::CVector4& _color,
                  const Base::Math::CRect& _uvRect);
    void DrawSoftQuad(const Base::Math::CRect& _rect,
                      const Base::Math::CVector4& _color, const float _width);

  private:
    void BuildDepthTexture();
};

MakeSmartPointers(CRendererMetal);

} // namespace DisplayOutput

#endif
