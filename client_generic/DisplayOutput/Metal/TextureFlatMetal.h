#ifndef _TEXTUREFLATMETAL_H
#define _TEXTUREFLATMETAL_H

#include "TextureFlat.h"
#include <CoreFoundation/CFBase.h>
#include <CoreVideo/CVMetalTextureCache.h>


namespace	DisplayOutput
{

class CRendererMetal;

/*
	CTextureFlatMetal.

*/
class CTextureFlatMetal : public CTextureFlat
{
    CGraphicsContext    m_pGraphicsContext;
    CFTypeRef           m_pTextureContext;
    CRendererMetal*     m_pRenderer;
    ContentDecoder::spCVideoFrame m_spBoundFrame;

	public:
            CTextureFlatMetal( CGraphicsContext _graphicsContext, const uint32 _flags, CRendererMetal* _pRenderer );
			virtual ~CTextureFlatMetal();

			bool	Upload( spCImage _spImage );
			bool	Bind( const uint32 _index );
			bool	Unbind( const uint32 _index );
            bool    BindFrame(ContentDecoder::spCVideoFrame _spFrame);
#ifdef __OBJC__
            bool GetMetalTextures(CVMetalTextureRef* _outYTexture, CVMetalTextureRef* _outUVTexture);
            CVMetalTextureRef GetCVMetalTextureRef();
            void ReleaseMetalTexture();
#endif
};

MakeSmartPointers( CTextureFlatMetal );

}

#endif
