#version 450

// Blended over the trail image every frame so old particle positions fade out.
layout(set = 0, binding = 0) uniform Params {
    vec4 color;
} params;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = params.color;
}
