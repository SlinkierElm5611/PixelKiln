//
// Created by Stefan Balta on 2026-09-21.
//

#include <cmath>
#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t scaleSpirv[] =
#include "scale.comp.h"
;
static const uint32_t gradientSpirv[] =
#include "gradient.comp.h"
;
static const uint32_t twoUniformsSpirv[] =
#include "twoUniforms.comp.h"
;
static const uint32_t storeSpirv[] =
#include "store.comp.h"
;
static const uint32_t doubleSpirv[] =
#include "double.comp.h"
;

struct ScaleParams {
    float scale;
    float bias;
    uint32_t count;
};

static uint64_t loadScale(PixelKiln &kiln)
{
    return kiln.loadComputeProgram({shaderFrom(scaleSpirv), {UNIFORM_BINDING_TYPE_BUFFER,
                                                              UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                              UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
}

static ProgramCall scaleCall(uint64_t program, const ScaleParams &params, uint64_t input, uint64_t output)
{
    ProgramCall call{};
    call.type = PROGRAM_TYPE_COMPUTE;
    call.program = program;
    call.bindings = {{.data = &params, .size = sizeof(params)}, {.resource = input}, {.resource = output}};
    call.groupCountX = (params.count + 63) / 64;
    return call;
}

TEST(compute_storage_buffer)
{
    PixelKiln kiln;
    uint64_t program = loadScale(kiln);
    const uint32_t count = 1000;
    std::vector<float> input(count);
    for (uint32_t i = 0; i < count; i++) {
        input[i] = float(i) * 0.5f;
    }
    uint64_t inputBuffer = kiln.createBuffer(count * sizeof(float));
    uint64_t outputBuffer = kiln.createBuffer(count * sizeof(float));
    kiln.uploadBuffer(inputBuffer, input.data(), count * sizeof(float));

    ScaleParams params{3.0f, 1.0f, count};
    kiln.call(scaleCall(program, params, inputBuffer, outputBuffer));

    std::vector<float> output = download<float>(kiln, outputBuffer, count);
    int wrong = 0;
    for (uint32_t i = 0; i < count; i++) {
        wrong += std::fabs(output[i] - (input[i] * 3.0f + 1.0f)) > 1e-4f;
    }
    CHECK_EQ(wrong, 0);
}

// The second call reads what the first one wrote, on the same queue.
TEST(compute_chained_calls)
{
    PixelKiln kiln;
    uint64_t program = loadScale(kiln);
    const uint32_t count = 4096;
    std::vector<float> input(count);
    for (uint32_t i = 0; i < count; i++) {
        input[i] = float(i % 97);
    }
    uint64_t a = kiln.createBuffer(count * sizeof(float));
    uint64_t b = kiln.createBuffer(count * sizeof(float));
    uint64_t c = kiln.createBuffer(count * sizeof(float));
    kiln.uploadBuffer(a, input.data(), count * sizeof(float));
    ScaleParams first{2.0f, 1.0f, count};
    ScaleParams second{3.0f, -4.0f, count};
    kiln.call(scaleCall(program, first, a, b));
    kiln.call(scaleCall(program, second, b, c));
    // Ping back into a, which the first call read.
    kiln.call(scaleCall(program, first, c, a));

    std::vector<float> output = download<float>(kiln, a, count);
    int wrong = 0;
    for (uint32_t i = 0; i < count; i++) {
        float expected = ((input[i] * 2.0f + 1.0f) * 3.0f - 4.0f) * 2.0f + 1.0f;
        wrong += std::fabs(output[i] - expected) > 1e-3f;
    }
    CHECK_EQ(wrong, 0);
}

// Calls without uniform data submit nothing to the transfer queue, so only the ordering between calls on the all
// queue keeps each one from reading its input before the previous call finished writing it.
TEST(compute_chained_calls_without_uniforms)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(doubleSpirv), {UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                                        UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    const uint32_t count = 64 * 256;
    std::vector<uint32_t> input(count);
    for (uint32_t i = 0; i < count; i++) {
        input[i] = i;
    }
    uint64_t a = kiln.createBuffer(count * sizeof(uint32_t));
    uint64_t b = kiln.createBuffer(count * sizeof(uint32_t));
    kiln.uploadBuffer(a, input.data(), count * sizeof(uint32_t));
    auto doubled = [&](uint64_t source, uint64_t destination) {
        ProgramCall call{};
        call.type = PROGRAM_TYPE_COMPUTE;
        call.program = program;
        call.bindings = {{.resource = source}, {.resource = destination}};
        call.groupCountX = count / 64;
        kiln.call(call);
    };
    // a -> b -> a -> b: read after write, write after read and write after write on the same buffers.
    doubled(a, b);
    doubled(b, a);
    doubled(a, b);
    std::vector<uint32_t> output = download<uint32_t>(kiln, b, count);
    int wrong = 0;
    for (uint32_t i = 0; i < count; i++) {
        wrong += output[i] != ((i * 2 + 1) * 2 + 1) * 2 + 1;
    }
    CHECK_EQ(wrong, 0);
}

TEST(compute_storage_image_with_empty_binding)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(gradientSpirv),
                                                {UNIFORM_BINDING_TYPE_EMPTY, UNIFORM_BINDING_TYPE_STORAGE_IMAGE}});
    const uint32_t size = 32;
    uint64_t image = kiln.createImage({size, size, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_STORAGE});
    ProgramCall call{};
    call.type = PROGRAM_TYPE_COMPUTE;
    call.program = program;
    call.bindings = {{}, {.resource = image}};
    call.groupCountX = size / 8;
    call.groupCountY = size / 8;
    kiln.call(call);

    std::vector<uint8_t> pixels = downloadPixels(kiln, image, size, size);
    int wrong = 0;
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            wrong += pixelAt(pixels, size, x, y) != packRgba(x, y, 0, 255);
        }
    }
    CHECK_EQ(wrong, 0);
}

// Several uniform buffers in one call, with sizes that need offset alignment between them.
TEST(compute_multiple_uniform_buffers)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(twoUniformsSpirv), {UNIFORM_BINDING_TYPE_BUFFER,
                                                                             UNIFORM_BINDING_TYPE_BUFFER,
                                                                             UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    uint64_t output = kiln.createBuffer(4 * sizeof(uint32_t));
    const uint32_t a = 0xA11CE;
    const uint32_t b[3] = {11, 22, 33};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_COMPUTE;
    call.program = program;
    call.bindings = {{.data = &a, .size = sizeof(a)}, {.data = b, .size = sizeof(b)}, {.resource = output}};
    kiln.call(call);

    std::vector<uint32_t> values = download<uint32_t>(kiln, output, 4);
    CHECK_EQ(values[0], a);
    CHECK_EQ(values[1], 11u);
    CHECK_EQ(values[2], 22u);
    CHECK_EQ(values[3], 33u);
}

// The caller's uniform memory may be reused as soon as call() returns.
TEST(compute_uniform_data_copied_during_call)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(storeSpirv),
                                                {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    const uint32_t count = 16;
    uint64_t output = kiln.createBuffer(count * sizeof(uint32_t));
    uint32_t params[2];
    for (uint32_t i = 0; i < count; i++) {
        params[0] = i;
        params[1] = 100 + i;
        ProgramCall call{};
        call.type = PROGRAM_TYPE_COMPUTE;
        call.program = program;
        call.bindings = {{.data = params, .size = sizeof(params)}, {.resource = output}};
        kiln.call(call);
        params[0] = 0xdeadbeef; // overwritten right away
        params[1] = 0xdeadbeef;
    }
    std::vector<uint32_t> values = download<uint32_t>(kiln, output, count);
    for (uint32_t i = 0; i < count; i++) {
        CHECK_EQ(values[i], 100 + i);
    }
}

TEST(compute_tickets)
{
    PixelKiln kiln;
    uint64_t program = loadScale(kiln);
    const uint32_t count = 64;
    uint64_t a = kiln.createBuffer(count * sizeof(float));
    uint64_t b = kiln.createBuffer(count * sizeof(float));
    std::vector<float> zeros(count, 0.0f);
    kiln.uploadBuffer(a, zeros.data(), count * sizeof(float));
    ScaleParams params{1.0f, 1.0f, count};

    uint64_t previous = 0;
    std::vector<uint64_t> tickets;
    for (int i = 0; i < 8; i++) {
        uint64_t ticket = kiln.call(scaleCall(program, params, a, b));
        CHECK(ticket > previous);
        previous = ticket;
        tickets.push_back(ticket);
    }
    kiln.wait(tickets[3]);
    for (int i = 0; i <= 3; i++) {
        CHECK(kiln.isComplete(tickets[i]));
    }
    kiln.waitIdle();
    for (uint64_t ticket : tickets) {
        CHECK(kiln.isComplete(ticket));
    }
    CHECK(!kiln.isComplete(previous + 1));
}
