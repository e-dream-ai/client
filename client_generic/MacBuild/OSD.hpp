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

// Helper to map FPS to Activity Level
inline double FPSToActivity(double fps) {
    return (2 * log(fps)) / log(2) - 1;
}

enum OSDType {
    ActivityLevel,
    Brightness
};

class COSD : public CHudEntry {
    
public:
    COSD(Base::Math::CRect rect, OSDType type, double min, double max) : CHudEntry(rect), min {min}, max {max}, type { type } {
        // initialise currentvalue to min, this gets updated by the render loop later on
        currentValue = min;
        
#ifndef LINUX_GNU
        std::string defaultDir = std::string(".\\");
#else
        std::string defaultDir = std::string("");
#endif
        
        // Grab our background texture
        DisplayOutput::spCImage tmpBg(new DisplayOutput::CImage());
        if (tmpBg->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-bg.png", false))
        {
            m_spBgTexture = g_Player().Renderer()->NewTextureFlat();
            m_spBgTexture->Upload(tmpBg);
        }

        // Grab our dot texture
        DisplayOutput::spCImage tmpDot(new DisplayOutput::CImage());
        if (tmpDot->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-dot.png", false))
        {
            m_spDotTexture = g_Player().Renderer()->NewTextureFlat();
            m_spDotTexture->Upload(tmpDot);
        }

        // Grab our symbol texture
        // TODO switch + brightness
        DisplayOutput::spCImage tmpSymbol(new DisplayOutput::CImage());
        if (tmpDot->Load(g_Settings()->Get("settings.app.InstallDir", defaultDir) +
                           "osd-speed.png", false))
        {
            m_spSymbolTexture = g_Player().Renderer()->NewTextureFlat();
            m_spSymbolTexture->Upload(tmpDot);
        }

        // Set Background size
        //
        // Compensate screen aspect ratio + aspect ratio of our image 16/9, 600x210
        float aspect = g_Player().Display()->Aspect();
        m_Rect.m_X0 = 0;
        m_Rect.m_X1 = rect.m_X1 * aspect;
        m_Rect.m_Y0 = 0;
        m_Rect.m_Y1 = rect.m_Y1 * 0.35f;

        m_BgCRect = m_Rect;
        const float s = 0.15f;  // This can be changed to adjust the overall scale of the OSD
        m_BgCRect.m_X0 = 0.5f - (m_Rect.Width() * s);
        m_BgCRect.m_Y0 = 0.75f - (m_Rect.Height() * s);
        m_BgCRect.m_X1 = 0.5f + (m_Rect.Width() * s);
        m_BgCRect.m_Y1 = 0.75f + (m_Rect.Height() * s);
        
        
        // Set dot size
        //
        // Compensate screen aspect ratio (image is squared)
        m_DotCRect = rect;
        m_DotCRect.m_X1 *= aspect;

        auto w = m_DotCRect.Width();
        auto h = m_DotCRect.Height();

        // Scale our dot relative to our bg (45w vs 600w)
        const float s2 = s * 45 / 600 ;

        // Position as the first dot (45x45), 30px left, 30px bottom to our main rect (600x210)
        m_DotCRect.m_X0 = 0.5 - m_BgCRect.Width() / 2 + m_BgCRect.Width() / 20;
        m_DotCRect.m_Y0 = 0.75f + m_BgCRect.Height() * 30 / 210;
        m_DotCRect.m_X1 = m_DotCRect.m_X0 + (2 * w * s2);
        m_DotCRect.m_Y1 = m_DotCRect.m_Y0 + (2 * h * s2);

        // We precalc our gap for the dot drawing, 45w + 10px margin
        dotGap = m_BgCRect.Width() * 55 / 600;
        
        // Set Symbol size
        //
        m_SymbolCRect = rect;
        m_SymbolCRect.m_X1 *= aspect;
        
        w = m_SymbolCRect.Width();
        h = m_SymbolCRect.Height();
        
        // Scale our dot relative to our bg (45w vs 600w)
        const float s3 = s * 65 / 600 ;

        // Position symbol (65x65) 100px left, 30px top to our main rect (600x210)
        m_SymbolCRect.m_X0 = 0.5 - m_BgCRect.Width() / 2 + m_BgCRect.Width() / 6;
        m_SymbolCRect.m_Y0 = 0.75f - m_BgCRect.Height() * 75 / 210;
        m_SymbolCRect.m_X1 = m_SymbolCRect.m_X0 + (2 * w * s3);
        m_SymbolCRect.m_Y1 = m_SymbolCRect.m_Y0 + (2 * h * s3);

         
        // Font initialization
        m_FontDesc.AntiAliased(true);
        m_FontDesc.Height(72 * g_Player().Display()->Height() / 2000);
        m_FontDesc.Style(DisplayOutput::CFontDescription::Normal);
        m_FontDesc.Italic(false);
        m_FontDesc.TypeFace("Leto");

        m_spFont = g_Player().Renderer()->GetFont(m_FontDesc);
        
        tmpBg = NULL;
        tmpDot = NULL;
        tmpSymbol = NULL;
    };
    
    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        if (!CHudEntry::Render(_time, _spRenderer))
            return false;

        if (m_spBgTexture == NULL)
            return false;

        DisplayOutput::spCRenderer spRenderer = g_Player().Renderer();

        // Setup & Draw background
        spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader);
        spRenderer->SetTexture(m_spBgTexture, 0);
        spRenderer->SetBlend("alphablend");
        spRenderer->SetShader(NULL);
        spRenderer->Apply();

        spRenderer->DrawQuad(m_BgCRect, Base::Math::CVector4(1, 1, 1, 1), m_spBgTexture->GetRect());
        
        // Setup & Draw symbol
        spRenderer->SetTexture(m_spSymbolTexture, 0);
        spRenderer->SetBlend("alphablend");
        spRenderer->SetShader(NULL);
        spRenderer->Apply();

        spRenderer->DrawQuad(m_SymbolCRect, Base::Math::CVector4(1, 1, 1, 1), m_spSymbolTexture->GetRect());
        
        // Setup & Draw dots
        spRenderer->SetTexture(m_spDotTexture, 0);
        spRenderer->SetBlend("alphablend");
        spRenderer->SetShader(NULL);
        spRenderer->Apply();

        // Scale back linearly min-max to 0-10
        auto scaledValue = (FPSToActivity(currentValue) - min) * 10 / (max - min);

        //printf("VAL %f %f %f\n", currentValue, FPSToActivity(currentValue), scaledValue);

        // TODO : Add a distinguisher for over/under min max, for now we clamp the display
        if (scaledValue > 10)
            scaledValue = 10;
        
        for (int i = 0 ; i < scaledValue ; i++) {
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
        
        // Draw FPS counter
        /*
         // TODO, check why text ALWAYS appear bottom left and never disappear
         
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

    void SetValue(double value) { currentValue = value; }
private:
    // Values we are displaying
    double min, max, currentValue;

    OSDType type;   // TODO impl brightness

    // Textures and coordinates
    DisplayOutput::spCTextureFlat m_spBgTexture, m_spDotTexture, m_spSymbolTexture;
    Base::Math::CRect m_BgCRect, m_DotCRect, m_SymbolCRect;
        
    // FPS Counter
    DisplayOutput::CFontDescription m_FontDesc;
    Base::Math::CRect m_TextRect;
    DisplayOutput::spCBaseFont m_spFont;
    DisplayOutput::spCBaseText m_spText;
    
    // Precalculations for rendering
    float dotGap;


};

// TODO : clean those old pointers at some point
MakeSmartPointers(COSD);

}

#endif /* OSD_hpp */
