#ifndef    _SHADERMETAL_H_
#define    _SHADERMETAL_H_

#import <Metal/Metal.h>

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
public:
    CShaderMetal(id<MTLDevice> device, id<MTLFunction> vertexFunction, id<MTLFunction> fragmentFunction, MTLVertexDescriptor* vertexDescriptor);
    virtual ~CShaderMetal();

    virtual    bool    Bind( void );
    virtual    bool    Unbind( void );
    virtual bool     Apply( void );
    virtual bool Build(const char *_pVertexShader, const char *_pFragmentShader) {};
    id <MTLRenderPipelineState> GetPipelineState() const { return m_PipelineState; }
};

MakeSmartPointers( CShaderMetal );

}

#endif
