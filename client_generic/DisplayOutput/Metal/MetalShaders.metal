//
//  MetalShaders.metal
//  ScreenSaver
//
//  Created by Tibor Hencz on 26.9.2023.
//

#include <metal_stdlib>

#include "ShaderUniforms.h"

using namespace metal;


typedef struct
{
    float4 position [[position]];
    float2 uv;
} ColorInOut;

vertex ColorInOut quadPassVertex(uint vid[[vertex_id]])
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    float4 position = float4(uv * float2(2, -2) + float2(-1, 1), 1, 1);
 
    ColorInOut out;
    out.position = position;
    out.uv = uv;
    return out;
}

fragment float4 texture_fragment_YUV2(ColorInOut vert [[stage_in]],
                                texture2d<float, access::sample> yTexture [[texture(0)]],
                                texture2d<float, access::sample> uvTexture [[texture(1)]])
{
    float3x3 colorMatrix = float3x3(
                                    float3(1, 1, 1),
                                    float3(0, -0.344, 1.770),
                                    float3(1.403, -0.714, 0)
                                    );
    constexpr sampler s( address::repeat, filter::linear );
    //return yTexture.sample(s, vert.uv);
    float3 yuv;
    yuv.x = yTexture.sample(s, vert.uv).x;
    yuv.yz = uvTexture.sample(s, vert.uv).xy;
    float3 rgb = colorMatrix * yuv;
    return float4(rgb, 1);
}

float4 SampleTextureRGBA(float2 uv, texture2d<float, access::sample> yTexture, texture2d<float, access::sample> uvTexture)
{
    constexpr sampler s( address::repeat, filter::linear );
    float3 yuv;
    float4 rgba;
    yuv.x = yTexture.sample(s, uv).x;
    
    yuv.yz = uvTexture.sample(s, uv).xy;
    //return float4(yuv, 1);
    if (yuv.x == 0) return float4(1, 0, 0, 0.5);
    if (all(yuv.yz == float2(0, 0))) return float4(0, 0, 1, 0.5);
    yuv.x = 1.164383561643836 * (yuv.x - 0.0625);
    yuv.y = yuv.y - 0.5;
    yuv.z = yuv.z - 0.5;

    rgba.x = yuv.x + 1.792741071428571 * yuv.z;
    rgba.y = yuv.x - 0.532909328559444 * yuv.z - 0.21324861427373 * yuv.y;
    rgba.z = yuv.x + 2.112401785714286 * yuv.y;
    rgba.x = saturate(1.661 * rgba.x - 0.588 * rgba.y - 0.073 * rgba.z);
    rgba.y = saturate(-0.125 * rgba.x + 1.133 * rgba.y - 0.008 * rgba.z);
    rgba.z = saturate(-0.018 * rgba.x - 0.101 * rgba.y + 1.119 * rgba.z);
    rgba.w = 1;
    return rgba;
}

fragment float4 drawDecodedFrameNoBlendingFragment(ColorInOut vert [[stage_in]],
                                     texture2d<float, access::sample> yTexture1 [[texture(0)]],
                                     texture2d<float, access::sample> uvTexture1 [[texture(1)]],
                                     constant QuadUniforms& uniforms [[buffer(0)]])
{
    float4 rgba = SampleTextureRGBA(vert.uv, yTexture1, uvTexture1);
    return rgba * uniforms.color;
}

fragment float4 drawDecodedFrameLinearFrameBlendFragment(ColorInOut vert [[stage_in]],
                                                    texture2d<float, access::sample> video1frame1Y [[texture(0)]],
                                                    texture2d<float, access::sample> video1frame1UV [[texture(1)]],
                                                    texture2d<float, access::sample> video1frame2Y [[texture(2)]],
                                                    texture2d<float, access::sample> video1frame2UV [[texture(3)]],
                                                    texture2d<float, access::sample> video2frame1Y [[texture(4)]],
                                                    texture2d<float, access::sample> video2frame1UV [[texture(5)]],
                                                    texture2d<float, access::sample> video2frame2Y [[texture(6)]],
                                                    texture2d<float, access::sample> video2frame2UV [[texture(7)]],
                                                    constant QuadUniforms& uniforms [[buffer(0)]],
                                                    constant float& delta [[buffer(1)]])
{
    float4 video1frame1RGBA = SampleTextureRGBA(vert.uv, video1frame1Y, video1frame1UV);
    float4 video1frame2RGBA = SampleTextureRGBA(vert.uv, video1frame2Y, video1frame2UV);

    return mix(video1frame1RGBA, video1frame2RGBA, delta);
}


fragment float4 drawTextureFragment(ColorInOut vert [[stage_in]],
                                texture2d<float, access::sample> texture [[texture(0)]],
                                     constant QuadUniforms& uniforms [[buffer(0)]])
{
    constexpr sampler s( address::clamp_to_zero, filter::linear );
    float2 adjustedUV = (vert.uv - uniforms.rect.xy) / uniforms.rect.zw;
    float4 color = texture.sample(s, adjustedUV);
    return color;
}



struct TransformedVertex
{
    float4 position [[position]];
    float2 texCoords;
};


vertex TransformedVertex drawTextVertex(constant VertexText* vertices [[buffer(0)]],
                                      constant TextUniforms& uniforms [[buffer(1)]],
                                      uint vid [[vertex_id]])
{
    TransformedVertex outVert;
    outVert.position = uniforms.viewProjectionMatrix * uniforms.modelMatrix * float4(vertices[vid].position);
    outVert.texCoords = vertices[vid].texCoords;
    return outVert;
}

fragment half4 drawTextFragment(TransformedVertex vert [[stage_in]],
                              constant TextUniforms& uniforms [[buffer(0)]],
                              texture2d<float, access::sample> texture [[texture(0)]])
{
    //return 1;
    constexpr sampler s( address::repeat, filter::linear );
    
    float4 color = uniforms.foregroundColor;
    // Outline of glyph is the isocontour with value 50%
    float edgeDistance = 0.5;
    // Sample the signed-distance field to find distance from this fragment to the glyph outline
    float sampleDistance = texture.sample(s, vert.texCoords).r;
    // Use local automatic gradients to find anti-aliased anisotropic edge width, cf. Gustavson 2012
    float edgeWidth = 0.75 * length(float2(dfdx(sampleDistance), dfdy(sampleDistance)));
    // Smooth the glyph edge by interpolating across the boundary in a band with the width determined above
    float insideness = smoothstep(edgeDistance - edgeWidth, edgeDistance + edgeWidth, sampleDistance);
    
    return half4(color.r, color.g, color.b, insideness);
}
