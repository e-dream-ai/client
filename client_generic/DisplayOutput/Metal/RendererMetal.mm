#import <Metal/Metal.h>
#import <Metal/MTLRenderPipeline.h>
#import <MetalKit/MetalKit.h>

#include <stdint.h>
#include <string.h>

#include "RendererMetal.h"
#include "TextureFlatMetal.h"

@interface RendererContext : NSObject
{
    @public id<MTLRenderPipelineState> renderPipeline;
    @public MTKView* metalView;
    @public std::map<uint32_t, DisplayOutput::CTextureFlatMetal*> boundTextures;
    @public id<MTLCommandQueue> commandQueue;
    @public CVMetalTextureCacheRef textureCache;
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
    
    if (USE_HW_ACCELERATION)
    {
        CVReturn textureCacheError = CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL, &rendererContext->textureCache);
        if (textureCacheError != kCVReturnSuccess)
        {
            g_Log->Error("Error creating CVTextureCache:%d", textureCacheError);
        }
    }

    m_pRendererContext = CFBridgingRetain(rendererContext);

    NSError* error;
    
    id<MTLLibrary> defaultLibrary = [device newDefaultLibrary];
    {
        id<MTLFunction> vertexFunc = [defaultLibrary newFunctionWithName:@"quadPassVertex"];
        id<MTLFunction> fragmentFunc = [defaultLibrary newFunctionWithName:@"texture_fragment"];

        MTLRenderPipelineDescriptor* renderPipelineDesc = [MTLRenderPipelineDescriptor new];
        renderPipelineDesc.label = @"Electric Sheep Render Pipeline";
        renderPipelineDesc.vertexFunction = vertexFunc;
        renderPipelineDesc.fragmentFunction = fragmentFunc;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].pixelFormat = metalView.colorPixelFormat;
        renderPipelineDesc.depthAttachmentPixelFormat = metalView.depthStencilPixelFormat;
        renderPipelineDesc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].blendingEnabled = true;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].alphaBlendOperation = MTLBlendOperationAdd;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationAlphaBlendFactor = MTLBlendFactorZero;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].rgbBlendOperation = MTLBlendOperationAdd;
        renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].writeMask = MTLColorWriteMaskAll;
        id <MTLRenderPipelineState> renderPipelineState = [device newRenderPipelineStateWithDescriptor:renderPipelineDesc
                                                                                                 error:&error];
        rendererContext->renderPipeline = renderPipelineState;
        if (error != nil)
            g_Log->Error("Failed to create render pipeline state: %s", error.localizedDescription.UTF8String);
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
void	CRendererMetal::DrawQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const Base::Math::CRect &_uvrect )
{
    @autoreleasepool
    {
        reader_lock lock( m_mutex );
        RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
        id <MTLRenderPipelineState> renderPipelineState = rendererContext->renderPipeline;
        id<MTLCommandQueue> commandQueue = rendererContext->commandQueue;
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        MTLRenderPassDescriptor *passDescriptor = rendererContext->metalView.currentRenderPassDescriptor;
        
        if (passDescriptor != nil)
        {
            id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
            [renderEncoder setRenderPipelineState:renderPipelineState];
            
            std::map<uint32_t, CTextureFlatMetal*> boundTextures(rendererContext->boundTextures);
            std::vector<CVMetalTextureRef> metalTexturesUsed;
            for (auto it = boundTextures.begin(); it != boundTextures.end(); ++it)
            {
                if (it->second->GetMetalTexture())
                {
                    [renderEncoder setFragmentTexture:it->second->GetMetalTexture() atIndex:it->first];
                    //metalTexturesUsed.push_back(it->second->GetCVMetalTextureRef());
                }
            }

            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [renderEncoder endEncoding];
            
            [commandBuffer addScheduledHandler:^(id<MTLCommandBuffer>)
            {
                [rendererContext->metalView.currentDrawable present];
            }];
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
            [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
                
                for (auto it = metalTexturesUsed.begin(); it != metalTexturesUsed.end(); ++it)
                {
                    CVMetalTextureRef metalTexture = *it;
                    //CVBufferRelease(metalTexture);
                    if (metalTexture)
                    {
                        //CFRelease(metalTexture);
                        //it->ReleaseMetalTexture();
                    }
                }
                dispatch_semaphore_signal(semaphore);
            }];
            [commandBuffer commit];
            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER); //@TODO: implement proper thread locking
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

bool    CRendererMetal::CreateMetalTextureFromDecoderFrame(CVPixelBufferRef pixelBuffer, CVMetalTextureRef* _outMetalTextureRef)
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    
    
    CVReturn ret;
    int plane = 0;
 
    ret = CVMetalTextureCacheCreateTextureFromImage(
        NULL,
        rendererContext->textureCache,
        pixelBuffer,
        NULL,
        MTLPixelFormatR8Unorm,
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
