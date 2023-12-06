#ifdef USE_METAL

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "DisplayOutput.h"
#include "Exception.h"
#include "Log.h"
#include "MathBase.h"
#include "RendererMetal.h"
#include "TextureFlatMetal.h"
#include "base.h"

@interface TextureFlatMetalContext : NSObject
{
@public
  id<MTLTexture> yTexture;
@public
  id<MTLTexture> uvTexture;
@public
  CVMetalTextureRef decoderFrameYTextureRef;
@public
  CVMetalTextureRef decoderFrameUVTextureRef;
@public
  IOSurfaceRef ioSurface;
}
@end

@implementation TextureFlatMetalContext
@end

namespace DisplayOutput
{

/*
 */
CTextureFlatMetal::CTextureFlatMetal(CGraphicsContext _graphicsContext,
                                     const uint32 _flags,
                                     CRendererMetal *_pRenderer)
    : CTextureFlat(_flags), m_pGraphicsContext(_graphicsContext),
      m_pRenderer(_pRenderer)
{
  TextureFlatMetalContext *textureContext = [[TextureFlatMetalContext alloc]
      init]; //@TODO: check what all these textures are
  m_pTextureContext = CFBridgingRetain(textureContext);
  for (int i = 0; i < 4; ++i)
    name[i] = 'A' + ((char)std::rand() % 22);
  name[4] = 0; //@TODO: cleanup
  m_spBoundFrame = NULL;
  // g_Log->Error("TEXTURES:ALLOCATING TEXTURE:%s %i isnull:%i", name,
  // m_spBoundFrame == NULL, m_spBoundFrame.IsNull());
}

/*
 */
CTextureFlatMetal::~CTextureFlatMetal()
{
  // g_Log->Error("TEXTURES:DESTROYING TEXTURE:%s bound:%i isnull:%i", name,
  // m_spBoundFrame == NULL, m_spBoundFrame.IsNull());
  CFBridgingRelease(m_pTextureContext);
}

/*
 */
bool CTextureFlatMetal::Upload(spCImage _spImage)
{
  m_spImage = _spImage;
  if (m_spImage == NULL)
    return false;

  CImageFormat format = _spImage->GetFormat();
  uint32_t width = _spImage->GetWidth();
  uint32_t height = _spImage->GetHeight();
  bool mipMapped = _spImage->GetNumMipMaps() > 1;
  uint32_t bytesPerRow = _spImage->GetPitch();

  // Upload the texture data
  uint8_t *pSrc;
  uint32_t mipMapLevel = 0;
  while ((pSrc = _spImage->GetData(mipMapLevel)) != NULL)
  {
    Upload(pSrc, format, width, height, bytesPerRow, mipMapped, mipMapLevel);
    mipMapLevel++;
  }
}

bool CTextureFlatMetal::Upload(const uint8_t *_data, CImageFormat _format,
                               uint32_t _width, uint32_t _height,
                               uint32_t _bytesPerRow, bool _mipMapped,
                               uint32_t _mipMapLevel)
{
  TextureFlatMetalContext *textureContext =
      (__bridge TextureFlatMetalContext *)m_pTextureContext;

  static const MTLPixelFormat srcFormats[] = {
      MTLPixelFormatInvalid,     // eImage_None
      MTLPixelFormatR8Unorm,     // eImage_I8
      MTLPixelFormatRG8Unorm,    // eImage_IA8
      MTLPixelFormatRGBA8Unorm,  // eImage_RGB8
      MTLPixelFormatRGBA8Unorm,  // eImage_RGBA8
      MTLPixelFormatR16Unorm,    // eImage_I16
      MTLPixelFormatRG16Unorm,   // eImage_RG16
      MTLPixelFormatRGBA16Unorm, // eImage_RGB16
      MTLPixelFormatRGBA16Unorm, // eImage_RGBA16
      MTLPixelFormatR16Float,    // eImage_I16F
      MTLPixelFormatRG16Float,   // eImage_RG16F
      MTLPixelFormatRGBA16Float, // eImage_RGB16F
      MTLPixelFormatRGBA16Float, // eImage_RGBA16F
      MTLPixelFormatR32Float,    // eImage_I32F
      MTLPixelFormatRG32Float,   // eImage_RG32F
      MTLPixelFormatRGBA32Float, // eImage_RGB32F
      MTLPixelFormatRGBA32Float, // eImage_RGBA32F
      MTLPixelFormatInvalid,     // eImage_RGBA4
      MTLPixelFormatInvalid,     // eImage_RGB565
      MTLPixelFormatInvalid,     // eImage_DXT1
      MTLPixelFormatInvalid,     // eImage_DXT3
      MTLPixelFormatInvalid,     // eImage_DXT5
      MTLPixelFormatInvalid,     // eImage_D16
      MTLPixelFormatInvalid,     // eImage_D24
  };

  MTLPixelFormat srcFormat = srcFormats[_format.getFormatEnum()];

  MTKView *view = (__bridge MTKView *)m_pGraphicsContext;

  id<MTLTexture> texture = textureContext->yTexture;

  // Create Metal texture if it doesn's exist yet
  if (texture == nil)
  {

    MTLTextureDescriptor *textureDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:srcFormat
                                                           width:_width
                                                          height:_height
                                                       mipmapped:_mipMapped];
    texture = [view.device newTextureWithDescriptor:textureDescriptor];
    textureContext->yTexture = texture;
  }

  MTLRegion region = {
      {0, 0, 0},           // Origin
      {_width, _height, 1} // Size
  };
  if (_format.isCompressed())
  {
    //@TODO: Implement compressed textures if needed
    g_Log->Error("Compressed texture are currently unsupported on Metal.");
    return false;
  }
  else
  {
    [texture replaceRegion:region
               mipmapLevel:_mipMapLevel
                 withBytes:_data
               bytesPerRow:_bytesPerRow];
  }

  m_bDirty = true;

  return true;
}

/*
 */
bool CTextureFlatMetal::Bind(const uint32 _index)
{
  m_pRenderer->SetBoundSlot(_index, this);
  return true;
}

/*
 */
bool CTextureFlatMetal::Unbind(const uint32 _index) { return true; }

bool CTextureFlatMetal::GetYUVMetalTextures(CVMetalTextureRef *_outYTexture,
                                            CVMetalTextureRef *_outUVTexture)
{
  if (m_spBoundFrame == NULL)
    return false;
  MTKView *view = (__bridge MTKView *)m_pGraphicsContext;
  const uint32_t kAVFrameDataPixelBufferIndex = 3;
  if (m_spBoundFrame->Frame() == NULL)
  {
    g_Log->Error("THIS SHOULDN'T BE NULL");
  }
  CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)m_spBoundFrame->Frame()
                                     ->data[kAVFrameDataPixelBufferIndex];
  m_pRenderer->CreateMetalTextureFromDecoderFrame(pixelBuffer, _outYTexture, 0);
  m_pRenderer->CreateMetalTextureFromDecoderFrame(pixelBuffer, _outUVTexture,
                                                  1);
  return true;
}

id<MTLTexture> CTextureFlatMetal::GetRGBMetalTexture()
{
  TextureFlatMetalContext *textureContext =
      (__bridge TextureFlatMetalContext *)m_pTextureContext;
  return textureContext->yTexture;
}

CVMetalTextureRef CTextureFlatMetal::GetCVMetalTextureRef()
{
  TextureFlatMetalContext *textureContext =
      (__bridge TextureFlatMetalContext *)m_pTextureContext;
  return textureContext->decoderFrameYTextureRef;
}

bool CTextureFlatMetal::BindFrame(ContentDecoder::spCVideoFrame _spFrame)
{
  m_Flags |= eTextureFlags::TEXTURE_YUV;
  m_spBoundFrame = _spFrame;
  if (m_spBoundFrame->Frame() == NULL)
  {
    g_Log->Error("THIS SHOULDN'T BE NULL");
  }
  return true;
}

void CTextureFlatMetal::ReleaseMetalTexture()
{
  TextureFlatMetalContext *textureContext =
      (__bridge TextureFlatMetalContext *)m_pTextureContext;

  textureContext->decoderFrameYTextureRef = NULL;
  textureContext->decoderFrameUVTextureRef = NULL;
  textureContext->yTexture = nil;
  textureContext->uvTexture = nil;
}

} // namespace DisplayOutput

#endif /*USE_METAL*/
