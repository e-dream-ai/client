#version 450

// Converts decoded 8-bit 4:2:0 video planes (NV12 or YUV420P) to RGB.
// Renders into the frame texture's R8G8B8A8_UNORM image, so the result is
// stored exactly like the RGBA frames uploaded from the CPU path: the
// gamma-encoded R'G'B' values, as on Mac and Windows.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D planeY;
layout(set = 0, binding = 1) uniform sampler2D planeU;  // NV12: interleaved UV in .rg
layout(set = 0, binding = 2) uniform sampler2D planeV;  // YUV420P only

// Must match VkPushConstantsYUV in RendererVulkan.h.
layout(push_constant) uniform PC {
    vec4 matrix;  // Cr->R, Cb->G, Cr->G, Cb->B
    vec4 range;   // luma scale, luma offset, chroma scale, chroma offset
    int  interleavedChroma;
} pc;

void main()
{
    float y = texture(planeY, fragUV).r;
    vec2 cbcr = (pc.interleavedChroma != 0)
        ? texture(planeU, fragUV).rg
        : vec2(texture(planeU, fragUV).r, texture(planeV, fragUV).r);

    y    = (y - pc.range.y) * pc.range.x;
    cbcr = (cbcr - pc.range.w) * pc.range.z;

    vec3 rgb = vec3(y + pc.matrix.x * cbcr.y,
                    y - pc.matrix.y * cbcr.x - pc.matrix.z * cbcr.y,
                    y + pc.matrix.w * cbcr.x);

    outColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
