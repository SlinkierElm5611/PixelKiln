//
// Created by Stefan Balta on 2026-09-21.
//

// Streams batches through a compute program: while call i runs on the all queue, batch i + 1 is uploaded on the
// transfer queue into the other of two ping-pong buffers. Uploading into a buffer that an earlier call may still be
// reading makes that upload wait for the call, which PixelKiln handles automatically.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "pixelKiln.h"

static const uint32_t accumulateSpirv[] =
#include "accumulate.comp.h"
;

struct Params {
    uint32_t iteration;
    uint32_t count;
};

int main()
{
    PixelKiln kiln;

    uint64_t accumulate = kiln.loadComputeProgram({
        {accumulateSpirv, sizeof(accumulateSpirv)},
        {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER},
    });

    const uint32_t iterations = 64;
    const uint32_t count = 4096;
    std::vector<std::vector<uint32_t>> batches(iterations, std::vector<uint32_t>(count));
    for (uint32_t i = 0; i < iterations; i++) {
        for (uint32_t j = 0; j < count; j++) {
            batches[i][j] = i * 7919u + j;
        }
    }

    uint64_t inputs[2] = {kiln.createBuffer(count * sizeof(uint32_t)), kiln.createBuffer(count * sizeof(uint32_t))};
    uint64_t results = kiln.createBuffer(uint64_t(iterations) * count * sizeof(uint32_t));

    kiln.uploadBuffer(inputs[0], batches[0].data(), count * sizeof(uint32_t));
    uint64_t lastTicket = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        Params params{i, count};
        ProgramCall call{};
        call.type = PROGRAM_TYPE_COMPUTE;
        call.program = accumulate;
        call.bindings = {
            {.data = &params, .size = sizeof(params)},
            {.resource = inputs[i % 2]},
            {.resource = results},
        };
        call.groupCountX = count / 64;
        uint64_t ticket = kiln.call(call);
        if (ticket <= lastTicket) {
            std::printf("tickets must increase\n");
            return 1;
        }
        lastTicket = ticket;
        // Runs on the transfer queue while the call above executes.
        if (i + 1 < iterations) {
            kiln.uploadBuffer(inputs[(i + 1) % 2], batches[i + 1].data(), count * sizeof(uint32_t));
        }
    }

    std::vector<uint32_t> output(uint64_t(iterations) * count);
    kiln.downloadBuffer(results, output.data(), output.size() * sizeof(uint32_t));
    if (!kiln.isComplete(lastTicket)) {
        std::printf("downloadBuffer returned before the last call completed\n");
        return 1;
    }

    int failures = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        for (uint32_t j = 0; j < count; j++) {
            uint32_t expected = batches[i][j] * 3u + i;
            uint32_t actual = output[uint64_t(i) * count + j];
            if (actual != expected) {
                if (failures < 10) {
                    std::printf("iteration %u element %u = %u, expected %u\n", i, j, actual, expected);
                }
                failures++;
            }
        }
    }

    kiln.destroyBuffer(results);
    kiln.destroyBuffer(inputs[1]);
    kiln.destroyBuffer(inputs[0]);
    kiln.unloadProgram(accumulate);

    if (failures > 0) {
        std::printf("PipelinedCompute: %d checks failed\n", failures);
        return 1;
    }
    std::printf("PipelinedCompute: ok (%u calls)\n", iterations);
    return 0;
}
