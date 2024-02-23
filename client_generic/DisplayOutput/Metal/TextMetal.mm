#import "TextMetal.h"

#include <cstdio>
#include <string>

#include "Player.h"
#include "base.h"

namespace DisplayOutput
{

inline void ExecuteOnMainThread(void (^_block)(void))
{
    if ([NSThread isMainThread])
    {
        _block();
    }
    else
    {
        dispatch_async(dispatch_get_main_queue(), _block);
    }
}

CTextMetal::CTextMetal(spCFontMetal _font, MTKView* _view,
                       float /*_contextAspect*/)
    : m_spFont(_font), m_pTextMesh(NULL), m_Device(_view.device)
#if !USE_SYSTEM_UI
      ,
      m_ContextAspect(
          9. /
          16.) //@TODO: update me when resized, for now just going with 16:9
#endif
{
#if USE_SYSTEM_UI
    __block spCFontMetal font = _font;
    __weak MTKView* view = _view;

    CATextLayer* textLayer = [[CATextLayer alloc] init];
    m_TextLayer = (__bridge CATextLayer*)CFBridgingRetain(textLayer);
    ExecuteOnMainThread(^{
        textLayer.font =
            (__bridge CFTypeRef) @(font->FontDescription().TypeFace().c_str());
        textLayer.string = @"";
        textLayer.frame = CGRectMake(0, 0, 100, 200);
        textLayer.fontSize = font->FontDescription().Height();
        textLayer.shadowOpacity = 1;
        textLayer.shadowColor = NSColor.blackColor.CGColor;
        textLayer.foregroundColor = NSColor.whiteColor.CGColor;
        textLayer.shadowOffset = NSMakeSize(1, -1);
        textLayer.shadowRadius = 0;
        [textLayer display];
        textLayer.autoresizingMask = NSViewMaxYMargin;
        [textLayer setHidden:YES];
#ifdef SCREEN_SAVER
        [[view.layer.sublayers objectAtIndex:0] addSublayer:textLayer];
#else
        [view.layer addSublayer:textLayer];
#endif
        [view setAutoresizesSubviews:NO];
        [view setContentHuggingPriority:NSLayoutPriorityRequired
                         forOrientation:NSLayoutConstraintOrientationVertical];
        [view
            setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                     forOrientation:
                                         NSLayoutConstraintOrientationVertical];
    });
#endif
}

CTextMetal::~CTextMetal()
{
#if USE_SYSTEM_UI
    __weak CATextLayer* textLayer = m_TextLayer;
    ExecuteOnMainThread(^{
        [textLayer removeFromSuperlayer];
        CFBridgingRelease((__bridge CFTypeRef)textLayer);
    });
#endif
}

void CTextMetal::SetText(const std::string& _text)
{
    if (_text != m_Text)
    {
        m_Text = _text;
#if USE_SYSTEM_UI
        __block NSString* str = @(_text.c_str());
        __weak CATextLayer* textLayer = m_TextLayer;
        ExecuteOnMainThread(^{
            if (g_Player().Stopped())
                return;
            textLayer.string = str;
            NSSize contentSize = [textLayer preferredFrameSize];
            textLayer.frame =
                NSMakeRect(0, 0, contentSize.width, contentSize.height);
        });
#else  /*USE_SYSTEM_UI*/
        NSString* text = @(_text.c_str());
        CGRect rect = CGRectMake(
            0, 0, kMetalTextReferenceContextSize * (1.f / m_ContextAspect),
            kMetalTextReferenceContextSize);
        CGFloat size = m_spFont->FontDescription().Height() * 3;
        MBEFontAtlas* atlas = m_spFont->GetAtlas();
        m_pTextMesh = [[MBETextMesh alloc] initWithString:text
                                                   inRect:rect
                                            withFontAtlas:atlas
                                                   atSize:size
                                                   device:m_Device];
#endif /*USE_SYSTEM_UI*/
    }
}
void CTextMetal::SetRect(const Base::Math::CRect& _rect)
{
    CBaseText::SetRect(_rect);
#if USE_SYSTEM_UI
    __block Base::Math::CRect rect = _rect;
    __block const Base::Math::CVector2& extents = m_Extents;
    __weak CATextLayer* textLayer = m_TextLayer;
    ExecuteOnMainThread(^{
        if (g_Player().Stopped())
            return;
        NSRect frame = textLayer.frame;
        NSRect parentFrame = textLayer.superlayer.frame;
        textLayer.frame = NSMakeRect(rect.m_X0 * parentFrame.size.width,
                                     ((1 - rect.m_Y0) - extents.m_Y) *
                                         parentFrame.size.height,
                                     frame.size.width, frame.size.height);
        [textLayer display];
    });
#endif
}

Base::Math::CVector2 CTextMetal::GetExtent()
{
#if USE_SYSTEM_UI
    __block Base::Math::CVector2& extents = m_Extents;
    __weak CATextLayer* textLayer = m_TextLayer;
    ExecuteOnMainThread(^{
        if (g_Player().Stopped())
            return;
        NSRect frame = textLayer.frame;
        NSRect parentFrame = textLayer.superlayer.frame;
        extents = {(float)frame.size.width / (float)parentFrame.size.width,
                   (float)frame.size.height / (float)parentFrame.size.height};
    });
    return extents;
#else  /*USE_SYSTEM_UI*/
    if (m_pTextMesh)
    {
        CGSize size = m_pTextMesh.textExtent;
        return {(float)(size.width / kMetalTextReferenceContextSize),
                (float)(size.height / kMetalTextReferenceContextSize)};
    }
    return {0.f, 0.f};
#endif /*USE_SYSTEM_UI*/
}

void CTextMetal::SetEnabled(bool _enabled)
{
    if (_enabled != m_Enabled)
    {
#if USE_SYSTEM_UI
        // if (m_Extents.m_X == 0.f)
        // return;
        __weak CATextLayer* textLayer = m_TextLayer;
        ExecuteOnMainThread(^{
            if (g_Player().Stopped())
                return;
            textLayer.hidden = !_enabled;
        });
#endif
        m_Enabled = _enabled;
    }
}

} // namespace DisplayOutput
