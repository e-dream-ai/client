#ifndef _RENDERER_H_
#define _RENDERER_H_

#include <map>
#include <string>

#include "DisplayOutput.h"
#include "Font.h"
#include "Image.h"
#include "Matrix.h"
#include "Shader.h"
#include "SmartPtr.h"
#include "Text.h"
#include "TextureFlat.h"
#include "Vector4.h"
#include "base.h"

namespace DisplayOutput
{

/*
 */
enum eTextureTargetType
{
    eTexture2D,
    eTexture2DRect
};

/*
 */
enum eStateResetFlags
{
    eEverything = 0xffff,
    eShader = 0x1,
    eDepth = 0x2,
    eBlend = 0x4,
    eRaster = 0x8,
    eTexture = 0x10,
    eSampler = 0x20,
    eMatrices = 0x40,
};

enum eRenderLimits
{
    MAX_TEXUNIT = 29
};

enum eRenderType
{
    eDX9,
    eMetal,
};

//	Blending constants
enum eBlendConstant
{
    eZero,
    eOne,
    eSrc_Color,
    eOne_Minus_Src_Color,
    eDst_Color,
    eOne_Minus_Dst_Color,
    eSrc_Alpha,
    eOne_Minus_Src_Alpha,
    eDst_Alpha,
    eOne_Minus_Dst_Alpha,
    eSrc_Alpha_Saturate,
    eBlendConstNone,
};

//	Blendmodes.
enum eBlendMode
{
    eAdd,
    eSub,
    eReverse_Sub,
    eMin,
    eMax,
    eBlendNone,
};

/*
 */
class CBlend
{
  public:
    CBlend(int32_t _src, int32_t _dst, int32_t _mode)
    {
        m_Src = _src;
        m_Dst = _dst;
        m_Mode = _mode;
        m_bEnabled = (_src != eOne || _dst != eZero);
    }
    ~CBlend(){};

    bool m_bEnabled;
    int32_t m_Src;
    int32_t m_Dst;
    int32_t m_Mode;
};

MakeSmartPointers(CBlend);

/*
        CRenderer().

*/
class CRenderer
{
  protected:
    spCTexture m_aspActiveTextures[MAX_TEXUNIT];
    spCTexture m_aspSelectedTextures[MAX_TEXUNIT];
    spCShader m_spActiveShader, m_spSelectedShader;
    spCBlend m_spActiveBlend, m_spSelectedBlend, m_spDefaultBlend;

    std::map<std::string, spCBlend> m_BlendMap;

    spCDisplayOutput m_spDisplay;

    //	Matrices.
    Base::Math::CMatrix4x4 m_WorldMat, m_ViewMat, m_ProjMat;
    uint32_t m_bDirtyMatrices;
    float m_Brightness;

  public:
    CRenderer();
    virtual ~CRenderer();

    virtual eRenderType Type(void) const = PureVirtual;
    virtual const std::string Description(void) const = PureVirtual;

    //
    virtual bool Initialize(spCDisplayOutput _spDisplay);
    spCDisplayOutput Display() { return m_spDisplay; };

    //
    virtual bool BeginFrame(void) { return (true); };
    virtual bool EndFrame(bool drawn = true) { return (drawn); };

    //	Textures.
    virtual spCTextureFlat
    NewTextureFlat(const uint32_t flags = 0) = PureVirtual;
    virtual spCTextureFlat
    NewTextureFlat(spCImage _spImage, const uint32_t flags = 0) = PureVirtual;

    //	Font.
    virtual spCBaseFont GetFont(CFontDescription& _desc) = PureVirtual;
    virtual spCBaseText NewText(spCBaseFont _font,
                                const std::string& _text) = PureVirtual;
    virtual void DrawText(spCBaseText _text,
                          const Base::Math::CVector4& _color) = PureVirtual;
    virtual Base::Math::CVector2 GetTextExtent(spCBaseFont /*_spFont*/,
                                               const std::string& /*_text*/)
    {
        return Base::Math::CVector2(0, 0);
    };

    virtual bool HasShaders() { return false; }
    //	Shaders.
    virtual spCShader
    NewShader(const char* _pVertexShader, const char* _pFragmentShader,
              std::vector<std::pair<std::string, eUniformType>> _uniforms =
                  {}) = PureVirtual;

    //	Shortcut helper function.
    void Orthographic();
    void Orthographic(const uint32_t _width, const uint32_t _height);

    //
    enum eMatrixTransformType
    {
        eWorld = 1,
        eView = 2,
        eProjection = 4,
    };

    void SetTransform(const Base::Math::CMatrix4x4& _transform,
                      const eMatrixTransformType _type);

    //	State api.
    virtual void Defaults() = PureVirtual;
    virtual void Reset(const uint32_t _flags);
    virtual void Apply();
    virtual void SetTexture(spCTexture _spTex, const uint32_t _index);
    virtual void SetShader(spCShader _spShader);
    virtual float GetBrightness() const { return m_Brightness; }
    virtual void SetBrightness(float _brightness)
    {
        m_Brightness = _brightness;
    }

    virtual eTextureTargetType GetTextureTargetType(void)
    {
        return eTexture2D;
    };

    //
    void AddBlend(std::string _name, int32_t _src, int32_t _dst, int32_t _mode);
    void SetBlend(std::string _blend);

    //	Aux functions.
    virtual void DrawLine(const Base::Math::CVector2& /*_start*/,
                          const Base::Math::CVector2& /*_end*/,
                          const Base::Math::CVector4& /*_color*/,
                          const float /*_width = 1.0f*/){};
    virtual void DrawRect(const Base::Math::CRect& /*_rect*/,
                          const Base::Math::CVector4& /*_color*/,
                          const float /*_width = 1.0f*/){};
    virtual void DrawQuad(const Base::Math::CRect& /*_rect*/,
                          const Base::Math::CVector4& /*_color*/){};
    virtual void DrawQuad(const Base::Math::CRect& /*_rect*/,
                          const Base::Math::CVector4& /*_color*/,
                          const Base::Math::CRect& /*_uvRect*/){};
    virtual void DrawSoftQuad(const Base::Math::CRect& /*_rect*/,
                              const Base::Math::CVector4& /*_color*/,
                              const float /*_width*/){};
};

MakeSmartPointers(CRenderer);

} // namespace DisplayOutput

#endif
