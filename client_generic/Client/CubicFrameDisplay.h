#ifndef _CUBICFRAMEDISPLAY_H_
#define _CUBICFRAMEDISPLAY_H_

#include <algorithm>
#include <unordered_map>

#include "Rect.h"
#include "Vector4.h"

static const uint32_t kMaxFrames = 4;

/**
    CCubicFrameDisplay().
    Does a piecewise cubic interpolation between two frames using Mitchell Netravali reconstruction filter.
*/
class CCubicFrameDisplay : public CFrameDisplay
{
    DisplayOutput::spCShader m_spShader;
    std::unordered_map<DisplayOutput::CRenderer*, DisplayOutput::spCShader> m_shaderByRenderer;

    //	The four frames.
    DisplayOutput::spCTextureFlat m_spFrames[2 * kMaxFrames];

    //	Simple ringbuffer...
    uint8_t m_FrameIndices[kMaxFrames];
    uint32_t m_NumFrames;

    bool m_bWaitNextFrame;

    //	Mitchell Netravali Reconstruction Filter.
    float MitchellNetravali(const float _x, const float _B, const float _C)
    {
        float ax = fabsf(_x);

        if (ax < 1.f)
            return ((12.f - 9.f * _B - 6.f * _C) * ax * ax * ax +
                    (-18.f + 12.f * _B + 6.f * _C) * ax * ax +
                    (6.f - 2.f * _B)) /
                   6.f;
        else if ((ax >= 1.f) && (ax < 2.f))
            return ((-_B - 6.f * _C) * ax * ax * ax +
                    (6.f * _B + 30.f * _C) * ax * ax +
                    (-12.f * _B - 48.f * _C) * ax + (8.f * _B + 24.f * _C)) /
                   6.f;
        else
            return 0.f;
    }

  public:
    CCubicFrameDisplay(DisplayOutput::spCRenderer _spRenderer)
        : CFrameDisplay(_spRenderer)
    {

        // DX11 and Metal use symbolic shader names. DirectDraw (eDX9) cannot
        // compile them and falls back to plain CFrameDisplay behavior.
        switch (_spRenderer->Type())
        {
        case DisplayOutput::eDX11:
        case DisplayOutput::eMetal:
            m_spShader = _spRenderer->NewShader(
                "quadPassVertex", "drawDecodedFrameCubicFrameBlendFragment",
                {{"weights", DisplayOutput::eUniform_Float4},
                 {"newalpha", DisplayOutput::eUniform_Float},
                 {"transPct", DisplayOutput::eUniform_Float}});
            break;
        }

        if (!m_spShader)
            m_bValid = false;
        if (_spRenderer && m_spShader)
            m_shaderByRenderer[_spRenderer.get()] = m_spShader;

        m_NumFrames = 0;

        m_FrameIndices[0] = 0;
        m_FrameIndices[1] = 1;
        m_FrameIndices[2] = 2;
        m_FrameIndices[3] = 3;

        m_bWaitNextFrame = false;
    }

    virtual ~CCubicFrameDisplay() {}

    inline void ShiftArrayBackOneStep(uint8_t _array[kMaxFrames])
    {
        uint8_t tmp = _array[0];
        _array[0] = _array[1];
        _array[1] = _array[2];
        _array[2] = _array[3];
        _array[3] = tmp;
    }

    virtual spCTextureFlat& RequestTargetTexture() override
    {
        m_NumFrames++;
        ShiftArrayBackOneStep(m_FrameIndices);
        return m_spFrames[m_FrameIndices[3]];
    }

    virtual uint32_t StartAtFrame() const override { return 3; }

    virtual bool Draw(DisplayOutput::spCRenderer _spRenderer, float _alpha,
                      double _interframeDelta) override
    {
        //	Enable the shader.
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
                        "quadPassVertex", "drawDecodedFrameCubicFrameBlendFragment",
                        {{"weights", DisplayOutput::eUniform_Float4},
                         {"newalpha", DisplayOutput::eUniform_Float},
                         {"transPct", DisplayOutput::eUniform_Float}});
                    break;
                }
                if (shader)
                    m_shaderByRenderer[_spRenderer.get()] = shader;
            }
        }

        _spRenderer->SetShader(shader);

        uint32_t framesToUse = std::min(m_NumFrames, kMaxFrames);

        for (uint32_t i = 0; i < kMaxFrames - framesToUse; i++)
        {
            uint32_t realIdx = m_FrameIndices[kMaxFrames - framesToUse];

            _spRenderer->SetTexture(m_spFrames[realIdx], i + 1);
        }

        for (uint32_t i = kMaxFrames - framesToUse; i < kMaxFrames; i++)
        {
            uint32_t realIdx = m_FrameIndices[i];

            _spRenderer->SetTexture(m_spFrames[realIdx], i + 1);
        }

        //	B = 1,   C = 0   - cubic B-spline
        //	B = 1/3, C = 1/3 - nice
        //	B = 0,   C = 1/2 - Catmull-Rom spline.
        const float B = 1.0f;
        const float C = 0.0f;

        //	Set the filter weights...
        if (shader)
        {
            shader->Set("weights",
                        MitchellNetravali((float)_interframeDelta + 1.f, B, C),
                        MitchellNetravali((float)_interframeDelta, B, C),
                        MitchellNetravali(1.f - (float)_interframeDelta, B, C),
                        MitchellNetravali(2.f - (float)_interframeDelta, B, C));
        }

        _spRenderer->SetBlend("alphablend");
        _spRenderer->Apply();

        ScrollVideoForNonMatchingAspectRatio(
            m_spFrames[m_FrameIndices[3]]->GetRect());

        _spRenderer->DrawQuad(m_texRect, Base::Math::CVector4(1, 1, 1, _alpha),
                              m_spFrames[m_FrameIndices[3]]->GetRect());
        return true;
    }

    virtual double GetFps(double /*_decodeFps*/, double _displayFps)
    {
        return _displayFps;
    }

    virtual void InheritFramesFrom(CFrameDisplay* previous) override {
        auto* cubicPrev = dynamic_cast<CCubicFrameDisplay*>(previous);
        if (!cubicPrev) return;
        
        g_Log->Info("Inheriting cubic frames: copying textures from slots [%d,%d,%d,%d]", 
                    cubicPrev->m_FrameIndices[0], 
                    cubicPrev->m_FrameIndices[1], 
                    cubicPrev->m_FrameIndices[2], 
                    cubicPrev->m_FrameIndices[3]);
        
        // Copy all 4 frames' textures in chronological order
        // This maintains the full cubic interpolation context
        for (int i = 0; i < 4; i++) {
            uint32_t srcIdx = cubicPrev->m_FrameIndices[i];
            m_spFrames[i] = cubicPrev->m_spFrames[srcIdx];
        }
        
        // Initialize ring buffer state - already have 4 frames
        m_FrameIndices[0] = 0;  // Oldest frame (n-3)
        m_FrameIndices[1] = 1;  // Frame n-2
        m_FrameIndices[2] = 2;  // Frame n-1
        m_FrameIndices[3] = 3;  // Frame n (newest)
        m_NumFrames = 4;  // We have all 4 frames pre-loaded
        
        g_Log->Info("Frame inheritance complete with full 4-frame context");
    }
};

MakeSmartPointers(CCubicFrameDisplay);

#endif
