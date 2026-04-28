#ifndef _LINEARFRAMEDISPLAY_H_
#define _LINEARFRAMEDISPLAY_H_

#include "Rect.h"
#include "Shader.h"
#include "Vector4.h"
#include "FrameDisplay.h"
#include <unordered_map>

/**
        CLinearFrameDisplay().
        Does a piecewise linear interpolation between two frames.
*/
class CLinearFrameDisplay : public CFrameDisplay
{
    static const uint32_t kFramesPerState = 2;
    //	Pixelshader.
    DisplayOutput::spCShader m_spShader;
    std::unordered_map<DisplayOutput::CRenderer*, DisplayOutput::spCShader> m_shaderByRenderer;

    //	The two frames.
    DisplayOutput::spCTextureFlat m_spFrames[kFramesPerState];
    uint8_t m_State;

  public:
    CLinearFrameDisplay(DisplayOutput::spCRenderer _spRenderer)
        : CFrameDisplay(_spRenderer)
    {
        m_State = 0;

        // DX11 and Metal use symbolic shader names. DirectDraw (eDX9) cannot
        // compile them and falls back to plain CFrameDisplay behavior.
        switch (_spRenderer->Type())
        {
        case DisplayOutput::eDX11:
        case DisplayOutput::eMetal:
            m_spShader = _spRenderer->NewShader(
                "quadPassVertex", "drawDecodedFrameLinearFrameBlendFragment",
                {{"delta", DisplayOutput::eUniform_Float},
                 {"newalpha", DisplayOutput::eUniform_Float},
                 {"transPct", DisplayOutput::eUniform_Float}});
            break;
        }

        if (!m_spShader)
            m_bValid = false;
        if (_spRenderer && m_spShader)
            m_shaderByRenderer[_spRenderer.get()] = m_spShader;
    }

    virtual ~CLinearFrameDisplay() {}

    virtual spCTextureFlat& RequestTargetTexture() override
    {
        m_State ^= 1;
        return m_spFrames[m_State];
    }

    virtual uint32_t StartAtFrame() const override { return 1; }

    virtual bool Draw(DisplayOutput::spCRenderer _spRenderer, float _alpha,
                      double _interframeDelta) override
    {
        if (m_spFrames[m_State] != nullptr)
        {
            Base::Math::CRect texRect;

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
                    switch (_spRenderer->Type())
                    {
                    case DisplayOutput::eDX11:
                    case DisplayOutput::eMetal:
                        shader = _spRenderer->NewShader(
                            "quadPassVertex", "drawDecodedFrameLinearFrameBlendFragment",
                            {{"delta", DisplayOutput::eUniform_Float},
                             {"newalpha", DisplayOutput::eUniform_Float},
                             {"transPct", DisplayOutput::eUniform_Float}});
                        break;
                    }
                    if (shader)
                        m_shaderByRenderer[_spRenderer.get()] = shader;
                }
            }

            _spRenderer->SetShader(shader);
            _spRenderer->SetBlend("alphablend");

            //	Only one frame so far, let's display it normally.
            if (m_spFrames[m_State ^ 1] == nullptr)
            {
                //	Bind texture and render a quad covering the screen.
                _spRenderer->SetTexture(m_spFrames[m_State], 1);
                _spRenderer->SetTexture(m_spFrames[m_State], 2);
            }
            else
            {
                _spRenderer->SetTexture(m_spFrames[0], (m_State ^ 1) + 1);
                _spRenderer->SetTexture(m_spFrames[1], m_State + 1);
            }
            texRect = m_spFrames[m_State]->GetRect();
            if (shader)
                shader->Set("delta", (float)_interframeDelta);
            _spRenderer->Apply();

            ScrollVideoForNonMatchingAspectRatio(texRect);

            _spRenderer->DrawQuad(
                m_texRect, Base::Math::CVector4(1, 1, 1, _alpha), texRect);
        }

        return true;
    }

    virtual double GetFps(double /*_decodeFps*/, double _displayFps) override
    {
        return _displayFps;
    }

    virtual void InheritFramesFrom(CFrameDisplay* previous) override {
        auto* linearPrev = dynamic_cast<CLinearFrameDisplay*>(previous);
        if (!linearPrev) return;
        
        g_Log->Info("Inheriting linear frame from slot %d", linearPrev->m_State);
        
        // Copy the last frame to maintain continuity
        m_spFrames[0] = linearPrev->m_spFrames[linearPrev->m_State];
        m_State = 1;  // Next frame will go to slot 1
        
        g_Log->Info("Linear frame inheritance complete");
    }
};

MakeSmartPointers(CLinearFrameDisplay);

#endif
