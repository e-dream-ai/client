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
#include <unordered_map>

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
    std::unordered_map<DisplayOutput::CRenderer*, DisplayOutput::spCShader> m_shaderByRenderer;
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
        if (_spRenderer && m_spShader)
            m_shaderByRenderer[_spRenderer.get()] = m_spShader;
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

        DisplayOutput::spCShader shader = m_spShader;
        if (_spRenderer)
        {
            auto it = m_shaderByRenderer.find(_spRenderer.get());
            if (it != m_shaderByRenderer.end())
            {
                shader = it->second;
            }
            else
            {
                shader = _spRenderer->NewShader("quadPassVertex", "drawDecodedFrameNoBlendingFragment");
                if (shader)
                    m_shaderByRenderer[_spRenderer.get()] = shader;
            }
        }

        _spRenderer->SetShader(shader);
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

    // Virtual method for seamless transition frame inheritance
    virtual void InheritFramesFrom(CFrameDisplay* previous) {
        // Base implementation does nothing (for normal display mode)
    }

    virtual void
    ScrollVideoForNonMatchingAspectRatio([[maybe_unused]] const Base::Math::CRect& texDim)
    {
        m_texRect.m_X0 = 0.f;
        m_texRect.m_Y0 = 0.f;
        m_texRect.m_X1 = 1.f;
        m_texRect.m_Y1 = 1.f;

        if (!m_bPreserveAR)
            return;

        const float dispW = static_cast<float>(m_dispSize.Width());
        const float dispH = static_cast<float>(m_dispSize.Height());
        if (dispW <= 0.f || dispH <= 0.f)
            return;

        const float targetAspect = 16.0f / 9.0f;
        const float displayAspect = dispW / dispH;

        // Preserve AR ON means fit to a fixed 16:9 viewport and let cleared
        // background show as black bars outside the viewport.
        if (displayAspect > targetAspect)
        {
            const float widthScale = targetAspect / displayAspect;
            const float xPad = (1.f - widthScale) * 0.5f;
            m_texRect.m_X0 = xPad;
            m_texRect.m_X1 = 1.f - xPad;
        }
        else if (displayAspect < targetAspect)
        {
            const float heightScale = displayAspect / targetAspect;
            const float yPad = (1.f - heightScale) * 0.5f;
            m_texRect.m_Y0 = yPad;
            m_texRect.m_Y1 = 1.f - yPad;
        }
    }
};

MakeSmartPointers(CFrameDisplay);

#endif
