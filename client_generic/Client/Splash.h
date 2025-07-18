#ifndef _SPLASH_H_
#define _SPLASH_H_

#include "Hud.h"
#include "Rect.h"
#include "fastbez.h"

namespace Hud
{

/*
        CSplash.

*/
class CSplash : public CHudEntry
{
    DisplayOutput::spCTextureFlat m_spTexture;
    Base::Math::CFastBezier *m_pAlphaBezier, *m_pSizeBezier;

  public:
    CSplash(const float _size, const std::string& _texture)
        : CHudEntry(Base::Math::CRect(_size, _size))
    {
        DisplayOutput::spCImage tmpImage(new DisplayOutput::CImage());
        if (tmpImage->Load(_texture, false))
        {
            m_spTexture = g_Player().Renderer()->NewTextureFlat();
            m_spTexture->Upload(tmpImage);
        }

        //	Adjust aspect...
        float aspect = g_Player().Display()->Aspect();
        m_Rect.m_X0 = 0;
        m_Rect.m_X1 = _size * aspect;
        m_Rect.m_Y0 = 0;
        m_Rect.m_Y1 = _size;

        tmpImage = NULL;

        m_pAlphaBezier = new Base::Math::CFastBezier(0, 2, 1, 0);
        m_pSizeBezier = new Base::Math::CFastBezier(1.f, 0.65f, 0.95f, 2.5f);
    }

    virtual ~CSplash()
    {
        SAFE_DELETE(m_pAlphaBezier);
        SAFE_DELETE(m_pSizeBezier);
    }

    //	Override to make it always visible.
    virtual bool Visible() const { return true; };

    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        if (!CHudEntry::Render(_time, _spRenderer))
            return false;

        if (m_spTexture == NULL)
            return false;

        DisplayOutput::spCRenderer spRenderer = g_Player().Renderer();

        spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
        spRenderer->SetTexture(m_spTexture, 0);
        spRenderer->SetBlend("alphablend");
        spRenderer->SetShader(NULL);
        spRenderer->Apply();

        Base::Math::CRect r = m_Rect;
        const float s = m_pSizeBezier->Sample(float(m_Delta));
        r.m_X0 = 0.5f - (m_Rect.Width() * s);
        r.m_Y0 = 0.5f - (m_Rect.Height() * s);
        r.m_X1 = 0.5f + (m_Rect.Width() * s);
        r.m_Y1 = 0.5f + (m_Rect.Height() * s);

        spRenderer->DrawQuad(
            r,
            Base::Math::CVector4(
                1, 1, 1,
                Base::Math::saturate(m_pAlphaBezier->Sample(float(m_Delta)))),
            m_spTexture->GetRect());

        return true;
    }
};

MakeSmartPointers(CSplash);

class CSplashImage : public CHudEntry
{
    DisplayOutput::spCTextureFlat m_spTexture;
    float m_FadeinTime;
    float m_HoldTime;
    float m_FadeoutTime;

  public:
    CSplashImage(const float _size, const std::string& _texture,
                 float fadeintime, float holdtime, float fadeouttime)
        : CHudEntry(Base::Math::CRect(_size, _size))
    {
        DisplayOutput::spCImage tmpImage =
            std::make_shared<DisplayOutput::CImage>();
        if (tmpImage->Load(_texture, false))
        {
            m_spTexture = g_Player().Renderer()->NewTextureFlat();
            m_spTexture->Upload(tmpImage);
        }

        //	Adjust aspect...
        float aspect = g_Player().Display()->Aspect();
        m_Rect.m_X0 = 0;
        m_Rect.m_X1 = _size * aspect;
        m_Rect.m_Y0 = 0;
        m_Rect.m_Y1 = _size;

        tmpImage = NULL;

        m_FadeinTime = fadeintime;
        m_HoldTime = holdtime;
        m_FadeoutTime = fadeouttime;
    }

    virtual ~CSplashImage() {}

    //	Override to make it always visible.
    virtual bool Visible() const { return true; };

    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        if (!CHudEntry::Render(_time, _spRenderer))
            return false;

        if (m_spTexture == NULL)
            return false;

        DisplayOutput::spCRenderer spRenderer = g_Player().Renderer();

        spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
        spRenderer->SetTexture(m_spTexture, 0);
        spRenderer->SetBlend("alphablend");
        spRenderer->SetShader(NULL);
        spRenderer->Apply();

        Base::Math::CRect r = m_Rect;
        r.m_X0 = 0.f;
        r.m_Y0 = 0.f;
        r.m_X1 = 1.f;
        r.m_Y1 = 1.f;

        double alpha = 0.;

        double timepassed = fmod(_time, m_StartTime + m_FadeinTime +
                                            m_HoldTime + m_FadeoutTime);
        if (timepassed > m_StartTime + m_FadeinTime + m_HoldTime) // fadeout
        {
            alpha =
                1. - (timepassed - (m_StartTime + m_FadeinTime + m_HoldTime)) /
                         m_FadeoutTime;
        }
        else
        {
            if (timepassed > m_StartTime + m_FadeinTime) // hold
                alpha = 1.;
            else // fade in
                alpha = ((timepassed - m_StartTime) / m_FadeinTime);
        }

        spRenderer->DrawQuad(r, Base::Math::CVector4(1, 1, 1, float(alpha)),
                             m_spTexture->GetRect());

        return true;
    }
};
MakeSmartPointers(CSplashImage);
}; // namespace Hud

#endif
