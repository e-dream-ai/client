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

