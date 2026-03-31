#import <Metal/MTLRenderPipeline.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <CoreText/CoreText.h>

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
    id<MTLRenderCommandEncoder> currentRenderEncoder;
  @public
    DisplayOutput::spCShaderMetal drawTextureShader;
  @public
    DisplayOutput::spCShaderMetal activeShader;
  @public
    std::atomic<uint8_t> framesStarted;
  @public
    std::atomic<uint8_t> framesFinished;
  @public
    std::atomic<float> avgGpuFrameTimeMs;
}
@end

@implementation RendererContext
@end

namespace DisplayOutput
{

typedef std::shared_lock<std::shared_mutex> reader_lock;
typedef std::unique_lock<std::shared_mutex> writer_lock;

/*
 */
CRendererMetal::CRendererMetal() : CRenderer() {}

/*
 */
CRendererMetal::~CRendererMetal()
{
    RendererContext* rendererContext = CFBridgingRelease(m_pRendererContext);
    //Make sure that all the semaphores are unblocked before letting ARC release the object
    dispatch_semaphore_signal(rendererContext->inFlightSemaphore);
    dispatch_semaphore_signal(rendererContext->inFlightSemaphore);
    dispatch_semaphore_signal(rendererContext->inFlightSemaphore);
    usleep(1000);
    
    *m_pIsAlive = false;
    //    while (rendererContext->framesStarted.load() !=
    //           rendererContext->framesFinished.load())
    //    {
    //        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    //    }
    if (rendererContext->textureCache)
    {
        CFRelease(rendererContext->textureCache);
    }
    m_pRendererContext = nullptr;
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
    // Register bundled Lato font so CATextLayer can find it by name.
    // This is needed because screensavers run inside ScreenSaverEngine,
    // where plist-based font registration (ATSApplicationFontsPath) does not apply.
    NSBundle* fontBundle = [NSBundle bundleForClass:RendererContext.class];
    NSString* fontPath = [fontBundle pathForResource:@"Lato-Regular" ofType:@"ttf"];
    if (fontPath) {
        NSURL* fontURL = [NSURL fileURLWithPath:fontPath];
        CFErrorRef cfError = NULL;
        if (!CTFontManagerRegisterFontsForURL((__bridge CFURLRef)fontURL,
                                               kCTFontManagerScopeProcess,
                                               &cfError)) {
            // kCTFontManagerErrorAlreadyRegistered is fine — ignore it
            if (cfError) CFRelease(cfError);
        }
    }

    m_pIsAlive = new bool(true);
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
    // Key font cache by typeface AND size so size changes take effect.
    std::string key = _desc.TypeFace() + "#" + std::to_string((int)_desc.Height());
    auto it = m_fontPool.find(key);
    if (it == m_fontPool.end())
    {
        spCBaseFont font =
            std::make_shared<CFontMetal>(_desc);
        if (!font->Create())
            return NULL;
        font->FontDescription(_desc);
        m_fontPool[key] = font;
        return font;
    }
    return it->second;
}

spCBaseText CRendererMetal::NewText(spCBaseFont _font, const std::string& _text)
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    CTextMetal* text = new CTextMetal(static_pointer_cast<CFontMetal>(_font),
                                      rendererContext->metalView);
    text->SetText(_text);

    return spCBaseText(text);
}

Base::Math::CVector2 CRendererMetal::GetTextExtent(spCBaseFont /*_spFont*/,
                                                   const std::string& /*_text*/)
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
    PROFILER_BEGIN_F("Metal Frame", "%d", rendererContext->frameCounter);
    id<MTLCommandQueue> commandQueue = rendererContext->commandQueue;
    rendererContext->currentCommandBuffer = [commandQueue commandBuffer];

    // Single render pass for the entire frame: clear once, draw everything,
    // end in EndFrame(). This avoids per-DrawQuad render passes which each
    // force a full framebuffer load/store on Apple's tile-based GPU.
    @autoreleasepool
    {
        id<CAMetalDrawable> drawable =
            rendererContext->metalView.currentDrawable;
        id<MTLTexture> mainTex = drawable.texture;
        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture = drawable.texture;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

        rendererContext->currentRenderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];
    }
    return true;
}

bool CRendererMetal::EndFrame(bool drawn)
{
    CRenderer::EndFrame(drawn);
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;

    // End the frame's single render pass.
    [rendererContext->currentRenderEncoder endEncoding];
    rendererContext->currentRenderEncoder = nil;
    __block dispatch_semaphore_t semaphore = rendererContext->inFlightSemaphore;
    __block std::vector<CVMetalTextureRef>& metalTexturesUsed =
        rendererContext->metalTexturesUsed[rendererContext->currentFrameIndex];
    __block uint32_t frameNumber = rendererContext->frameCounter;
    __block std::atomic<uint8_t>& framesFinished =
        rendererContext->framesFinished;
    __block std::atomic<float>& avgGpuFrameTimeMs =
        rendererContext->avgGpuFrameTimeMs;
    rendererContext->framesStarted++;
    bool* isAlivePtr = m_pIsAlive;
    [rendererContext->currentCommandBuffer
        addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull commandBuffer) {
            {
                if (!*isAlivePtr)
                    return;
                writer_lock lock(m_textureMutex);
                for (auto it = metalTexturesUsed.begin();
                     it != metalTexturesUsed.end(); ++it)
                {
                    CVMetalTextureRef metalTexture = *it;
                    CVBufferRelease(metalTexture);
                }
                metalTexturesUsed.clear();
            }
            // Measure actual GPU execution time via Metal timestamps.
            float gpuTimeMs =
                (float)(commandBuffer.GPUEndTime -
                        commandBuffer.GPUStartTime) * 1000.0f;
            float prev = avgGpuFrameTimeMs.load(std::memory_order_relaxed);
            avgGpuFrameTimeMs.store(prev * 0.99f + gpuTimeMs * 0.01f,
                                    std::memory_order_relaxed);
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
    // Clear is handled by MTLLoadActionClear in BeginFrame().
}

void CRendererMetal::DrawText(spCBaseText _text,
                              const Base::Math::CVector4& _color)
{
    if (_text) {
        spCTextMetal textMetal = std::static_pointer_cast<CTextMetal>(_text);
        if (textMetal) {
            textMetal->SetColor(_color);
        }
    }
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

        // Reuse the frame's single render encoder from BeginFrame().
        id<MTLRenderCommandEncoder> renderEncoder =
            rendererContext->currentRenderEncoder;
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
            else
            {
                // Clear stale texture bindings from previous draw calls
                // that used this same render encoder.  Without this, a
                // DrawQuad with no texture (e.g. DrawSoftQuad overlay)
                // would sample whatever texture the previous draw left
                // bound, causing ghost images.
                [renderEncoder setFragmentTexture:nil atIndex:i * 2 + 0];
                [renderEncoder setFragmentTexture:nil atIndex:i * 2 + 1];
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
        uniforms.brightness = m_Brightness;

        [renderEncoder setFragmentBytes:&uniforms
                                 length:sizeof(uniforms)
                                atIndex:0];
        activeShader->UploadUniforms(renderEncoder);
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                          vertexStart:0
                          vertexCount:4];
    }
}

void CRendererMetal::DrawQuad(const Base::Math::CRect& _rect,
                              const Base::Math::CVector4& _color)
{
    DrawQuad(_rect, _color, Base::Math::CRect{0, 0, 1, 1});
}

void CRendererMetal::DrawSoftQuad(const Base::Math::CRect& _rect,
                                  const Base::Math::CVector4& _color,
                                  const float /*_width*/)
{
    DrawQuad(_rect, _color, Base::Math::CRect{0, 0, 1, 1});
}


float CRendererMetal::GetGPUFrameTimeMs()
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    return rendererContext->avgGpuFrameTimeMs.load(std::memory_order_relaxed);
}

float CRendererMetal::GetGPUUtilization()
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    float gpuMs =
        rendererContext->avgGpuFrameTimeMs.load(std::memory_order_relaxed);
    float budgetMs =
        1000.0f / rendererContext->metalView.preferredFramesPerSecond;
    return gpuMs / budgetMs * 100.0f;
}

} // namespace DisplayOutput
