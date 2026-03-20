#version 450

// Per-vertex input
layout(location = 0) in vec2 inPos;   // screen-space x,y (pixels)
layout(location = 1) in vec2 inUV;    // texture coords [0,1]

// Push constants — same layout as quad.frag
layout(push_constant) uniform PC {
    float screenWidth;
    float screenHeight;
    float r, g, b, a;
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main()
{
    // Convert pixel coords → NDC: x in [-1,1], y in [-1,1] (Vulkan: +Y down)
    float nx =  (inPos.x / pc.screenWidth)  * 2.0 - 1.0;
    float ny = -(inPos.y / pc.screenHeight) * 2.0 + 1.0;  // flip Y

    gl_Position = vec4(nx, ny, 0.0, 1.0);
    fragUV      = inUV;
    fragColor   = vec4(pc.r, pc.g, pc.b, pc.a);
}
