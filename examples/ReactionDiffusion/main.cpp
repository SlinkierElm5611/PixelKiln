//
// Created by Stefan Balta on 2026-09-21.
//

// Gray-Scott reaction-diffusion. Every frame runs 16 simulation steps, ping-ponging between two storage buffers, then
// a compute program colors the result into a storage image that is drawn to the window.
//   left mouse  paint
//   1 - 4       coral, mitosis, maze, holes
//   R           reset
//   Space       pause

#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>

#include "exampleWindow.h"

static const uint32_t stepSpirv[] =
#include "step.comp.h"
;
static const uint32_t colorizeSpirv[] =
#include "colorize.comp.h"
;
static const uint32_t fullscreenVertSpirv[] =
#include "fullscreen.vert.h"
;
static const uint32_t blitFragSpirv[] =
#include "blit.frag.h"
;

static const uint32_t SIZE = 512;
static const int STEPS_PER_FRAME = 16;

struct Preset {
    const char* name;
    float feed;
    float kill;
};
static const Preset PRESETS[] = {
    {"coral", 0.0545f, 0.062f},
    {"mitosis", 0.0367f, 0.0649f},
    {"maze", 0.029f, 0.057f},
    {"holes", 0.039f, 0.058f},
};

struct StepParams {
    uint32_t width, height;
    float feed, kill;
    float brushX, brushY;
    float brushRadius;
    uint32_t brushActive;
};

// All feed chemical, with a few random square seeds of the second one.
static std::vector<float> seedCells()
{
    std::vector<float> cells(size_t(SIZE) * SIZE * 2);
    for (size_t i = 0; i < cells.size(); i += 2) {
        cells[i] = 1.0f;
        cells[i + 1] = 0.0f;
    }
    std::mt19937 random(42);
    std::uniform_int_distribution<uint32_t> position(0, SIZE - 12);
    for (int seed = 0; seed < 40; seed++) {
        uint32_t seedX = position(random), seedY = position(random);
        for (uint32_t y = seedY; y < seedY + 10; y++) {
            for (uint32_t x = seedX; x < seedX + 10; x++) {
                cells[(size_t(y) * SIZE + x) * 2 + 0] = 0.5f;
                cells[(size_t(y) * SIZE + x) * 2 + 1] = 0.5f;
            }
        }
    }
    return cells;
}

int main(int argc, char** argv)
{
    PixelKiln kiln;
    ExampleWindow window(kiln, "PixelKiln - Reaction Diffusion", 900, 900, argc, argv);

    const uint64_t cellBytes = uint64_t(SIZE) * SIZE * 2 * sizeof(float);
    uint64_t cells[2] = {kiln.createBuffer(cellBytes), kiln.createBuffer(cellBytes)};
    std::vector<float> seed = seedCells();
    kiln.uploadBuffer(cells[0], seed.data(), cellBytes);
    uint64_t colors = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_STORAGE | IMAGE_USAGE_SAMPLED});

    uint64_t step = kiln.loadComputeProgram({{stepSpirv, sizeof(stepSpirv)},
                                             {UNIFORM_BINDING_TYPE_STORAGE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                              UNIFORM_BINDING_TYPE_BUFFER}});
    uint64_t colorize = kiln.loadComputeProgram({{colorizeSpirv, sizeof(colorizeSpirv)},
                                                 {UNIFORM_BINDING_TYPE_STORAGE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_IMAGE,
                                                  UNIFORM_BINDING_TYPE_BUFFER}});
    RasterDrawProgram blitProgram{};
    blitProgram.vertexShader = {fullscreenVertSpirv, sizeof(fullscreenVertSpirv)};
    blitProgram.fragmentShader = {blitFragSpirv, sizeof(blitFragSpirv)};
    blitProgram.uniformBindings = {UNIFORM_BINDING_TYPE_SAMPLER, UNIFORM_BINDING_TYPE_BUFFER};
    blitProgram.colorFormats = {window.format()};
    uint64_t blit = kiln.loadRasterDrawProgram(blitProgram);

    const Preset* preset = &PRESETS[0];
    bool paused = false;
    int current = 0; // which of the two cell buffers holds the latest state
    std::printf("preset: %s\n", preset->name);
    while (window.nextFrame()) {
        for (int key = GLFW_KEY_1; key <= GLFW_KEY_4; key++) {
            if (window.keyPressed(key)) {
                preset = &PRESETS[key - GLFW_KEY_1];
                std::printf("preset: %s\n", preset->name);
            }
        }
        if (window.keyPressed(GLFW_KEY_R)) {
            kiln.uploadBuffer(cells[current], seed.data(), cellBytes); // waits for the steps reading it on the GPU
        }
        if (window.keyPressed(GLFW_KEY_SPACE)) {
            paused = !paused;
        }
        uint64_t image = window.acquire();
        if (image == 0) {
            continue;
        }
        float scale[2];
        letterbox(1.0f, window.aspect(), scale);

        // The brush in cell coordinates, through the letterboxing.
        StepParams params{SIZE, SIZE, preset->feed, preset->kill, 0.0f, 0.0f, 6.0f, 0};
        if (window.mouseDown()) {
            float mouseX, mouseY;
            window.mouse(mouseX, mouseY);
            float u = (mouseX / float(window.width()) - 0.5f) / scale[0] + 0.5f;
            float v = (mouseY / float(window.height()) - 0.5f) / scale[1] + 0.5f;
            params.brushX = u * float(SIZE);
            params.brushY = v * float(SIZE);
            params.brushActive = 1;
        }

        for (int i = 0; i < (paused ? 0 : STEPS_PER_FRAME); i++) {
            ProgramCall stepCall{};
            stepCall.type = PROGRAM_TYPE_COMPUTE;
            stepCall.program = step;
            stepCall.bindings = {{.resource = cells[current]}, {.resource = cells[1 - current]},
                                 {.data = &params, .size = sizeof(params)}};
            stepCall.groupCountX = SIZE / 16;
            stepCall.groupCountY = SIZE / 16;
            kiln.call(stepCall);
            current = 1 - current;
        }

        const uint32_t size[2] = {SIZE, SIZE};
        ProgramCall colorizeCall{};
        colorizeCall.type = PROGRAM_TYPE_COMPUTE;
        colorizeCall.program = colorize;
        colorizeCall.bindings = {{.resource = cells[current]}, {.resource = colors}, {.data = size, .size = sizeof(size)}};
        colorizeCall.groupCountX = SIZE / 16;
        colorizeCall.groupCountY = SIZE / 16;
        kiln.call(colorizeCall);

        ProgramCall blitCall{};
        blitCall.type = PROGRAM_TYPE_RASTER_DRAW;
        blitCall.program = blit;
        blitCall.bindings = {{.resource = colors, .sampler = {SAMPLER_FILTER_LINEAR}},
                             {.data = scale, .size = sizeof(scale)}};
        blitCall.colorTargets = {{image, true}};
        blitCall.vertexCount = 3;
        kiln.call(blitCall);

        window.present();
    }
    return 0;
}
