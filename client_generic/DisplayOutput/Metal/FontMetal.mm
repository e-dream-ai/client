#ifdef USE_METAL
#include <fstream>
#include <iostream>

#include "FontMetal.h"
#include "Log.h"
#include "Settings.h"
#include "TextMetal.h"

#define GENERATE_FONT_ATLAS 0

namespace DisplayOutput
{

const uint32_t kFontAtlasSize = 4096;

/*
        CFontMetal().

*/
CFontMetal::CFontMetal(CFontDescription &_desc, spCTextureFlat _textTexture)
    : CBaseFont()
{
    m_spAtlasTexture =
        std::dynamic_pointer_cast<CTextureFlatMetal>(_textTexture);
    m_typeFace = _desc.TypeFace();
}

bool CFontMetal::Create()
{
#if !USE_SYSTEM_UI
    NSString *typeFace = @(m_typeFace.c_str());
    NSError *error;
#if GENERATE_FONT_ATLAS
    NSString *fileName = [NSString stringWithFormat:@"%@.sddf", typeFace];
    NSFont *font = [NSFont fontWithName:typeFace size:24];
    MBEFontAtlas *atlas = [[MBEFontAtlas alloc] initWithFont:font
                                                 textureSize:kFontAtlasSize];
    NSData *data = [NSKeyedArchiver archivedDataWithRootObject:atlas
                                         requiringSecureCoding:NO
                                                         error:&error];
    if (error)
    {
        g_Log->Error("Error while archiving font file:%s",
                     error.localizedDescription.UTF8String);
        return false;
    }
    [data writeToFile:fileName atomically:YES];
#else  /*GENERATE_FONT_ATLAS*/
    NSString *fileName = [[NSBundle mainBundle] pathForResource:typeFace
                                                         ofType:@"sddf"];
    NSData *data = [NSData dataWithContentsOfFile:fileName];
    if (!data)
    {
        g_Log->Error(
            "Font atlas doesn't exist in the resources. Choose another "
            "font or create a font atlas for it.");
        return false;
    }
    NSArray *allowedClasses = @[
        MBEFontAtlas.class, NSString.class, NSData.class, NSArray.class,
        MBEGlyphDescriptor.class
    ];
    MBEFontAtlas *atlas = [NSKeyedUnarchiver
        unarchivedObjectOfClasses:[NSSet setWithArray:allowedClasses]
                         fromData:data
                            error:&error];
    if (error)
    {
        g_Log->Error("Error while reading font file:%s",
                     error.localizedDescription.UTF8String);
        return false;
    }
#endif /*GENERATE_FONT_ATLAS*/
    m_pFontAtlas = CFBridgingRetain(atlas);
    m_spAtlasTexture->Upload((uint8_t *)GetAtlas().textureData.bytes, eImage_I8,
                             kFontAtlasSize, kFontAtlasSize, kFontAtlasSize,
                             false, 0);
#endif /*!USE_SYSTEM_UI*/
    return true;
}

/*
        ~CFontMetal().

*/
CFontMetal::~CFontMetal()
{
#if !USE_SYSTEM_UI
    CFBridgingRelease(m_pFontAtlas);
#endif
}

MBEFontAtlas *CFontMetal::GetAtlas() const
{
#if !USE_SYSTEM_UI
    return (__bridge MBEFontAtlas *)m_pFontAtlas;
#else
    return nullptr;
#endif
}

};     // namespace DisplayOutput
#endif // USE_METAL
