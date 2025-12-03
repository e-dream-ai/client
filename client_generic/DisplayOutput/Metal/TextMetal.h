#ifndef __TEXTMETAL_H_
#define __TEXTMETAL_H_

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CATextLayer.h>

#import "TextRendering/MBETextMesh.h"

#include <string>

#include "FontMetal.h"
#include "Rect.h"
#include "SmartPtr.h"
#include "Text.h"
#include "Vector4.h"
#include "base.h"

#define USE_SYSTEM_UI 1

namespace DisplayOutput
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
    id<MTLDevice> m_Device;
    CATextLayer* m_TextLayer;
#if !USE_SYSTEM_UI
    float m_ContextAspect;
#endif
    Base::Math::CVector2 m_Extents;
    bool m_Enabled;
    Base::Math::CVector4 m_Color;

  public:
    CTextMetal(spCFontMetal _font, MTKView* _view, float _contextAspect);
    virtual ~CTextMetal();
    virtual void SetText(const std::string& _text);
    virtual Base::Math::CVector2 GetExtent();
    virtual void SetRect(const Base::Math::CRect& _rect);
    virtual void SetEnabled(bool _enabled);
    void SetColor(const Base::Math::CVector4& _color);

  public:
    const MBETextMesh* GetTextMesh() const { return m_pTextMesh; }
    const spCFontMetal GetFont() const { return m_spFont; }
};

MakeSmartPointers(CTextMetal);

} // namespace DisplayOutput

#endif /*__TEXTMETAL_H_*/
