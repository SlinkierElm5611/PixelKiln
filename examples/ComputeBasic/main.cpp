//
// Created by Stefan Balta on 2026-09-21.
//

// Two compute programs: one scales a storage buffer using uniform parameters, one writes a gradient into a storage
// image. Both results are downloaded and checked.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "pixelKiln.h"

static const uint32_t scaleSpirv[] =
#include "scale.comp.h"
;
static const uint32_t gradientSpirv[] =
#include "gradient.comp.h"
;

struct ScaleParams {
    float scale;
    float bias;
    uint32_t count;
};

int main()
{
    PixelKiln kiln;
    int failures = 0;

    // Storage buffers: out[i] = in[i] * scale + bias
    uint64_t scale = kiln.loadComputeProgram({
        {scaleSpirv, sizeof(scaleSpirv)},
        {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER},
    });
    const uint32_t count = 1000;
    std::vector<float> input(count);
    for (uint32_t i = 0; i < count; i++) {
        input[i] = float(i) * 0.5f;
    }
    uint64_t inputBuffer = kiln.createBuffer(count * sizeof(float));
    uint64_t outputBuffer = kiln.createBuffer(count * sizeof(float));
    kiln.uploadBuffer(inputBuffer, input.data(), count * sizeof(float));

    ScaleParams params{3.0f, 1.0f, count};
    ProgramCall scaleCall{};
    scaleCall.type = PROGRAM_TYPE_COMPUTE;
    scaleCall.program = scale;
    scaleCall.bindings = {
        {.data = &params, .size = sizeof(params)},
        {.resource = inputBuffer},
        {.resource = outputBuffer},
    };
    scaleCall.groupCountX = (count + 63) / 64;
    uint64_t ticket = kiln.call(scaleCall);
    kiln.wait(ticket);
    if (!kiln.isComplete(ticket)) {
        std::printf("ticket not complete after wait\n");
        failures++;
    }

    std::vector<float> output(count);
    kiln.downloadBuffer(outputBuffer, output.data(), count * sizeof(float));
    for (uint32_t i = 0; i < count; i++) {
        float expected = input[i] * params.scale + params.bias;
        if (std::fabs(output[i] - expected) > 1e-4f) {
            if (failures < 10) {
                std::printf("output[%u] = %f, expected %f\n", i, output[i], expected);
            }
            failures++;
        }
    }

    // Storage image: pixel (x, y) = (x, y, 0, 255). Binding 0 is a hole.
    uint64_t gradient = kiln.loadComputeProgram({
        {gradientSpirv, sizeof(gradientSpirv)},
        {UNIFORM_BINDING_TYPE_EMPTY, UNIFORM_BINDING_TYPE_STORAGE_IMAGE},
    });
    const uint32_t size = 32;
    uint64_t image = kiln.createImage({size, size, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_STORAGE});
    ProgramCall gradientCall{};
    gradientCall.type = PROGRAM_TYPE_COMPUTE;
    gradientCall.program = gradient;
    gradientCall.bindings = {{}, {.resource = image}};
    gradientCall.groupCountX = size / 8;
    gradientCall.groupCountY = size / 8;
    kiln.call(gradientCall);

    std::vector<uint8_t> pixels(size * size * 4);
    kiln.downloadImage(image, pixels.data(), pixels.size());
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            const uint8_t* p = &pixels[(y * size + x) * 4];
            if (p[0] != x || p[1] != y || p[2] != 0 || p[3] != 255) {
                if (failures < 10) {
                    std::printf("pixel (%u, %u) = (%d, %d, %d, %d)\n", x, y, p[0], p[1], p[2], p[3]);
                }
                failures++;
            }
        }
    }

    kiln.destroyImage(image);
    kiln.destroyBuffer(outputBuffer);
    kiln.destroyBuffer(inputBuffer);
    kiln.unloadProgram(gradient);
    kiln.unloadProgram(scale);

    if (failures > 0) {
        std::printf("ComputeBasic: %d checks failed\n", failures);
        return 1;
    }
    std::printf("ComputeBasic: ok\n");
    return 0;
}
