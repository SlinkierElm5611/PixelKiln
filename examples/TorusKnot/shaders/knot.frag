#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in float inAlong;

layout(set = 0, binding = 0) uniform Params {
    mat4 model;
    mat4 viewProjection;
    vec4 light;
    vec4 eye;
} params;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(inNormal);
    vec3 l = normalize(params.light.xyz);
    vec3 v = normalize(params.eye.xyz - inPosition);
    vec3 h = normalize(l + v);
    // Colors cycle three times around the knot and slowly flow along it.
    vec3 base = 0.55 + 0.45 * cos(6.2831853 * (inAlong * 3.0 - params.light.w + vec3(0.0, 0.33, 0.67)));
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 64.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    outColor = vec4(base * (0.12 + 0.88 * diffuse) + vec3(0.5 * specular) + 0.3 * rim * base, 1.0);
}
