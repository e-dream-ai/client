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


namespace	DisplayOutput
{


/*
*/
CTextureFlatMetal::CTextureFlatMetal( CGraphicsContext _graphicsContext, const uint32 _flags ) : CTextureFlat( _flags )
{
    m_GraphicsContext = _graphicsContext;
}

/*
*/
CTextureFlatMetal::~CTextureFlatMetal()
{
	
}

/*
*/
bool CTextureFlatMetal::Upload( spCImage _spImage )
{
    m_spImage = _spImage;

	if (m_spImage==NULL) return false;

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
    
    MTLTextureDescriptor *textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:srcFormat width:512 height:512 mipmapped:NO];
    MTKView* view = m_GraphicsContext;
    // Create a Metal texture
    id<MTLTexture> texture = [view.device newTextureWithDescriptor:textureDescriptor];
    
    //TODO: Probably need to set sampler descriptor here

    // Upload the texture data
    uint8_t *pSrc;
    uint32_t mipMapLevel = 0;
    uint32_t width = _spImage->GetWidth();
    uint32_t height = _spImage->GetHeight();

    while ((pSrc = _spImage->GetData(mipMapLevel)) != NULL)
    {
        MTLRegion region =
        {
           {0, 0, 0},  // Origin
           {width, height, 1}  // Size
        };
        if (format.isCompressed())
        {
            //TODO: Implement compressed textures if applicable
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

    // Unbind the texture (not directly equivalent to OpenGL)

}

/*
*/
bool	CTextureFlatMetal::Bind( const uint32 _index )
{
    /*
	glActiveTextureARB( GL_TEXTURE0 + _index );
	glEnable( m_TexTarget );
	glBindTexture( m_TexTarget, m_TexID );
	
	m_bDirty = false;
	
	VERIFYGL;
     */
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
