#ifndef _STARTUPSCREEN_H_
#define _STARTUPSCREEN_H_

#include "Console.h"
#include "Hud.h"
#include "Rect.h"
#include <algorithm>
#include <sstream>

namespace Hud
{

class CStartupScreen : public CHudEntry
{
    std::string m_StartupMessage;
    DisplayOutput::CFontDescription m_Desc;
    DisplayOutput::spCBaseFont m_spFont;
    DisplayOutput::spCBaseText m_spText;
    DisplayOutput::spCImage m_spImageRef;
    DisplayOutput::spCTextureFlat m_spVideoTexture;

    Base::Math::CRect m_LogoSize;
    float m_MoveMessageCounter;

    float m_Alpha = 1.0f;
    bool m_IsFading = false;
    double m_FadeStartTime = 0.0;
    const double m_FadeDuration = 5.0; 
    bool m_TextureUploaded = false;
    
  public:
    CStartupScreen(Base::Math::CRect _rect, const std::string& _FontName,
                   const uint32_t _fontHeight)
        : CHudEntry(_rect)
    {
        DisplayOutput::CFontDescription fontDesc;

        m_Desc.AntiAliased(true);
        m_Desc.Height(_fontHeight);
        m_Desc.Style(DisplayOutput::CFontDescription::Normal);
        m_Desc.Italic(false);
        m_Desc.TypeFace(_FontName);
        // Renderer can be unavailable during early startup; lazily acquire font/text in Render().
        m_spFont = nullptr;
        m_StartupMessage =
            "Connecting to infinidream.ai...\nPress F1 for help.";
        m_spText = nullptr;
        m_spImageRef = std::make_shared<DisplayOutput::CImage>();
        m_spImageRef->Create(256, 256, DisplayOutput::eImage_RGBA8, false,
                             true);
        if (!m_spImageRef->Load(
                g_Settings()->Get("settings.app.InstallDir", PlatformUtils::GetWorkingDir()) +
                    "logo.png",
                false))
        {
            // Logo not found — skip rendering it rather than showing a white square.
            m_spImageRef = nullptr;
        }
        auto spDisplay = g_Player().Display();
        float aspect = (spDisplay && spDisplay->Width() != 0) ? spDisplay->Aspect() : 1.0f;
        m_LogoSize.m_X0 = 0.f;
        m_LogoSize.m_X1 = 0.2f * aspect;
        m_LogoSize.m_Y0 = 0.f;
        m_LogoSize.m_Y1 = 0.2f;

        m_spVideoTexture = NULL;
        m_MoveMessageCounter = 0.;
    }

    virtual ~CStartupScreen() { m_spVideoTexture = NULL; }

    void StartFadeOut(double currentTime) {
        m_IsFading = true;
        m_FadeStartTime = currentTime;
    }

    bool IsFadingOut() const { return m_IsFading; }
    bool IsFullyFaded() const { return m_Alpha <= 0.0f; }
    
    virtual void Visible(const bool _bState) override
    {
        CHudEntry::Visible(_bState);
        if (m_spText)
            m_spText->SetEnabled(_bState);
    }

    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        // Update fade if needed
        if (m_IsFading) {
            double fadeProgress = (_time - m_FadeStartTime) / m_FadeDuration;
            m_Alpha = std::max(0.0f, 1.0f - static_cast<float>(fadeProgress));
            
            if (m_Alpha <= 0.0f) {
                return false; // Skip rendering when fully transparent
            }
        }
        
        CHudEntry::Render(_time, _spRenderer);

        // Lazily init font/text once we have a renderer.
        if (_spRenderer && (!m_spFont || !m_spText))
        {
            if (!m_spFont)
                m_spFont = _spRenderer->GetFont(m_Desc);
            if (m_spFont && !m_spText)
                m_spText = _spRenderer->NewText(m_spFont, m_StartupMessage);
        }

        if (m_bServerMessageStartTimer == false)
        {
            m_bServerMessageStartTimer = true;
            m_ServerMessageStartTimer =
                boost::posix_time::second_clock::local_time();
        }

        // draw picture (only if logo loaded successfully)
        if (m_spImageRef)
        {
            // Create and upload the logo texture only once — the image never changes.
            if (!m_spVideoTexture)
            {
                m_spVideoTexture = _spRenderer->NewTextureFlat();
                m_spVideoTexture->Upload(m_spImageRef);
            }

            _spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
            _spRenderer->SetBlend("alphablend");
            _spRenderer->SetTexture(m_spVideoTexture, 0);
            _spRenderer->SetShader(NULL);
            _spRenderer->Apply();

            // Recompute aspect each frame so the logo stays circular after
            // the window is resized (e.g. F-key fullscreen toggle).  Caching
            // it in the constructor produced the wrong ratio after resize.
            const float currentAspect = _spRenderer->Display()->Aspect();
            Base::Math::CRect rr;
            rr.m_X0 = 0.5f - 0.2f * currentAspect;
            rr.m_Y0 = 0.5f - 0.2f + m_MoveMessageCounter;
            rr.m_X1 = 0.5f + 0.2f * currentAspect;
            rr.m_Y1 = 0.5f + 0.2f + m_MoveMessageCounter;

            _spRenderer->DrawQuad(rr, Base::Math::CVector4(1, 1, 1, m_Alpha),
                                  m_spVideoTexture->GetRect());
        }

        // draw text — centered near top of screen
        if (m_spText)
        {
            static constexpr float kHudReferenceHeight = 1080.f;
            const float screenH = static_cast<float>(_spRenderer->Display()->Height());
            const float screenW = static_cast<float>(_spRenderer->Display()->Width());
            const float edge = (float)m_Desc.Height() * (screenH / kHudReferenceHeight) / screenW;

            // GetExtent() measures up to the first \n, giving the width of the
            // longer first line — sufficient to horizontally center the block.
            Base::Math::CVector2 extent = m_spText->GetExtent();
            const float x0 = extent.m_X > 0.f ? 0.5f - extent.m_X * 0.5f : edge;

            m_spText->SetRect(Base::Math::CRect(x0, edge, x0 + extent.m_X + edge, edge + extent.m_Y * 3.0f));
            _spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader |
                               DisplayOutput::eBlend);
            _spRenderer->SetBlend("alphablend");
            _spRenderer->Apply();
            _spRenderer->DrawText(m_spText, Base::Math::CVector4(1, 1, 1, m_Alpha));
        }

        return true;
    }
};

MakeSmartPointers(CStartupScreen);

}; // namespace Hud
#endif
