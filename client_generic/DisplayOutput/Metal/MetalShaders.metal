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

vertex ColorInOut quadPassVertex(uint vid [[vertex_id]])
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    float4 position = float4(uv * float2(2, -2) + float2(-1, 1), 1, 1);

    ColorInOut out;
    out.position = position;
    out.uv = uv;
    return out;
}

fragment float4 texture_fragment_YUV2(ColorInOut vert [[stage_in]],
                                      texture2d<float, access::sample> yTexture
                                      [[texture(0)]],
                                      texture2d<float, access::sample> uvTexture
                                      [[texture(1)]])
{
    float3x3 colorMatrix = float3x3(float3(1, 1, 1), float3(0, -0.344, 1.770),
                                    float3(1.403, -0.714, 0));
    constexpr sampler s(address::repeat, filter::linear);
    //return yTexture.sample(s, vert.uv);
    float3 yuv;
    yuv.x = yTexture.sample(s, vert.uv).x;
    yuv.yz = uvTexture.sample(s, vert.uv).xy;
    float3 rgb = colorMatrix * yuv;
    return float4(rgb, 1);
}

fragment float4 drawTextureFragment(ColorInOut vert [[stage_in]],
                                    texture2d<float, access::sample> texture
                                    [[texture(0)]],
                                    constant QuadUniforms& uniforms
                                    [[buffer(0)]])
{
    constexpr sampler s(address::clamp_to_zero, filter::linear);
    float2 adjustedUV = (vert.uv - uniforms.rect.xy) / uniforms.rect.zw;
    float alpha = uniforms.color.a * all(adjustedUV > 0 && adjustedUV < 1);
    adjustedUV = (adjustedUV.xy + uniforms.uvRect.xy) * uniforms.uvRect.zw;
    float4 color = texture.sample(s, adjustedUV).zyxw;
    color.rgb += uniforms.brightness;
    return float4(color.rgb * uniforms.color.rgb, alpha * color.a);
}

float4 SampleYUVTexturesRGBA(float2 uv,
                             texture2d<float, access::sample> yTexture,
                             texture2d<float, access::sample> uvTexture)
{
    constexpr sampler s(address::repeat, filter::linear);
    float3 yuv;
    float4 rgba;
    yuv.x = yTexture.sample(s, uv).x;

    yuv.yz = uvTexture.sample(s, uv).xy;
    //return float4(yuv, 1);
    if (yuv.x == 0)
        return float4(1, 0, 0, 0.5);
    if (all(yuv.yz == float2(0, 0)))
        return float4(0, 0, 1, 0.5);
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

fragment float4 drawDecodedFrameNoBlendingFragment(
    ColorInOut vert [[stage_in]],
    texture2d<float, access::sample> yTexture1 [[texture(0)]],
    texture2d<float, access::sample> uvTexture1 [[texture(1)]],
    constant QuadUniforms& uniforms [[buffer(0)]])
{
    float4 color = SampleYUVTexturesRGBA(vert.uv, yTexture1, uvTexture1);
    color.rgb += uniforms.brightness;
    return color * uniforms.color;
}

struct LinearFrameBlendParameters
{
    float delta;
    float newAlpha;
    float transPct;
};

fragment float4 drawDecodedFrameLinearFrameBlendFragment(
    ColorInOut vert [[stage_in]],
    texture2d<float, access::sample> frame1Y [[texture(2)]],
    texture2d<float, access::sample> frame1UV [[texture(3)]],
    texture2d<float, access::sample> frame2Y [[texture(4)]],
    texture2d<float, access::sample> frame2UV [[texture(5)]],
    constant QuadUniforms& uniforms [[buffer(0)]],
    constant LinearFrameBlendParameters& frameBlend [[buffer(1)]])
{
    float4 frame1RGBA = SampleYUVTexturesRGBA(vert.uv, frame1Y, frame1UV);
    float4 frame2RGBA = SampleYUVTexturesRGBA(vert.uv, frame2Y, frame2UV);
    float4 color = mix(frame1RGBA, frame2RGBA, frameBlend.delta);
    color.a = uniforms.color.a;
    color.rgb += uniforms.brightness;
    return color;
}

struct CubicFrameBlendParameters
{
    float4 weights;
    float newAlpha;
    float transPct;
};

fragment float4 drawDecodedFrameCubicFrameBlendFragment(
    ColorInOut vert [[stage_in]],
    texture2d<float, access::sample> frame1Y [[texture(2)]],
    texture2d<float, access::sample> frame1UV [[texture(3)]],
    texture2d<float, access::sample> frame2Y [[texture(4)]],
    texture2d<float, access::sample> frame2UV [[texture(5)]],
    texture2d<float, access::sample> frame3Y [[texture(6)]],
    texture2d<float, access::sample> frame3UV [[texture(7)]],
    texture2d<float, access::sample> frame4Y [[texture(8)]],
    texture2d<float, access::sample> frame4UV [[texture(9)]],
    constant QuadUniforms& uniforms [[buffer(0)]],
    constant CubicFrameBlendParameters& frameBlend [[buffer(1)]])
{

#if 0
    float4 f1 =
        SampleYUVTexturesRGBA(vert.uv * 2, frame1Y, frame1UV);
    float4 f2 = SampleYUVTexturesRGBA(vert.uv * 2 - float2(1, 0), frame2Y,
                                      frame2UV);
    float4 f3 = SampleYUVTexturesRGBA(vert.uv * 2 - float2(1, 1), frame3Y,
                                      frame3UV);
    float4 f4 = SampleYUVTexturesRGBA(vert.uv * 2 - float2(0, 1), frame4Y,
                                      frame4UV);

    float4 s1 =
        SampleYUVTexturesRGBA(vert.uv * 2, video2frame1Y, video2frame1UV);
    float4 s2 = SampleYUVTexturesRGBA(vert.uv * 2 - float2(1, 0), video2frame2Y,
                                      video2frame2UV);
    float4 s3 = SampleYUVTexturesRGBA(vert.uv * 2 - float2(1, 1), video2frame3Y,
                                      video2frame3UV);
    float4 s4 = SampleYUVTexturesRGBA(vert.uv * 2 - float2(0, 1), video2frame4Y,
                                      video2frame4UV);

    float transPct = frameBlend.transPct;

    //if (transPct > 0)
    //  transPct = 1;

    //transPct = 0;

    if (vert.uv.x < 0.5)
    {
        if (vert.uv.y < 0.5)
        {
            if (vert.uv.x < 0.05)
                return float4(step(vert.uv.y * 2, frameBlend.weights.x), 0, 0, 1);
            return mix(f1, s1, transPct);
        }
        else
        {
            if (vert.uv.x < 0.05)
                return float4(step(vert.uv.y * 2 - 1, frameBlend.weights.w), 0, 0, 1);
            return mix(f4, s4, transPct);
        }
    }
    else
    {
        if (vert.uv.y < 0.5)
        {
            if (vert.uv.x < 0.55)
                return float4(step(vert.uv.y * 2, frameBlend.weights.y), 0, 0, 1);
            return mix(f2, s2, transPct);
        }
        else
        {
            if (vert.uv.x < 0.55)
                return float4(step(vert.uv.y * 2 - 1, frameBlend.weights.z), 0, 0, 1);
            return mix(f3, s3, transPct);
        }
    }
#endif
    //return frameBlend.weights;

    float4 c1 = SampleYUVTexturesRGBA(vert.uv, frame1Y, frame1UV);
    float4 c2 = SampleYUVTexturesRGBA(vert.uv, frame2Y, frame2UV);
    float4 c3 = SampleYUVTexturesRGBA(vert.uv, frame3Y, frame3UV);
    float4 c4 = SampleYUVTexturesRGBA(vert.uv, frame4Y, frame4UV);
    float4 color = (c1 * frameBlend.weights.x) + (c2 * frameBlend.weights.y) +
                   (c3 * frameBlend.weights.z) + (c4 * frameBlend.weights.w);

    color.a = uniforms.color.a;
    color.rgb += uniforms.brightness;
    return color;
}

struct TransformedVertex
{
    float4 position [[position]];
    float2 texCoords;
};

vertex TransformedVertex drawTextVertex(constant VertexText* vertices
                                        [[buffer(0)]],
                                        constant TextUniforms& uniforms
                                        [[buffer(1)]],
                                        uint vid [[vertex_id]])
{
    TransformedVertex outVert;
    outVert.position = uniforms.viewProjectionMatrix * uniforms.modelMatrix *
                       float4(vertices[vid].position);
    outVert.texCoords = vertices[vid].texCoords;
    return outVert;
}

fragment half4 drawTextFragment(TransformedVertex vert [[stage_in]],
                                constant TextUniforms& uniforms [[buffer(0)]],
                                texture2d<float, access::sample> texture
                                [[texture(0)]])
{
    //return 1;
    constexpr sampler s(address::repeat, filter::linear);

    float4 color = uniforms.foregroundColor;
    // Outline of glyph is the isocontour with value 50%
    float edgeDistance = 0.5;
    // Sample the signed-distance field to find distance from this fragment to the glyph outline
    float sampleDistance = texture.sample(s, vert.texCoords).r;
    // Use local automatic gradients to find anti-aliased anisotropic edge width, cf. Gustavson 2012
    float edgeWidth =
        0.75 * length(float2(dfdx(sampleDistance), dfdy(sampleDistance))) *
        0.25;
    // Smooth the glyph edge by interpolating across the boundary in a band with the width determined above
    float insideness = smoothstep(edgeDistance - edgeWidth,
                                  edgeDistance + edgeWidth, sampleDistance);

    return half4(color.r, color.g, color.b, insideness);
}
