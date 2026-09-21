//
// Created by Stefan Balta on 2026-09-21.
//

// A spinning triangle in a GLFW window. The application owns the window and the event loop; PixelKiln only receives
// the native handles. Run with `--frames N` to exit after N frames.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

#include "pixelKiln.h"

static const uint32_t spinVertSpirv[] =
#include "spin.vert.h"
;
static const uint32_t spinFragSpirv[] =
#include "spin.frag.h"
;

// The GLFW window's native handles. Other windowing libraries (SDL, Qt, ...) expose the same handles.
static NativeWindow toNativeWindow(GLFWwindow* window)
{
#if defined(_WIN32)
    return {NATIVE_WINDOW_WIN32, GetModuleHandleW(nullptr), glfwGetWin32Window(window)};
#elif defined(__APPLE__)
    return {NATIVE_WINDOW_COCOA_VIEW, nullptr, glfwGetCocoaView(window)};
#else
    return {NATIVE_WINDOW_XLIB, glfwGetX11Display(),
            reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(window)))};
#endif
}

static bool resized = false;

int main(int argc, char** argv)
{
    long maxFrames = -1;
    if (argc == 3 && std::strcmp(argv[1], "--frames") == 0) {
        maxFrames = std::atol(argv[2]);
    }

    if (!glfwInit()) {
        std::printf("Window: glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // PixelKiln renders with Vulkan, GLFW must not create a context
    GLFWwindow* window = glfwCreateWindow(800, 600, "PixelKiln", nullptr, nullptr);
    if (!window) {
        std::printf("Window: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int, int) { resized = true; });

    {
        PixelKiln kiln;
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        uint64_t swapchain = kiln.createSwapchain(toNativeWindow(window), {uint32_t(width), uint32_t(height)});

        RasterDrawProgram program{};
        program.vertexShader = {spinVertSpirv, sizeof(spinVertSpirv)};
        program.fragmentShader = {spinFragSpirv, sizeof(spinFragSpirv)};
        program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
        program.colorFormats = {kiln.getSwapchainInfo(swapchain).format};
        uint64_t spin = kiln.loadRasterDrawProgram(program);

        long frames = 0;
        while (!glfwWindowShouldClose(window) && frames != maxFrames) {
            glfwPollEvents();
            if (resized) {
                resized = false;
                glfwGetFramebufferSize(window, &width, &height);
                kiln.resizeSwapchain(swapchain, uint32_t(width), uint32_t(height));
            }
            uint64_t image = kiln.acquireSwapchainImage(swapchain);
            if (image == 0) {
                glfwWaitEvents(); // minimized, nothing to draw until the window comes back
                continue;
            }
            SwapchainInfo info = kiln.getSwapchainInfo(swapchain);
            struct {
                float angle;
                float aspect;
            } params{float(glfwGetTime()), float(info.width) / float(info.height)};

            ProgramCall call{};
            call.type = PROGRAM_TYPE_RASTER_DRAW;
            call.program = spin;
            call.bindings = {{.data = &params, .size = sizeof(params)}};
            call.colorTargets = {{image, true, {0.05f, 0.05f, 0.08f, 1.0f}}};
            call.vertexCount = 3;
            kiln.call(call);
            kiln.present(swapchain);
            frames++;
        }

        kiln.unloadProgram(spin);
        kiln.destroySwapchain(swapchain); // before the window goes away
        std::printf("Window: ok (%ld frames)\n", frames);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
