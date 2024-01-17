#ifndef _LINEARFRAMEDISPLAY_H_
#define _LINEARFRAMEDISPLAY_H_

#include "Rect.h"
#include "Shader.h"
#include "Vector4.h"
#include "FrameDisplay.h"

/**
        CLinearFrameDisplay().
        Does a piecewise linear interpolation between two frames.
*/
class CLinearFrameDisplay : public CFrameDisplay
{
    static const uint32_t kFramesPerState = 2;

    float m_LastAlpha;
    //	Pixelshader.
    DisplayOutput::spCShader m_spShader;

    //	The two frames.
    DisplayOutput::spCTextureFlat m_spFrames[kFramesPerState];
    uint8_t m_State;

    bool m_bWaitNextFrame;

  public:
    CLinearFrameDisplay(DisplayOutput::spCRenderer _spRenderer)
        : CFrameDisplay(_spRenderer)
    {
        m_State = 0;

        //	vertexshader...
        static const char* linear_vertexshader = "\
					float4x4 WorldViewProj: WORLDVIEWPROJECTION;\
					struct VS_OUTPUT\
					{\
						float4 Pos  : POSITION;\
						float2 Tex	: TEXCOORD0;\
					};\
					VS_OUTPUT main( float4 Pos : POSITION, float2 Tex : TEXCOORD0 )\
					{\
					  VS_OUTPUT Out = (VS_OUTPUT)0;\
					  Out.Pos = mul( Pos, WorldViewProj );\
					  Out.Tex = Tex;\
					  return Out;\
					}";

        //	Pixelshader to lerp between two textures.
        static const char* linear_pixelshaderDX = "\
					float delta;\
					float newalpha;\
					float transPct;\
					sampler2D texUnit1: register(s1);\
					sampler2D texUnit2: register(s2);\
					sampler2D texUnit3: register(s3);\
					sampler2D texUnit4: register(s4);\
					float4 main( float2 _uv : TEXCOORD0 ) : COLOR0\
					{\
						float4 c1 = tex2D( texUnit1, _uv );\
						float4 c2 = tex2D( texUnit2, _uv );\
						float4 c3 = lerp( c1, c2, delta );\
						c1 = tex2D( texUnit3, _uv );\
						c2 = tex2D( texUnit4, _uv );\
						float4 c4 = lerp( c1, c2, delta );\
						\
						float4 c5 = lerp( c3, c4, transPct / 100.0 );\
						c5.a = newalpha;\
						return c5;\
					}";

        //	Compile the shader.
        switch (_spRenderer->Type())
        {
        case DisplayOutput::eDX9:
            m_spShader = _spRenderer->NewShader(linear_vertexshader,
                                                linear_pixelshaderDX);
            break;
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

            _spRenderer->SetShader(m_spShader);
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
            m_spShader->Set("delta", (float)_interframeDelta);
            _spRenderer->Apply();

            ScrollVideoForNonMatchingAspectRatio(texRect);

            _spRenderer->DrawQuad(
                m_texRect, Base::Math::CVector4(1, 1, 1, _alpha), texRect);
        }

        return true;
    }

    virtual double GetFps(double /*_decodeFps*/, double _displayFps)
    {
        return _displayFps;
    }
};

MakeSmartPointers(CLinearFrameDisplay);

#endif
