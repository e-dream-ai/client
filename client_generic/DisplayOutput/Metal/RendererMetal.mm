#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <stdint.h>
#include <string.h>

#include "RendererMetal.h"
#include "TextureFlatMetal.h"

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
}

/*
*/
bool	CRendererMetal::Initialize( spCDisplayOutput _spDisplay )
{
    if (!CRenderer::Initialize(_spDisplay))
        return false;
	return true;
}

//
void	CRendererMetal::Defaults()
{
	
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
    spCTextureFlat spTex = new CTextureFlatMetal( m_spDisplay->GetContext(),  _flags );
    spTex->Upload( _spImage );
    return spTex;
}

/*
*/
spCTextureFlat	CRendererMetal::NewTextureFlat( const uint32 _flags )
{
    spCTextureFlat spTex = new CTextureFlatMetal( m_spDisplay->GetContext(), _flags );
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
    MTKView* metalView = m_spDisplay->GetContext();

    // Get the Metal device from the view
    id<MTLDevice> device = metalView.device;

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
        MTLRenderPassDescriptor *passDescriptor = metalView.currentRenderPassDescriptor;
        
        if (passDescriptor != nil) {
            // Create a Metal render command encoder
            id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
            
            // Set the render pipeline state
            //[renderEncoder setRenderPipelineState:yourPipelineState];
            
            // Set vertex buffer
            [renderEncoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
            
            // Draw the quad (4 vertices)
            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            
            // End encoding
            [renderEncoder endEncoding];
            
            // Commit the command buffer
            [commandBuffer presentDrawable:metalView.currentDrawable];
            [commandBuffer commit];
        }
    

}
	

/*
*/
void	CRendererMetal::DrawQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const Base::Math::CRect &_uvrect )
{
	
}
	
void	CRendererMetal::DrawSoftQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const fp4 _width )
{
	
}


}
