#import <Metal/Metal.h>
#import <Metal/MTLRenderPipeline.h>
#import <MetalKit/MetalKit.h>

#include <stdint.h>
#include <string.h>

#include "RendererMetal.h"
#include "TextureFlatMetal.h"

@interface RendererContext : NSObject
/// The main GPU of the device.
@property (nonatomic, nonnull) id<MTLDevice> device;
@property (nonatomic, nonnull) id<MTLRenderPipelineState> renderPipeline;
@property (nonatomic, nonnull) MTKView* metalView;
@property (nonatomic, nonnull) NSMutableDictionary<NSNumber*, id<MTLTexture>>* boundTextures;
@end

@implementation RendererContext
@end

namespace DisplayOutput
{
	
	
/*
 */
CRendererMetal::CRendererMetal() : CRenderer()
{
}

/*
*/
CRendererMetal::~CRendererMetal()
{
    CFBridgingRelease(m_pRendererContext);
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
    
    RendererContext* rendererContext = [[RendererContext alloc] init];
    rendererContext.metalView = metalView;
    rendererContext.device = metalView.device;
    rendererContext.boundTextures = [[NSMutableDictionary alloc] init];

    m_pRendererContext = CFBridgingRetain(rendererContext);

    id<MTLDevice> device = metalView.device;
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
        rendererContext.renderPipeline = renderPipelineState;
        if (error != nil)
            g_Log->Error("Failed to create render pipeline state: %s", error.localizedDescription.UTF8String);
    }
	return true;
}


//
void	CRendererMetal::Defaults()
{
	
}

void    CRendererMetal::SetBoundSlot(uint32_t _slot, id<MTLTexture> _texture)
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    rendererContext.boundTextures[@(_slot)] = _texture;
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
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    id <MTLRenderPipelineState> renderPipelineState = rendererContext.renderPipeline;

    // Get the Metal device from the view
    id<MTLDevice> device = rendererContext.device;

    // Define the vertices for your quad (in normalized device coordinates)
    float vertices[] = {
        -0.5f, -0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, 0.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 1.0f,
         0.5f,  0.5f, 0.0f, 1.0f,
    };

    // Create a Metal buffer for the vertices
    id<MTLBuffer> vertexBuffer = [device newBufferWithBytes:vertices
                                                       length:sizeof(vertices)
                                                      options:MTLResourceStorageModeShared];

    // Create a Metal shader (vertex and fragment shaders)
    // Your shader code goes here...

    // Create a Metal render pipeline with your shaders
    // Your render pipeline setup code goes here...

    // Create a Metal command queue
    id<MTLCommandQueue> commandQueue = [device newCommandQueue];

    // Render loop (Objective-C/C++ code)
    
        // Start a new Metal command buffer
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        
        // Create a Metal render pass descriptor
        MTLRenderPassDescriptor *passDescriptor = rendererContext.metalView.currentRenderPassDescriptor;
        
        if (passDescriptor != nil) {
            // Create a Metal render command encoder
            id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
            
            // Set the render pipeline state
            [renderEncoder setRenderPipelineState:renderPipelineState];
            
            // Set vertex buffer
            [renderEncoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
            
            for (NSNumber* slot in rendererContext.boundTextures)
            {
                [renderEncoder setFragmentTexture:rendererContext.boundTextures[slot] atIndex:slot.unsignedIntegerValue];
            }
            
            
            // Draw the quad (4 vertices)
            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            
            // End encoding
            [renderEncoder endEncoding];
            
            // Commit the command buffer
            [commandBuffer presentDrawable:rendererContext.metalView.currentDrawable];
            [commandBuffer commit];
        }
    
}
	
void	CRendererMetal::DrawSoftQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const fp4 _width )
{
	
}


}
