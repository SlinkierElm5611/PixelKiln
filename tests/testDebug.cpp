//
// Created by Stefan Balta on 2026-09-22.
//

// Debug names/labels (VK_EXT_debug_utils) are purely diagnostic: passing them must not change behavior, and must be
// safe to omit (every other test in this suite does). Run this test directly (not through ctest, which suppresses
// validation layer output) to see them show up as "PixelKiln validation:" messages if something is misused.

#include <cmath>
#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t scaleSpirv[] =
#include "scale.comp.h"
;
static const uint32_t positionVertSpirv[] =
#include "position.vert.h"
;
static const uint32_t solidFragSpirv[] =
#include "solid.frag.h"
;

struct ScaleParams {
    float scale;
    float bias;
    uint32_t count;
};

TEST(debug_names_and_labels)
{
    PixelKiln kiln;
    uint64_t compute = kiln.loadComputeProgram({shaderFrom(scaleSpirv), {UNIFORM_BINDING_TYPE_BUFFER,
                                                                         UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                                         UNIFORM_BINDING_TYPE_STORAGE_BUFFER}},
                                               "scale");

    RasterDrawProgram drawProgram{};
    drawProgram.vertexShader = shaderFrom(positionVertSpirv);
    drawProgram.fragmentShader = shaderFrom(solidFragSpirv);
    drawProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    drawProgram.vertexLayout.buffers = {{4 * sizeof(float)}};
    drawProgram.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    drawProgram.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    uint64_t draw = kiln.loadRasterDrawProgram(drawProgram, "solid triangle");

    const uint32_t count = 64;
    std::vector<float> input(count);
    for (uint32_t i = 0; i < count; i++) {
        input[i] = float(i);
    }
    uint64_t inputBuffer = kiln.createBuffer(count * sizeof(float), "scale input");
    uint64_t outputBuffer = kiln.createBuffer(count * sizeof(float), "scale output");
    kiln.uploadBuffer(inputBuffer, input.data(), count * sizeof(float));

    ScaleParams params{2.0f, 1.0f, count};
    ProgramCall computeCall{};
    computeCall.type = PROGRAM_TYPE_COMPUTE;
    computeCall.program = compute;
    computeCall.bindings = {{.data = &params, .size = sizeof(params)}, {.resource = inputBuffer},
                            {.resource = outputBuffer}};
    computeCall.groupCountX = (count + 63) / 64;
    computeCall.debugLabel = "scale pass";
    kiln.call(computeCall);

    std::vector<float> output = download<float>(kiln, outputBuffer, count);
    int wrong = 0;
    for (uint32_t i = 0; i < count; i++) {
        wrong += std::fabs(output[i] - (input[i] * 2.0f + 1.0f)) > 1e-4f;
    }
    CHECK_EQ(wrong, 0);

    const float positions[] = {-1.0f, -1.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f, -1.0f, 3.0f, 0.0f, 1.0f};
    uint64_t triangle = kiln.createBuffer(sizeof(positions), "triangle vertices");
    kiln.uploadBuffer(triangle, positions, sizeof(positions));
    uint64_t color = kiln.createImage({32, 32, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET}, "color target");

    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ProgramCall drawCall{};
    drawCall.type = PROGRAM_TYPE_RASTER_DRAW;
    drawCall.program = draw;
    drawCall.bindings = {{.data = white, .size = sizeof(white)}};
    drawCall.colorTargets = {{color, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
    drawCall.vertexBuffers = {triangle};
    drawCall.vertexCount = 3;
    drawCall.debugLabel = "draw pass";
    kiln.call(drawCall);

    std::vector<uint8_t> pixels = downloadPixels(kiln, color, 32, 32);
    CHECK_EQ(pixelAt(pixels, 32, 16, 16), packRgba(255, 255, 255, 255));
}
