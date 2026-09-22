#version 450

// The particle buffer the compute shader just wrote, read directly as a vertex buffer.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inVelocity;

layout(set = 0, binding = 0) uniform Params {
    float aspect;
    float alpha;
} params;

layout(location = 0) out vec4 outColor;

void main()
{
    gl_Position = vec4(inPosition.x / params.aspect, inPosition.y, 0.0, 1.0);
    gl_PointSize = 1.0;
    // Blue when slow, magenta, then gold when fast.
    float t = clamp(length(inVelocity) * 0.6, 0.0, 1.0);
    vec3 slow = vec3(0.15, 0.35, 1.0);
    vec3 medium = vec3(0.95, 0.25, 0.85);
    vec3 fast = vec3(1.0, 0.85, 0.35);
    vec3 color = t < 0.5 ? mix(slow, medium, t * 2.0) : mix(medium, fast, t * 2.0 - 1.0);
    outColor = vec4(color, params.alpha);
}
