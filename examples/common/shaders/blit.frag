#version 450

// Draws an image over the whole target, scaled around the center (letterboxing when scale < 1).
layout(set = 0, binding = 0) uniform sampler2D image;
layout(set = 0, binding = 1) uniform Params {
    vec2 scale; // fraction of the target the image covers on each axis
} params;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = (inUv - 0.5) / params.scale + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        outColor = vec4(0.02, 0.02, 0.03, 1.0);
    } else {
        outColor = vec4(texture(image, uv).rgb, 1.0);
    }
}
