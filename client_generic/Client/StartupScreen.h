#ifndef _STARTUPSCREEN_H_
#define _STARTUPSCREEN_H_

#include "Hud.h"
#include "Rect.h"
#include <algorithm>

namespace Hud
{

class CStartupScreen : public CHudEntry
{
    DisplayOutput::spCImage m_spImageRef;
    DisplayOutput::spCTextureFlat m_spVideoTexture;

    float m_Alpha = 1.0f;
    bool m_IsFading = false;
    double m_FadeStartTime = 0.0;
    static constexpr double kFadeDuration = 5.0;

  public:
    CStartupScreen(Base::Math::CRect _rect) : CHudEntry(_rect)
    {
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
    }

    void StartFadeOut(double currentTime)
    {
        m_IsFading = true;
        m_FadeStartTime = currentTime;
    }

    bool IsFadingOut() const { return m_IsFading; }
    bool IsFullyFaded() const { return m_Alpha <= 0.0f; }

    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        if (m_IsFading)
        {
            double fadeProgress = (_time - m_FadeStartTime) / kFadeDuration;
            m_Alpha = std::max(0.0f, 1.0f - static_cast<float>(fadeProgress));
            if (m_Alpha <= 0.0f)
                return false;
        }

        CHudEntry::Render(_time, _spRenderer);

        if (!m_spImageRef || !_spRenderer)
            return true;

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

        // Recompute aspect each frame so the logo stays circular after the
        // window is resized (e.g. F-key fullscreen toggle).
        const float aspect = _spRenderer->Display()->Aspect();
        Base::Math::CRect rr;
        rr.m_X0 = 0.5f - 0.2f * aspect;
        rr.m_Y0 = 0.5f - 0.2f;
        rr.m_X1 = 0.5f + 0.2f * aspect;
        rr.m_Y1 = 0.5f + 0.2f;

        _spRenderer->DrawQuad(rr, Base::Math::CVector4(1, 1, 1, m_Alpha),
                              m_spVideoTexture->GetRect());

        return true;
    }
};

MakeSmartPointers(CStartupScreen);

}; // namespace Hud
#endif
