#ifndef _FONTMETAL_H_
#define _FONTMETAL_H_

#import <CoreFoundation/CoreFoundation.h>

#include <boost/thread.hpp>

#include "DisplayOutput.h"
#include "Font.h"
#include "SmartPtr.h"
#include "TextRendering/MBEFontAtlas.h"
#include "TextureFlatMetal.h"
#include "base.h"

namespace DisplayOutput
{

/*
        CFontMetal.

*/
class CFontMetal : public CBaseFont
{
    CFTypeRef m_pFontAtlas;
    spCTextureFlatMetal m_spAtlasTexture;
    std::string m_typeFace;

  public:
    CFontMetal(CFontDescription& _desc, spCTextureFlat _textTexture);
    virtual ~CFontMetal();
    virtual bool Create();
    MBEFontAtlas* GetAtlas() const;
    spCTextureFlatMetal GetAtlasTexture() { return m_spAtlasTexture; }
};

MakeSmartPointers(CFontMetal);

}; // namespace DisplayOutput

#endif
