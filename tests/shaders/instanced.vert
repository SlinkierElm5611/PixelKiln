#version 450

// Per vertex corner, per instance offset.
layout(location = 0) in vec2 inCorner;
layout(location = 1) in vec2 inOffset;

void main()
{
    gl_Position = vec4(inCorner + inOffset, 0.0, 1.0);
}
