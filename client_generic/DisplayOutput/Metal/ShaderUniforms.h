#ifndef ShaderUniforms_h
#define ShaderUniforms_h

#include <simd/simd.h>

struct QuadUniforms
{
    vector_float4 rect;
    vector_float4 uvRect;
    vector_float4 color;
    float brightness;
    int rotation;  // video rotation in degrees: 0 = none, 90 = rotate 90 CW
};

#endif /* ShaderUniforms_h */
