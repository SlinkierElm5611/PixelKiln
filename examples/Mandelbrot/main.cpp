//
// Created by Stefan Balta on 2026-09-21.
//

// A Mandelbrot explorer computed at window resolution every frame.
//   left drag   pan
//   scroll      zoom at the cursor
//   R           reset
// Left alone for a few seconds it zooms into Seahorse Valley by itself (32 bit floats limit the depth, so it starts
// over when it gets there).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "exampleWindow.h"

static const uint32_t mandelbrotSpirv[] =
#include "mandelbrot.comp.h"
;
static const uint32_t fullscreenVertSpirv[] =
#include "fullscreen.vert.h"
;
static const uint32_t blitFragSpirv[] =
#include "blit.frag.h"
;

struct MandelbrotParams {
    float centerX, centerY;
    float scale;
    float time;
    uint32_t width, height;
    uint32_t maxIterations;
};

int main(int argc, char** argv)
{
    PixelKiln kiln;
    ExampleWindow window(kiln, "PixelKiln - Mandelbrot", 1280, 800, argc, argv);

    uint64_t mandelbrot = kiln.loadComputeProgram({{mandelbrotSpirv, sizeof(mandelbrotSpirv)},
                                                   {UNIFORM_BINDING_TYPE_STORAGE_IMAGE, UNIFORM_BINDING_TYPE_BUFFER}});
    RasterDrawProgram blitProgram{};
    blitProgram.vertexShader = {fullscreenVertSpirv, sizeof(fullscreenVertSpirv)};
    blitProgram.fragmentShader = {blitFragSpirv, sizeof(blitFragSpirv)};
    blitProgram.uniformBindings = {UNIFORM_BINDING_TYPE_SAMPLER, UNIFORM_BINDING_TYPE_BUFFER};
    blitProgram.colorFormats = {window.format()};
    uint64_t blit = kiln.loadRasterDrawProgram(blitProgram);

    const double homeX = -0.6, homeY = 0.0, homeSpan = 3.0; // span = plane height visible
    const double targetX = -0.743643887037151, targetY = 0.131825904205330;
    double centerX = homeX, centerY = homeY, span = homeSpan;
    double idleTime = 0.0;
    float lastMouseX = 0.0f, lastMouseY = 0.0f;
    bool dragging = false;

    uint64_t fractal = 0;
    uint32_t fractalWidth = 0, fractalHeight = 0;
    while (window.nextFrame()) {
        uint64_t image = window.acquire();
        if (image == 0) {
            continue;
        }
        if (window.width() != fractalWidth || window.height() != fractalHeight) {
            if (fractal) {
                kiln.destroyImage(fractal);
            }
            fractalWidth = window.width();
            fractalHeight = window.height();
            fractal = kiln.createImage({fractalWidth, fractalHeight, IMAGE_FORMAT_RGBA8_UNORM,
                                        IMAGE_USAGE_STORAGE | IMAGE_USAGE_SAMPLED});
        }

        // Input: drag to pan, scroll to zoom around the cursor.
        double scale = span / fractalHeight;
        float mouseX, mouseY;
        window.mouse(mouseX, mouseY);
        const double cursorX = mouseX - 0.5 * fractalWidth, cursorY = mouseY - 0.5 * fractalHeight;
        bool interacted = false;
        if (window.mouseDown()) {
            if (dragging) {
                centerX -= (mouseX - lastMouseX) * scale;
                centerY += (mouseY - lastMouseY) * scale;
            }
            dragging = true;
            interacted = true;
        } else {
            dragging = false;
        }
        lastMouseX = mouseX;
        lastMouseY = mouseY;
        float scroll = window.takeScroll();
        if (scroll != 0.0f) {
            double worldX = centerX + cursorX * scale, worldY = centerY - cursorY * scale;
            span = std::clamp(span * std::pow(0.85, double(scroll)), 1e-5, 6.0);
            scale = span / fractalHeight;
            centerX = worldX - cursorX * scale;
            centerY = worldY + cursorY * scale;
            interacted = true;
        }
        if (window.keyPressed(GLFW_KEY_R)) {
            centerX = homeX;
            centerY = homeY;
            span = homeSpan;
            interacted = true;
        }

        // Idle: glide into Seahorse Valley, start over at the float precision limit.
        idleTime = interacted ? 0.0 : idleTime + window.deltaTime();
        if (idleTime > 3.0) {
            double follow = 1.0 - std::exp(-1.5 * window.deltaTime());
            centerX += (targetX - centerX) * follow;
            centerY += (targetY - centerY) * follow;
            span *= std::exp(-0.35 * window.deltaTime());
            if (span < 2e-5) {
                centerX = homeX;
                centerY = homeY;
                span = homeSpan;
            }
        }
        scale = span / fractalHeight;

        MandelbrotParams params{};
        params.centerX = float(centerX);
        params.centerY = float(centerY);
        params.scale = float(scale);
        params.time = window.time();
        params.width = fractalWidth;
        params.height = fractalHeight;
        params.maxIterations = uint32_t(std::clamp(100.0 + 60.0 * std::log2(homeSpan / span), 100.0, 1500.0));
        ProgramCall compute{};
        compute.type = PROGRAM_TYPE_COMPUTE;
        compute.program = mandelbrot;
        compute.bindings = {{.resource = fractal}, {.data = &params, .size = sizeof(params)}};
        compute.groupCountX = (fractalWidth + 15) / 16;
        compute.groupCountY = (fractalHeight + 15) / 16;
        kiln.call(compute);

        const float blitScale[2] = {1.0f, 1.0f};
        ProgramCall blitCall{};
        blitCall.type = PROGRAM_TYPE_RASTER_DRAW;
        blitCall.program = blit;
        blitCall.bindings = {{.resource = fractal, .sampler = {SAMPLER_FILTER_NEAREST}},
                             {.data = blitScale, .size = sizeof(blitScale)}};
        blitCall.colorTargets = {{image, true}};
        blitCall.vertexCount = 3;
        kiln.call(blitCall);

        window.present();
    }
    return 0;
}
