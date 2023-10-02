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
@property (nonatomic, nonnull) id<MTLTexture> texture;
@end

@implementation TextureFlatMetalContext
@end

namespace	DisplayOutput
{


/*
*/
CTextureFlatMetal::CTextureFlatMetal( CGraphicsContext _graphicsContext, const uint32 _flags, spCRendererMetal _renderer  )
    : CTextureFlat( _flags ), m_pGraphicsContext(_graphicsContext), m_spRenderer(_renderer)
{
    TextureFlatMetalContext* textureContext = [[TextureFlatMetalContext alloc] init];
    m_pTextureContext = CFBridgingRetain(textureContext);
}

/*
*/
CTextureFlatMetal::~CTextureFlatMetal()
{
    CFBridgingRelease(m_pTextureContext);
}

/*
*/
bool CTextureFlatMetal::Upload( spCImage _spImage )
{
    TextureFlatMetalContext* textureContext = (__bridge TextureFlatMetalContext*)m_pTextureContext;
    m_spImage = _spImage;

	if (m_spImage == NULL) return false;

	CImageFormat	format = _spImage->GetFormat();

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
    id<MTLTexture> texture = textureContext.texture;
    
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
        textureContext.texture = texture;
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
bool	CTextureFlatMetal::Bind( const uint32 _index )
{
    TextureFlatMetalContext* textureContext = (__bridge TextureFlatMetalContext*)m_pTextureContext;
    m_spRenderer->SetBoundSlot(_index, textureContext.texture);
	return true;
}

/*
*/
bool	CTextureFlatMetal::Unbind( const uint32 _index )
{
    /*
	glActiveTextureARB( GL_TEXTURE0 + _index );
	glBindTexture( m_TexTarget, 0 );
	glDisable( m_TexTarget );
	VERIFYGL;
     */
	return true;
}

}
