#ifndef _TEXTUREFLATMETAL_H
#define _TEXTUREFLATMETAL_H

#include "TextureFlat.h"

namespace	DisplayOutput
{

/*
	CTextureFlatMetal.

*/
class CTextureFlatMetal : public CTextureFlat
{
    CGraphicsContext m_GraphicsContext;

	public:
            CTextureFlatMetal( CGraphicsContext _graphicsContext, const uint32 _flags );
			virtual ~CTextureFlatMetal();

			bool	Upload( spCImage _spImage );
			bool	Bind( const uint32 _index );
			bool	Unbind( const uint32 _index );
};

MakeSmartPointers( CTextureFlatMetal );

}

#endif
