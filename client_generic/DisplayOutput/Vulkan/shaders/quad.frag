#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

// Same layout as quad.vert
layout(push_constant) uniform PC {
    float screenWidth;
    float screenHeight;
    float r, g, b, a;
} pc;

// Texture sampler (bind white 1x1 for solid-color quads)
layout(binding = 0) uniform sampler2D texSampler;

void main()
{
    vec4 texel = texture(texSampler, fragUV);
    outColor   = texel * fragColor;
}
