#ifndef _RENDERERDX11_H_
#define _RENDERERDX11_H_

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include "Renderer.h"
#include "SmartPtr.h"
#include "ShaderDX11.h"
#include "TextureFlatDX11.h"

using Microsoft::WRL::ComPtr;

namespace DisplayOutput {

class CRendererDX11 : public CRenderer {
protected:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11BlendState> m_blendState;
    ComPtr<ID3D11DepthStencilState> m_depthStencilNoDepthTest;
    ComPtr<ID3D11SamplerState> m_defaultSampler;
    ComPtr<ID3D11SamplerState> m_glyphPointSampler;
    ComPtr<ID3D11RasterizerState> m_rasterizerDefault;
    ComPtr<ID3D11RasterizerState> m_rasterizerScissor;

    void DrawTexturedQuad(const Base::Math::CRect& _rect, const Base::Math::CVector4& _color,
                          const Base::Math::CRect& _uvRect, ID3D11SamplerState* _pixelSampler);

    std::map<std::string, spCBaseFont> m_fontPool;

public:
    CRendererDX11();
    virtual ~CRendererDX11();

    virtual eRenderType Type(void) const override { return eDX9; }
    virtual const std::string Description(void) const override { return "DirectX 11"; }
    
    virtual bool Initialize(spCDisplayOutput _spDisplay) override;
    /// Release RTV (and depth) that reference the swap chain back buffer; call before ResizeBuffers.
    void PrepareForSwapChainResize();
    /// Call after IDXGISwapChain::ResizeBuffers so RTV/depth match the new backbuffer and Display sizes.
    bool RecreateRenderTargetsAfterResize();
    virtual void Defaults() override;
    virtual bool BeginFrame(void) override;
    virtual bool EndFrame(bool drawn = true) override;
    virtual void Apply() override;
    virtual void Reset(const uint32_t _flags) override;

    // Resource creation
    virtual spCTextureFlat NewTextureFlat(const uint32_t flags = 0) override;
    virtual spCTextureFlat NewTextureFlat(spCImage _spImage, const uint32_t flags = 0) override;
    virtual spCBaseFont GetFont(CFontDescription& _desc) override;
    virtual spCBaseText NewText(spCBaseFont _font, const std::string& _text) override;
    virtual spCShader NewShader(const char* _pVertexShader, const char* _pFragmentShader,
                               std::vector<std::pair<std::string, eUniformType>> uniforms = {}) override;

    // Drawing methods
    virtual void DrawText(spCBaseText _text, const Base::Math::CVector4& _color) override;
    virtual void DrawQuad(const Base::Math::CRect& _rect, const Base::Math::CVector4& _color) override;
    virtual void DrawQuad(const Base::Math::CRect& _rect, const Base::Math::CVector4& _color,
                         const Base::Math::CRect& _uvRect) override;
    virtual void DrawSoftQuad(const Base::Math::CRect& _rect, const Base::Math::CVector4& _color,
                             const float _width) override;

    float GetGPUFrameTimeMs() override;
    float GetGPUUtilization() override;

private:
    struct QuadUniforms
    {
        float rect[4];
        float uvRect[4];
        float color[4];
        float brightness;
        float padding[3];
    };

    struct Vertex
    {
        float pos[3];
        float tex[2];
    };

    ComPtr<ID3D11Buffer> m_quadVertexBuffer;
    ComPtr<ID3D11Buffer> m_quadIndexBuffer;
    ComPtr<ID3D11Buffer> m_quadUniformBuffer;
    spCShader m_drawTextureShader;
    bool CreateQuadBuffers();

    // GPU frame timing (D3D11 timestamp queries) — HUD parity with Metal.
    float m_avgGpuFrameTimeMs;
    int m_gpuTimingSlot;
    int m_gpuLastClosedSlot;
    bool m_gpuTimingOpen;
    bool m_gpuQueriesOk;
    ComPtr<ID3D11Query> m_disjointQuery[2];
    ComPtr<ID3D11Query> m_tsStart[2];
    ComPtr<ID3D11Query> m_tsEnd[2];

    bool CreateGpuTimingQueries();
    void ResolveGpuTimingSlot(int slot);
    void EndGpuTimingFrame();
    float DisplayRefreshHz() const;

    struct PendingTextDraw
    {
        spCBaseText text;
        Base::Math::CVector4 color;
    };
    std::vector<PendingTextDraw> m_pendingTextDraws;

    bool CreateRenderTargets();
    bool CreateBlendStates();
    bool CreateDepthStencilStates();
    bool CreateRasterizerStates();
};

MakeSmartPointers(CRendererDX11);

} // namespace DisplayOutput

#endif