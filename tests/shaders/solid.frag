#version 450

layout(set = 0, binding = 0) uniform Params {
    vec4 color;
} params;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = params.color;
}
