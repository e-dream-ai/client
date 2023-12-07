#ifndef __TEXTMETAL_H_
#define __TEXTMETAL_H_

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#import "TextRendering/MBETextMesh.h"

#include <string>

#include "FontMetal.h"
#include "Rect.h"
#include "SmartPtr.h"
#include "Text.h"
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
    NSTextField* m_TextField;
    float m_ContextAspect;
    Base::Math::CVector2 m_Extents;
    bool m_Enabled;

  public:
    CTextMetal(spCFontMetal _font, MTKView* _view, float _contextAspect);
    virtual ~CTextMetal();
    virtual void SetText(const std::string& _text);
    virtual Base::Math::CVector2 GetExtent();
    virtual void SetRect(const Base::Math::CRect& _rect);
    virtual void SetEnabled(bool _enabled);

  public:
    const MBETextMesh* GetTextMesh() const { return m_pTextMesh; }
    const spCFontMetal GetFont() const { return m_spFont; }
};

MakeSmartPointers(CTextMetal);

} // namespace DisplayOutput

#endif /*__TEXTMETAL_H_*/
