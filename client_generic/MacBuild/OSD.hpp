//
//  OSD.hpp
//  e-dream
//
//      Reusable OSD
//
//  Created by Guillaume Louel on 06/03/2024.
//

#ifndef OSD_hpp
#define OSD_hpp

#include <stdio.h>

#include "Hud.h"
#include "Rect.h"
#include "fastbez.h"
#include "Settings.h"
#include "Player.h"
#include "StringFormat.h"

namespace Hud {

// Helper to map Perceptual FPS to Speed
inline double PerceptualFPSToSpeed(double fps) {
    return (2 * log(fps)) / log(2) - 1;
}


enum OSDType {
    Speed,
    Brightness,
    Pause,
    Play,
    Previous,
    Next,
    Back10,
    Forward10,
    Like,
    Dislike
};

class COSD : public CHudEntry {
    
public:
    COSD(Base::Math::CRect rect, double minFps, double maxFps, double minBrightness, double maxBrightness) : CHudEntry(rect), minFps {minFps}, maxFps {maxFps}, minBrightness { minBrightness }, maxBrightness { maxBrightness} {
        
        // initialise currentvalues to mins, this gets updated by the render loop later on
        currentFps = minFps;
        currentBrightness = minBrightness;
        
#ifndef LINUX_GNU
        std::string defaultDir = PlatformUtils::GetWorkingDir();
#else
        std::string defaultDir = std::string("");
#endif
        
        // Grab our background texture
        DisplayOutput::spCImage tmpBg(new DisplayOutput::CImage());
        if (tmpBg->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-bg-700.png", false))
        {
            m_spBgTexture = g_Player().Renderer()->NewTextureFlat();
            m_spBgTexture->Upload(tmpBg);
        }

        // Grab our dot classic texture
        DisplayOutput::spCImage tmpDot(new DisplayOutput::CImage());
        if (tmpDot->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-pill.png", false))
        {
            m_spDotTexture = g_Player().Renderer()->NewTextureFlat();
            m_spDotTexture->Upload(tmpDot);
        }
        
        // Grab our dot unselected texture
        DisplayOutput::spCImage tmpDotU(new DisplayOutput::CImage());
        if (tmpDotU->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-pill-u.png", false))
        {
            m_spDotTexture_u = g_Player().Renderer()->NewTextureFlat();
            m_spDotTexture_u->Upload(tmpDotU);
        }
        
        // Grab our dot red texture
        DisplayOutput::spCImage tmpDotR(new DisplayOutput::CImage());
        if (tmpDotR->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-pill-r.png", false))
        {
            m_spDotTexture_r = g_Player().Renderer()->NewTextureFlat();
            m_spDotTexture_r->Upload(tmpDotR);
        }

        // Grab our dot blue texture
        DisplayOutput::spCImage tmpDotB(new DisplayOutput::CImage());
        if (tmpDotB->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-pill-b.png", false))
        {
            m_spDotTexture_b = g_Player().Renderer()->NewTextureFlat();
            m_spDotTexture_b->Upload(tmpDotB);
        }
        
        // Grab our symbols texture
        DisplayOutput::spCImage tmpSymbolSpeed(new DisplayOutput::CImage());
        if (tmpSymbolSpeed->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-speed.png", false))
        {
            m_spSymbolSpeedTexture = g_Player().Renderer()->NewTextureFlat();
            m_spSymbolSpeedTexture->Upload(tmpSymbolSpeed);
        }
        
        DisplayOutput::spCImage tmpSymbolBrightness(new DisplayOutput::CImage());
        if (tmpSymbolBrightness->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-brightness.png", false))
        {
            m_spSymbolBrightnessTexture = g_Player().Renderer()->NewTextureFlat();
            m_spSymbolBrightnessTexture->Upload(tmpSymbolBrightness);
        }

        // Mini hud + symbols (play, pause, back/forward 10s, prev/next, like/dislike
        DisplayOutput::spCImage tmpBgSq(new DisplayOutput::CImage());
        if (tmpBgSq->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-bg-sq.png", false))
        {
            m_spBgSqTexture = g_Player().Renderer()->NewTextureFlat();
            m_spBgSqTexture->Upload(tmpBgSq);
        }
        
        DisplayOutput::spCImage tmpPlay(new DisplayOutput::CImage());
        if (tmpPlay->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-play.png", false))
        {
            m_spPlayTexture = g_Player().Renderer()->NewTextureFlat();
            m_spPlayTexture->Upload(tmpPlay);
        }

        DisplayOutput::spCImage tmpPause(new DisplayOutput::CImage());
        if (tmpPause->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-pause.png", false))
        {
            m_spPauseTexture = g_Player().Renderer()->NewTextureFlat();
            m_spPauseTexture->Upload(tmpPause);
        }

        DisplayOutput::spCImage tmpBack(new DisplayOutput::CImage());
        if (tmpBack->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-back.png", false))
        {
            m_spBackTexture = g_Player().Renderer()->NewTextureFlat();
            m_spBackTexture->Upload(tmpBack);
        }
        
        DisplayOutput::spCImage tmpForward(new DisplayOutput::CImage());
        if (tmpForward->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-forward.png", false))
        {
            m_spForwardTexture = g_Player().Renderer()->NewTextureFlat();
            m_spForwardTexture->Upload(tmpForward);
        }
        
        DisplayOutput::spCImage tmpBack10(new DisplayOutput::CImage());
        if (tmpBack10->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-back10.png", false))
        {
            m_spBack10Texture = g_Player().Renderer()->NewTextureFlat();
            m_spBack10Texture->Upload(tmpBack10);
        }
        
        DisplayOutput::spCImage tmpForward10(new DisplayOutput::CImage());
        if (tmpForward10->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-forward10.png", false))
        {
            m_spForward10Texture = g_Player().Renderer()->NewTextureFlat();
            m_spForward10Texture->Upload(tmpForward10);
        }

        DisplayOutput::spCImage tmpLike(new DisplayOutput::CImage());
        if (tmpLike->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-like.png", false))
        {
            m_spLikeTexture = g_Player().Renderer()->NewTextureFlat();
            m_spLikeTexture->Upload(tmpLike);
        }
        
        DisplayOutput::spCImage tmpDislike(new DisplayOutput::CImage());
        if (tmpDislike->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-dislike.png", false))
        {
            m_spDislikeTexture = g_Player().Renderer()->NewTextureFlat();
            m_spDislikeTexture->Upload(tmpDislike);
        }
        
        // Set mini BG size
        // Fix A/R
        float aspect = g_Player().Display()->Aspect();
        const float s_mini = 0.045f;  // This can be changed to adjust the overall scale of the mini OSD

        m_BgSqCRect = rect;
        m_BgSqCRect.m_X1 *= aspect;

        auto w_bgsq = m_BgSqCRect.Width();
        auto h_bgsq = m_BgSqCRect.Height();
        
        m_BgSqCRect.m_X0 = 0.5f - (w_bgsq * s_mini);
        m_BgSqCRect.m_Y0 = 0.75f - (h_bgsq * s_mini);
        m_BgSqCRect.m_X1 = 0.5f + (w_bgsq * s_mini);
        m_BgSqCRect.m_Y1 = 0.75f + (h_bgsq * s_mini);

        m_LargeSymbolCRect = rect;
        m_LargeSymbolCRect.m_X1 *= aspect;

        auto w_lgs = m_LargeSymbolCRect.Width();
        auto h_lgs = m_LargeSymbolCRect.Height();
        const float s_lgs = s_mini * 135 / 210 ;

        // Position as the first dot (16x45), 30px left, 30px bottom to our main rect (700x210)
        m_LargeSymbolCRect.m_X0 = 0.5f - (w_lgs) * s_lgs;
        m_LargeSymbolCRect.m_Y0 = 0.75f - (h_lgs) * s_lgs;
        m_LargeSymbolCRect.m_X1 = m_LargeSymbolCRect.m_X0 + (2 * w_lgs * s_lgs);
        m_LargeSymbolCRect.m_Y1 = m_LargeSymbolCRect.m_Y0 + (2 * h_lgs * s_lgs);

        
        // Set Background size
        //
        // Compensate screen aspect ratio + aspect ratio of our image 16/9, 700x210
        m_Rect.m_X0 = 0;
        m_Rect.m_X1 = rect.m_X1 * aspect;
        m_Rect.m_Y0 = 0;
        m_Rect.m_Y1 = rect.m_Y1 * 0.3f;

        const float s = 0.15f;  // This can be changed to adjust the overall scale of the OSD
        m_BgCRect = m_Rect;
        m_BgCRect.m_X0 = 0.5f - (m_Rect.Width() * s);
        m_BgCRect.m_Y0 = 0.75f - (m_Rect.Height() * s);
        m_BgCRect.m_X1 = 0.5f + (m_Rect.Width() * s);
        m_BgCRect.m_Y1 = 0.75f + (m_Rect.Height() * s);
        
        
        // Set dot size
        //
        // Compensate screen aspect ratio (image is squared)
        m_DotCRect = rect;
        m_DotCRect.m_X1 *= aspect;
        //m_DotCRect.m_Y1 *= 45 / 16;

        auto w = m_DotCRect.Width();
        auto h = m_DotCRect.Height();

        // Scale our dot relative to our bg (16w vs 700w)
        const float s2_w = s * 16 / 700 ;
        const float s2_h = s * 45 / 700;
        
        // Position as the first dot (16x45), 30px left, 30px bottom to our main rect (700x210)
        m_DotCRect.m_X0 = 0.5 - m_BgCRect.Width() / 2 + m_BgCRect.Width() * 30 / 700;
        m_DotCRect.m_Y0 = 0.75f + m_BgCRect.Height() * 30 / 210;
        m_DotCRect.m_X1 = m_DotCRect.m_X0 + (2 * w * s2_w);
        m_DotCRect.m_Y1 = m_DotCRect.m_Y0 + (2 * h * s2_h);

        // We precalc our gap for the dot drawing, 16 + 8px margin
        dotGap = m_BgCRect.Width() * 24 / 700;
        
        // Set Symbol size
        //
        m_SymbolCRect = rect;
        m_SymbolCRect.m_X1 *= aspect;
        
        w = m_SymbolCRect.Width();
        h = m_SymbolCRect.Height();
        
        // Scale our symbol relative to our bg (65w vs 700w)
        const float s3 = s * 65 / 700 ;

        // Position symbol (65x65) 100px left, 30px top to our main rect (600x210)
        //m_SymbolCRect.m_X0 = 0.5 - m_BgCRect.Width() / 2 + m_BgCRect.Width() / 6;
        // TMP : center the symbol until we fix text
        m_SymbolCRect.m_X0 = 0.5f - (w*s3);
        m_SymbolCRect.m_Y0 = 0.75f - m_BgCRect.Height() * 75 / 210;
        m_SymbolCRect.m_X1 = m_SymbolCRect.m_X0 + (2 * w * s3);
        m_SymbolCRect.m_Y1 = m_SymbolCRect.m_Y0 + (2 * h * s3);

         
        // Font initialization
        /*m_FontDesc.AntiAliased(true);
        m_FontDesc.Height(72 * g_Player().Display()->Height() / 2000);
        m_FontDesc.Style(DisplayOutput::CFontDescription::Normal);
        m_FontDesc.Italic(false);
        m_FontDesc.TypeFace("Leto");

        m_spFont = g_Player().Renderer()->GetFont(m_FontDesc);*/
        
        // Large HUD
        tmpBg = NULL;
        tmpDot = NULL;
        tmpDotR = NULL;
        tmpDotB = NULL;
        tmpDotU = NULL;
        tmpSymbolSpeed = NULL;
        tmpSymbolBrightness = NULL;

        // Mini HUD
        tmpBgSq = NULL;
        tmpPlay = NULL;
        tmpPause = NULL;
        tmpBack = NULL;
        tmpForward = NULL;
        tmpBack10 = NULL;
        tmpForward10 = NULL;
    };
    
    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        if (!CHudEntry::Render(_time, _spRenderer))
            return false;

        if (m_spBgTexture == NULL)
            return false;

        DisplayOutput::spCRenderer spRenderer = g_Player().Renderer();

        if (type == Speed || type == Brightness) {
            // Setup & Draw background
            spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
            spRenderer->SetTexture(m_spBgTexture, 0);
            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            spRenderer->Apply();

            spRenderer->DrawQuad(m_BgCRect, Base::Math::CVector4(1, 1, 1, 1), m_spBgTexture->GetRect());
            
            // Setup & Draw symbol
            if (type == Speed) {
                spRenderer->SetTexture(m_spSymbolSpeedTexture, 0);
            } else {
                spRenderer->SetTexture(m_spSymbolBrightnessTexture, 0);
            }
            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            spRenderer->Apply();

            spRenderer->DrawQuad(m_SymbolCRect, Base::Math::CVector4(1, 1, 1, 1), m_spSymbolSpeedTexture->GetRect());
            
            // Setup & Draw dots

            // Scale back linearly min-max to 0-10
            double scaledValue = 0;
            
            if (type == Speed) {
                // We cheat a bit to align our rough fps to dots
                scaledValue = (PerceptualFPSToSpeed(currentFps - 0.01) - minFps) * 27 / (maxFps - minFps);

            } else {
                scaledValue = (currentBrightness - minBrightness) * 27 / (maxBrightness - minBrightness);

            }

            for (int i = 0 ; i < 27 ; i++) {
                if (scaledValue > 27 && i < (scaledValue - 27)) {
                    // over
                    spRenderer->SetTexture(m_spDotTexture_r, 0);
                    spRenderer->SetBlend("alphablend");
                    spRenderer->SetShader(NULL);
                    spRenderer->Apply();
                } else if (scaledValue < 0.9 && i > (scaledValue + 27)) {
                    // under
                    spRenderer->SetTexture(m_spDotTexture_b, 0);
                    spRenderer->SetBlend("alphablend");
                    spRenderer->SetShader(NULL);
                    spRenderer->Apply();
                } else if (i < scaledValue) {
                    // classic
                    spRenderer->SetTexture(m_spDotTexture, 0);
                    spRenderer->SetBlend("alphablend");
                    spRenderer->SetShader(NULL);
                    spRenderer->Apply();
                } else {
                    // unselected
                    spRenderer->SetTexture(m_spDotTexture_u, 0);
                    spRenderer->SetBlend("alphablend");
                    spRenderer->SetShader(NULL);
                    spRenderer->Apply();
                }

                spRenderer->DrawQuad(
                                    Base::Math::CRect(
                                        m_DotCRect.m_X0 + i*dotGap,
                                        m_DotCRect.m_Y0,
                                        m_DotCRect.m_X1 + i*dotGap,
                                        m_DotCRect.m_Y1),
                                    Base::Math::CVector4(1, 1, 1, 1),
                                    m_spDotTexture->GetRect()
                                     );
            }
        } else {
            // Show mini hud!
            
            // Setup & Draw background
            spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
            spRenderer->SetTexture(m_spBgSqTexture, 0);
            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            spRenderer->Apply();

            spRenderer->DrawQuad(m_BgSqCRect, Base::Math::CVector4(1, 1, 1, 1), m_spBgSqTexture->GetRect());
            
            // Setup & Draw symbol
            switch (type) {
                case Pause:
                    spRenderer->SetTexture(m_spPauseTexture, 0);
                    break;
                case Play:
                    spRenderer->SetTexture(m_spPlayTexture, 0);
                    break;
                case Previous:
                    spRenderer->SetTexture(m_spBackTexture, 0);
                    break;
                case Next:
                    spRenderer->SetTexture(m_spForwardTexture, 0);
                    break;
                case Back10:
                    spRenderer->SetTexture(m_spBack10Texture, 0);
                    break;
                case Forward10:
                    spRenderer->SetTexture(m_spForward10Texture, 0);
                    break;
                case Like:
                    spRenderer->SetTexture(m_spLikeTexture, 0);
                    break;
                case Dislike:
                    spRenderer->SetTexture(m_spDislikeTexture, 0);
                    break;
                default:
                    spRenderer->SetTexture(m_spPauseTexture, 0);
                    printf("Shouldn't be here");
                    break;
            }

            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            spRenderer->Apply();

            spRenderer->DrawQuad(m_LargeSymbolCRect, Base::Math::CVector4(1, 1, 1, 1), m_spPlayTexture->GetRect());
        }
        
        
        // Draw FPS counter
        /*
         // @TODO : check why text ALWAYS appear bottom left and never disappear
         
        m_FontDesc.Height(72 * g_Player().Display()->Height() / 2000);
        m_spFont = g_Player().Renderer()->GetFont(m_FontDesc);

        m_spText = g_Player().Renderer()->NewText(m_spFont, string_format(" %.2f FPS", currentValue));
        m_spText->SetEnabled(true);

        float edge = 12 / (float)_spRenderer->Display()->Width();

        std::map<std::string, CStat*>::const_iterator i;

        //    Figure out text extent for all strings.
        Base::Math::CRect extent;
        Base::Math::CVector2 size = m_spText->GetExtent();
        extent = extent.Union(Base::Math::CRect(0, 0, size.m_X + (edge * 2),
                                                size.m_Y + (edge * 2)));

        //    Draw quad.
        spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);

        Base::Math::CRect r(
            0.5f - (extent.Width() * 0.5f), extent.m_Y0 ,
            0.5f + (extent.Width() * 0.5f), extent.m_Y1 );

        spRenderer->SetBlend("alphablend");
        spRenderer->SetShader(NULL);
        spRenderer->Apply();

        spRenderer->DrawQuad(r, Base::Math::CVector4(1, 0.855f, 0.722f, 1));
        
         */
        return true;
    }

    void SetFPS(double value) { currentFps = value; }
    void SetBrightness(double value) { currentBrightness = value; }
    void SetType(OSDType value) { type = value; }
private:
    // Values we are displaying
    double minFps, maxFps, currentFps;
    double minBrightness, maxBrightness, currentBrightness;

    OSDType type;

    // Textures and coordinates for main hud (large 700px)
    DisplayOutput::spCTextureFlat m_spBgTexture, m_spDotTexture, m_spDotTexture_u, m_spDotTexture_r, m_spDotTexture_b, m_spSymbolSpeedTexture, m_spSymbolBrightnessTexture;
    Base::Math::CRect m_BgCRect, m_DotCRect, m_SymbolCRect;
    
    // Textures and coordinates for mini hud (210sq)
    DisplayOutput::spCTextureFlat m_spBgSqTexture, m_spPlayTexture, m_spPauseTexture, m_spBackTexture, m_spForwardTexture, m_spBack10Texture, m_spForward10Texture, m_spLikeTexture, m_spDislikeTexture;

    Base::Math::CRect m_BgSqCRect, m_LargeSymbolCRect;

    
    // FPS Counter
/*    DisplayOutput::CFontDescription m_FontDesc;
    Base::Math::CRect m_TextRect;
    DisplayOutput::spCBaseFont m_spFont;
    DisplayOutput::spCBaseText m_spText;*/
    
    // Precalculations for rendering
    float dotGap;
};

// @TODO : clean those old pointers at some point
MakeSmartPointers(COSD);

}

#endif /* OSD_hpp */
