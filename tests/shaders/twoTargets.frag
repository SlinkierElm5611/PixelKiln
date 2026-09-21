#version 450

layout(set = 0, binding = 0) uniform Params {
    vec4 color;
} params;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outInverse;

void main()
{
    outColor = params.color;
    outInverse = vec4(1.0 - params.color.rgb, 1.0);
}
