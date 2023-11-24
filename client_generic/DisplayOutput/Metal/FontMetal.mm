#ifdef USE_METAL
#include	"FontMetal.h"
#include	"Log.h"
#include	"Settings.h"
#include	<fstream>
#include	<iostream>

#define GENERATE_FONT_ATLAS 0

namespace	DisplayOutput
{

const uint32_t kFontAtlasSize = 4096;

/*
	CFontMetal().

*/
CFontMetal::CFontMetal(CFontDescription& _desc, spCTextureFlat _textTexture) : CBaseFont()
{
    m_spAtlasTexture = _textTexture;
    m_typeFace = _desc.TypeFace();
}

bool CFontMetal::Create()
{
    NSString* typeFace = @(m_typeFace.c_str());
    NSError* error;
#if GENERATE_FONT_ATLAS
    NSString* fileName = [NSString stringWithFormat:@"%@.sddf", typeFace];
    NSFont* font = [NSFont fontWithName:typeFace size:24];
    MBEFontAtlas* atlas = [[MBEFontAtlas alloc] initWithFont:font textureSize:kFontAtlasSize];
    NSData* data = [NSKeyedArchiver archivedDataWithRootObject:atlas requiringSecureCoding:NO error:&error];
    if (error)
    {
        g_Log->Error("Error while archiving font file:%s", error.localizedDescription.UTF8String);
        return false;
    }
    [data writeToFile:fileName atomically:YES];
#else
    NSString* fileName = [[NSBundle mainBundle] pathForResource:typeFace ofType:@"sddf"];
    NSData* data = [NSData dataWithContentsOfFile:fileName];
    if (!data)
    {
        g_Log->Error("Font atlas doesn't exist in the resources. Choose another font or create a font atlas for it.");
        return false;
    }
    NSArray* allowedClasses = @[
        MBEFontAtlas.class,
        NSString.class,
        NSData.class,
        NSArray.class,
        MBEGlyphDescriptor.class
    ];
    MBEFontAtlas* atlas = [NSKeyedUnarchiver unarchivedObjectOfClasses:[NSSet setWithArray:allowedClasses] fromData:data error:&error];
    if (error)
    {
        g_Log->Error("Error while reading font file:%s", error.localizedDescription.UTF8String);
        return false;
    }
#endif
    m_pFontAtlas = CFBridgingRetain(atlas);
    m_spAtlasTexture->Upload((uint8_t*)GetAtlas().textureData.bytes, eImage_I8, kFontAtlasSize, kFontAtlasSize, kFontAtlasSize, false, 0);
    return true;
}

/*
	~CFontMetal().

*/
CFontMetal::~CFontMetal()
{
    CFBridgingRelease(m_pFontAtlas);
}

MBEFontAtlas* CFontMetal::GetAtlas() const
{
    return (__bridge MBEFontAtlas*)m_pFontAtlas;
}

};
#endif //USE_METAL
