#ifndef _FRAMEDISPLAY_H_
#define _FRAMEDISPLAY_H_

#include "base.h"
#include "Rect.h"
#include "Settings.h"
#include "Renderer.h"
#include "TextureFlat.h"
#include "Vector4.h"
#include "Timer.h"
#include "ContentDecoder.h"

#ifdef MAC
#include <CoreVideo/CVPixelBuffer.h>
#endif
// #ifndef FRAME_DIAG
// #define FRAME_DIAG
// #endif

/**
    CFrameDisplay().
    Basic display handling, simply blits texture.
*/
class CFrameDisplay
{
    double m_LastTexMoveClock;
    float m_CurTexMoveOff;
    float m_CurTexMoveDir;
    const double TEX_MOVE_SECS = 60.f * 30.f; // 30 minutes

  protected:
    using spCTextureFlat = DisplayOutput::spCTextureFlat;
    ContentDecoder::sFrameMetadata m_MetaData;

    DisplayOutput::spCShader m_spShader;
    spCTextureFlat m_spVideoTexture;

    //	Dimensions of the display surface.
    Base::Math::CRect m_dispSize;

    //  texture Rect
    Base::Math::CRect m_texRect;
    Base::CTimer m_Timer;

    bool m_bPreserveAR;

    bool m_bValid;

  public:
    CFrameDisplay(DisplayOutput::spCRenderer _spRenderer)
    {
        m_bValid = true;
        m_bPreserveAR = g_Settings()->Get("settings.player.preserve_AR", false);
        m_texRect = Base::Math::CRect(1, 1);
        m_LastTexMoveClock = -1;
        m_CurTexMoveOff = 0;
        m_CurTexMoveDir = 1.;
        m_spShader = _spRenderer->NewShader(
            "quadPassVertex", "drawDecodedFrameNoBlendingFragment");
    }

    bool Valid() { return m_bValid; };

    //
    void SetDisplaySize(const uint32_t _w, const uint32_t _h)
    {
        m_dispSize = Base::Math::CRect(_w, _h);
        m_CurTexMoveOff = 0.f;
    }

    virtual spCTextureFlat& RequestTargetTexture() { return m_spVideoTexture; }

    virtual uint32_t StartAtFrame() const { return 0; }

    //	Decode a frame, and render it.
    virtual bool Draw(DisplayOutput::spCRenderer _spRenderer, float _alpha,
                      [[maybe_unused]] double _interframeDelta)
    {
        if (!m_spVideoTexture)
            return false;

        _spRenderer->SetShader(m_spShader);
        //    Bind texture and render a quad covering the screen.
        _spRenderer->SetBlend("alphablend");
        _spRenderer->SetTexture(m_spVideoTexture, 0);
        _spRenderer->Apply();

        ScrollVideoForNonMatchingAspectRatio(m_spVideoTexture->GetRect());

        _spRenderer->DrawQuad(m_texRect, Base::Math::CVector4(1, 1, 1, _alpha),
                              m_spVideoTexture->GetRect());

        return true;
    }

    virtual double GetFps(double /*_decodeFps*/, double _displayFps)
    {
        return _displayFps;
    }

    virtual void
    ScrollVideoForNonMatchingAspectRatio(const Base::Math::CRect& texDim)
    {
        m_texRect.m_X0 = 0.f;
        m_texRect.m_Y0 = 0.f;
        m_texRect.m_X1 = 1.f;
        m_texRect.m_Y1 = 1.f;

        if (!m_bPreserveAR)
            return;

        bool landscape = true;

        float r1 = (float)m_dispSize.Width() / (float)m_dispSize.Height();
        float r2 = texDim.Width() / texDim.Height();

        if (r2 > r1)
        {
            r1 = 1.f / r1;
            r2 = 1.f / r2;
            landscape = false;
        }

        float bars = (r1 - r2) / (2 * r1);

        if (bars > 0)
        {
            double acttm = m_Timer.Time();

            if (m_LastTexMoveClock < 0)
            {
                m_LastTexMoveClock = acttm;
            }

            if ((acttm - m_LastTexMoveClock) > TEX_MOVE_SECS)
            {
                m_CurTexMoveOff += bars / 20.f * m_CurTexMoveDir;
                m_LastTexMoveClock = acttm;
            }

            float* a;
            float* b;

            if (landscape)
            {
                a = &m_texRect.m_X0;
                b = &m_texRect.m_X1;
            }
            else
            {
                a = &m_texRect.m_Y0;
                b = &m_texRect.m_Y1;
            }

            *a += bars + m_CurTexMoveOff;
            *b -= bars - m_CurTexMoveOff;

            if (*a <= 0.f)
            {
                *b += -*a;
                *a += -*a;
                m_CurTexMoveOff -= bars / 20.f * m_CurTexMoveDir;
                m_CurTexMoveDir = -m_CurTexMoveDir;
            }

            if (*b >= 1.f)
            {
                *a -= *b - 1.f;
                *b -= *b - 1.f;
                m_CurTexMoveOff -= bars / 20.f * m_CurTexMoveDir;
                m_CurTexMoveDir = -m_CurTexMoveDir;
            }
        }
    }
};

MakeSmartPointers(CFrameDisplay);

#endif
