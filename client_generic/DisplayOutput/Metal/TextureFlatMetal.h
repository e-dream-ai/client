#ifndef _TEXTUREFLATMETAL_H
#define _TEXTUREFLATMETAL_H

#include "TextureFlat.h"
#include <CoreFoundation/CFBase.h>
#include <CoreVideo/CVMetalTextureCache.h>

namespace DisplayOutput
{

class CRendererMetal;

/*
        CTextureFlatMetal.

*/
class CTextureFlatMetal : public CTextureFlat
{
    CGraphicsContext m_pGraphicsContext;
    CFTypeRef m_pTextureContext;
    CRendererMetal *m_pRenderer;
    ContentDecoder::spCVideoFrame m_spBoundFrame;
    char name[5];

  public:
    CTextureFlatMetal(CGraphicsContext _graphicsContext, const uint32 _flags,
                      CRendererMetal *_pRenderer);
    virtual ~CTextureFlatMetal();

    bool Upload(spCImage _spImage);
    bool Upload(const uint8_t *_data, CImageFormat _format, uint32_t _width,
                uint32_t _height, uint32_t _bytesPerRow, bool _mipMapped,
                uint32_t _mipMapLevel);
    bool Bind(const uint32 _index);
    bool Unbind(const uint32 _index);
    bool BindFrame(ContentDecoder::spCVideoFrame _spFrame);
#ifdef __OBJC__
    bool GetYUVMetalTextures(CVMetalTextureRef *_outYTexture,
                             CVMetalTextureRef *_outUVTexture);
    id<MTLTexture> GetRGBMetalTexture();
    CVMetalTextureRef GetCVMetalTextureRef();
    void ReleaseMetalTexture();
#endif
};

MakeSmartPointers(CTextureFlatMetal);

} // namespace DisplayOutput

#endif
