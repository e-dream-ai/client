#include "RendererD3D12.h"
#include "DisplayD3D12.h"
#include "TextureFlatD3D12.h"

namespace DisplayOutput
{
    using namespace DirectX;
    int CRendererD3D12::textureIndex = 0;

CRendererD3D12::CRendererD3D12() 
{ 
	m_WindowHandle = NULL; 

    // Create the device resources object. This object owns the Direct3D device and all of the resources that are associated with it.
    m_deviceResources = std::make_unique<DeviceResources>();
    // TODO: Provide parameters for swapchain format, depth/stencil format, and backbuffer count.
    //   Add DX::DeviceResources::c_AllowTearing to opt-in to variable rate displays.
    //   Add DX::DeviceResources::c_EnableHDR for HDR10 display.
    //   Add DX::DeviceResources::c_ReverseDepth to optimize depth buffer clears for 0 instead of 1.
    m_deviceResources->RegisterDeviceNotify(this);  // Register to be notified if the device is lost or created.


}

CRendererD3D12::~CRendererD3D12() 
{
    if (m_deviceResources)
    {
        m_deviceResources->WaitForGpu();
    }
}

// IDeviceNotify
void CRendererD3D12::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.

    m_graphicsMemory.reset();
}

void CRendererD3D12::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}


// These are the resources that depend on the device.
void CRendererD3D12::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Check Shader Model 6 support
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {D3D_SHADER_MODEL_6_0};
    if (FAILED(device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
        (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0))
    {
        g_Log->Error("ERROR: Shader Model 6.0 is not supported!");
        throw std::runtime_error("Shader Model 6.0 is not supported!");
    }

    m_graphicsMemory = std::make_unique<GraphicsMemory>(device);

    m_states = std::make_unique<CommonStates>(device);

    m_resourceDescriptors = std::make_shared<DescriptorHeap>(device, Descriptors::Count);



    ResourceUploadBatch resourceUpload(device);

    resourceUpload.Begin();

    ThrowIfFailed(
        CreateWICTextureFromFile(device, resourceUpload, L"logo.png",
                                 m_texture.ReleaseAndGetAddressOf()));

    device->CreateShaderResourceView(
        m_texture.Get(), nullptr,
        m_resourceDescriptors->GetCpuHandle(0));

    auto uploadResourcesFinished =
        resourceUpload.End(m_deviceResources->GetCommandQueue());

    uploadResourcesFinished.wait();



    // TODO: Initialize device dependent objects here (independent of window size).
    
    // Create our batch renderer for drawing primitives.
    primitiveBatch = std::make_unique<PrimitiveBatch<VertexPositionColor>>(device);
    texturedBatch = std::make_unique<PrimitiveBatch<VertexPositionTexture>>(device);

    m_batch = std::make_unique<PrimitiveBatch<VertexPositionColor>>(device);

    const RenderTargetState rtState(m_deviceResources->GetBackBufferFormat(),
                                    m_deviceResources->GetDepthBufferFormat());

    //{
    //    EffectPipelineStateDescription pd(
    //        &VertexPositionColor::InputLayout, CommonStates::Opaque,
    //        CommonStates::DepthNone, CommonStates::CullNone, rtState,
    //        D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);

    //    m_lineEffect = std::make_unique<BasicEffect>(
    //        device, EffectFlags::VertexColor, pd);
    //}

    {
        EffectPipelineStateDescription pd(
            &VertexPositionTexture::InputLayout, CommonStates::Opaque,
            CommonStates::DepthNone, CommonStates::CullNone, rtState);

        m_textureEffect =
            std::make_unique<BasicEffect>(device, EffectFlags::Texture, pd);
        m_textureEffect->SetTexture(
            m_resourceDescriptors->GetGpuHandle(Descriptors::Font),
            m_states->LinearWrap());
    }
}

// Allocate all memory resources that change on a window SizeChanged event.
void CRendererD3D12::CreateWindowSizeDependentResources()
{
    // TODO: Initialize windows-size dependent objects here.
}


bool CRendererD3D12::Initialize(spCDisplayOutput _spDisplay) 
{ 
    m_spDisplay = _spDisplay;
	m_WindowHandle = m_spDisplay->GetWindowHandle();

    // Initialize Direct3D
    m_deviceResources->SetWindow(m_WindowHandle, m_spDisplay->Width(),
                                     m_spDisplay->Height());

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();


    return true; 
}

void CRendererD3D12::Defaults() {}

void CRendererD3D12::Clear()
{
    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Clear");

    // Clear the views.
    auto const rtvDescriptor = m_deviceResources->GetRenderTargetView();
    auto const dsvDescriptor = m_deviceResources->GetDepthStencilView();

    commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
    //commandList->ClearRenderTargetView(rtvDescriptor, Colors::Black, 0,
    //                                   nullptr);
    commandList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH,
                                       1.0f, 0, 0, nullptr);

    // Set the viewport and scissor rect.
    auto const viewport = m_deviceResources->GetScreenViewport();
    auto const scissorRect = m_deviceResources->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    PIXEndEvent(commandList);
}

bool CRendererD3D12::BeginFrame(void) {

    if (!CRenderer::BeginFrame())
        return false;

    //if (TestResetDevice())
    //    return false;

    // Prepare the command list to render a new frame.
    m_deviceResources->Prepare();
    Clear();

    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Render");
    PIXEndEvent(commandList);

    // Draw procedurally generated dynamic grid
    //const XMVECTORF32 xaxis = {20.f, 0.f, 0.f};
    //const XMVECTORF32 yaxis = {0.f, 0.f, 20.f};
    //DrawGrid(xaxis, yaxis, g_XMZero, 20, 20, Colors::Gray);
    
    return true;  
}

void XM_CALLCONV CRendererD3D12::DrawGrid(FXMVECTOR xAxis, FXMVECTOR yAxis,
                                FXMVECTOR origin, size_t xdivs, size_t ydivs,
                                GXMVECTOR color)
{
    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Draw grid");

    m_lineEffect->Apply(commandList);

    m_batch->Begin(commandList);

    xdivs = std::max<size_t>(1, xdivs);
    ydivs = std::max<size_t>(1, ydivs);

    for (size_t i = 0; i <= xdivs; ++i)
    {
        float fPercent = float(i) / float(xdivs);
        fPercent = (fPercent * 2.0f) - 1.0f;
        XMVECTOR vScale = XMVectorScale(xAxis, fPercent);
        vScale = XMVectorAdd(vScale, origin);

        const VertexPositionColor v1(XMVectorSubtract(vScale, yAxis), color);
        const VertexPositionColor v2(XMVectorAdd(vScale, yAxis), color);
        m_batch->DrawLine(v1, v2);
    }

    for (size_t i = 0; i <= ydivs; i++)
    {
        float fPercent = float(i) / float(ydivs);
        fPercent = (fPercent * 2.0f) - 1.0f;
        XMVECTOR vScale = XMVectorScale(yAxis, fPercent);
        vScale = XMVectorAdd(vScale, origin);

        const VertexPositionColor v1(XMVectorSubtract(vScale, xAxis), color);
        const VertexPositionColor v2(XMVectorAdd(vScale, xAxis), color);
        m_batch->DrawLine(v1, v2);
    }

    m_batch->End();

    PIXEndEvent(commandList);
}


bool CRendererD3D12::EndFrame(bool drawn) 
{ 
    g_Log->Info("CRendererD3D12::EndFrame %d", drawn);
    if (!CRenderer::EndFrame())
        return false;

    //if (TestResetDevice())
    //    return false;

    // Show the new frame.
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Present");
    m_deviceResources->Present();

    // If using the DirectX Tool Kit for DX12, uncomment this line:
    m_graphicsMemory->Commit(m_deviceResources->GetCommandQueue());

    PIXEndEvent();

    return true; 
}


void CRendererD3D12::Apply() { 
    CRenderer::Apply(); 
}
void CRendererD3D12::Reset(const uint32_t _flags) {
    CRenderer::Reset(_flags);
}

// We probably can get rid of that...
bool CRendererD3D12::TestResetDevice() { return false; }

spCTextureFlat CRendererD3D12::NewTextureFlat(const uint32_t flags) 
{
    CRendererD3D12::textureIndex++;

    g_Log->Info("CRendererD3D12::NewTextureFlat %d",
                CRendererD3D12::textureIndex);

    spCTextureFlat texture = std::make_shared<CTextureFlatD3D12>(
        GetDevice(), GetCommandQueue(), GetResourceDescriptors(), this,
        CRendererD3D12::textureIndex, flags);

    return texture;
}

spCTextureFlat CRendererD3D12::NewTextureFlat(spCImage _spImage, const uint32_t flags) 
{
    CRendererD3D12::textureIndex++;

    g_Log->Info("CRendererD3D12::NewTextureFlat2 %d",
                CRendererD3D12::textureIndex);

    spCTextureFlat texture = std::make_shared<CTextureFlatD3D12>(
        GetDevice(), GetCommandQueue(), GetResourceDescriptors(), this,
        CRendererD3D12::textureIndex, flags);
    texture->Upload(_spImage);

    return texture;
}

void CRendererD3D12::BindTexture(int index) 
{
    g_Log->Info("CRendererD3D12::BindTexture %d", index);
    m_currentTextureIndex = index;
}


spCBaseFont CRendererD3D12::NewFont(CFontDescription& _desc) { return spCBaseFont(); }

void CRendererD3D12::Text(spCBaseFont _spFont, const std::string& _text,
                          const Base::Math::CVector4& _color,
                          const Base::Math::CRect& _rect, uint32_t _flags)
{
}

Base::Math::CVector2 CRendererD3D12::GetTextExtent(spCBaseFont _spFont, const std::string& _text) { return Base::Math::CVector2(); }


void CRendererD3D12::DrawQuad(const Base::Math::CRect& _rect,
                              const Base::Math::CVector4& _color)
{
    g_Log->Info("CRendererD3D12::DrawQuad1 not impl");
}

void CRendererD3D12::DrawQuad(const Base::Math::CRect& _rect,
    const Base::Math::CVector4& _color,
    const Base::Math::CRect& _uvRect)
{
    g_Log->Info("CRendererD3D12::DrawQuad2");
    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Draw quad2");

    // Set the descriptor heaps
    ID3D12DescriptorHeap* heaps[] = {m_resourceDescriptors->Heap(),
                                     m_states->Heap()};
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    
    // Looks like we need to create a new BasicEffect for each texture in the same pass?
    auto device = m_deviceResources->GetD3DDevice();

    const RenderTargetState rtState(m_deviceResources->GetBackBufferFormat(),
                                    m_deviceResources->GetDepthBufferFormat());

    EffectPipelineStateDescription pd(
        &VertexPositionTexture::InputLayout, CommonStates::AlphaBlend,
        CommonStates::DepthNone, CommonStates::CullNone, rtState);

    auto textureEffect =
        std::make_unique<BasicEffect>(device, EffectFlags::Texture, pd);

    // Index got from BindTexture
    textureEffect->SetTexture(m_resourceDescriptors->GetGpuHandle(m_currentTextureIndex),
                              m_states->LinearWrap());

    textureEffect->Apply(commandList);
    //

    auto l_texturedBatch =
        std::make_unique<PrimitiveBatch<VertexPositionTexture>>(device);
    // Maybe we should not reuse batch either?
    l_texturedBatch->Begin(commandList);
 
    // Y texture axis inverted on DX from whatever we get passed to us
    VertexPositionTexture v1(
        XMFLOAT3(_rect.m_X0 * 2 - 1, 1 - _rect.m_Y0 * 2, 0),
        XMFLOAT2(_uvRect.m_X0, _uvRect.m_Y0));
    VertexPositionTexture v2(
        XMFLOAT3(_rect.m_X1 * 2 - 1, 1 - _rect.m_Y0 * 2, 0),
        XMFLOAT2(_uvRect.m_X1, _uvRect.m_Y0));
    VertexPositionTexture v3(
        XMFLOAT3(_rect.m_X1 * 2 - 1, 1 - _rect.m_Y1 * 2, 0),
        XMFLOAT2(_uvRect.m_X1, _uvRect.m_Y1));
    VertexPositionTexture v4(
        XMFLOAT3(_rect.m_X0 * 2 - 1, 1 - _rect.m_Y1 * 2, 0),
        XMFLOAT2(_uvRect.m_X0, _uvRect.m_Y1));

    l_texturedBatch->DrawQuad(v1, v2, v3, v4);

    l_texturedBatch->End();

    PIXEndEvent(commandList);

}
void CRendererD3D12::DrawSoftQuad(const Base::Math::CRect& /*_rect*/,
                          const Base::Math::CVector4& /*_color*/,
                                  const float /*_width*/)
{
    g_Log->Info("CRendererD3D12::DrawSoftQuad not impl");
    // TODO : used by text rendering
}

spCShader CRendererD3D12::NewShader(const char* _pVertexShader,
                                    const char* _pFragmentShader)
{
    g_Log->Info("CRendererD3D12::NewShader not impl");

    return nullptr;
}

spCShader CRendererD3D12::NewShader(const char* _pVertexShader, const char* _pFragmentShader,
    std::vector<std::pair<std::string, eUniformType>> _uniforms)
{
    g_Log->Info("CRendererD3D12::NewShader2 not impl");

    return nullptr;
}



} // namespace DisplayOutput