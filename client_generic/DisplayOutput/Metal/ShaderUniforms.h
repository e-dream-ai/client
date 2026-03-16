#ifndef ShaderUniforms_h
#define ShaderUniforms_h

#include <simd/simd.h>

struct QuadUniforms
{
    vector_float4 rect;
    vector_float4 uvRect;
    vector_float4 color;
    float brightness;
};

#endif /* ShaderUniforms_h */
