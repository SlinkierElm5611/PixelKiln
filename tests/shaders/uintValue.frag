#version 450

// Writes a constant to an unsigned integer target.
layout(set = 0, binding = 0) uniform Params {
    uint value;
} params;

layout(location = 0) out uint outValue;

void main()
{
    outValue = params.value;
}
