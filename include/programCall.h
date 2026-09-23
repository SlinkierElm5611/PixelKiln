//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_PROGRAMCALL_H
#define PIXELKILN_PROGRAMCALL_H
#include <cstdint>
#include <vector>

#include "indexType.h"
#include "programType.h"
#include "samplerDesc.h"

struct CallBinding {
    const void* data = nullptr; // BUFFER: uniform bytes, copied during call() so the caller may free them right after
    uint64_t size = 0;
    uint64_t resource = 0; // STORAGE_BUFFER: buffer handle. SAMPLER / STORAGE_IMAGE: image handle
    SamplerDesc sampler{}; // SAMPLER only
};

struct ColorTarget {
    uint64_t image = 0;
    bool clear = true; // false keeps the existing contents
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Multisampled targets only: a single sample image of the same size and format (e.g. a swapchain image) that
    // receives the resolved result, the average of each pixel's samples, when the call ends. All of it is overwritten.
    uint64_t resolveImage = 0;
    // false: the image's contents aren't needed after the call and become undefined, which saves memory bandwidth.
    // Typical for a multisampled target once it has been resolved.
    bool store = true;
};

struct DepthTarget {
    uint64_t image = 0;
    bool clear = true;
    float clearDepth = 1.0f;
    bool store = true; // false: the depth values aren't needed after the call (see ColorTarget::store)
};

struct ProgramCall {
    ProgramType type = PROGRAM_TYPE_COMPUTE; // must match the program's type
    uint64_t program = 0;
    std::vector<CallBinding> bindings; // bindings[i] feeds the program's uniformBindings[i], EMPTY slots are ignored

    // PROGRAM_TYPE_COMPUTE
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;

    // PROGRAM_TYPE_RASTER_DRAW
    std::vector<ColorTarget> colorTargets; // one per RasterDrawProgram::colorFormats entry, all the same size
    DepthTarget depthTarget; // required if and only if the program has a depthFormat
    std::vector<uint64_t> vertexBuffers; // one buffer handle per VertexLayout::buffers entry
    uint64_t indexBuffer = 0;
    IndexType indexType = INDEX_TYPE_NONE;
    uint32_t vertexCount = 0; // used when indexType == INDEX_TYPE_NONE
    uint32_t indexCount = 0; // used otherwise
    uint32_t instanceCount = 1;
};

#endif //PIXELKILN_PROGRAMCALL_H
