#import <Metal/Metal.h>
#import <Metal/MTLRenderPipeline.h>
#import <MetalKit/MetalKit.h>

#import "TextRendering/MBEMathUtilities.h"

#include <stdint.h>
#include <string.h>

#include "RendererMetal.h"
#include "TextureFlatMetal.h"
#include "TextMetal.h"
#include "ShaderUniforms.h"

static const NSUInteger MaxFramesInFlight = 3;

@interface RendererContext : NSObject
{
    @public id<MTLRenderPipelineState> yuvPipeline;
    @public id<MTLRenderPipelineState> rgbPipeline;
    @public id<MTLRenderPipelineState> textPipeline;
    @public MTKView* metalView;
    @public std::map<uint32_t, DisplayOutput::CTextureFlatMetal*> boundTextures;
    @public id<MTLCommandQueue> commandQueue;
    @public id<MTLCommandBuffer> currentCommandBuffer;
    @public id<MTLTexture> depthTexture;
    @public std::vector<CVMetalTextureRef> metalTexturesUsed[MaxFramesInFlight];
    @public CVMetalTextureCacheRef textureCache;
    @public dispatch_semaphore_t inFlightSemaphore;
    @public uint8_t drawCallsInFrame[MaxFramesInFlight];
    @public uint32_t frameCounter;
    @public uint8_t currentFrameIndex;
    @public size_t drawCallIndex;
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

static MTLRenderPipelineDescriptor* CreateRenderPipelineDesc(MTKView* metalView)
{
    MTLRenderPipelineDescriptor* renderPipelineDesc = [MTLRenderPipelineDescriptor new];
    renderPipelineDesc.label = @"Electric Sheep Render Pipeline";
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].pixelFormat = metalView.colorPixelFormat;
    renderPipelineDesc.depthAttachmentPixelFormat = metalView.depthStencilPixelFormat;
    renderPipelineDesc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].blendingEnabled = true;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].alphaBlendOperation = MTLBlendOperationAdd;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].rgbBlendOperation = MTLBlendOperationAdd;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].writeMask = MTLColorWriteMaskAll;

    return renderPipelineDesc;
}

static MTLVertexDescriptor* CreateTextVertexDescriptor()
{
    MTLVertexDescriptor *vertexDescriptor = [MTLVertexDescriptor new];

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
    
    id<MTLLibrary> defaultLibrary = [device newDefaultLibrary];
    {
        id<MTLFunction> vertexFuncQuad = [defaultLibrary newFunctionWithName:@"quadPassVertex"];
        id<MTLFunction> vertexFuncText = [defaultLibrary newFunctionWithName:@"drawText_vertex"];
        id<MTLFunction> fragmentFuncQuadYUV = [defaultLibrary newFunctionWithName:@"texture_fragment_YUV"];
        id<MTLFunction> fragmentFuncQuadRGB = [defaultLibrary newFunctionWithName:@"texture_fragment_RGB"];
        id<MTLFunction> fragmentFuncText = [defaultLibrary newFunctionWithName:@"drawText_fragment"];
        
        auto createRenderPipeline = ^(id<MTLFunction> vertexFunc, id<MTLFunction> fragmentFunc, MTLVertexDescriptor* vertexDesc)
        {
            MTLRenderPipelineDescriptor* renderPipelineDesc = CreateRenderPipelineDesc(metalView);
            renderPipelineDesc.vertexFunction = vertexFunc;
            renderPipelineDesc.fragmentFunction = fragmentFunc;
            renderPipelineDesc.vertexDescriptor = vertexDesc;
            
            NSError* error;
            id <MTLRenderPipelineState> pipelineState = [device newRenderPipelineStateWithDescriptor:renderPipelineDesc
                                                                                                     error:&error];
            if (error != nil)
                g_Log->Error("Failed to create render pipeline state: %s", error.localizedDescription.UTF8String);
            return pipelineState;
        };
        rendererContext->yuvPipeline = createRenderPipeline(vertexFuncQuad, fragmentFuncQuadYUV, nil);
        rendererContext->rgbPipeline = createRenderPipeline(vertexFuncQuad, fragmentFuncQuadRGB, nil);
        rendererContext->textPipeline = createRenderPipeline(vertexFuncText, fragmentFuncText, CreateTextVertexDescriptor());
        
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
    return new CFontMetal(_desc, NewTextureFlat());
}

spCBaseText CRendererMetal::NewText( spCBaseFont _font, const std::string& _text )
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    CTextMetal* text = new CTextMetal(static_cast<spCFontMetal>(_font), rendererContext->metalView.device);
    text->SetText(_text, Base::Math::CRect{0,0,1,1});
    return text;
}

Base::Math::CVector2 CRendererMetal::GetTextExtent( spCBaseFont _spFont, const std::string &_text )
{
    return Base::Math::CVector2( 0,0.05f );
}


/*
*/
bool    CRendererMetal::BeginFrame( void )
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    rendererContext->currentFrameIndex = rendererContext->frameCounter++ % MaxFramesInFlight;
    dispatch_semaphore_wait(rendererContext->inFlightSemaphore, DISPATCH_TIME_FOREVER);
    id<MTLCommandQueue> commandQueue = rendererContext->commandQueue;
    rendererContext->currentCommandBuffer = [commandQueue commandBuffer];
    
    rendererContext->drawCallIndex = 0;
    return true;
}

/*
*/
bool    CRendererMetal::EndFrame( bool drawn )
{
    if (!CRenderer::EndFrame(drawn))
        return false;
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    __block dispatch_semaphore_t semaphore = rendererContext->inFlightSemaphore;
    __block std::vector<CVMetalTextureRef>& metalTexturesUsed = rendererContext->metalTexturesUsed[rendererContext->currentFrameIndex];
    [rendererContext->currentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull)
     {
        for (auto it = metalTexturesUsed.begin(); it != metalTexturesUsed.end(); ++it)
        {
            CVMetalTextureRef metalTexture = *it;
            CVBufferRelease(metalTexture);
        }
        metalTexturesUsed.clear();
        dispatch_semaphore_signal(semaphore);
    }];
    rendererContext->drawCallIndex++;
    
    @autoreleasepool
    {
        auto drawable = rendererContext->metalView.currentDrawable;
        if (drawable)
        {
            [rendererContext->currentCommandBuffer presentDrawable:drawable];
        }
    }
    [rendererContext->currentCommandBuffer commit];

    return true;
}

void CRendererMetal::DrawText( spCBaseText _text, const Base::Math::CVector4& _color, const Base::Math::CRect &_rect )
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    @autoreleasepool
    {
        spCTextMetal textMetal = static_cast<spCTextMetal>(_text);
        const MBETextMesh* textMesh = textMetal->GetTextMesh();

        MTLRenderPassDescriptor *passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        if (passDescriptor != nil)
        {
            passDescriptor.colorAttachments[0].texture = rendererContext->metalView.currentDrawable.texture;
            passDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad; // Don't clear the old image
            passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
            
            passDescriptor.depthAttachment.texture = rendererContext->depthTexture;
            passDescriptor.depthAttachment.clearDepth = 1.0;
            passDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
            passDescriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
        }

        id<MTLRenderCommandEncoder> renderEncoder = [rendererContext->currentCommandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
        [renderEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [renderEncoder setCullMode:MTLCullModeNone];
        [renderEncoder setRenderPipelineState:rendererContext->textPipeline];
        [renderEncoder setVertexBuffer:textMesh.vertexBuffer offset:0 atIndex:0];

        vector_float3 translation = { _rect.m_X0, (1-_rect.m_Y0) * 512, 0 };
        vector_float3 scale = { 1, 1, 1 }; //@TODO: implement scale
        matrix_float4x4 modelMatrix = matrix_multiply(matrix_translation(translation), matrix_scale(scale));
        matrix_float4x4 projectionMatrix = matrix_orthographic_projection(0, 512, 0, 512);//@TODO: <--this is the size
        
        TextUniforms uniforms;
        uniforms.modelMatrix = modelMatrix;
        uniforms.viewProjectionMatrix = projectionMatrix;
        uniforms.foregroundColor = { _color.m_X, _color.m_Y, _color.m_Z, _color.m_W };
        
        [renderEncoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [renderEncoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
        spCTextMetal metalText = static_cast<spCTextMetal>(_text);
        spCFontMetal font = metalText->GetFont();
        id<MTLTexture> atlasTexture = font->GetAtlasTexture()->GetRGBMetalTexture();
        [renderEncoder setFragmentTexture:atlasTexture atIndex:0];

        [renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                   indexCount:[textMesh.indexBuffer length] / sizeof(uint16_t)
                                    indexType:MTLIndexTypeUInt16
                                  indexBuffer:textMesh.indexBuffer
                            indexBufferOffset:0];

        [renderEncoder endEncoding];
    }
}
		

void	CRendererMetal::DrawQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const Base::Math::CRect &_uvrect, float crossfadeRatio )
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    @autoreleasepool
    {
        id <MTLRenderPipelineState> renderPipelineState;
        if (!m_aspSelectedTextures[0].IsNull() && m_aspSelectedTextures[0]->IsYUVTexture())
        {
            renderPipelineState = rendererContext->yuvPipeline;
        }
        else
        {
            renderPipelineState = rendererContext->rgbPipeline;
        }
        MTLRenderPassDescriptor *passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        if (passDescriptor != nil)
        {
            id<CAMetalDrawable> drawable = rendererContext->metalView.currentDrawable;
            id<MTLTexture> mainTex = drawable.texture;
            if (rendererContext->depthTexture == nil ||
                mainTex.width != rendererContext->depthTexture.width ||
                mainTex.height != rendererContext->depthTexture.height)
            {
                BuildDepthTexture();
            }
            passDescriptor.colorAttachments[0].texture = rendererContext->metalView.currentDrawable.texture;
            passDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad; // Don't clear the old image
            passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
            
            passDescriptor.depthAttachment.texture = rendererContext->depthTexture;
            passDescriptor.depthAttachment.clearDepth = 1.0;
            passDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
            passDescriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
        }

        id<MTLRenderCommandEncoder> renderEncoder = [rendererContext->currentCommandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
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
                        g_Log->Error("Unable to get Metal textures.");
                    }
                }
                else
                {
                    id<MTLTexture> rgbTexture = selectedTexture->GetRGBMetalTexture();
                    [renderEncoder setFragmentTexture:rgbTexture atIndex:i];
                }
            }
        }
        QuadUniforms uniforms;
        uniforms.crossfadeRatio = crossfadeRatio;
        uniforms.rect = vector_float4 { _rect.m_X0, _rect.m_Y0, _rect.m_X1 - _rect.m_X0, _rect.m_Y1 - _rect.m_Y0 };

        [renderEncoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [renderEncoder endEncoding];
    }
}

void    CRendererMetal::DrawQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color )
{
    DrawQuad(_rect, _color, Base::Math::CRect{0,0,1,1}, 0.0f);
}

void	CRendererMetal::DrawSoftQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const fp4 _width )
{
    DrawQuad(_rect, _color, Base::Math::CRect{0,0,1,1}, 0.0f);
}

void CRendererMetal::BuildDepthTexture()
{
    RendererContext* rendererContext = (__bridge RendererContext*)m_pRendererContext;
    id<MTLDevice> device = rendererContext->metalView.device;
    id<MTLTexture> texture = rendererContext->metalView.currentDrawable.texture;
    MTLTextureDescriptor *depthTextureDescriptor = [[MTLTextureDescriptor alloc] init];
    depthTextureDescriptor.pixelFormat = MTLPixelFormatDepth32Float;
    depthTextureDescriptor.width = texture.width;
    depthTextureDescriptor.height = texture.height;
    depthTextureDescriptor.storageMode = MTLStorageModePrivate;
    depthTextureDescriptor.usage = MTLTextureUsageRenderTarget;

    rendererContext->depthTexture = [device newTextureWithDescriptor:depthTextureDescriptor];
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
