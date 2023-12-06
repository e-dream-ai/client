#ifdef USE_METAL

#import "TextMetal.h"

#include <stdio.h>
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

CTextMetal::CTextMetal(spCFontMetal _font, MTKView *_view,
                       float /*_contextAspect*/)
    : m_spFont(_font), m_pTextMesh(NULL), m_Device(_view.device),
      m_ContextAspect(
          9. /
          16.) //@TODO: update me when resized, for now just going with 16:9
{
#if USE_SYSTEM_UI
    __block spCFontMetal font = _font;
    __block MTKView *view = _view;
    __block NSTextField *__strong &resultTextField = m_TextField;
    ExecuteOnMainThread(^{
        NSTextField *textField = [[NSTextField alloc] init];
        [textField setBezeled:NO];
        [textField setBordered:NO];
        [textField setBackgroundColor:NSColor.clearColor];
        [textField setEditable:NO];
        [textField setSelectable:NO];
        [textField
            setFont:[NSFont fontWithName:@(font->FontDescription()
                                               .TypeFace()
                                               .c_str())
                                    size:font->FontDescription().Height()]];
        [textField setTranslatesAutoresizingMaskIntoConstraints:YES];
        [textField setAutoresizingMask:NSViewMaxYMargin];
        [textField setHidden:YES];

        NSShadow *textShadow = [[NSShadow alloc] init];
        [textShadow setShadowColor:[NSColor blackColor]];
        [textShadow setShadowOffset:NSMakeSize(1, 1)];
        [textField setShadow:textShadow];

        [view setAutoresizesSubviews:NO];
        [view setContentHuggingPriority:NSLayoutPriorityRequired
                         forOrientation:NSLayoutConstraintOrientationVertical];
        [view
            setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                     forOrientation:
                                         NSLayoutConstraintOrientationVertical];

        [view addSubview:textField];

        resultTextField = (__bridge NSTextField *)CFBridgingRetain(textField);
    });
#endif
}

CTextMetal::~CTextMetal()
{
#if USE_SYSTEM_UI
    __block NSTextField *textField =
        CFBridgingRelease((__bridge CFTypeRef)m_TextField);
    ExecuteOnMainThread(^{
        [textField removeFromSuperview];
    });
#endif
}

void CTextMetal::SetText(const std::string &_text)
{
    if (_text != m_Text)
    {
        m_Text = _text;
#if USE_SYSTEM_UI
        __block NSString *str = [NSString stringWithUTF8String:_text.c_str()];
        ExecuteOnMainThread(^{
            if (g_Player().Stopped())
                return;
            [m_TextField setStringValue:str];
            NSSize contentSize =
                [m_TextField sizeThatFits:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
            [m_TextField setFrame:NSMakeRect(0, 0, contentSize.width,
                                             contentSize.height)];
        });
#else  /*USE_SYSTEM_UI*/
        NSString *text = @(_text.c_str());
        CGRect rect = CGRectMake(
            0, 0, kMetalTextReferenceContextSize * (1.f / m_ContextAspect),
            kMetalTextReferenceContextSize);
        CGFloat size = m_spFont->FontDescription().Height() * 3;
        MBEFontAtlas *atlas = m_spFont->GetAtlas();
        m_pTextMesh = [[MBETextMesh alloc] initWithString:text
                                                   inRect:rect
                                            withFontAtlas:atlas
                                                   atSize:size
                                                   device:m_Device];
#endif /*USE_SYSTEM_UI*/
    }
}
void CTextMetal::SetRect(const Base::Math::CRect &_rect)
{
    CBaseText::SetRect(_rect);
#if USE_SYSTEM_UI
    __block Base::Math::CRect rect = _rect;
    __block const Base::Math::CVector2 &extents = m_Extents;
    ExecuteOnMainThread(^{
        if (g_Player().Stopped())
            return;
        NSRect frame = m_TextField.frame;
        NSRect parentFrame = m_TextField.superview.frame;
        [m_TextField setFrame:NSMakeRect(rect.m_X0 * parentFrame.size.width,
                                         ((1 - rect.m_Y0) - extents.m_Y) *
                                             parentFrame.size.height,
                                         frame.size.width, frame.size.height)];
    });
#endif
}

Base::Math::CVector2 CTextMetal::GetExtent()
{
#if USE_SYSTEM_UI
    __block Base::Math::CVector2 &extents = m_Extents;
    ExecuteOnMainThread(^{
        if (g_Player().Stopped())
            return;
        NSRect frame = m_TextField.frame;
        NSRect parentFrame = m_TextField.superview.frame;
        extents = {(fp4)frame.size.width / (fp4)parentFrame.size.width,
                   (fp4)frame.size.height / (fp4)parentFrame.size.height};
    });
    return extents;
#else  /*USE_SYSTEM_UI*/
    if (m_pTextMesh)
    {
        CGSize size = m_pTextMesh.textExtent;
        return {(fp4)(size.width / kMetalTextReferenceContextSize),
                (fp4)(size.height / kMetalTextReferenceContextSize)};
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
        ExecuteOnMainThread(^{
            if (g_Player().Stopped())
                return;
            [m_TextField setHidden:!_enabled];
        });
#endif
        m_Enabled = _enabled;
    }
}

} // namespace DisplayOutput

#endif /*USE_METAL*/
