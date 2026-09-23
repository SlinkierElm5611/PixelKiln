//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_SWAPCHAINDESC_H
#define PIXELKILN_SWAPCHAINDESC_H
#include <cstdint>

#include "imageDesc.h"
#include "imageFormat.h"

enum PresentMode {
    PRESENT_MODE_VSYNC,       // always available
    PRESENT_MODE_LOW_LATENCY, // falls back to VSYNC
    PRESENT_MODE_IMMEDIATE,   // may tear, falls back to LOW_LATENCY then VSYNC
    PRESENT_MODE_COUNT
};

struct SwapchainDesc {
    // Framebuffer size in pixels. Only used when the platform doesn't dictate the size itself (Wayland).
    uint32_t width = 0;
    uint32_t height = 0;
    ImageFormat format = IMAGE_FORMAT_BGRA8_UNORM; // preferred, getSwapchainInfo reports the one actually used
    PresentMode presentMode = PRESENT_MODE_VSYNC;
    ImageUsageFlags usage = IMAGE_USAGE_COLOR_TARGET; // IMAGE_USAGE_COLOR_TARGET and/or IMAGE_USAGE_STORAGE
    // How far the CPU may run ahead: acquireSwapchainImage waits until the frame maxFramesInFlight presents back is done
    // on the GPU. 1 gives the lowest latency, more lets the CPU record the next frame while the GPU renders this one.
    uint32_t maxFramesInFlight = 2;
};

struct SwapchainInfo {
    uint32_t width = 0; // 0 while the window has no area (e.g. minimized)
    uint32_t height = 0;
    ImageFormat format = IMAGE_FORMAT_UNDEFINED; // color format for programs drawing to the window
    uint32_t imageCount = 0;
};

#endif //PIXELKILN_SWAPCHAINDESC_H
