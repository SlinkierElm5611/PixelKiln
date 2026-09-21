//
// Created by Stefan Balta on 2026-09-21.
//

// Transfer queue / all queue interplay: uploads overlapping calls, resources moving between uses, ring reuse.

#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t storeSpirv[] =
#include "store.comp.h"
;
static const uint32_t readbackSpirv[] =
#include "readback.comp.h"
;
static const uint32_t positionVertSpirv[] =
#include "position.vert.h"
;
static const uint32_t solidFragSpirv[] =
#include "solid.frag.h"
;
static const uint32_t accumulateSpirv[] =
#include "scale.comp.h"
;

// Batch i + 1 is uploaded into the other of two buffers while call i runs. Each upload may target a buffer that the
// previous call is still reading, so it has to wait for exactly that call.
TEST(pipelined_ping_pong_uploads)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(accumulateSpirv), {UNIFORM_BINDING_TYPE_BUFFER,
                                                                            UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                                            UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    const uint32_t iterations = 48;
    const uint32_t count = 2048;
    auto batch = [&](uint32_t i) {
        std::vector<float> values(count);
        for (uint32_t j = 0; j < count; j++) {
            values[j] = float((i * 131 + j) % 1000);
        }
        return values;
    };
    uint64_t inputs[2] = {kiln.createBuffer(count * sizeof(float)), kiln.createBuffer(count * sizeof(float))};
    std::vector<uint64_t> outputs;
    for (uint32_t i = 0; i < iterations; i++) {
        outputs.push_back(kiln.createBuffer(count * sizeof(float)));
    }
    struct Params {
        float scale;
        float bias;
        uint32_t count;
    };

    kiln.uploadBuffer(inputs[0], batch(0).data(), count * sizeof(float));
    for (uint32_t i = 0; i < iterations; i++) {
        Params params{2.0f, float(i), count};
        ProgramCall call{};
        call.type = PROGRAM_TYPE_COMPUTE;
        call.program = program;
        call.bindings = {{.data = &params, .size = sizeof(params)}, {.resource = inputs[i % 2]},
                         {.resource = outputs[i]}};
        call.groupCountX = count / 64;
        kiln.call(call);
        if (i + 1 < iterations) {
            kiln.uploadBuffer(inputs[(i + 1) % 2], batch(i + 1).data(), count * sizeof(float));
        }
    }
    int wrong = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        std::vector<float> expected = batch(i);
        std::vector<float> actual = download<float>(kiln, outputs[i], count);
        for (uint32_t j = 0; j < count; j++) {
            wrong += actual[j] != expected[j] * 2.0f + float(i);
        }
    }
    CHECK_EQ(wrong, 0);
}

// Thousands of calls with 1 KiB of uniforms each wrap the uniform ring several times and cycle descriptor pools.
// Every call writes the same few slots, so the result also checks that calls execute in order.
TEST(uniform_ring_wraparound)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(storeSpirv),
                                                {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    const uint32_t slots = 256;
    const uint32_t calls = 6000;
    uint64_t output = kiln.createBuffer(slots * sizeof(uint32_t));
    struct Params {
        uint32_t index;
        uint32_t value;
        uint32_t padding[254];
    } params{};
    uint64_t firstTicket = 0;
    uint64_t lastTicket = 0;
    for (uint32_t i = 0; i < calls; i++) {
        params.index = i % slots;
        params.value = i;
        ProgramCall call{};
        call.type = PROGRAM_TYPE_COMPUTE;
        call.program = program;
        call.bindings = {{.data = &params, .size = sizeof(params)}, {.resource = output}};
        lastTicket = kiln.call(call);
        if (i == 0) {
            firstTicket = lastTicket;
        }
    }
    kiln.wait(lastTicket);
    CHECK(kiln.isComplete(firstTicket));
    std::vector<uint32_t> values = download<uint32_t>(kiln, output, slots);
    int wrong = 0;
    for (uint32_t k = 0; k < slots; k++) {
        wrong += values[k] != k + slots * ((calls - 1 - k) / slots);
    }
    CHECK_EQ(wrong, 0);
}

static uint64_t loadReadback(PixelKiln &kiln)
{
    return kiln.loadComputeProgram({shaderFrom(readbackSpirv), {UNIFORM_BINDING_TYPE_SAMPLER,
                                                                UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                                UNIFORM_BINDING_TYPE_BUFFER}});
}

static ProgramCall readbackCall(uint64_t program, uint64_t image, uint64_t output, const uint32_t &width)
{
    ProgramCall call{};
    call.type = PROGRAM_TYPE_COMPUTE;
    call.program = program;
    call.bindings = {{.resource = image, .sampler = {SAMPLER_FILTER_NEAREST}}, {.resource = output},
                     {.data = &width, .size = sizeof(width)}};
    call.groupCountX = width / 8;
    call.groupCountY = width / 8;
    return call;
}

// Render target -> sampled by the next call -> downloaded: every layout change between the two queues.
TEST(render_target_sampled_then_downloaded)
{
    PixelKiln kiln;
    const uint32_t size = 16;
    RasterDrawProgram drawProgram{};
    drawProgram.vertexShader = shaderFrom(positionVertSpirv);
    drawProgram.fragmentShader = shaderFrom(solidFragSpirv);
    drawProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    drawProgram.vertexLayout.buffers = {{4 * sizeof(float)}};
    drawProgram.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    drawProgram.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    uint64_t draw = kiln.loadRasterDrawProgram(drawProgram);
    uint64_t readback = loadReadback(kiln);

    const float positions[] = {-1.0f, -1.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f, -1.0f, 3.0f, 0.0f, 1.0f};
    uint64_t triangle = kiln.createBuffer(sizeof(positions));
    kiln.uploadBuffer(triangle, positions, sizeof(positions));
    uint64_t target = kiln.createImage({size, size, IMAGE_FORMAT_RGBA8_UNORM,
                                        IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_SAMPLED});
    uint64_t output = kiln.createBuffer(size * size * sizeof(uint32_t));

    const uint32_t expected[2] = {packRgba(0, 255, 0, 255), packRgba(255, 0, 255, 255)};
    const float colors[2][4] = {{0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}};
    for (int round = 0; round < 2; round++) {
        ProgramCall call{};
        call.type = PROGRAM_TYPE_RASTER_DRAW;
        call.program = draw;
        call.bindings = {{.data = colors[round], .size = sizeof(colors[round])}};
        call.colorTargets = {{target, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
        call.vertexBuffers = {triangle};
        call.vertexCount = 3;
        kiln.call(call);
        kiln.call(readbackCall(readback, target, output, size));

        int wrong = 0;
        for (uint32_t value : download<uint32_t>(kiln, output, size * size)) {
            wrong += value != expected[round];
        }
        CHECK_EQ(wrong, 0);
        std::vector<uint8_t> pixels = downloadPixels(kiln, target, size, size);
        CHECK_EQ(pixelAt(pixels, size, 7, 9), expected[round]);
    }
}

// Re-uploading a texture right after a call that samples it: the upload must wait for that call only.
TEST(texture_reupload_while_sampled)
{
    PixelKiln kiln;
    const uint32_t size = 16;
    uint64_t readback = loadReadback(kiln);
    uint64_t texture = kiln.createImage({size, size, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    uint64_t first = kiln.createBuffer(size * size * sizeof(uint32_t));
    uint64_t second = kiln.createBuffer(size * size * sizeof(uint32_t));
    std::vector<uint32_t> textureA(size * size, packRgba(10, 20, 30, 255));
    std::vector<uint32_t> textureB(size * size, packRgba(200, 100, 50, 255));

    kiln.uploadImage(texture, textureA.data(), size * size * 4);
    kiln.call(readbackCall(readback, texture, first, size));
    kiln.uploadImage(texture, textureB.data(), size * size * 4);
    kiln.call(readbackCall(readback, texture, second, size));

    int wrongA = 0;
    for (uint32_t value : download<uint32_t>(kiln, first, size * size)) {
        wrongA += value != textureA[0];
    }
    int wrongB = 0;
    for (uint32_t value : download<uint32_t>(kiln, second, size * size)) {
        wrongB += value != textureB[0];
    }
    CHECK_EQ(wrongA, 0);
    CHECK_EQ(wrongB, 0);
}
