#include <iostream>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include    "Exception.h"
#include    "ShaderMetal.h"
#include    "Log.h"

namespace DisplayOutput
{

typedef enum AAPLRenderTargetIndices
{
    AAPLRenderTargetColor           = 0,
} AAPLRenderTargetIndices;
    
/*
*/

CShaderMetal::CShaderMetal(id<MTLDevice> device, id<MTLFunction> vertexFunction, id<MTLFunction> fragmentFunction, MTLVertexDescriptor* vertexDescriptor)
{
    MTLRenderPipelineDescriptor* renderPipelineDesc = [MTLRenderPipelineDescriptor new];
    renderPipelineDesc.label = @"Electric Sheep Render Pipeline";
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].pixelFormat = MTLPixelFormatBGRA8Unorm;
    renderPipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    renderPipelineDesc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].blendingEnabled = true;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].alphaBlendOperation = MTLBlendOperationAdd;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceAlphaBlendFactor = MTLBlendFactorOne;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].rgbBlendOperation = MTLBlendOperationAdd;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].writeMask = MTLColorWriteMaskAll;
    renderPipelineDesc.vertexFunction = vertexFunction;
    renderPipelineDesc.fragmentFunction = fragmentFunction;
    renderPipelineDesc.vertexDescriptor = vertexDescriptor;

    NSError* error;
    m_PipelineState = [device newRenderPipelineStateWithDescriptor:renderPipelineDesc error:&error];
    if (error != nil)
        g_Log->Error("Failed to create render pipeline state: %s", error.localizedDescription.UTF8String);
    
}

/*
*/
CShaderMetal::~CShaderMetal()
{
    
}

bool CShaderMetal::Set( [[maybe_unused]] const std::string _name, uint64_t _uniformIndex, const fp4 _value )
{
    m_UniformValues[_uniformIndex] = _value;
    return true;
}

void CShaderMetal::UploadUniforms(id<MTLRenderCommandEncoder> renderEncoder)
{
    for (auto it = m_UniformValues.begin(); it != m_UniformValues.end(); ++it)
    {
        [renderEncoder setFragmentBytes:&it->second length:sizeof(float) atIndex:it->first];
    }
}

/*
*/
bool    CShaderMetal::Bind()
{
    return true;
}

/*
*/
bool    CShaderMetal::Apply()
{
    return true;
}

/*
*/
bool    CShaderMetal::Unbind()
{
    return true;
}
    
};
