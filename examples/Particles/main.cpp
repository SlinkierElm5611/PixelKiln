//
// Created by Stefan Balta on 2026-09-21.
//

// 262144 particles simulated by a compute program every frame. The draw that follows reads the same buffer as its
// vertex buffer, and renders points into an offscreen image that keeps fading trails. Hold the left mouse button to
// pull the swarm to the cursor; otherwise it chases a wandering attractor.

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "exampleWindow.h"

static const uint32_t simulateSpirv[] =
#include "simulate.comp.h"
;
static const uint32_t pointsVertSpirv[] =
#include "points.vert.h"
;
static const uint32_t pointsFragSpirv[] =
#include "points.frag.h"
;
static const uint32_t fadeFragSpirv[] =
#include "fade.frag.h"
;
static const uint32_t fullscreenVertSpirv[] =
#include "fullscreen.vert.h"
;
static const uint32_t blitFragSpirv[] =
#include "blit.frag.h"
;

struct Particle {
    float x, y;
    float velocityX, velocityY;
};

struct SimulateParams {
    float attractorX, attractorY;
    float deltaTime;
    float time;
    float strength;
    float aspect;
    uint32_t count;
};

int main(int argc, char** argv)
{
    PixelKiln kiln;
    ExampleWindow window(kiln, "PixelKiln - Particles", 1280, 720, argc, argv);

    // A disc of particles already orbiting the center.
    const uint32_t count = 1 << 18;
    std::vector<Particle> particles(count);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (Particle &p : particles) {
        float radius = 0.1f + 0.8f * std::sqrt(unit(random));
        float angle = unit(random) * 6.2831853f;
        float speed = 0.3f / std::sqrt(radius);
        p = {radius * std::cos(angle), radius * std::sin(angle), -std::sin(angle) * speed, std::cos(angle) * speed};
    }
    uint64_t particleBuffer = kiln.createBuffer(count * sizeof(Particle));
    kiln.uploadBuffer(particleBuffer, particles.data(), count * sizeof(Particle));

    uint64_t simulate = kiln.loadComputeProgram({{simulateSpirv, sizeof(simulateSpirv)},
                                                 {UNIFORM_BINDING_TYPE_STORAGE_BUFFER, UNIFORM_BINDING_TYPE_BUFFER}});

    const ImageFormat trailFormat = IMAGE_FORMAT_RGBA16_FLOAT;
    RasterDrawProgram pointsProgram{};
    pointsProgram.vertexShader = {pointsVertSpirv, sizeof(pointsVertSpirv)};
    pointsProgram.fragmentShader = {pointsFragSpirv, sizeof(pointsFragSpirv)};
    pointsProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    pointsProgram.vertexLayout.buffers = {{sizeof(Particle)}};
    pointsProgram.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT2, 0},
                                             {1, 0, VERTEX_FORMAT_FLOAT2, 2 * sizeof(float)}};
    pointsProgram.topology = PRIMITIVE_TOPOLOGY_POINT_LIST;
    pointsProgram.blendEnable = true;
    pointsProgram.colorFormats = {trailFormat};
    uint64_t points = kiln.loadRasterDrawProgram(pointsProgram);

    RasterDrawProgram fadeProgram{};
    fadeProgram.vertexShader = {fullscreenVertSpirv, sizeof(fullscreenVertSpirv)};
    fadeProgram.fragmentShader = {fadeFragSpirv, sizeof(fadeFragSpirv)};
    fadeProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    fadeProgram.blendEnable = true;
    fadeProgram.colorFormats = {trailFormat};
    uint64_t fade = kiln.loadRasterDrawProgram(fadeProgram);

    RasterDrawProgram blitProgram{};
    blitProgram.vertexShader = {fullscreenVertSpirv, sizeof(fullscreenVertSpirv)};
    blitProgram.fragmentShader = {blitFragSpirv, sizeof(blitFragSpirv)};
    blitProgram.uniformBindings = {UNIFORM_BINDING_TYPE_SAMPLER, UNIFORM_BINDING_TYPE_BUFFER};
    blitProgram.colorFormats = {window.format()};
    uint64_t blit = kiln.loadRasterDrawProgram(blitProgram);

    uint64_t trail = 0;
    uint32_t trailWidth = 0, trailHeight = 0;
    while (window.nextFrame()) {
        uint64_t image = window.acquire();
        if (image == 0) {
            continue;
        }
        // The trail image follows the window size.
        bool clearTrail = false;
        if (window.width() != trailWidth || window.height() != trailHeight) {
            if (trail) {
                kiln.destroyImage(trail); // deferred until the GPU is done with it
            }
            trailWidth = window.width();
            trailHeight = window.height();
            trail = kiln.createImage({trailWidth, trailHeight, trailFormat,
                                      IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_SAMPLED});
            clearTrail = true;
        }
        const float aspect = window.aspect();

        // 1. Simulate.
        SimulateParams simulateParams{};
        if (window.mouseDown()) {
            float mouseX, mouseY;
            window.mouse(mouseX, mouseY);
            simulateParams.attractorX = (mouseX / float(trailWidth) * 2.0f - 1.0f) * aspect;
            simulateParams.attractorY = mouseY / float(trailHeight) * 2.0f - 1.0f;
            simulateParams.strength = 2.5f;
        } else {
            float t = window.time();
            simulateParams.attractorX = 0.55f * aspect * std::sin(t * 0.37f);
            simulateParams.attractorY = 0.5f * std::sin(t * 0.61f + 1.0f);
            simulateParams.strength = 1.0f;
        }
        simulateParams.deltaTime = std::min(window.deltaTime(), 1.0f / 30.0f);
        simulateParams.time = window.time();
        simulateParams.aspect = aspect;
        simulateParams.count = count;
        ProgramCall simulateCall{};
        simulateCall.type = PROGRAM_TYPE_COMPUTE;
        simulateCall.program = simulate;
        simulateCall.bindings = {{.resource = particleBuffer}, {.data = &simulateParams, .size = sizeof(simulateParams)}};
        simulateCall.groupCountX = count / 256;
        kiln.call(simulateCall);

        // 2. Fade the previous trails.
        const float fadeColor[4] = {0.0f, 0.0f, 0.0f, 0.12f};
        ProgramCall fadeCall{};
        fadeCall.type = PROGRAM_TYPE_RASTER_DRAW;
        fadeCall.program = fade;
        fadeCall.bindings = {{.data = fadeColor, .size = sizeof(fadeColor)}};
        fadeCall.colorTargets = {{trail, clearTrail, {0.0f, 0.0f, 0.0f, 1.0f}}};
        fadeCall.vertexCount = 3;
        kiln.call(fadeCall);

        // 3. Draw the particles on top, straight from the buffer the simulation wrote.
        const float pointParams[2] = {aspect, 0.35f};
        ProgramCall pointsCall{};
        pointsCall.type = PROGRAM_TYPE_RASTER_DRAW;
        pointsCall.program = points;
        pointsCall.bindings = {{.data = pointParams, .size = sizeof(pointParams)}};
        pointsCall.colorTargets = {{trail, false}};
        pointsCall.vertexBuffers = {particleBuffer};
        pointsCall.vertexCount = count;
        kiln.call(pointsCall);

        // 4. Show the trail image in the window.
        const float scale[2] = {1.0f, 1.0f};
        ProgramCall blitCall{};
        blitCall.type = PROGRAM_TYPE_RASTER_DRAW;
        blitCall.program = blit;
        blitCall.bindings = {{.resource = trail, .sampler = {SAMPLER_FILTER_NEAREST}},
                             {.data = scale, .size = sizeof(scale)}};
        blitCall.colorTargets = {{image, true}};
        blitCall.vertexCount = 3;
        kiln.call(blitCall);

        window.present();
    }
    return 0;
}
