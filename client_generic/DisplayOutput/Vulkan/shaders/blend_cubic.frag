#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D frame1;
layout(set = 2, binding = 0) uniform sampler2D frame2;
layout(set = 3, binding = 0) uniform sampler2D frame3;
layout(set = 4, binding = 0) uniform sampler2D frame4;

layout(push_constant) uniform PC {
    float screenWidth;
    float screenHeight;
    float r, g, b, a;
    float w0, w1, w2, w3;
} pc;

void main()
{
    vec4 c1 = texture(frame1, fragUV);
    vec4 c2 = texture(frame2, fragUV);
    vec4 c3 = texture(frame3, fragUV);
    vec4 c4 = texture(frame4, fragUV);
    outColor = clamp(c1*pc.w0 + c2*pc.w1 + c3*pc.w2 + c4*pc.w3, 0.0, 1.0) * fragColor;
}
