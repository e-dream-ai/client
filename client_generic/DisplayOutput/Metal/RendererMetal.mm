#import <Metal/Metal.h>
#import <Metal/MTLRenderPipeline.h>
#import <MetalKit/MetalKit.h>

#include <stdint.h>
#include <string.h>

#include "RendererMetal.h"
#include "TextureFlatMetal.h"
#include "ShaderUniforms.h"

static const NSUInteger MaxFramesInFlight = 3;

@interface RendererContext : NSObject
{
    @public id<MTLRenderPipelineState> yuvPipeline;
    @public id<MTLRenderPipelineState> rgbPipeline;
    @public MTKView* metalView;
    @public std::map<uint32_t, DisplayOutput::CTextureFlatMetal*> boundTextures;
    @public id<MTLCommandQueue> commandQueue;
    @public CVMetalTextureCacheRef textureCache;
    @public id<MTLBuffer> uniformBuffers[MaxFramesInFlight];
    @public dispatch_semaphore_t inFlightSemaphore;
    @public uint32_t frameCounter;
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
CRendererMetal::CRendererMetal() : CRenderer()
{
}

/*
*/
CRendererMetal::~CRendererMetal()
{
    RendererContext* rendererContext = CFBridgingRelease(m_pRendererContext);
    if (rendererContext->textureCache)
    {
        CFRelease(rendererContext->textureCache);
    }
}

typedef enum AAPLRenderTargetIndices
{
    AAPLRenderTargetColor           = 0,
} AAPLRenderTargetIndices;

/*
*/
bool	CRendererMetal::Initialize( spCDisplayOutput _spDisplay )
{
    if (!CRenderer::Initialize(_spDisplay))
        return false;

    MTKView* metalView = (__bridge MTKView*)m_spDisplay->GetContext();
    id<MTLDevice> device = metalView.device;
    
    RendererContext* rendererContext = [[RendererContext alloc] init];
    rendererContext->metalView = metalView;
    rendererContext->commandQueue = [device newCommandQueue];
    rendererContext->inFlightSemaphore = dispatch_semaphore_create(MaxFramesInFlight);
    
    if (USE_HW_ACCELERATION)
    {
        CVReturn textureCacheError = CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL, &rendererContext->textureCache);
        if (textureCacheError != kCVReturnSuccess)
        {
            g_Log->Error("Error creating CVTextureCache:%d", textureCacheError);
        }
    }

    m_pRendererContext = CFBridgingRetain(rendererContext);
    float alpha = 0;
    for (uint32_t i = 0; i < MaxFramesInFlight; ++i)
    {
        rendererContext->uniformBuffers[i] = [device newBufferWithBytes:&alpha length:sizeof(float) options:MTLResourceStorageModeShared];
    }
    
    
    id<MTLLibrary> defaultLibrary = [device newDefaultLibrary];
    {
        id<MTLFunction> vertexFunc = [defaultLibrary newFunctionWithName:@"quadPassVertex"];
        id<MTLFunction> fragmentFuncYUV = [defaultLibrary newFunctionWithName:@"texture_fragment_YUV"];
        id<MTLFunction> fragmentFuncRGB = [defaultLibrary newFunctionWithName:@"texture_fragment_RGB"];

        id <MTLRenderPipelineState> (^createRenderPipeline)(id<MTLFunction> fragmentFunc) = ^(id<MTLFunction> fragmentFunc)
        {
            MTLRenderPipelineDescriptor* renderPipelineDesc = [MTLRenderPipelineDescriptor new];
            renderPipelineDesc.label = @"Electric Sheep Render Pipeline";
            renderPipelineDesc.vertexFunction = vertexFunc;
            renderPipelineDesc.fragmentFunction = fragmentFunc;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].pixelFormat = metalView.colorPixelFormat;
            renderPipelineDesc.depthAttachmentPixelFormat = metalView.depthStencilPixelFormat;
            renderPipelineDesc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].blendingEnabled = true;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].alphaBlendOperation = MTLBlendOperationAdd;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].rgbBlendOperation = MTLBlendOperationAdd;
            renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].writeMask = MTLColorWriteMaskAll;
            
            NSError* error;
            id <MTLRenderPipelineState> pipelineState = [device newRenderPipelineStateWithDescriptor:renderPipelineDesc
                                                                                                     error:&error];
            if (error != nil)
                g_Log->Error("Failed to create render pipeline state: %s", error.localizedDescription.UTF8String);
            return pipelineState;
        };
        rendererContext->yuvPipeline = createRenderPipeline(fragmentFuncYUV);
        rendererContext->rgbPipeline = createRenderPipeline(fragmentFuncRGB);
        
    }
	return true;
}


//
void	CRendererMetal::Defaults()
{
	
}

void    CRendererMetal::SetBoundSlot(uint32_t _slot, CTextureFlatMetal* _texture)
{
    writer_lock lock( m_mutex );
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    rendererContext->boundTextures[_slot] = _texture;
}

/*
*/
bool	CRendererMetal::BeginFrame( void )
{

	return true;
}

/*
*/
bool	CRendererMetal::EndFrame( bool drawn )
{
    if (!CRenderer::EndFrame())
        return false;
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    auto drawable = rendererContext->metalView.currentDrawable;
    if (drawable)
    {
        [drawable present];
    }
	return true;
}

/*
*/
void	CRendererMetal::Apply()
{
    CRenderer::Apply();
}

/*
*/
void	CRendererMetal::Reset( const uint32 _flags )
{	
	
	CRenderer::Reset( _flags );
}


/*
*/
spCTextureFlat	CRendererMetal::NewTextureFlat( spCImage _spImage, const uint32 _flags )
{
    spCTextureFlat spTex = new CTextureFlatMetal( m_spDisplay->GetContext(),  _flags, this );
    spTex->Upload( _spImage );
    return spTex;
}

/*
*/
spCTextureFlat	CRendererMetal::NewTextureFlat( const uint32 _flags )
{
    spCTextureFlat spTex = new CTextureFlatMetal( m_spDisplay->GetContext(), _flags, this );
    return spTex;
}

/*
*/
spCShader	CRendererMetal::NewShader( const char *_pVertexShader, const char *_pFragmentShader )
{
    return NULL;
}

/*
*/
spCBaseFont	CRendererMetal::NewFont( CFontDescription &_desc )
{	
    return NULL;
}
		
void CRendererMetal::Text( spCBaseFont _spFont, const std::string &_text, const Base::Math::CVector4 &/*_color*/, const Base::Math::CRect &_rect, uint32 /*_flags*/ )
{
	
}
		
Base::Math::CVector2 CRendererMetal::GetTextExtent( spCBaseFont _spFont, const std::string &_text )
{
	
	return Base::Math::CVector2( 0,0 );
}	

/*
 */
void	CRendererMetal::DrawQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color )
{
 

}
	

/*
*/
void	CRendererMetal::DrawQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const Base::Math::CRect &_uvrect, float crossfadeRatio )
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    @autoreleasepool
    {
        dispatch_semaphore_wait(rendererContext->inFlightSemaphore, DISPATCH_TIME_FOREVER);

        id <MTLRenderPipelineState> renderPipelineState;
        if (!m_aspSelectedTextures[0].IsNull() && m_aspSelectedTextures[0]->IsYUVTexture())
        {
            renderPipelineState = rendererContext->yuvPipeline;
        }
        else
        {
            renderPipelineState = rendererContext->rgbPipeline;
        }
        id<MTLCommandQueue> commandQueue = rendererContext->commandQueue;
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        MTLRenderPassDescriptor *passDescriptor = rendererContext->metalView.currentRenderPassDescriptor;
        
        if (passDescriptor != nil)
        {
            uint32_t currentFrameIndex = rendererContext->frameCounter++ % MaxFramesInFlight;
            id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
            [renderEncoder setRenderPipelineState:renderPipelineState];
            std::map<uint32_t, CTextureFlatMetal*> boundTextures(rendererContext->boundTextures);
            std::vector<CVMetalTextureRef> metalTexturesUsed;
            
            reader_lock lock( m_mutex );
            for (uint32_t i = 0; i < MAX_TEXUNIT; ++i)
            {
                spCTextureFlatMetal selectedTexture = static_cast<spCTextureFlatMetal>(m_aspSelectedTextures[i]);
                if (!selectedTexture.IsNull())
                {
                    if (selectedTexture->IsYUVTexture())
                    {
                        CVMetalTextureRef yTextureRef;
                        CVMetalTextureRef uvTextureRef;
                        if (selectedTexture->GetYUVMetalTextures(&yTextureRef, &uvTextureRef))
                        {
                            id<MTLTexture> yTexture = CVMetalTextureGetTexture(yTextureRef);
                            id<MTLTexture> uvTexture = CVMetalTextureGetTexture(uvTextureRef);
                            [renderEncoder setFragmentTexture:yTexture atIndex:i * 2 + 0];
                            [renderEncoder setFragmentTexture:uvTexture atIndex:i * 2 + 1];
                            metalTexturesUsed.push_back(yTextureRef);
                            metalTexturesUsed.push_back(uvTextureRef);
                        }
                        else
                        {
                            g_Log->Error("DAS");
                        }
                    }
                    else
                    {
                        id<MTLTexture> rgbTexture = selectedTexture->GetRGBMetalTexture();
                        [renderEncoder setFragmentTexture:rgbTexture atIndex:i];
                    }
                }
            }
            FragmentUniforms uniforms;
            uniforms.crossfadeRatio = crossfadeRatio;
            uniforms.rect = vector_float4 { _rect.m_X0, _rect.m_Y0, _rect.m_X1 - _rect.m_X0, _rect.m_Y1 - _rect.m_Y0 };
            id<MTLBuffer> uniformBuffer = rendererContext->uniformBuffers[currentFrameIndex];
            memcpy(uniformBuffer.contents, &uniforms, sizeof(FragmentUniforms));
            [renderEncoder setFragmentBuffer:uniformBuffer offset:0 atIndex:0];
            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [renderEncoder endEncoding];
            [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull)
             {
                for (auto it = metalTexturesUsed.begin(); it != metalTexturesUsed.end(); ++it)
                {
                    CVMetalTextureRef metalTexture = *it;
                    CVBufferRelease(metalTexture);
                }
                dispatch_semaphore_signal(rendererContext->inFlightSemaphore);
            }];
            [commandBuffer commit];
        }
        else
        {
            [commandBuffer commit];
        }
    }
}
	
void	CRendererMetal::DrawSoftQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const fp4 _width )
{
	
}

bool    CRendererMetal::CreateMetalTextureFromDecoderFrame(CVPixelBufferRef pixelBuffer, CVMetalTextureRef* _outMetalTextureRef, uint32_t plane)
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    
    
    CVReturn ret;
 
    ret = CVMetalTextureCacheCreateTextureFromImage(
        NULL,
        rendererContext->textureCache,
        pixelBuffer,
        NULL,
        plane == 0 ? MTLPixelFormatR8Unorm : MTLPixelFormatRG8Unorm,
        CVPixelBufferGetWidthOfPlane(pixelBuffer, plane),
        CVPixelBufferGetHeightOfPlane(pixelBuffer, plane),
        plane,
        _outMetalTextureRef
    );
    if (ret != kCVReturnSuccess)
    {
        g_Log->Error("Failed to create CVMetalTexture from image: %d", ret);
        return false;
    }
    return true;
}

}
