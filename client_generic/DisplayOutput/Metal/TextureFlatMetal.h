#ifndef _TEXTUREFLATMETAL_H
#define _TEXTUREFLATMETAL_H

#include "TextureFlat.h"
#include <CoreFoundation/CFBase.h>


namespace	DisplayOutput
{

/*
	CTextureFlatMetal.

*/
class CTextureFlatMetal : public CTextureFlat
{
    CGraphicsContext    m_pGraphicsContext;
    CFTypeRef           m_pTextureContext;
    spCRendererMetal    m_spRenderer;

	public:
            CTextureFlatMetal( CGraphicsContext _graphicsContext, const uint32 _flags, spCRendererMetal _renderer );
			virtual ~CTextureFlatMetal();

			bool	Upload( spCImage _spImage );
			bool	Bind( const uint32 _index );
			bool	Unbind( const uint32 _index );
};

MakeSmartPointers( CTextureFlatMetal );

}

#endif
