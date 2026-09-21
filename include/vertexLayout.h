//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_VERTEXLAYOUT_H
#define PIXELKILN_VERTEXLAYOUT_H
#include <cstdint>
#include <vector>

enum VertexFormat {
    VERTEX_FORMAT_FLOAT,
    VERTEX_FORMAT_FLOAT2,
    VERTEX_FORMAT_FLOAT3,
    VERTEX_FORMAT_FLOAT4,
    VERTEX_FORMAT_INT,
    VERTEX_FORMAT_INT2,
    VERTEX_FORMAT_INT3,
    VERTEX_FORMAT_INT4,
    VERTEX_FORMAT_UINT,
    VERTEX_FORMAT_UINT2,
    VERTEX_FORMAT_UINT3,
    VERTEX_FORMAT_UINT4,
    VERTEX_FORMAT_UNORM8X4,
    VERTEX_FORMAT_COUNT
};

struct VertexAttribute {
    uint32_t location;
    uint32_t buffer; // index into VertexLayout::buffers
    VertexFormat format;
    uint32_t offset;
};

struct VertexBufferLayout {
    uint32_t stride;
    bool perInstance = false;
};

struct VertexLayout {
    std::vector<VertexBufferLayout> buffers; // buffers[i] == vertex buffer slot i
    std::vector<VertexAttribute> attributes;
};

#endif //PIXELKILN_VERTEXLAYOUT_H
