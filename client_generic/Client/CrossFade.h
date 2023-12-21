#ifndef _CROSSFADE_H_
#define _CROSSFADE_H_

#include "Hud.h"
#include "Rect.h"

namespace Hud
{

/*
        CFade.

*/
class CCrossFade : public CHudEntry
{
    DisplayOutput::spCTextureFlat m_spTexture;
    bool m_bSkipped;
    bool m_bSkipToNext;
    uint32_t m_currID;

  public:
    CCrossFade(uint32_t width, uint32_t height, bool skipToNext)
        : CHudEntry(Base::Math::CRect(float(width), float(height))),
          m_bSkipped(false), m_bSkipToNext(skipToNext)
    {
        DisplayOutput::spCImage tmpImage =
            std::make_shared<DisplayOutput::CImage>();

        tmpImage->Create(width, height, DisplayOutput::eImage_RGBA8, 0, false);

        for (uint32_t x = 0; x < width; x++)
        {
            for (uint32_t y = 0; y < height; y++)
            {
                tmpImage->PutPixel(static_cast<int32_t>(x),
                                   static_cast<int32_t>(y), 0, 0, 0, 255);
            }
        }

        m_spTexture = g_Player().Renderer()->NewTextureFlat();
        m_spTexture->Upload(tmpImage);
    }

    virtual ~CCrossFade() {}

    //	Override to make it always visible.
    virtual bool Visible() const { return true; };

    void Reset()
    {
        m_bSkipped = false;
        m_currID = g_Player().GetCurrentPlayingID();
    }

    virtual bool Render(const double _time,
                        DisplayOutput::spCRenderer _spRenderer)
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

        float alpha = float(m_Delta * 4.0f);

        if (alpha > 1.0f)
        {
            if (!m_bSkipped && m_bSkipToNext)
            {
                // skip to another only if we are still in the same sheep as at
                // the beginning.
                if (m_currID == g_Player().GetCurrentPlayingID())
                    g_Player().SkipToNext();

                m_bSkipped = true;
            }

            if (alpha > 2.0f)
                alpha = 1.0f - (alpha - 1.0f);
            else
                alpha = 1.0f;
        }

        spRenderer->DrawQuad(Base::Math::CRect(1, 1),
                             Base::Math::CVector4(1, 1, 1, alpha),
                             m_spTexture->GetRect());

        return true;
    }
};

MakeSmartPointers(CCrossFade);

}; // namespace Hud

#endif
