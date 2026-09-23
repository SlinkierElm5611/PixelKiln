#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inAlong; // 0..1 along the knot

layout(set = 0, binding = 0) uniform Params {
    mat4 model;
    mat4 viewProjection;
    vec4 light; // xyz direction toward the light, w how far the colors have flowed along the knot
    vec4 eye;
} params;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outPosition;
layout(location = 2) out float outAlong;

void main()
{
    vec4 world = params.model * vec4(inPosition, 1.0);
    outNormal = mat3(params.model) * inNormal;
    outPosition = world.xyz;
    outAlong = inAlong;
    gl_Position = params.viewProjection * world;
}
