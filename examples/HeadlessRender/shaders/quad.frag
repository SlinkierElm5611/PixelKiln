#version 450

layout(set = 0, binding = 0) uniform Params {
    vec4 tint;
} params;
layout(set = 0, binding = 1) uniform sampler2D tex;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(tex, inUv) * params.tint;
}
