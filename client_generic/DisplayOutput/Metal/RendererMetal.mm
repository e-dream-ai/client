#import <Metal/MTLRenderPipeline.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#import "TextRendering/MBEMathUtilities.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "RendererMetal.h"
#include "ShaderMetal.h"
#include "ShaderUniforms.h"
#include "TextMetal.h"
#include "TextureFlatMetal.h"

static const NSUInteger MaxFramesInFlight = 3;

@interface RendererContext : NSObject
{
  @public
    MTKView* metalView;
  @public
    id<MTLCommandQueue> commandQueue;
  @public
    id<MTLCommandBuffer> currentCommandBuffer;
  @public
    id<MTLTexture> depthTexture;
  @public
    id<MTLLibrary> shaderLibrary;
  @public
    std::vector<CVMetalTextureRef> metalTexturesUsed[MaxFramesInFlight];
  @public
    CVMetalTextureCacheRef textureCache;
  @public
    dispatch_semaphore_t inFlightSemaphore;
  @public
    uint8_t drawCallsInFrame[MaxFramesInFlight];
  @public
    uint32_t frameCounter;
  @public
    uint8_t currentFrameIndex;
  @public
    MTLLoadAction currentLoadAction;
  @public
    DisplayOutput::spCShaderMetal drawTextureShader;
  @public
    DisplayOutput::spCShaderMetal drawTextShader;
  @public
    DisplayOutput::spCShaderMetal activeShader;
  @public
    std::atomic<uint8_t> framesStarted;
  @public
    std::atomic<uint8_t> framesFinished;
}
@end

@implementation RendererContext
@end

namespace DisplayOutput
{

typedef boost::shared_lock<boost::shared_mutex> reader_lock;
typedef boost::unique_lock<boost::shared_mutex> writer_lock;

/*
 */
CRendererMetal::CRendererMetal() : CRenderer() {}

/*
 */
CRendererMetal::~CRendererMetal()
{
    RendererContext* rendererContext = CFBridgingRelease(m_pRendererContext);
    while (rendererContext->framesStarted.load() !=
           rendererContext->framesFinished.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (rendererContext->textureCache)
    {
        CFRelease(rendererContext->textureCache);
    }
}

static MTLVertexDescriptor* CreateTextVertexDescriptor()
{
    MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor new];

    // Position
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;

    // Texture coordinates
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(vector_float4);
    vertexDescriptor.attributes[1].bufferIndex = 0;

    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    vertexDescriptor.layouts[0].stride = sizeof(VertexText);

    return vertexDescriptor;
}

bool CRendererMetal::Initialize(spCDisplayOutput _spDisplay)
{
    if (!CRenderer::Initialize(_spDisplay))
        return false;

    MTKView* metalView = (__bridge MTKView*)m_spDisplay->GetContext();
    id<MTLDevice> device;
    ASSERT(device = metalView.device);

    RendererContext* rendererContext = [[RendererContext alloc] init];
    m_pRendererContext = CFBridgingRetain(rendererContext);
    rendererContext->metalView = metalView;
    ASSERT(rendererContext->commandQueue = [device newCommandQueue]);

    rendererContext->inFlightSemaphore =
        dispatch_semaphore_create(MaxFramesInFlight);
    NSError* err;
    rendererContext->shaderLibrary = [device
        newLibraryWithFile:[[NSBundle bundleForClass:RendererContext.class]
                               pathForResource:@"default"
                                        ofType:@"metallib"]
                     error:&err];
    NSAssert(err == nil, @"%@", error);
    ASSERT(rendererContext->shaderLibrary);

    rendererContext->drawTextureShader = std::static_pointer_cast<CShaderMetal>(
        NewShader("quadPassVertex", "drawTextureFragment"));
    rendererContext->drawTextShader = spCShaderMetal(new CShaderMetal(
        device,
        [rendererContext->shaderLibrary newFunctionWithName:@"drawTextVertex"],
        [rendererContext
             ->shaderLibrary newFunctionWithName : @"drawTextFragment"],
        CreateTextVertexDescriptor(), {}));

    if (USE_HW_ACCELERATION)
    {
        CVReturn textureCacheError =
            CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL,
                                      &rendererContext->textureCache);
        if (textureCacheError != kCVReturnSuccess)
        {
            g_Log->Error("Error creating CVTextureCache:%d", textureCacheError);
        }
    }
    return true;
}

void CRendererMetal::Defaults() {}

void CRendererMetal::Apply() { CRenderer::Apply(); }

void CRendererMetal::Reset(const uint32_t _flags) { CRenderer::Reset(_flags); }

spCTextureFlat CRendererMetal::NewTextureFlat(spCImage _spImage,
                                              const uint32_t _flags)
{
    spCTextureFlat spTex =
        std::make_shared<CTextureFlatMetal>(m_spDisplay->GetContext(), _flags);
    spTex->Upload(_spImage);
    return spTex;
}

spCTextureFlat CRendererMetal::NewTextureFlat(const uint32_t _flags)
{
    spCTextureFlat spTex =
        std::make_shared<CTextureFlatMetal>(m_spDisplay->GetContext(), _flags);
    return spTex;
}

spCShader CRendererMetal::NewShader(
    const char* _pVertexShader, const char* _pFragmentShader,
    std::vector<std::pair<std::string, eUniformType>> _uniforms)
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    id<MTLFunction> vertexFunc =
        [rendererContext->shaderLibrary newFunctionWithName:@(_pVertexShader)];
    id<MTLFunction> fragmentFunc = [rendererContext->shaderLibrary
        newFunctionWithName:@(_pFragmentShader)];
    ASSERT(vertexFunc);
    ASSERT(fragmentFunc);
    return std::make_shared<CShaderMetal>(rendererContext->metalView.device,
                                          vertexFunc, fragmentFunc, nil,
                                          _uniforms);
}

spCBaseFont CRendererMetal::GetFont(CFontDescription& _desc)
{
    auto it = m_fontPool.find(_desc.TypeFace());
    if (it == m_fontPool.end())
    {
        spCBaseFont font =
            std::make_shared<CFontMetal>(_desc, NewTextureFlat());
        if (!font->Create())
            return NULL;
        font->FontDescription(_desc);
        m_fontPool[_desc.TypeFace()] = font;
        return font;
    }
    return it->second;
}

spCBaseText CRendererMetal::NewText(spCBaseFont _font, const std::string& _text)
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    float aspect = m_spDisplay->Aspect();
    CTextMetal* text = new CTextMetal(static_pointer_cast<CFontMetal>(_font),
                                      rendererContext->metalView, aspect);
    text->SetText(_text);

    return spCBaseText(text);
}

Base::Math::CVector2 CRendererMetal::GetTextExtent(spCBaseFont _spFont,
                                                   const std::string& _text)
{
    return Base::Math::CVector2(0, 0.05f);
}

bool CRendererMetal::BeginFrame(void)
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    rendererContext->currentFrameIndex =
        rendererContext->frameCounter++ % MaxFramesInFlight;
    dispatch_semaphore_wait(rendererContext->inFlightSemaphore,
                            DISPATCH_TIME_FOREVER);
    id<MTLCommandQueue> commandQueue = rendererContext->commandQueue;
    rendererContext->currentCommandBuffer = [commandQueue commandBuffer];
    rendererContext->currentLoadAction = MTLLoadActionClear;
    PROFILER_BEGIN_F("Metal Frame", "%d", rendererContext->frameCounter);
    return true;
}

bool CRendererMetal::EndFrame(bool drawn)
{
    CRenderer::EndFrame(drawn);
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    __block dispatch_semaphore_t semaphore = rendererContext->inFlightSemaphore;
    __block std::vector<CVMetalTextureRef>& metalTexturesUsed =
        rendererContext->metalTexturesUsed[rendererContext->currentFrameIndex];
    __block uint32_t frameNumber = rendererContext->frameCounter;
    __block std::atomic<uint8_t>& framesFinished =
        rendererContext->framesFinished;
    rendererContext->framesStarted++;
    [rendererContext->currentCommandBuffer
        addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
            {
                writer_lock lock(m_textureMutex);
                for (auto it = metalTexturesUsed.begin();
                     it != metalTexturesUsed.end(); ++it)
                {
                    CVMetalTextureRef metalTexture = *it;
                    CVBufferRelease(metalTexture);
                }
                metalTexturesUsed.clear();
            }
            PROFILER_EVENT_F("Metal Frame Finished", "%d", frameNumber);
            dispatch_semaphore_signal(semaphore);
            framesFinished++;
        }];

    @autoreleasepool
    {
        auto drawable = rendererContext->metalView.currentDrawable;
        if (drawable)
        {
            [rendererContext->currentCommandBuffer presentDrawable:drawable];
        }
    }
    [rendererContext->currentCommandBuffer commit];
    PROFILER_END_F("Metal Frame", "%d", rendererContext->frameCounter);
    return true;
}

void CRendererMetal::Clear()
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    @autoreleasepool
    {
        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        if (passDescriptor != nil)
        {
            passDescriptor.colorAttachments[0].texture =
                rendererContext->metalView.currentDrawable.texture;
            passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
            passDescriptor.colorAttachments[0].storeAction =
                MTLStoreActionStore;

            passDescriptor.depthAttachment.texture =
                rendererContext->depthTexture;
            passDescriptor.depthAttachment.clearDepth = 1.0;
            passDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
            passDescriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
        }

        id<MTLRenderCommandEncoder> renderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];

        [renderEncoder endEncoding];
    }
}

void CRendererMetal::DrawText(spCBaseText _text,
                              const Base::Math::CVector4& _color)
{
#if !USE_SYSTEM_UI
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    @autoreleasepool
    {
        spCTextMetal textMetal = static_cast<spCTextMetal>(_text);
        const MBETextMesh* textMesh = textMetal->GetTextMesh();

        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        if (passDescriptor != nil)
        {
            passDescriptor.colorAttachments[0].texture =
                rendererContext->metalView.currentDrawable.texture;
            passDescriptor.colorAttachments[0].loadAction =
                rendererContext->currentLoadAction;
            passDescriptor.colorAttachments[0].storeAction =
                MTLStoreActionStore;

            passDescriptor.depthAttachment.texture =
                rendererContext->depthTexture;
            passDescriptor.depthAttachment.clearDepth = 1.0;
            passDescriptor.depthAttachment.loadAction =
                rendererContext->currentLoadAction;
            passDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
        }

        id<MTLRenderCommandEncoder> renderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];
        [renderEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [renderEncoder setCullMode:MTLCullModeNone];
        [renderEncoder setRenderPipelineState:rendererContext->drawTextShader
                                                  ->GetPipelineState()];
        [renderEncoder setVertexBuffer:textMesh.vertexBuffer
                                offset:0
                               atIndex:0];

        vector_float3 translation = {_text->GetRect().m_X0,
                                     _text->GetRect().m_Y0, 0};
        vector_float3 scale = {1, 1, 1};
        float aspect = m_spDisplay->Aspect();
        matrix_float4x4 modelMatrix = matrix_multiply(
            matrix_translation(translation * kMetalTextReferenceContextSize),
            matrix_scale(scale * vector_float3{aspect, 1, 1}));
        matrix_float4x4 projectionMatrix =
            matrix_orthographic_projection(0, kMetalTextReferenceContextSize, 0,
                                           kMetalTextReferenceContextSize);

        TextUniforms uniforms;
        uniforms.modelMatrix = modelMatrix;
        uniforms.viewProjectionMatrix = projectionMatrix;
        uniforms.foregroundColor = {_color.m_X, _color.m_Y, _color.m_Z,
                                    _color.m_W};

        [renderEncoder setVertexBytes:&uniforms
                               length:sizeof(uniforms)
                              atIndex:1];
        [renderEncoder setFragmentBytes:&uniforms
                                 length:sizeof(uniforms)
                                atIndex:0];
        spCTextMetal metalText = static_cast<spCTextMetal>(_text);
        spCFontMetal font = metalText->GetFont();
        id<MTLTexture> atlasTexture =
            font->GetAtlasTexture()->GetRGBMetalTexture();
        [renderEncoder setFragmentTexture:atlasTexture atIndex:0];

        [renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                  indexCount:[textMesh.indexBuffer length] /
                                             sizeof(uint16_t)
                                   indexType:MTLIndexTypeUint16_t
                                 indexBuffer:textMesh.indexBuffer
                           indexBufferOffset:0];

        [renderEncoder endEncoding];
        rendererContext->currentLoadAction = MTLLoadActionLoad;
    }
#endif /*USE_SYSTEM_UI*/
}

bool CRendererMetal::CreateMetalTextureFromDecoderFrame(
    CVPixelBufferRef pixelBuffer, CVMetalTextureRef* _outMetalTextureRef,
    uint32_t plane)
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;

    CVReturn ret;

    ret = CVMetalTextureCacheCreateTextureFromImage(
        NULL, rendererContext->textureCache, pixelBuffer, NULL,
        plane == 0 ? MTLPixelFormatR8Unorm : MTLPixelFormatRG8Unorm,
        CVPixelBufferGetWidthOfPlane(pixelBuffer, plane),
        CVPixelBufferGetHeightOfPlane(pixelBuffer, plane), plane,
        _outMetalTextureRef);
    if (ret != kCVReturnSuccess)
    {
        g_Log->Error("Failed to create CVMetalTexture from image: %d", ret);
        return false;
    }
    return true;
}

bool CRendererMetal::GetYUVMetalTextures(spCTextureFlatMetal _texture,
                                         CVMetalTextureRef* _outYTexture,
                                         CVMetalTextureRef* _outUVTexture)
{
    CVPixelBufferRef pixelBuffer = _texture->GetPixelBuffer();
    CreateMetalTextureFromDecoderFrame(pixelBuffer, _outYTexture, 0);
    CreateMetalTextureFromDecoderFrame(pixelBuffer, _outUVTexture, 1);
    return true;
}

void CRendererMetal::DrawQuad(const Base::Math::CRect& _rect,
                              const Base::Math::CVector4& _color,
                              const Base::Math::CRect& _uvrect)
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    @autoreleasepool
    {
        spCShaderMetal activeShader =
            m_spSelectedShader != nullptr
                ? std::static_pointer_cast<CShaderMetal>(m_spSelectedShader)
                : rendererContext->drawTextureShader;
        id<MTLRenderPipelineState> renderPipelineState =
            activeShader->GetPipelineState();
        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        if (passDescriptor != nil)
        {
            id<CAMetalDrawable> drawable =
                rendererContext->metalView.currentDrawable;
            id<MTLTexture> mainTex = drawable.texture;
            if (rendererContext->depthTexture == nil ||
                mainTex.width != rendererContext->depthTexture.width ||
                mainTex.height != rendererContext->depthTexture.height)
            {
                BuildDepthTexture();
            }
            passDescriptor.colorAttachments[0].texture =
                rendererContext->metalView.currentDrawable.texture;
            passDescriptor.colorAttachments[0].loadAction =
                rendererContext->currentLoadAction;
            passDescriptor.colorAttachments[0].storeAction =
                MTLStoreActionStore;

            passDescriptor.depthAttachment.texture =
                rendererContext->depthTexture;
            passDescriptor.depthAttachment.clearDepth = 1.0;
            passDescriptor.depthAttachment.loadAction =
                rendererContext->currentLoadAction;
            ;
            passDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
        }

        id<MTLRenderCommandEncoder> renderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];
        [renderEncoder setRenderPipelineState:renderPipelineState];
        std::vector<CVMetalTextureRef>& metalTexturesUsed =
            rendererContext
                ->metalTexturesUsed[rendererContext->currentFrameIndex];

        for (uint32_t i = 0; i < MAX_TEXUNIT; ++i)
        {
            spCTextureFlatMetal selectedTexture =
                static_pointer_cast<CTextureFlatMetal>(
                    m_aspSelectedTextures[i]);
            if (selectedTexture)
            {
                if (selectedTexture->IsYUVTexture())
                {
                    CVMetalTextureRef yTextureRef;
                    CVMetalTextureRef uvTextureRef;
                    if (GetYUVMetalTextures(selectedTexture, &yTextureRef,
                                            &uvTextureRef))
                    {
                        id<MTLTexture> yTexture =
                            CVMetalTextureGetTexture(yTextureRef);
                        id<MTLTexture> uvTexture =
                            CVMetalTextureGetTexture(uvTextureRef);
                        [renderEncoder setFragmentTexture:yTexture
                                                  atIndex:i * 2 + 0];
                        [renderEncoder setFragmentTexture:uvTexture
                                                  atIndex:i * 2 + 1];
                        {
                            writer_lock lock(m_textureMutex);
                            metalTexturesUsed.push_back(yTextureRef);
                            metalTexturesUsed.push_back(uvTextureRef);
                        }
                    }
                    else
                    {
                        g_Log->Error("Unable to get Metal textures.");
                    }
                }
                else
                {
                    id<MTLTexture> rgbTexture =
                        selectedTexture->GetRGBMetalTexture();
                    [renderEncoder setFragmentTexture:rgbTexture atIndex:i];
                }
            }
        }
        QuadUniforms uniforms;
        uniforms.color =
            vector_float4{_color.m_X, _color.m_Y, _color.m_Z, _color.m_W};
        uniforms.rect =
            vector_float4{_rect.m_X0, _rect.m_Y0, _rect.m_X1 - _rect.m_X0,
                          _rect.m_Y1 - _rect.m_Y0};
        uniforms.uvRect = vector_float4{_uvrect.m_X0, _uvrect.m_Y0,
                                        _uvrect.m_X1 - _uvrect.m_X0,
                                        _uvrect.m_Y1 - _uvrect.m_Y0};

        [renderEncoder setFragmentBytes:&uniforms
                                 length:sizeof(uniforms)
                                atIndex:0];
        activeShader->UploadUniforms(renderEncoder);
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                          vertexStart:0
                          vertexCount:4];
        [renderEncoder endEncoding];
        rendererContext->currentLoadAction = MTLLoadActionLoad;
    }
}

void CRendererMetal::DrawQuad(const Base::Math::CRect& _rect,
                              const Base::Math::CVector4& _color)
{
    DrawQuad(_rect, _color, Base::Math::CRect{0, 0, 1, 1});
}

void CRendererMetal::DrawSoftQuad(const Base::Math::CRect& _rect,
                                  const Base::Math::CVector4& _color,
                                  const float _width)
{
    DrawQuad(_rect, _color, Base::Math::CRect{0, 0, 1, 1});
}

void CRendererMetal::BuildDepthTexture()
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    id<MTLDevice> device = rendererContext->metalView.device;
    id<MTLTexture> texture = rendererContext->metalView.currentDrawable.texture;
    MTLTextureDescriptor* depthTextureDescriptor =
        [[MTLTextureDescriptor alloc] init];
    depthTextureDescriptor.pixelFormat = MTLPixelFormatDepth32Float;
    depthTextureDescriptor.width = texture.width;
    depthTextureDescriptor.height = texture.height;
    depthTextureDescriptor.storageMode = MTLStorageModePrivate;
    depthTextureDescriptor.usage = MTLTextureUsageRenderTarget;

    rendererContext->depthTexture =
        [device newTextureWithDescriptor:depthTextureDescriptor];
}

} // namespace DisplayOutput
