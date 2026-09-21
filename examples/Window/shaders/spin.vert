#version 450

layout(set = 0, binding = 0) uniform Params {
    float angle;
    float aspect; // width / height
} params;

layout(location = 0) out vec3 outColor;

// A triangle without vertex buffers, rotated by the uniform angle.
void main()
{
    const vec2 corners[3] = vec2[](vec2(0.0, -0.6), vec2(0.52, 0.3), vec2(-0.52, 0.3));
    const vec3 colors[3] = vec3[](vec3(1.0, 0.3, 0.2), vec3(0.2, 1.0, 0.4), vec3(0.2, 0.4, 1.0));
    vec2 p = corners[gl_VertexIndex];
    float c = cos(params.angle);
    float s = sin(params.angle);
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    gl_Position = vec4(p.x / params.aspect, p.y, 0.0, 1.0);
    outColor = colors[gl_VertexIndex];
}
