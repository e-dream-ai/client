#import <Metal/MTLRenderPipeline.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <CoreText/CoreText.h>
#import <AppKit/AppKit.h>

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
#ifndef SCREEN_SAVER
#include "SyphonOutput.h"
#endif

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
#ifndef SCREEN_SAVER
  @public
    // Offscreen video target when Syphon is on: published before HUD overlays
    // so OSD icons/indicators never appear in the Syphon feed. Also avoids
    // CAMetalLayer nextDrawable stalls (~3Hz) when the window is occluded.
    id<MTLTexture> syphonOffscreenTexture;
  @public
    BOOL renderedToSyphonOffscreen;
  @public
    BOOL syphonPublishedThisFrame;
  @public
    BOOL presentingToDrawable;
#endif
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

#ifndef SCREEN_SAVER
static bool WindowIsFullyOccluded(NSWindow* window)
{
    if (!window)
        return true;
    return (window.occlusionState & NSWindowOcclusionStateVisible) == 0;
}

static id<MTLTexture> EnsureSyphonOffscreenTexture(RendererContext* ctx,
                                                   NSUInteger width,
                                                   NSUInteger height)
{
    if (width == 0 || height == 0)
        return nil;

    id<MTLTexture> existing = ctx->syphonOffscreenTexture;
    if (existing && existing.width == width && existing.height == height)
        return existing;

    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:ctx->metalView.colorPixelFormat
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    ctx->syphonOffscreenTexture =
        [ctx->metalView.device newTextureWithDescriptor:desc];
    return ctx->syphonOffscreenTexture;
}
#endif

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
#ifndef SCREEN_SAVER
    rendererContext->renderedToSyphonOffscreen = NO;
    rendererContext->syphonPublishedThisFrame = NO;
    rendererContext->presentingToDrawable = NO;
    // Decide once per frame. Update() keeps the server registered while the
    // setting is on; the offscreen pass and copy only run while a client is
    // actually receiving, so an idle server costs nothing per frame.
    m_syphonCaptureThisFrame =
        m_externalOutputThisFrame &&
        SyphonOutput_Update(rendererContext->metalView.device);
    m_externalOutputThisFrame = false;
    // Copying into the drawable needs framebufferOnly = NO, which turns off
    // some GPU framebuffer optimizations, so only do it while capturing.
    // (Changes only when a client connects or disconnects.)
    if (rendererContext->metalView.framebufferOnly == m_syphonCaptureThisFrame)
        rendererContext->metalView.framebufferOnly = !m_syphonCaptureThisFrame;
#endif

    // Single render pass for the entire frame: clear once, draw everything,
    // end in EndFrame(). This avoids per-DrawQuad render passes which each
    // force a full framebuffer load/store on Apple's tile-based GPU.
    @autoreleasepool
    {
        id<MTLTexture> colorTexture = nil;
#ifndef SCREEN_SAVER
        // Primary-display Syphon: render video offscreen so we can publish a
        // HUD-free frame, and avoid nextDrawable stalls when occluded.
        if (m_syphonCaptureThisFrame)
        {
            CGSize drawableSize = rendererContext->metalView.drawableSize;
            colorTexture = EnsureSyphonOffscreenTexture(
                rendererContext, (NSUInteger)drawableSize.width,
                (NSUInteger)drawableSize.height);
            if (colorTexture)
            {
                rendererContext->renderedToSyphonOffscreen = YES;
            }
            else
            {
                dispatch_semaphore_signal(rendererContext->inFlightSemaphore);
                rendererContext->currentCommandBuffer = nil;
                return false;
            }
        }
        else
#endif
        {
            id<CAMetalDrawable> drawable =
                rendererContext->metalView.currentDrawable;
            colorTexture = drawable.texture;
#ifndef SCREEN_SAVER
            rendererContext->presentingToDrawable = (colorTexture != nil);
#endif
        }
        if (!colorTexture)
        {
            // Drawable timeout / unavailable — skip this frame.
            dispatch_semaphore_signal(rendererContext->inFlightSemaphore);
            rendererContext->currentCommandBuffer = nil;
            return false;
        }

        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture = colorTexture;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

        rendererContext->currentRenderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];
    }
    return true;
}

#ifndef SCREEN_SAVER
void CRendererMetal::CaptureExternalOutputBeforeOverlay()
{
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;
    if (!rendererContext->currentCommandBuffer ||
        !rendererContext->currentRenderEncoder)
        return;
    if (!rendererContext->renderedToSyphonOffscreen ||
        !rendererContext->syphonOffscreenTexture)
        return;

    // Finish the video pass, publish a HUD-free frame, then continue on the
    // window drawable (when visible) so OSD/icons only affect the local view.
    [rendererContext->currentRenderEncoder endEncoding];
    rendererContext->currentRenderEncoder = nil;

    SyphonOutput_Publish(rendererContext->syphonOffscreenTexture,
                         rendererContext->currentCommandBuffer);
    rendererContext->syphonPublishedThisFrame = YES;
    rendererContext->renderedToSyphonOffscreen = NO;

    const bool occluded =
        WindowIsFullyOccluded(rendererContext->metalView.window);
    id<CAMetalDrawable> drawable =
        occluded ? nil : rendererContext->metalView.currentDrawable;

    if (drawable)
    {
        id<MTLBlitCommandEncoder> blit =
            [rendererContext->currentCommandBuffer blitCommandEncoder];
        const MTLSize size =
            MTLSizeMake(rendererContext->syphonOffscreenTexture.width,
                        rendererContext->syphonOffscreenTexture.height, 1);
        [blit copyFromTexture:rendererContext->syphonOffscreenTexture
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:size
                    toTexture:drawable.texture
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];

        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture = drawable.texture;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        rendererContext->currentRenderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];
        rendererContext->presentingToDrawable = YES;
    }
    else
    {
        // Occluded: keep an encoder on the offscreen target so HUD draws after
        // publish are harmless (Syphon blit already encoded above).
        MTLRenderPassDescriptor* passDescriptor =
            [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture =
            rendererContext->syphonOffscreenTexture;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        rendererContext->currentRenderEncoder =
            [rendererContext->currentCommandBuffer
                renderCommandEncoderWithDescriptor:passDescriptor];
        rendererContext->presentingToDrawable = NO;
    }
}
#else
void CRendererMetal::CaptureExternalOutputBeforeOverlay() {}
#endif

bool CRendererMetal::EndFrame(bool drawn)
{
    CRenderer::EndFrame(drawn);
    RendererContext* rendererContext =
        (__bridge RendererContext*)m_pRendererContext;

    if (!rendererContext->currentCommandBuffer ||
        !rendererContext->currentRenderEncoder)
    {
        m_syphonCaptureThisFrame = false;
#ifndef SCREEN_SAVER
        rendererContext->renderedToSyphonOffscreen = NO;
        rendererContext->syphonPublishedThisFrame = NO;
        rendererContext->presentingToDrawable = NO;
#endif
        return false;
    }

#ifndef SCREEN_SAVER
    // Frames that skip the HUD path (nothing drawn, early exits) never called
    // CaptureExternalOutputBeforeOverlay. Do it now so the frame still gets
    // published and copied to the window instead of being dropped.
    if (rendererContext->renderedToSyphonOffscreen)
        CaptureExternalOutputBeforeOverlay();
#endif

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
#ifndef SCREEN_SAVER
        if (rendererContext->syphonPublishedThisFrame)
        {
            // Video already published pre-HUD. Present only when we blitted to
            // the window drawable.
            if (rendererContext->presentingToDrawable)
            {
                auto drawable = rendererContext->metalView.currentDrawable;
                if (drawable)
                    [rendererContext->currentCommandBuffer
                        presentDrawable:drawable];
            }
        }
        else
#endif
        {
            auto drawable = rendererContext->metalView.currentDrawable;
            if (drawable)
                [rendererContext->currentCommandBuffer presentDrawable:drawable];
        }
    }
    m_syphonCaptureThisFrame = false;
#ifndef SCREEN_SAVER
    rendererContext->renderedToSyphonOffscreen = NO;
    rendererContext->syphonPublishedThisFrame = NO;
    rendererContext->presentingToDrawable = NO;
#endif
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
                // that used this same render encoder. Without this, a
                // DrawQuad with no texture would sample whatever texture
                // the previous draw left bound, causing ghost images.
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
        uniforms.rotation = m_VideoRotation;

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
