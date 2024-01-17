#ifndef _TEXTUREFLATMETAL_H
#define _TEXTUREFLATMETAL_H

#include "TextureFlat.h"
#include <CoreFoundation/CFBase.h>
#include <CoreVideo/CVMetalTextureCache.h>

namespace DisplayOutput
{

class CTextureFlatMetal : public CTextureFlat
{
    CGraphicsContext m_pGraphicsContext;
    CFTypeRef m_pTextureContext;
    ContentDecoder::spCVideoFrame m_spBoundFrame;
    char name[5];

  public:
    CTextureFlatMetal(CGraphicsContext _graphicsContext, const uint32_t _flags);
    virtual ~CTextureFlatMetal();

    bool Upload(spCImage _spImage);
    bool Upload(const uint8_t* _data, CImageFormat _format, uint32_t _width,
                uint32_t _height, uint32_t _bytesPerRow, bool _mipMapped,
                uint32_t _mipMapLevel);
    bool BindFrame(ContentDecoder::spCVideoFrame _spFrame);
    bool GetYUVMetalTextures(CVImageBufferRef* _outYTexture,
                             CVImageBufferRef* _outUVTexture);

#ifdef __OBJC__
    id<MTLTexture> GetRGBMetalTexture();
    CVMetalTextureRef GetCVMetalTextureRef();
    CVPixelBufferRef GetPixelBuffer();
    void ReleaseMetalTexture();
#endif
};

MakeSmartPointers(CTextureFlatMetal);

} // namespace DisplayOutput

#endif
