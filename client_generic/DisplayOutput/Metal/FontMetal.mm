#ifdef USE_METAL
#include	"FontMetal.h"
#include	"Log.h"
#include	"Settings.h"
#include	<fstream>
#include	<iostream>

namespace	DisplayOutput
{

const uint32_t kFontAtlasSize = 2048;

/*
	CFontGL().

*/
CFontMetal::CFontMetal(CFontDescription& _desc, spCTextureFlat _textTexture) : CBaseFont()
{
    NSString* typeFace = [NSString stringWithUTF8String:_desc.TypeFace().c_str()];
    NSFont* font = [NSFont fontWithName:typeFace size:_desc.Height()];
    m_pFontAtlas = [[MBEFontAtlas alloc] initWithFont:font textureSize:kFontAtlasSize];
    m_spAtlasTexture = _textTexture;
    m_spAtlasTexture->Upload((uint8_t*)m_pFontAtlas.textureData.bytes, eImage_I8, kFontAtlasSize, kFontAtlasSize, kFontAtlasSize, false, 0);
}

/*
	~CFontGL().

*/
CFontMetal::~CFontMetal()
{
	
}

};
#endif //USE_METAL
