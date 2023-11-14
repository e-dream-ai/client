#ifdef USE_METAL
#include <stdio.h>
#include <string>

#include "base.h"

 #import "TextMetal.h"

namespace DisplayOutput
{

CTextMetal::CTextMetal(spCFontMetal _font, id<MTLDevice> _device)
{
    m_spFont = _font;
    m_pTextMesh = NULL;
    m_Device = _device;
}

CTextMetal::~CTextMetal()
{
    
}

void CTextMetal::SetText(const std::string& _text, const Base::Math::CRect& _alignRect)
{
    if (_text != m_Text)
    {
        m_Text = _text;
        NSString* text = @(_text.c_str());
        CGRect rect = CGRectMake(0, 0, 1024, 1024); //@TODO: is this ok?
        CGFloat size = m_spFont->FontDescription().Height();
        MBEFontAtlas* atlas = m_spFont->GetAtlas();
        m_pTextMesh = [[MBETextMesh alloc] initWithString:text inRect:rect withFontAtlas:atlas atSize:size device:m_Device];
    }
}

}

#endif /*USE_METAL*/
