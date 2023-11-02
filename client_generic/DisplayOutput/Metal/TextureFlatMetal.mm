#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "base.h"
#include "Log.h"
#include "MathBase.h"
#include "Exception.h"
#include "DisplayOutput.h"
#include "RendererMetal.h"
#include "TextureFlatMetal.h"


@interface TextureFlatMetalContext : NSObject
{
@public id<MTLTexture> yTexture;
@public id<MTLTexture> uvTexture;
@public CVMetalTextureRef decoderFrameYTextureRef;
@public CVMetalTextureRef decoderFrameUVTextureRef;
@public IOSurfaceRef ioSurface;
}
@end

@implementation TextureFlatMetalContext
@end

namespace    DisplayOutput
{


/*
*/
CTextureFlatMetal::CTextureFlatMetal( CGraphicsContext _graphicsContext, const uint32 _flags, CRendererMetal* _pRenderer  )
    : CTextureFlat( _flags ), m_pGraphicsContext(_graphicsContext), m_pRenderer(_pRenderer)
{
    TextureFlatMetalContext* textureContext = [[TextureFlatMetalContext alloc] init]; //@TODO: check what all these textures are
    m_pTextureContext = CFBridgingRetain(textureContext);
    for(int i=0;i<4;++i)name[i]='A'+((char)std::rand()%22);name[4] = 0; //@TODO: cleanup
    m_spBoundFrame = NULL;
    g_Log->Error("TEXTURES:ALLOCATING TEXTURE:%s %i isnull:%i", name, m_spBoundFrame == NULL, m_spBoundFrame.IsNull());
}

/*
*/
CTextureFlatMetal::~CTextureFlatMetal()
{
    g_Log->Error("TEXTURES:DESTROYING TEXTURE:%s bound:%i isnull:%i", name, m_spBoundFrame == NULL, m_spBoundFrame.IsNull());
    CFBridgingRelease(m_pTextureContext);
}

/*
*/
bool CTextureFlatMetal::Upload( spCImage _spImage )
{
    TextureFlatMetalContext* textureContext = (__bridge TextureFlatMetalContext*)m_pTextureContext;
    m_spImage = _spImage;

    if (m_spImage == NULL) return false;

    CImageFormat    format = _spImage->GetFormat();

    static const MTLPixelFormat srcFormats[] =
    {
        MTLPixelFormatInvalid,      // eImage_None
        MTLPixelFormatA8Unorm,      // eImage_I8
        MTLPixelFormatRG8Unorm,     // eImage_IA8
        MTLPixelFormatRGBA8Unorm,   // eImage_RGB8
        MTLPixelFormatRGBA8Unorm,   // eImage_RGBA8
        MTLPixelFormatR16Unorm,     // eImage_I16
        MTLPixelFormatRG16Unorm,    // eImage_RG16
        MTLPixelFormatRGBA16Unorm,  // eImage_RGB16
        MTLPixelFormatRGBA16Unorm,  // eImage_RGBA16
        MTLPixelFormatR16Float,     // eImage_I16F
        MTLPixelFormatRG16Float,    // eImage_RG16F
        MTLPixelFormatRGBA16Float,  // eImage_RGB16F
        MTLPixelFormatRGBA16Float,  // eImage_RGBA16F
        MTLPixelFormatR32Float,     // eImage_I32F
        MTLPixelFormatRG32Float,    // eImage_RG32F
        MTLPixelFormatRGBA32Float,  // eImage_RGB32F
        MTLPixelFormatRGBA32Float,  // eImage_RGBA32F
        MTLPixelFormatInvalid,      // eImage_RGBA4
        MTLPixelFormatInvalid,      // eImage_RGB565
        MTLPixelFormatInvalid,      // eImage_DXT1
        MTLPixelFormatInvalid,      // eImage_DXT3
        MTLPixelFormatInvalid,      // eImage_DXT5
        MTLPixelFormatInvalid,      // eImage_D16
        MTLPixelFormatInvalid,      // eImage_D24
    };

    MTLPixelFormat srcFormat = srcFormats[ format.getFormatEnum() ];
    
    MTKView* view = (__bridge MTKView*)m_pGraphicsContext;
    
    uint32_t width = _spImage->GetWidth();
    uint32_t height = _spImage->GetHeight();
    id<MTLTexture> texture = textureContext->yTexture;
    
    //TODO: Probably need to set sampler descriptor here
    //Create Metal texture if it doesn's exist yet
    if (texture == nil)
    {
        bool mipMapped = _spImage->GetNumMipMaps() > 1;
        MTLTextureDescriptor *textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:srcFormat
                                                                                                     width:width
                                                                                                    height:height
                                                                                                 mipmapped:mipMapped];
        texture = [view.device newTextureWithDescriptor:textureDescriptor];
        textureContext->yTexture = texture;
    }
    
    // Upload the texture data
    uint8_t *pSrc;
    uint32_t mipMapLevel = 0;
    while ((pSrc = _spImage->GetData(mipMapLevel)) != NULL)
    {
        MTLRegion region =
        {
           {0, 0, 0},  // Origin
           {width, height, 1}  // Size
        };
        if (format.isCompressed())
        {
            //TODO: Implement compressed textures if needed
            g_Log->Error( "Compressed texture are currently unsupported on Metal." );
            return false;
        }
        else
        {
            uint32_t bytesPerRow = _spImage->GetPitch();
            [texture replaceRegion:region mipmapLevel:mipMapLevel withBytes:pSrc bytesPerRow:bytesPerRow];
        }

        mipMapLevel++;
    }

    m_bDirty = true;

    return true;
}

/*
*/
bool    CTextureFlatMetal::Bind( const uint32 _index )
{
    m_pRenderer->SetBoundSlot(_index, this);
    return true;
}

/*
*/
bool    CTextureFlatMetal::Unbind( const uint32 _index )
{
    /*
    glActiveTextureARB( GL_TEXTURE0 + _index );
    glBindTexture( m_TexTarget, 0 );
    glDisable( m_TexTarget );
    VERIFYGL;
     */
    return true;
}

bool CTextureFlatMetal::GetMetalTextures(CVMetalTextureRef* _outYTexture, CVMetalTextureRef* _outUVTexture)
{
    if (m_spBoundFrame == NULL)
        return false;
    TextureFlatMetalContext* textureContext = (__bridge TextureFlatMetalContext*)m_pTextureContext;
    MTKView* view = (__bridge MTKView*)m_pGraphicsContext;
    //if (textureContext->yTexture == nil)
    {
        const uint32_t kAVFrameDataPixelBufferIndex = 3;
        if (m_spBoundFrame->Frame() == NULL)
        {
            g_Log->Error("THIS SHOULDN'T BE NULL");
        }
        CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)m_spBoundFrame->Frame()->data[kAVFrameDataPixelBufferIndex];
        /*
        textureContext->ioSurface = CVPixelBufferGetIOSurface(pixelBuffer);
        
        uint32_t width = m_spBoundFrame->Width();
        uint32_t height= m_spBoundFrame->Height();
        bool mipMapped = false;
        
        
        MTLTextureDescriptor *yTextureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                                     width:width
                                                                                                    height:height
                                                                                                 mipmapped:mipMapped];
        MTLTextureDescriptor *uvTextureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
                                                                                                     width:width/2
                                                                                                    height:height/2
                                                                                                 mipmapped:mipMapped];
        textureContext->yTexture = [view.device newTextureWithDescriptor:yTextureDescriptor iosurface:textureContext->ioSurface plane:0];
        textureContext->uvTexture = [view.device newTextureWithDescriptor:uvTextureDescriptor iosurface:textureContext->ioSurface plane:1];
        *_outYTexture = textureContext->yTexture;
        *_outUVTexture = textureContext->uvTexture;
        
        IOSurfaceIncrementUseCount(textureContext->ioSurface);
        */
        m_pRenderer->CreateMetalTextureFromDecoderFrame(pixelBuffer, _outYTexture, 0);
        m_pRenderer->CreateMetalTextureFromDecoderFrame(pixelBuffer, _outUVTexture, 1);
        //textureContext->yTexture = CVMetalTextureGetTexture(textureContext->decoderFrameYTextureRef);
        //textureContext->uvTexture = CVMetalTextureGetTexture(textureContext->decoderFrameUVTextureRef);
        //*_outYTexture = textureContext->yTexture;
        //*_outUVTexture = textureContext->uvTexture;
        //g_Log->Info("Create Texture %p", this);
        //NSLog(@"yTex:%@", textureContext->yTexture);
        //CVPixelBufferRelease(pixelBuffer);
    }
    return true;
}

CVMetalTextureRef CTextureFlatMetal::GetCVMetalTextureRef()
{
    TextureFlatMetalContext* textureContext = (__bridge TextureFlatMetalContext*)m_pTextureContext;
    return textureContext->decoderFrameYTextureRef;
}

bool    CTextureFlatMetal::BindFrame(ContentDecoder::spCVideoFrame _spFrame)
{
    //ReleaseMetalTexture();
    m_spBoundFrame = _spFrame;
    if (m_spBoundFrame->Frame() == NULL)
    {
        g_Log->Error("THIS SHOULDN'T BE NULL");
    }
    return true;
}

void CTextureFlatMetal::ReleaseMetalTexture()
{
    TextureFlatMetalContext* textureContext = (__bridge TextureFlatMetalContext*)m_pTextureContext;
    
    
    textureContext->decoderFrameYTextureRef = NULL;
    textureContext->decoderFrameUVTextureRef = NULL;
    textureContext->yTexture = nil;
    textureContext->uvTexture = nil;
    
    //CVPixelBufferRelease(pixelBuffer);
    //CVBufferRelease(textureContext->decoderFrameYTextureRef);
    //CVBufferRelease(textureContext->decoderFrameUVTextureRef);
    //m_spBoundFrame = NULL;
    //return;
}

}
