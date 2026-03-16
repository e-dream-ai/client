#ifndef __TEXTMETAL_H_
#define __TEXTMETAL_H_

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CATextLayer.h>

#include <string>

#include "FontMetal.h"
#include "Rect.h"
#include "SmartPtr.h"
#include "Text.h"
#include "Vector4.h"
#include "base.h"

namespace DisplayOutput
{

/*
 CTextMetal.

*/
class CTextMetal : public CBaseText
{
    std::string m_Text;
    spCFontMetal m_spFont;
    CATextLayer* m_TextLayer;
    Base::Math::CVector2 m_Extents;
    bool m_Enabled = false;
    Base::Math::CVector4 m_Color;

    void UpdateExtents();

  public:
    CTextMetal(spCFontMetal _font, MTKView* _view);
    virtual ~CTextMetal();
    virtual void SetText(const std::string& _text);
    virtual Base::Math::CVector2 GetExtent();
    virtual void SetRect(const Base::Math::CRect& _rect);
    virtual void SetEnabled(bool _enabled);
    void SetColor(const Base::Math::CVector4& _color);
};

MakeSmartPointers(CTextMetal);

} // namespace DisplayOutput

#endif /*__TEXTMETAL_H_*/
