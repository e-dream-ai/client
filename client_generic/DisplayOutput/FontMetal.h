#ifndef	_FONTMETAL_H_
#define _FONTMETAL_H_

#include	<boost/thread.hpp>
#include	"base.h"
#include	"SmartPtr.h"
#include	"Font.h"
#include	"Image.h"
#include	"TextureFlat.h"

namespace	DisplayOutput
{

/*
	CFontMetal.

*/
class	CFontMetal :	public CBaseFont
{
public:
	typedef struct
	{
		fp4 tex_x1, tex_y1, tex_x2;
		fp4 advance;
	} Glyph;

private:
	Glyph*				m_glyphs;

	Glyph*				m_table[256];
	
	fp4					m_lineHeight;
	
	fp4					m_texLineHeight;

	spCImage			m_spTextImage;
	
	spCTextureFlat		m_spTextTexture;

	public:
			CFontMetal( spCTextureFlat textTexture );
			virtual ~CFontMetal();

			bool	Create( void );
			
			fp4 LineHeight( void ) const;
			
			fp4 TexLineHeight( void ) const;
			
			fp4 CharWidth( uint8 c ) const;
			
			fp4 StringWidth( const std::string& str ) const;
			
			Glyph *GetGlyph( uint8 c );
			
			spCTextureFlat GetTexture( void );
	
			virtual void	Reupload();

};

MakeSmartPointers( CFontMetal );

};

#endif
