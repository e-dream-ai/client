#ifndef    _SHADERMETAL_H_
#define    _SHADERMETAL_H_

#import <Metal/Metal.h>

#include <map>

#include "Log.h"
#include "Shader.h"

namespace    DisplayOutput
{
/*
    CShaderMetal().

*/
class CShaderMetal : public CShader
{
    id <MTLRenderPipelineState> m_PipelineState;
    std::map<uint64_t, fp4>  m_UniformValues;
public:
    CShaderMetal(id<MTLDevice> device, id<MTLFunction> vertexFunction, id<MTLFunction> fragmentFunction, MTLVertexDescriptor* vertexDescriptor);
    virtual ~CShaderMetal();

    virtual    bool    Bind( void );
    virtual    bool    Unbind( void );
    virtual bool     Apply( void );
    
    virtual bool Build( const char*, const char* ) { return true; };
    virtual bool Set( const std::string _name, uint64_t _uniformIndex, const fp4 _value );
    id <MTLRenderPipelineState> GetPipelineState() const { return m_PipelineState; }
    void UploadUniforms( id<MTLRenderCommandEncoder> renderEncoder );
};

MakeSmartPointers( CShaderMetal );

}

#endif
