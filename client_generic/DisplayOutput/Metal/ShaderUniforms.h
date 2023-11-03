#ifndef ShaderUniforms_h
#define ShaderUniforms_h

#include <simd/simd.h>

typedef struct
{
    vector_float4 rect;
    float crossfadeRatio;
} FragmentUniforms;

#endif /* ShaderUniforms_h */
