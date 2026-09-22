#version 450

// Full screen triangle without vertex buffers. outUv is (0, 0) at the top left of the target.
layout(location = 0) out vec2 outUv;

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
