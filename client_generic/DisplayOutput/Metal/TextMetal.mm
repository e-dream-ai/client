#ifdef USE_METAL
#include <stdio.h>
#include <string>

#include "base.h"

 #import "TextMetal.h"

namespace DisplayOutput
{

CTextMetal::CTextMetal(spCFontMetal _font, id<MTLDevice> _device, float /*_contextAspect*/) :
    m_spFont(_font),
    m_pTextMesh(NULL),
    m_Device(_device),
    m_ContextAspect(9./16.)//@TODO: update me when resized, for now just going with 16:9
{
}

CTextMetal::~CTextMetal()
{
}

void CTextMetal::SetText(const std::string& _text)
{
    if (_text != m_Text)
    {
        m_Text = _text;
        NSString* text = @(_text.c_str());
        CGRect rect = CGRectMake(0, 0, kMetalTextReferenceContextSize * (1.f / m_ContextAspect), kMetalTextReferenceContextSize);
        CGFloat size = m_spFont->FontDescription().Height() * 6;
        MBEFontAtlas* atlas = m_spFont->GetAtlas();
        m_pTextMesh = [[MBETextMesh alloc] initWithString:text inRect:rect withFontAtlas:atlas atSize:size device:m_Device];
    }
}

Base::Math::CVector2 CTextMetal::GetExtent() const
{
    if (m_pTextMesh)
    {
        CGSize size = m_pTextMesh.textExtent;
        return { (fp4)(size.width / kMetalTextReferenceContextSize), (fp4)(size.height / kMetalTextReferenceContextSize) };
    }
    return { 0.f, 0.f };
}

}

#endif /*USE_METAL*/
