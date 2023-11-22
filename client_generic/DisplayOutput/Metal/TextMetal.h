#ifndef	__TEXTMETAL_H_
#define	__TEXTMETAL_H_

#include	<string>
#include	"base.h"
#include	"SmartPtr.h"
#include    "Text.h"
#include    "FontMetal.h"

#import     <Metal/Metal.h>

#import     "TextRendering/MBETextMesh.h"


namespace	DisplayOutput
{

const CGFloat kMetalTextReferenceContextSize = 2048.;

/*
 CTextMetal.

*/
class CTextMetal : public CBaseText
{
    std::string m_Text;
    spCFontMetal m_spFont;
    MBETextMesh* m_pTextMesh;
    Base::Math::CRect m_AlignRect;
    id<MTLDevice>   m_Device;
    float m_ContextAspect;

public:
    CTextMetal(spCFontMetal _font, id<MTLDevice> _device, float _contextAspect);
    virtual ~CTextMetal();
    virtual void SetText(const std::string& _text);
    virtual Base::Math::CVector2 GetExtent() const;
    const MBETextMesh* GetTextMesh() const { return m_pTextMesh; }
    const spCFontMetal GetFont() const { return m_spFont; }
};

MakeSmartPointers( CTextMetal );

}

#endif /*__TEXTMETAL_H_*/
