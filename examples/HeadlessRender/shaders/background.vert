#version 450

// Full screen triangle at depth 0.9, no vertex buffers.
void main()
{
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.9, 1.0);
}
