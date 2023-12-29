#include <assert.h>
#include <iostream>
#include <cstdlib>
#include <string>

#include "Exception.h"
#include "Log.h"
#include "ShaderMetal.h"

namespace DisplayOutput
{

typedef enum AAPLRenderTargetIndices
{
    AAPLRenderTargetColor = 0,
} AAPLRenderTargetIndices;

/*
 */

CShaderMetal::CShaderMetal(
    id<MTLDevice> device, id<MTLFunction> vertexFunction,
    id<MTLFunction> fragmentFunction, MTLVertexDescriptor* vertexDescriptor,
    std::vector<std::pair<std::string, eUniformType>> _uniforms)
{
    MTLRenderPipelineDescriptor* renderPipelineDesc =
        [MTLRenderPipelineDescriptor new];
    renderPipelineDesc.label = @"e-dream Render Pipeline";
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].pixelFormat =
        MTLPixelFormatBGRA8Unorm;
    renderPipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    renderPipelineDesc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].blendingEnabled =
        true;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor]
        .alphaBlendOperation = MTLBlendOperationAdd;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor]
        .sourceAlphaBlendFactor = MTLBlendFactorOne;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor]
        .destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor]
        .sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor]
        .destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor]
        .rgbBlendOperation = MTLBlendOperationAdd;
    renderPipelineDesc.colorAttachments[AAPLRenderTargetColor].writeMask =
        MTLColorWriteMaskAll;
    renderPipelineDesc.vertexFunction = vertexFunction;
    renderPipelineDesc.fragmentFunction = fragmentFunction;
    renderPipelineDesc.vertexDescriptor = vertexDescriptor;

    NSError* error;
    m_PipelineState =
        [device newRenderPipelineStateWithDescriptor:renderPipelineDesc
                                               error:&error];
    if (error != nil)
        g_Log->Error("Failed to create render pipeline state: %s",
                     error.localizedDescription.UTF8String);

    int index = 0;
    size_t bufferSize = 0;
    for (size_t i = 0; i < _uniforms.size(); ++i)
    {
        eUniformType uniformType = _uniforms[i].second;
        size_t size = UniformTypeSizes[uniformType];
        spCShaderUniformMetal uniform = std::make_shared<CShaderUniformMetal>(
            _uniforms[i].first, uniformType, index, size);
        m_Uniforms[_uniforms[i].first] = uniform;
        m_SortedUniformRefs.push_back(uniform);
        bufferSize += size;
    }
    const int kGPUBufferAlignment = 32;
    bufferSize =
        ((bufferSize + kGPUBufferAlignment - 1) / kGPUBufferAlignment) *
        kGPUBufferAlignment;
    m_UniformBuffer.resize(bufferSize);
}

/*
 */
CShaderMetal::~CShaderMetal() {}

void CShaderMetal::UploadUniforms(id<MTLRenderCommandEncoder> renderEncoder)
{
    if (m_Uniforms.size())
    {
        uint8_t* data = m_UniformBuffer.data();
        for (auto uniform : m_SortedUniformRefs)
        {
            size_t size = uniform->GetSize();
            std::memcpy(data, uniform->GetData(), size);
            data += size;
        }
        [renderEncoder setFragmentBytes:m_UniformBuffer.data()
                                 length:m_UniformBuffer.size()
                                atIndex:1];
    }
}

/*
 */
bool CShaderMetal::Bind() { return true; }

/*
 */
bool CShaderMetal::Apply() { return true; }

/*
 */
bool CShaderMetal::Unbind() { return true; }

}; // namespace DisplayOutput
