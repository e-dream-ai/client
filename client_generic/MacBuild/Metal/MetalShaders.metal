//
//  MetalShaders.metal
//  ScreenSaver
//
//  Created by Tibor Hencz on 26.9.2023.
//

#include <metal_stdlib>
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

fragment float4 texture_fragment(ColorInOut vert [[stage_in]],
                                texture2d<float, access::sample> texture [[texture(0)]])
{
    constexpr sampler s( address::repeat, filter::linear );
    float4 color = texture.sample(s, vert.uv);
    return color;
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

fragment float4 texture_fragment_YUV(ColorInOut vert [[stage_in]],
                                texture2d<float, access::sample> yTexture [[texture(0)]],
                                texture2d<float, access::sample> uvTexture [[texture(1)]])
{
    constexpr sampler s( address::repeat, filter::linear );
    float3 yuv;
    float4 rgba;
    yuv.x = yTexture.sample(s, vert.uv).x;
    yuv.yz = uvTexture.sample(s, vert.uv).xy;
    
    yuv.x = 1.164383561643836 * (yuv.x - 0.0625);
    yuv.y = yuv.y - 0.5;
    yuv.z = yuv.z - 0.5;

    rgba.x = yuv.x + 1.792741071428571 * yuv.z;
    rgba.y = yuv.x - 0.532909328559444 * yuv.z - 0.21324861427373 * yuv.y;
    rgba.z = yuv.x + 2.112401785714286 * yuv.y;
    rgba.x = saturate(1.661 * rgba.x - 0.588 * rgba.y - 0.073 * rgba.z);
    rgba.y = saturate(-0.125 * rgba.x + 1.133 * rgba.y - 0.008 * rgba.z);
    rgba.z = saturate(-0.018 * rgba.x - 0.101 * rgba.y + 1.119 * rgba.z);
    return rgba;
}
