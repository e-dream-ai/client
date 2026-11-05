#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D frame1;
layout(set = 2, binding = 0) uniform sampler2D frame2;

layout(push_constant) uniform PC {
    float screenWidth;
    float screenHeight;
    float r, g, b, a;
    float delta;
} pc;

void main()
{
    vec4 c1 = texture(frame1, fragUV);
    vec4 c2 = texture(frame2, fragUV);
    outColor = mix(c1, c2, pc.delta) * fragColor;
}
