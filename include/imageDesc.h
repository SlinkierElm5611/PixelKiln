//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_IMAGEDESC_H
#define PIXELKILN_IMAGEDESC_H
#include <cstdint>

#include "imageFormat.h"

enum ImageUsage {
    IMAGE_USAGE_SAMPLED = 1 << 0,
    IMAGE_USAGE_STORAGE = 1 << 1,
    IMAGE_USAGE_COLOR_TARGET = 1 << 2,
    IMAGE_USAGE_DEPTH_TARGET = 1 << 3
};

typedef uint32_t ImageUsageFlags;

struct ImageDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    ImageFormat format = IMAGE_FORMAT_RGBA8_UNORM;
    ImageUsageFlags usage = IMAGE_USAGE_SAMPLED;
};

#endif //PIXELKILN_IMAGEDESC_H
