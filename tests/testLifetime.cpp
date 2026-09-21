//
// Created by Stefan Balta on 2026-09-21.
//

// Creation, configuration and destruction, including destroying things the GPU is still using. Use-after-free on
// the GPU shows up as validation errors, which fail the test.

#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t storeSpirv[] =
#include "store.comp.h"
;

static uint64_t loadStore(PixelKiln &kiln)
{
    return kiln.loadComputeProgram({shaderFrom(storeSpirv),
                                    {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
}

static uint64_t store(PixelKiln &kiln, uint64_t program, uint64_t output, uint32_t index, uint32_t value)
{
    const uint32_t params[2] = {index, value};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_COMPUTE;
    call.program = program;
    call.bindings = {{.data = params, .size = sizeof(params)}, {.resource = output}};
    return kiln.call(call);
}

TEST(create_and_destroy)
{
    PixelKiln kiln;
}

TEST(config_options)
{
    Config config;
    config.applicationName = "PixelKilnTests";
    config.gpuType = INTEGRATED;
    config.enableValidation = true;
    PixelKiln kiln(config);
    uint64_t program = loadStore(kiln);
    uint64_t output = kiln.createBuffer(16);
    store(kiln, program, output, 1, 77);
    CHECK_EQ(download<uint32_t>(kiln, output, 4)[1], 77u);
}

TEST(two_instances)
{
    PixelKiln first;
    PixelKiln second;
    uint64_t firstProgram = loadStore(first);
    uint64_t secondProgram = loadStore(second);
    uint64_t firstOutput = first.createBuffer(16);
    uint64_t secondOutput = second.createBuffer(16);
    store(first, firstProgram, firstOutput, 0, 1);
    store(second, secondProgram, secondOutput, 0, 2);
    CHECK_EQ(download<uint32_t>(first, firstOutput, 1)[0], 1u);
    CHECK_EQ(download<uint32_t>(second, secondOutput, 1)[0], 2u);
}

// Everything is destroyed right after being used, without waiting.
TEST(destroy_while_in_flight)
{
    PixelKiln kiln;
    for (int round = 0; round < 20; round++) {
        uint64_t program = loadStore(kiln);
        uint64_t output = kiln.createBuffer(1024);
        uint64_t image = kiln.createImage({64, 64, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
        std::vector<uint32_t> pixels(64 * 64, 0xffffffff);
        kiln.uploadImage(image, pixels.data(), pixels.size() * 4);
        kiln.uploadBuffer(output, pixels.data(), 1024);
        for (uint32_t i = 0; i < 10; i++) {
            store(kiln, program, output, i, i);
        }
        kiln.destroyImage(image);
        kiln.destroyBuffer(output);
        kiln.unloadProgram(program);
    }
    // Still fully usable afterwards.
    uint64_t program = loadStore(kiln);
    uint64_t output = kiln.createBuffer(16);
    store(kiln, program, output, 2, 99);
    CHECK_EQ(download<uint32_t>(kiln, output, 4)[2], 99u);
}

// The destructor must wait for queued work and free resources the user never destroyed.
TEST(destructor_with_work_in_flight)
{
    PixelKiln kiln;
    uint64_t program = loadStore(kiln);
    uint64_t output = kiln.createBuffer(1 << 20);
    std::vector<uint8_t> data(1 << 20, 7);
    kiln.uploadBuffer(output, data.data(), data.size());
    for (uint32_t i = 0; i < 200; i++) {
        store(kiln, program, output, i, i);
    }
    kiln.uploadBuffer(output, data.data(), 4096);
    kiln.createImage({32, 32, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED | IMAGE_USAGE_STORAGE});
    kiln.createBuffer(256);
}
