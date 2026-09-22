//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_EXAMPLEWINDOW_H
#define PIXELKILN_EXAMPLEWINDOW_H

// Window plumbing shared by the interactive examples: a GLFW window presenting through a PixelKiln swapchain, input
// state and frame timing. For automated runs:
//   --frames N             exit after N frames. The run is deterministic: time advances 1/60 s per frame and mouse /
//                          keyboard input is ignored, so stray input can't change the result
//   --screenshot file.bmp  save the last frame (needs --frames)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
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

#include "pixelKiln.h"

class ExampleWindow
{
public:
    // The kiln must outlive the window (declare it first).
    ExampleWindow(PixelKiln &kiln, const char* title, int width, int height, int argc, char** argv) : m_kiln(kiln)
    {
        for (int i = 1; i + 1 < argc; i++) {
            if (std::strcmp(argv[i], "--frames") == 0) {
                m_maxFrames = std::atol(argv[++i]);
            } else if (std::strcmp(argv[i], "--screenshot") == 0) {
                m_screenshotPath = argv[++i];
            }
        }
        if (!glfwInit()) {
            throw std::runtime_error("glfwInit failed");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!m_window) {
            glfwTerminate();
            throw std::runtime_error("glfwCreateWindow failed");
        }
        glfwSetWindowUserPointer(m_window, this);
        glfwSetScrollCallback(m_window, [](GLFWwindow* window, double, double y) {
            static_cast<ExampleWindow*>(glfwGetWindowUserPointer(window))->m_scroll += float(y);
        });
        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int, int action, int) {
            if (action == GLFW_PRESS) {
                static_cast<ExampleWindow*>(glfwGetWindowUserPointer(window))->m_pressedKeys.insert(key);
            }
        });

        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
        m_swapchain = m_kiln.createSwapchain(nativeWindow(),
                                             {uint32_t(framebufferWidth), uint32_t(framebufferHeight)});
        m_info = m_kiln.getSwapchainInfo(m_swapchain);
        m_lastTime = automated() ? 0.0 : glfwGetTime();
    }

    ~ExampleWindow()
    {
        m_kiln.destroySwapchain(m_swapchain);
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    // Polls events and advances the clock. False once the window is closed, Escape is pressed or --frames is reached.
    bool nextFrame()
    {
        m_pressedKeys.clear();
        glfwPollEvents();
        if (glfwWindowShouldClose(m_window) || keyPressed(GLFW_KEY_ESCAPE) || m_frame == m_maxFrames) {
            return false;
        }
        if (automated()) {
            m_deltaTime = 1.0f / 60.0f;
            m_lastTime = double(m_frame) / 60.0;
            m_pressedKeys.clear();
            m_scroll = 0.0f;
        } else {
            double now = glfwGetTime();
            m_deltaTime = float(std::min(now - m_lastTime, 0.1));
            m_lastTime = now;
        }
        return true;
    }

    // The swapchain image to render this frame into, 0 while the window is minimized (skip the frame).
    uint64_t acquire()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (uint32_t(width) != m_info.width || uint32_t(height) != m_info.height) {
            m_kiln.resizeSwapchain(m_swapchain, uint32_t(width), uint32_t(height));
        }
        m_image = m_kiln.acquireSwapchainImage(m_swapchain);
        if (m_image == 0) {
            glfwWaitEventsTimeout(0.1);
            return 0;
        }
        m_info = m_kiln.getSwapchainInfo(m_swapchain);
        return m_image;
    }

    void present()
    {
        if (!m_screenshotPath.empty() && m_frame + 1 == m_maxFrames) {
            saveScreenshot();
        }
        m_kiln.present(m_swapchain);
        m_frame++;
    }

    uint32_t width() const { return m_info.width; }
    uint32_t height() const { return m_info.height; }
    float aspect() const { return float(m_info.width) / float(m_info.height); }
    ImageFormat format() const { return m_info.format; }
    float time() const { return float(m_lastTime); }
    float deltaTime() const { return m_deltaTime; }
    long frame() const { return m_frame; }

    // Cursor position in framebuffer pixels.
    void mouse(float &x, float &y) const
    {
        double cursorX = 0.0, cursorY = 0.0;
        int windowWidth = 1, windowHeight = 1;
        glfwGetCursorPos(m_window, &cursorX, &cursorY);
        glfwGetWindowSize(m_window, &windowWidth, &windowHeight);
        x = float(cursorX) * float(m_info.width) / float(std::max(windowWidth, 1));
        y = float(cursorY) * float(m_info.height) / float(std::max(windowHeight, 1));
    }
    bool mouseDown(int button = GLFW_MOUSE_BUTTON_LEFT) const
    {
        return !automated() && glfwGetMouseButton(m_window, button) == GLFW_PRESS;
    }
    // Scroll since the last call.
    float takeScroll()
    {
        float scroll = m_scroll;
        m_scroll = 0.0f;
        return scroll;
    }
    // Pressed during this frame's event polling.
    bool keyPressed(int key) const { return m_pressedKeys.count(key) > 0; }

private:
    bool automated() const { return m_maxFrames >= 0; }

    NativeWindow nativeWindow() const
    {
#if defined(_WIN32)
        return {NATIVE_WINDOW_WIN32, GetModuleHandleW(nullptr), glfwGetWin32Window(m_window)};
#elif defined(__APPLE__)
        return {NATIVE_WINDOW_COCOA_VIEW, nullptr, glfwGetCocoaView(m_window)};
#else
        return {NATIVE_WINDOW_XLIB, glfwGetX11Display(),
                reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(m_window)))};
#endif
    }

    // Writes the current swapchain image as a 24 bit BMP.
    void saveScreenshot()
    {
        const uint32_t width = m_info.width, height = m_info.height;
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        m_kiln.downloadImage(m_image, pixels.data(), pixels.size());
        const bool bgra = m_info.format == IMAGE_FORMAT_BGRA8_UNORM || m_info.format == IMAGE_FORMAT_BGRA8_SRGB;
        const uint32_t rowSize = (width * 3 + 3) & ~3u;
        const uint32_t dataSize = rowSize * height;
        uint8_t header[54] = {'B', 'M'};
        auto put32 = [&](int offset, uint32_t value) {
            for (int i = 0; i < 4; i++) {
                header[offset + i] = uint8_t(value >> (8 * i));
            }
        };
        put32(2, 54 + dataSize);
        put32(10, 54);
        put32(14, 40);
        put32(18, width);
        put32(22, height);
        header[26] = 1;
        header[28] = 24;
        put32(34, dataSize);
        FILE* file = std::fopen(m_screenshotPath.c_str(), "wb");
        if (!file) {
            std::printf("could not write %s\n", m_screenshotPath.c_str());
            return;
        }
        std::fwrite(header, 1, sizeof(header), file);
        std::vector<uint8_t> row(rowSize, 0);
        for (uint32_t y = 0; y < height; y++) {
            const uint8_t* source = &pixels[size_t(height - 1 - y) * width * 4]; // BMP rows go bottom up
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t* p = source + x * 4;
                row[x * 3 + 0] = bgra ? p[0] : p[2];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = bgra ? p[2] : p[0];
            }
            std::fwrite(row.data(), 1, rowSize, file);
        }
        std::fclose(file);
        std::printf("wrote %s\n", m_screenshotPath.c_str());
    }

    PixelKiln &m_kiln;
    GLFWwindow* m_window = nullptr;
    uint64_t m_swapchain = 0;
    uint64_t m_image = 0;
    SwapchainInfo m_info{};
    long m_frame = 0;
    long m_maxFrames = -1;
    std::string m_screenshotPath;
    double m_lastTime = 0.0;
    float m_deltaTime = 0.0f;
    float m_scroll = 0.0f;
    std::set<int> m_pressedKeys;
};

// Scale for blit.frag that fits an image of the given aspect ratio inside the window without stretching.
inline void letterbox(float imageAspect, float windowAspect, float scale[2])
{
    scale[0] = imageAspect < windowAspect ? imageAspect / windowAspect : 1.0f;
    scale[1] = imageAspect < windowAspect ? 1.0f : windowAspect / imageAspect;
}

#endif //PIXELKILN_EXAMPLEWINDOW_H
