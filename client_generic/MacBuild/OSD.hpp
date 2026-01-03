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
    Dislike,
    Report,
    Buffering,
    Repeat,
    Shuffle
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
                           "osd-next.png", false))
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
        
        DisplayOutput::spCImage tmpReport(new DisplayOutput::CImage());
        if (tmpReport->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                               "osd-report.png", false))
        {
            m_spReportTexture = g_Player().Renderer()->NewTextureFlat();
            m_spReportTexture->Upload(tmpReport);
        }
        
        DisplayOutput::spCImage tmpBuffering(new DisplayOutput::CImage());
        if (tmpBuffering->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                               "network.png", false))
        {
            m_spBufferingTexture = g_Player().Renderer()->NewTextureFlat();
            m_spBufferingTexture->Upload(tmpBuffering);
        }

        DisplayOutput::spCImage tmpRepeat(new DisplayOutput::CImage());
        if (tmpRepeat->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                               "osd-repeat.png", false))
        {
            m_spRepeatTexture = g_Player().Renderer()->NewTextureFlat();
            m_spRepeatTexture->Upload(tmpRepeat);
        }

        DisplayOutput::spCImage tmpShuffle(new DisplayOutput::CImage());
        if (tmpShuffle->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                               "osd-shuffle.png", false))
        {
            m_spShuffleTexture = g_Player().Renderer()->NewTextureFlat();
            m_spShuffleTexture->Upload(tmpShuffle);
        }

        // Grab turtle icon (for slower speed indicator)
        DisplayOutput::spCImage tmpTurtle(new DisplayOutput::CImage());
        if (tmpTurtle->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                               "osd-turtle.png", false))
        {
            m_spTurtleTexture = g_Player().Renderer()->NewTextureFlat();
            m_spTurtleTexture->Upload(tmpTurtle);
        }

        // Grab rabbit icon (for faster speed indicator)
        DisplayOutput::spCImage tmpRabbit(new DisplayOutput::CImage());
        if (tmpRabbit->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                               "osd-rabbit.png", false))
        {
            m_spRabbitTexture = g_Player().Renderer()->NewTextureFlat();
            m_spRabbitTexture->Upload(tmpRabbit);
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

        // Set turtle icon position (top-left corner of OSD background)
        const float iconScale = s * 70 / 700;  // Scale for turtle/rabbit icons (larger)
        m_TurtleCRect = rect;
        m_TurtleCRect.m_X1 *= aspect;
        float iconW = m_TurtleCRect.Width() * iconScale;
        float iconH = m_TurtleCRect.Height() * iconScale;
        
        // Position turtle in top-left corner of OSD bg - closer to dots
        m_TurtleCRect.m_X0 = m_BgCRect.m_X0 + m_BgCRect.Width() * 30 / 700;
        m_TurtleCRect.m_Y0 = m_DotCRect.m_Y0 - iconH * 2 - m_BgCRect.Height() * 30 / 210;
        m_TurtleCRect.m_X1 = m_TurtleCRect.m_X0 + iconW * 2;
        m_TurtleCRect.m_Y1 = m_TurtleCRect.m_Y0 + iconH * 2;
        
        // Set rabbit icon position (top-right corner of OSD background)
        m_RabbitCRect = rect;
        m_RabbitCRect.m_X1 *= aspect;
        
        // Position rabbit in top-right corner of OSD bg - closer to dots
        m_RabbitCRect.m_X1 = m_BgCRect.m_X1 - m_BgCRect.Width() * 30 / 700;
        m_RabbitCRect.m_X0 = m_RabbitCRect.m_X1 - iconW * 2;
        m_RabbitCRect.m_Y0 = m_DotCRect.m_Y0 - iconH * 2 - m_BgCRect.Height() * 30 / 210;
        m_RabbitCRect.m_Y1 = m_RabbitCRect.m_Y0 + iconH * 2;

        // Set second turtle icon position (next to first turtle, for slow zone)
        m_Turtle2CRect = m_TurtleCRect;
        m_Turtle2CRect.m_X0 = m_TurtleCRect.m_X1 + m_BgCRect.Width() * 5 / 700;  // Small gap between icons
        m_Turtle2CRect.m_X1 = m_Turtle2CRect.m_X0 + (m_TurtleCRect.m_X1 - m_TurtleCRect.m_X0);

        // Set second rabbit icon position (next to first rabbit, for fast zone)
        m_Rabbit2CRect = m_RabbitCRect;
        m_Rabbit2CRect.m_X1 = m_RabbitCRect.m_X0 - m_BgCRect.Width() * 5 / 700;  // Small gap between icons
        m_Rabbit2CRect.m_X0 = m_Rabbit2CRect.m_X1 - (m_RabbitCRect.m_X1 - m_RabbitCRect.m_X0);

        // Set FPS text position (centered, where symbol was)
        m_FpsTextRect = m_SymbolCRect;
         
        // Font initialization for FPS display
        m_FontDesc.AntiAliased(true);
        // Scale font size with app size: proportional to OSD bg height in pixels
        float fontPx = m_BgCRect.Height() * g_Player().Display()->Height() * 0.3f; // tuned for readability
        m_FontDesc.Height(fontPx);
        m_FontDesc.Style(DisplayOutput::CFontDescription::Normal);
        m_FontDesc.Italic(false);
        m_FontDesc.TypeFace("Lato");

        m_spFont = g_Player().Renderer()->GetFont(m_FontDesc);
        
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
        tmpLike = NULL;
        tmpDislike = NULL;
        tmpReport = NULL;
        
        // Set buffering icon position in top left
        m_BufferingCRect = rect;
        m_BufferingCRect.m_X1 *= aspect;

        auto w_buffering = m_BufferingCRect.Width();
        auto h_buffering = m_BufferingCRect.Height();
        const float s_buffering = 0.05f;  // Scale for buffering icon

        // Position in top left with padding
        m_BufferingCRect.m_X0 = 0.02f;  // 2% from left edge
        m_BufferingCRect.m_Y0 = 0.04f;  // 4% from top edge
        m_BufferingCRect.m_X1 = m_BufferingCRect.m_X0 + (w_buffering * s_buffering);
        m_BufferingCRect.m_Y1 = m_BufferingCRect.m_Y0 + (h_buffering * s_buffering);

        // Remember to clean up the temporary image
        tmpBuffering = NULL;
    };
    
    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        if (!CHudEntry::Render(_time, _spRenderer))
        {
            // OSD is being hidden, disable FPS text
            if (m_spFpsText != NULL) {
                m_spFpsText->SetEnabled(false);
            }
            return false;
        }

        if (m_spBgTexture == NULL)
            return false;

        DisplayOutput::spCRenderer spRenderer = g_Player().Renderer();

        if (type == Buffering) {
            // Hide FPS text for non-Speed types
            if (m_spFpsText != NULL) {
                m_spFpsText->SetEnabled(false);
            }
            
            // Use a sine wave for smooth pulsing
            float pulse = (sin(_time * m_BufferingPulseSpeed) + 1.0f) * 0.5f; // Produces 0.0 to 1.0
            float alpha = m_BufferingMinAlpha + pulse * (m_BufferingMaxAlpha - m_BufferingMinAlpha);
            
            // Draw just the buffering icon without background
            spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
            spRenderer->SetTexture(m_spBufferingTexture, 0);
            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            spRenderer->Apply();
            
            // Draw with pulsing alpha
            spRenderer->DrawQuad(
                m_BufferingCRect,
                Base::Math::CVector4(1, 1, 1, alpha),
                m_spBufferingTexture->GetRect()
            );
            
            return true;
        } else if (type == Speed || type == Brightness) {
            // Setup & Draw background
            spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
            spRenderer->SetTexture(m_spBgTexture, 0);
            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            spRenderer->Apply();

            spRenderer->DrawQuad(m_BgCRect, Base::Math::CVector4(1, 1, 1, 1), m_spBgTexture->GetRect());
            
            // Setup & Draw symbol/text based on type
            if (type == Speed) {
                // Calculate scaled value to determine which zone we're in
                double scaledValueForIcons = (PerceptualFPSToSpeed(currentFps - 0.01) - minFps) * 27 / (maxFps - minFps);
                bool isFastZone = scaledValueForIcons > 27;  // Red zone
                bool isSlowZone = scaledValueForIcons < 0.8; // Blue zone
                
                if (isFastZone) {
                    // Fast zone: One rabbit on left, two rabbits on right
                    if (m_spRabbitTexture != NULL) {
                        spRenderer->SetTexture(m_spRabbitTexture, 0);
                        spRenderer->SetBlend("alphablend");
                        spRenderer->SetShader(NULL);
                        spRenderer->Apply();
                        // Draw one rabbit on left (in turtle's position)
                        spRenderer->DrawQuad(m_TurtleCRect, Base::Math::CVector4(1, 1, 1, 1), m_spRabbitTexture->GetRect());
                        // Draw two rabbits on right
                        spRenderer->DrawQuad(m_RabbitCRect, Base::Math::CVector4(1, 1, 1, 1), m_spRabbitTexture->GetRect());
                        spRenderer->DrawQuad(m_Rabbit2CRect, Base::Math::CVector4(1, 1, 1, 1), m_spRabbitTexture->GetRect());
                    }
                } else if (isSlowZone) {
                    // Slow zone: Two turtles on left, one turtle on right
                    if (m_spTurtleTexture != NULL) {
                        spRenderer->SetTexture(m_spTurtleTexture, 0);
                        spRenderer->SetBlend("alphablend");
                        spRenderer->SetShader(NULL);
                        spRenderer->Apply();
                        // Draw two turtles on left
                        spRenderer->DrawQuad(m_TurtleCRect, Base::Math::CVector4(1, 1, 1, 1), m_spTurtleTexture->GetRect());
                        spRenderer->DrawQuad(m_Turtle2CRect, Base::Math::CVector4(1, 1, 1, 1), m_spTurtleTexture->GetRect());
                        // Draw one turtle on right (in rabbit's position)
                        spRenderer->DrawQuad(m_RabbitCRect, Base::Math::CVector4(1, 1, 1, 1), m_spTurtleTexture->GetRect());
                    }
                } else {
                    // Normal zone: One turtle on left, one rabbit on right
                    if (m_spTurtleTexture != NULL) {
                        spRenderer->SetTexture(m_spTurtleTexture, 0);
                        spRenderer->SetBlend("alphablend");
                        spRenderer->SetShader(NULL);
                        spRenderer->Apply();
                        spRenderer->DrawQuad(m_TurtleCRect, Base::Math::CVector4(1, 1, 1, 1), m_spTurtleTexture->GetRect());
                    }
                    
                    if (m_spRabbitTexture != NULL) {
                        spRenderer->SetTexture(m_spRabbitTexture, 0);
                        spRenderer->SetBlend("alphablend");
                        spRenderer->SetShader(NULL);
                        spRenderer->Apply();
                        spRenderer->DrawQuad(m_RabbitCRect, Base::Math::CVector4(1, 1, 1, 1), m_spRabbitTexture->GetRect());
                    }
                }
                
                // Draw FPS numeric display (horizontally centered, above the indicator dots)
                // Recompute font size each frame to follow current display size
                float fontPx = m_BgCRect.Height() * g_Player().Display()->Height() * 0.3f;
                m_FontDesc.Height(fontPx);
                m_spFont = g_Player().Renderer()->GetFont(m_FontDesc);
                if (m_spFont != NULL) {
                    // Create/update text with current FPS value (2 decimal places)
                    std::string fpsText = string_format("%.2f", currentFps);
                    m_spFpsText = g_Player().Renderer()->NewText(m_spFont, fpsText);
                    if (m_spFpsText != NULL) {
                        // Manual extent estimate (Metal GetExtent is async and may return 0)
                        // Approx width = charCount * 0.6 * fontPx; height = fontPx
                        float displayW = (float)g_Player().Display()->Width();
                        float displayH = (float)g_Player().Display()->Height();
                        float fontPx   = m_FontDesc.Height();
                        float approxWidthPx  = (float)fpsText.size() * fontPx * 0.6f;
                        float approxHeightPx = fontPx;
                        float textW = approxWidthPx / displayW;
                        float textH = approxHeightPx / displayH;

                        // Position just above the dots with minimal gap
                        float textY = m_DotCRect.m_Y0 + m_BgCRect.Height() * 0.15f - textH;
                        // Center horizontally based on estimated width, nudge slightly right to compensate layout
                        float textX = 0.5f - (textW * 0.5f) + (m_BgCRect.Width() * 0.02f);

                        m_spFpsText->SetRect(Base::Math::CRect(textX, textY, textX + textW, textY + textH));
                        m_spFpsText->SetEnabled(true);
                        // Use renderer's DrawText to set color (osd-pill orange)
                        spRenderer->DrawText(m_spFpsText, Base::Math::CVector4(1.0f, 0.855f, 0.722f, 1.0f));
                    }
                }
            } else {
                // Brightness - use original symbol, hide FPS text
                if (m_spFpsText != NULL) {
                    m_spFpsText->SetEnabled(false);
                }
                spRenderer->SetTexture(m_spSymbolBrightnessTexture, 0);
                spRenderer->SetBlend("alphablend");
                spRenderer->SetShader(NULL);
                spRenderer->Apply();
                spRenderer->DrawQuad(m_SymbolCRect, Base::Math::CVector4(1, 1, 1, 1), m_spSymbolBrightnessTexture->GetRect());
            }
            
            // Setup & Draw dots

            // Scale back linearly min-max to 0-10
            double scaledValue = 0;
            
            if (type == Speed) {
                // We cheat a bit to align our rough fps to dots
                scaledValue = (PerceptualFPSToSpeed(currentFps - 0.01) - minFps) * 27 / (maxFps - minFps);

            } else {
                scaledValue = (currentBrightness - minBrightness) * 27 / (maxBrightness - minBrightness);

            }

            bool isFastZone = scaledValue > 27;
            bool isSlowZone = scaledValue < 0.8;
            bool isNormalZone = !isFastZone && !isSlowZone;

            spRenderer->SetBlend("alphablend");
            spRenderer->SetShader(NULL);
            for (int i = 0 ; i < 27 ; i++) {
                if (isFastZone && i < (scaledValue - 27)) {
                    spRenderer->SetTexture(m_spDotTexture, 0);
                } else if (isSlowZone && i < (scaledValue + 27)) {
                    spRenderer->SetTexture(m_spDotTexture, 0);
                } else if (isNormalZone && i < scaledValue) {
                    spRenderer->SetTexture(m_spDotTexture, 0);
                } else {
                    spRenderer->SetTexture(m_spDotTexture_u, 0);
                }
                spRenderer->Apply();


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
            // Hide FPS text for mini hud types
            if (m_spFpsText != NULL) {
                m_spFpsText->SetEnabled(false);
            }
            
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
                case Report:
                    spRenderer->SetTexture(m_spReportTexture, 0);
                    break;
                case Repeat:
                    spRenderer->SetTexture(m_spRepeatTexture, 0);
                    break;
                case Shuffle:
                    spRenderer->SetTexture(m_spShuffleTexture, 0);
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
    DisplayOutput::spCTextureFlat m_spBgSqTexture, m_spPlayTexture, m_spPauseTexture, m_spBackTexture, m_spForwardTexture, m_spBack10Texture, m_spForward10Texture, m_spLikeTexture, m_spDislikeTexture, m_spReportTexture, m_spRepeatTexture, m_spShuffleTexture;

    Base::Math::CRect m_BgSqCRect, m_LargeSymbolCRect;

    // Buffering indicator
    DisplayOutput::spCTextureFlat m_spBufferingTexture;
    Base::Math::CRect m_BufferingCRect;
    float m_BufferingPulseSpeed = 2.0f; // Speed of pulsing (adjust as needed)
    float m_BufferingMinAlpha = 0.3f;   // Minimum alpha value
    float m_BufferingMaxAlpha = 1.0f;   // Maximum alpha value
    
    // Turtle and Rabbit icons for speed OSD
    DisplayOutput::spCTextureFlat m_spTurtleTexture, m_spRabbitTexture;
    Base::Math::CRect m_TurtleCRect, m_RabbitCRect;
    Base::Math::CRect m_Turtle2CRect, m_Rabbit2CRect;  // Second icons for fast/slow zones
    
    // FPS Counter text
    DisplayOutput::CFontDescription m_FontDesc;
    DisplayOutput::spCBaseFont m_spFont;
    DisplayOutput::spCBaseText m_spFpsText;
    Base::Math::CRect m_FpsTextRect;
    
    // Precalculations for rendering
    float dotGap;
};

// @TODO : clean those old pointers at some point
MakeSmartPointers(COSD);

}

#endif /* OSD_hpp */
