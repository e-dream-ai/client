#ifndef	_FONTMETAL_H_
#define _FONTMETAL_H_

#include	<boost/thread.hpp>
#include	"base.h"
#include	"SmartPtr.h"
#include	"Font.h"
#include    "DisplayOutput.h"
#include    "TextureFlatMetal.h"
#include    "TextRendering/MBEFontAtlas.h"

namespace	DisplayOutput
{

/*
	CFontMetal.

*/
class	CFontMetal :	public CBaseFont
{
    MBEFontAtlas* m_pFontAtlas;
    spCTextureFlatMetal m_spAtlasTexture;
    std::string m_typeFace;
public:
    CFontMetal(CFontDescription& _desc, spCTextureFlat _textTexture);
    virtual ~CFontMetal();
    virtual bool Create();
    MBEFontAtlas* GetAtlas() const { return m_pFontAtlas; }
    spCTextureFlatMetal GetAtlasTexture() { return m_spAtlasTexture; }
};

MakeSmartPointers( CFontMetal );

};

#endif
