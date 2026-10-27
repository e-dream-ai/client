#import "TextMetal.h"

#include <cstdio>
#include <string>

#include "Player.h"
#include "base.h"

namespace DisplayOutput
{

CTextMetal::CTextMetal(spCFontMetal _font, MTKView* _view)
    : m_spFont(_font), m_View(_view)
{
    CATextLayer* textLayer = [[CATextLayer alloc] init];
    m_TextLayer = textLayer;

    textLayer.font =
        (__bridge CFTypeRef) @(_font->FontDescription().TypeFace().c_str());
    textLayer.string = @"";
    textLayer.frame = CGRectMake(0, 0, 100, 200);
    textLayer.fontSize = _font->FontDescription().Height();
    textLayer.shadowOpacity = 1;
    textLayer.shadowColor = NSColor.blackColor.CGColor;
    textLayer.foregroundColor = NSColor.whiteColor.CGColor;
    textLayer.shadowOffset = NSMakeSize(1, -1);
    textLayer.shadowRadius = 0;

    NSDictionary* noAnimActions = @{
        @"position" : [NSNull null],
        @"bounds" : [NSNull null],
        @"contents" : [NSNull null],
        @"sublayers" : [NSNull null],
        @"hidden" : [NSNull null],
        @"foregroundColor" : [NSNull null],
        @"string" : [NSNull null],
    };
    textLayer.actions = noAnimActions;
    textLayer.hidden = YES;
    UpdateContentsScale();

#ifdef SCREEN_SAVER
    [[_view.layer.sublayers objectAtIndex:0] addSublayer:textLayer];
#else
    [_view.layer addSublayer:textLayer];
#endif
}

CTextMetal::~CTextMetal()
{
    [m_TextLayer removeFromSuperlayer];
}

void CTextMetal::SetText(const std::string& _text)
{
    m_Text = _text;
    m_TextLayer.string = @(_text.c_str());
    UpdateContentsScale();
    NSSize contentSize = [m_TextLayer preferredFrameSize];
    m_TextLayer.frame = NSMakeRect(0, 0, contentSize.width, contentSize.height);
    UpdateExtents();
}

void CTextMetal::SetRect(const Base::Math::CRect& _rect)
{
    CBaseText::SetRect(_rect);

    UpdateContentsScale();
    NSSize contentSize = [m_TextLayer preferredFrameSize];
    NSRect parentFrame = m_TextLayer.superlayer.frame;
    // Snap the origin to whole device pixels so glyphs aren't resampled.
    CGFloat scale = m_TextLayer.contentsScale;
    CGFloat x = _rect.m_X0 * parentFrame.size.width;
    CGFloat y = ((1 - _rect.m_Y0) - m_Extents.m_Y) * parentFrame.size.height;
    m_TextLayer.frame = NSMakeRect(round(x * scale) / scale,
                                   round(y * scale) / scale,
                                   contentSize.width, contentSize.height);
}

Base::Math::CVector2 CTextMetal::GetExtent()
{
    return m_Extents;
}

void CTextMetal::SetEnabled(bool _enabled)
{
    if (_enabled != m_Enabled)
    {
        m_TextLayer.hidden = !_enabled;
        m_Enabled = _enabled;
    }
}

void CTextMetal::SetColor(const Base::Math::CVector4& _color)
{
    if (m_Color == _color)
        return;

    m_Color = _color;
    CGColorRef color = CGColorCreateGenericRGB(_color.m_X, _color.m_Y, _color.m_Z, _color.m_W);
    m_TextLayer.foregroundColor = color;
    CGColorRelease(color);
}

// CATextLayer rasterizes at contentsScale 1.0 by default, which is blurry on
// Retina and gets much worse at small font sizes. Track the window's backing
// scale (it changes when the window moves between displays).
void CTextMetal::UpdateContentsScale()
{
    CGFloat scale = m_View.window ? m_View.window.backingScaleFactor
                                  : NSScreen.mainScreen.backingScaleFactor;
    if (scale > 0)
        m_TextLayer.contentsScale = scale;
}

void CTextMetal::UpdateExtents()
{
    NSRect frame = m_TextLayer.frame;
    NSRect parentFrame = m_TextLayer.superlayer.frame;
    if (parentFrame.size.width > 0 && parentFrame.size.height > 0)
    {
        m_Extents = {(float)frame.size.width / (float)parentFrame.size.width,
                     (float)frame.size.height / (float)parentFrame.size.height};
    }
}

} // namespace DisplayOutput
