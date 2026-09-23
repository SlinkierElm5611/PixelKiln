//
// Created by Stefan Balta on 2026-09-21.
//

// Presentation tests. They open real windows, so they need a display and are only registered with
// -DPIXELKILN_TEST_WINDOW=ON (see tests/CMakeLists.txt). Tests run on the main thread, as AppKit requires.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t backgroundVertSpirv[] =
#include "background.vert.h"
;
static const uint32_t solidFragSpirv[] =
#include "solid.frag.h"
;

// A visible GLFW window for the duration of a test.
struct TestWindow {
    GLFWwindow* window = nullptr;

    TestWindow(int width, int height)
    {
        REQUIRE(glfwInit());
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(width, height, "PixelKiln test", nullptr, nullptr);
        REQUIRE(window);
        pumpEvents();
    }
    ~TestWindow()
    {
        glfwDestroyWindow(window);
    }

    NativeWindow native() const
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

    void framebufferSize(uint32_t &width, uint32_t &height) const
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        width = uint32_t(w);
        height = uint32_t(h);
    }

    static void pumpEvents()
    {
        for (int i = 0; i < 10; i++) {
            glfwWaitEventsTimeout(0.01);
        }
    }
};

static uint64_t createSwapchain(PixelKiln &kiln, const TestWindow &window)
{
    uint32_t width, height;
    window.framebufferSize(width, height);
    SwapchainDesc desc{};
    desc.width = width;
    desc.height = height;
    return kiln.createSwapchain(window.native(), desc);
}

// Full screen solid color in the swapchain's format.
static uint64_t loadSolid(PixelKiln &kiln, uint64_t swapchain)
{
    RasterDrawProgram program{};
    program.vertexShader = shaderFrom(backgroundVertSpirv);
    program.fragmentShader = shaderFrom(solidFragSpirv);
    program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    program.colorFormats = {kiln.getSwapchainInfo(swapchain).format};
    return kiln.loadRasterDrawProgram(program);
}

static void drawSolid(PixelKiln &kiln, uint64_t program, uint64_t image, const float (&color)[4])
{
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = program;
    call.bindings = {{.data = color, .size = sizeof(color)}};
    call.colorTargets = {{image, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
    call.vertexCount = 3;
    kiln.call(call);
}

// Pixel (x, y) of a downloaded swapchain image as packRgba, whatever the channel order of the format.
static uint32_t swapchainPixel(const std::vector<uint8_t> &pixels, ImageFormat format, uint32_t width, uint32_t x,
                               uint32_t y)
{
    const uint8_t* p = &pixels[(size_t(y) * width + x) * 4];
    bool bgra = format == IMAGE_FORMAT_BGRA8_UNORM || format == IMAGE_FORMAT_BGRA8_SRGB;
    return bgra ? packRgba(p[2], p[1], p[0], p[3]) : packRgba(p[0], p[1], p[2], p[3]);
}

TEST(window_render_and_screenshot)
{
    TestWindow window(256, 256);
    PixelKiln kiln;
    uint64_t swapchain = createSwapchain(kiln, window);
    SwapchainInfo info = kiln.getSwapchainInfo(swapchain);
    uint32_t width, height;
    window.framebufferSize(width, height);
    CHECK_EQ(info.width, width);
    CHECK_EQ(info.height, height);
    CHECK(info.imageCount >= 2);
    CHECK_EQ(info.format, IMAGE_FORMAT_BGRA8_UNORM); // requested, and universally supported on desktop
    uint64_t solid = loadSolid(kiln, swapchain);

    const float colors[2][4] = {{1.0f, 0.5f, 0.25f, 1.0f}, {0.0f, 0.25f, 1.0f, 1.0f}};
    const uint32_t expected[2] = {packRgba(255, 128, 64, 255), packRgba(0, 64, 255, 255)};
    for (int frame = 0; frame < 60; frame++) {
        TestWindow::pumpEvents();
        uint64_t image = kiln.acquireSwapchainImage(swapchain);
        REQUIRE(image != 0);
        drawSolid(kiln, solid, image, colors[frame % 2]);
        if (frame % 10 == 0) {
            info = kiln.getSwapchainInfo(swapchain);
            std::vector<uint8_t> pixels(size_t(info.width) * info.height * 4);
            kiln.downloadImage(image, pixels.data(), pixels.size());
            CHECK(nearRgba(swapchainPixel(pixels, info.format, info.width, info.width / 2, info.height / 2),
                           expected[frame % 2]));
            CHECK(nearRgba(swapchainPixel(pixels, info.format, info.width, 0, 0), expected[frame % 2]));
        }
        kiln.present(swapchain);
    }
    kiln.destroySwapchain(swapchain);
}

// Hundreds of frames with uniform uploads: every acquire / rendered semaphore gets reused many times.
TEST(window_many_frames)
{
    TestWindow window(128, 128);
    PixelKiln kiln;
    uint64_t swapchain = createSwapchain(kiln, window);
    uint64_t solid = loadSolid(kiln, swapchain);
    for (int frame = 0; frame < 300; frame++) {
        glfwPollEvents();
        uint64_t image = kiln.acquireSwapchainImage(swapchain);
        REQUIRE(image != 0);
        const float color[4] = {float(frame % 100) / 100.0f, 0.5f, 0.5f, 1.0f};
        drawSolid(kiln, solid, image, color);
        kiln.present(swapchain);
    }
    kiln.waitIdle();
}

// Acquire and present without any call in between: present consumes the acquire itself.
TEST(window_present_without_rendering)
{
    TestWindow window(128, 128);
    PixelKiln kiln;
    uint64_t swapchain = createSwapchain(kiln, window);
    for (int frame = 0; frame < 20; frame++) {
        glfwPollEvents();
        REQUIRE(kiln.acquireSwapchainImage(swapchain) != 0);
        kiln.present(swapchain);
    }
}

TEST(window_resize)
{
    TestWindow window(200, 150);
    PixelKiln kiln;
    uint64_t swapchain = createSwapchain(kiln, window);
    uint64_t solid = loadSolid(kiln, swapchain);
    const float color[4] = {0.0f, 1.0f, 0.0f, 1.0f};

    const int sizes[3][2] = {{320, 200}, {160, 240}, {200, 150}};
    for (const auto &size : sizes) {
        glfwSetWindowSize(window.window, size[0], size[1]);
        TestWindow::pumpEvents();
        uint32_t width, height;
        window.framebufferSize(width, height);
        kiln.resizeSwapchain(swapchain, width, height);
        for (int frame = 0; frame < 3; frame++) {
            uint64_t image = kiln.acquireSwapchainImage(swapchain);
            REQUIRE(image != 0);
            SwapchainInfo info = kiln.getSwapchainInfo(swapchain);
            CHECK_EQ(info.width, width);
            CHECK_EQ(info.height, height);
            drawSolid(kiln, solid, image, color);
            std::vector<uint8_t> pixels(size_t(info.width) * info.height * 4);
            kiln.downloadImage(image, pixels.data(), pixels.size());
            CHECK(nearRgba(swapchainPixel(pixels, info.format, info.width, info.width - 1, info.height - 1),
                           packRgba(0, 255, 0, 255)));
            kiln.present(swapchain);
        }
    }
}

TEST(window_two_windows)
{
    TestWindow first(160, 120);
    TestWindow second(120, 160);
    PixelKiln kiln;
    uint64_t firstSwapchain = createSwapchain(kiln, first);
    uint64_t secondSwapchain = createSwapchain(kiln, second);
    uint64_t solid = loadSolid(kiln, firstSwapchain);
    CHECK_EQ(kiln.getSwapchainInfo(secondSwapchain).format, kiln.getSwapchainInfo(firstSwapchain).format);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int frame = 0; frame < 30; frame++) {
        glfwPollEvents();
        uint64_t firstImage = kiln.acquireSwapchainImage(firstSwapchain);
        uint64_t secondImage = kiln.acquireSwapchainImage(secondSwapchain);
        REQUIRE(firstImage != 0 && secondImage != 0);
        drawSolid(kiln, solid, firstImage, red);
        drawSolid(kiln, solid, secondImage, blue);
        kiln.present(secondSwapchain);
        kiln.present(firstSwapchain);
    }
    kiln.destroySwapchain(firstSwapchain);
    kiln.destroySwapchain(secondSwapchain);
}

// Destroying a swapchain (or the whole kiln) with an image acquired but never presented.
TEST(window_destroy_with_acquired_image)
{
    TestWindow window(128, 128);
    {
        PixelKiln kiln;
        uint64_t swapchain = createSwapchain(kiln, window);
        REQUIRE(kiln.acquireSwapchainImage(swapchain) != 0);
        kiln.destroySwapchain(swapchain);

        swapchain = createSwapchain(kiln, window);
        uint64_t solid = loadSolid(kiln, swapchain);
        const float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        uint64_t image = kiln.acquireSwapchainImage(swapchain);
        REQUIRE(image != 0);
        drawSolid(kiln, solid, image, color);
        // The kiln goes away without destroySwapchain or present.
    }
}

TEST(window_misuse)
{
    TestWindow window(128, 128);
    PixelKiln kiln;
    uint64_t swapchain = createSwapchain(kiln, window);
    uint64_t solid = loadSolid(kiln, swapchain);
    const float color[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    SwapchainInfo info = kiln.getSwapchainInfo(swapchain);
    std::vector<uint8_t> pixels(size_t(info.width) * info.height * 4);

    CHECK_THROWS_INVALID(kiln.present(swapchain)); // nothing acquired
    uint64_t image = kiln.acquireSwapchainImage(swapchain);
    REQUIRE(image != 0);
    CHECK_THROWS_INVALID(kiln.acquireSwapchainImage(swapchain)); // one at a time
    CHECK_THROWS_INVALID(kiln.destroyImage(image));
    CHECK_THROWS_INVALID(kiln.uploadImage(image, pixels.data(), pixels.size()));
    CHECK_THROWS_INVALID(kiln.downloadImage(image, pixels.data(), pixels.size())); // nothing rendered yet
    drawSolid(kiln, solid, image, color);
    kiln.downloadImage(image, pixels.data(), pixels.size());
    kiln.present(swapchain);

    // After present the handle is stale until acquired again.
    CHECK_THROWS_INVALID(drawSolid(kiln, solid, image, color));
    CHECK_THROWS_INVALID(kiln.downloadImage(image, pixels.data(), pixels.size()));

    // A program for another format can't draw into the window.
    RasterDrawProgram other{};
    other.vertexShader = shaderFrom(backgroundVertSpirv);
    other.fragmentShader = shaderFrom(solidFragSpirv);
    other.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    other.colorFormats = {IMAGE_FORMAT_RGBA16_FLOAT};
    uint64_t wrongFormat = kiln.loadRasterDrawProgram(other);
    image = kiln.acquireSwapchainImage(swapchain);
    REQUIRE(image != 0);
    CHECK_THROWS_INVALID(drawSolid(kiln, wrongFormat, image, color));
    drawSolid(kiln, solid, image, color);
    kiln.present(swapchain);
}

// A multisampled target resolved straight into the window's image, which isn't otherwise rendered to.
TEST(window_msaa_resolve)
{
    TestWindow window(160, 120);
    PixelKiln kiln;
    uint64_t swapchain = createSwapchain(kiln, window);
    SwapchainInfo info = kiln.getSwapchainInfo(swapchain);
    uint32_t samples = 0;
    for (uint32_t count : {4u, 8u, 2u}) {
        if (kiln.getSupportedSampleCounts(info.format) & count) {
            samples = count;
            break;
        }
    }
    if (!samples) {
        std::printf("  skipped: the window's format can't be multisampled\n");
        return;
    }
    RasterDrawProgram program{};
    program.vertexShader = shaderFrom(backgroundVertSpirv);
    program.fragmentShader = shaderFrom(solidFragSpirv);
    program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    program.colorFormats = {info.format};
    program.samples = samples;
    uint64_t draw = kiln.loadRasterDrawProgram(program);
    uint64_t target = kiln.createImage({info.width, info.height, info.format, IMAGE_USAGE_COLOR_TARGET, samples});

    const float color[4] = {0.25f, 0.5f, 1.0f, 1.0f};
    for (int frame = 0; frame < 12; frame++) {
        TestWindow::pumpEvents();
        uint64_t image = kiln.acquireSwapchainImage(swapchain);
        REQUIRE(image != 0);
        ProgramCall call{};
        call.type = PROGRAM_TYPE_RASTER_DRAW;
        call.program = draw;
        call.bindings = {{.data = color, .size = sizeof(color)}};
        call.colorTargets = {{.image = target, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .resolveImage = image,
                              .store = false}};
        call.vertexCount = 3;
        kiln.call(call);
        if (frame % 4 == 0) {
            std::vector<uint8_t> pixels(size_t(info.width) * info.height * 4);
            kiln.downloadImage(image, pixels.data(), pixels.size());
            CHECK(nearRgba(swapchainPixel(pixels, info.format, info.width, info.width / 2, info.height / 2),
                           packRgba(64, 128, 255, 255)));
            CHECK(nearRgba(swapchainPixel(pixels, info.format, info.width, 0, info.height - 1),
                           packRgba(64, 128, 255, 255)));
        }
        kiln.present(swapchain);
    }
    kiln.destroySwapchain(swapchain);
}
