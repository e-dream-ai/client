//
//  MetalShaders.metal
//  ScreenSaver
//
//  Created by Tibor Hencz on 26.9.2023.
//

#include <metal_stdlib>
using namespace metal;

constant float3 kLightDirection(0.2, -0.96, 0.2);

constant float kMinDiffuseIntensity = 0.5;

constant float kAlphaTestReferenceValue = 0.5;

struct Vertex
{
    float4 position [[attribute(0)]];
    float4 normal [[attribute(1)]];
    float4 diffuseColor [[attribute(2)]];
    float2 texCoords [[attribute(3)]];
};

struct ProjectedVertex
{
    float4 position [[position]];
    float4 normal;
    float4 diffuseColor;
    float2 texCoords;
};

struct Uniforms
{
    float4x4 viewProjectionMatrix;
};

struct InstanceUniforms
{
    float4x4 modelMatrix;
    float4x4 normalMatrix;
};

vertex ProjectedVertex project_vertex(Vertex vertexIn [[stage_in]],
                                      constant Uniforms &uniforms [[buffer(1)]],
                                      constant InstanceUniforms *instanceUniforms [[buffer(2)]],
                                      ushort vid [[vertex_id]],
                                      ushort iid [[instance_id]])
{
    float4x4 modelMatrix = instanceUniforms[iid].modelMatrix;
    float4x4 normalMatrix = instanceUniforms[iid].normalMatrix;

    ProjectedVertex outVert;
    outVert.position = uniforms.viewProjectionMatrix * modelMatrix * float4(vertexIn.position);
    outVert.normal = normalMatrix * float4(vertexIn.normal);
    outVert.diffuseColor = vertexIn.diffuseColor;
    outVert.texCoords = vertexIn.texCoords;

    return outVert;
}

typedef struct
{
    float4 position [[position]];
    half3  worldNormal;
} ColorInOut;

vertex ColorInOut quadPassVertex(uint vid[[vertex_id]])
{
    ColorInOut out;

    float4 position;
    position.x = (vid == 2) ? 3.0 : -1.0;
    position.y = (vid == 0) ? -3.0 : 1.0;
    position.zw = 1.0;

    out.position = position;
    return out;
}

fragment float4 texture_fragment(ColorInOut vert [[stage_in]],
                                texture2d<float, access::sample> texture [[texture(0)]])
{
    constexpr sampler s( address::repeat, filter::linear );

float4 color = texture.sample(s, float2(vert.position.x * 0.001, vert.position.y * 0.001));
    return color;
    return float4(vert.position.x * 0.001, vert.position.y * 0.001, 0, 1);
}

