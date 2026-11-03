#version 450

// Fullscreen triangle generated from gl_VertexIndex (draw 3 vertices, no
// vertex buffer). UV (0,0) is the top-left texel of the render target.
layout(location = 0) out vec2 fragUV;

void main()
{
    vec2 uv     = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    fragUV      = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
