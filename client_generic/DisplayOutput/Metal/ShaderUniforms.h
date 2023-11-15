#ifndef ShaderUniforms_h
#define ShaderUniforms_h

#include <simd/simd.h>

struct QuadUniforms
{
    vector_float4 rect;
    vector_float4 color;
};

struct TextUniforms
{
    matrix_float4x4 modelMatrix;
    matrix_float4x4 viewProjectionMatrix;
    vector_float4 foregroundColor;
};

struct VertexText
{
    packed_float4 position;
    packed_float2 texCoords;
};

#endif /* ShaderUniforms_h */
