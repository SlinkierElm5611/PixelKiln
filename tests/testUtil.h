//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_TESTUTIL_H
#define PIXELKILN_TESTUTIL_H
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pixelKiln.h"

template <size_t N>
Shader shaderFrom(const uint32_t (&spirv)[N])
{
    return {spirv, static_cast<uint32_t>(sizeof(spirv))};
}

template <typename T>
std::vector<T> download(PixelKiln &kiln, uint64_t buffer, size_t count, uint64_t offset = 0)
{
    std::vector<T> values(count);
    kiln.downloadBuffer(buffer, values.data(), count * sizeof(T), offset);
    return values;
}

inline std::vector<uint8_t> downloadPixels(PixelKiln &kiln, uint64_t image, uint32_t width, uint32_t height)
{
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    kiln.downloadImage(image, pixels.data(), pixels.size());
    return pixels;
}

inline uint32_t packRgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return r | (g << 8) | (b << 16) | (a << 24);
}

// Pixel of an RGBA8 image packed like packRgba.
inline uint32_t pixelAt(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t x, uint32_t y)
{
    const uint8_t* p = &pixels[(size_t(y) * width + x) * 4];
    return packRgba(p[0], p[1], p[2], p[3]);
}

// Every channel within `tolerance` (rasterization/blending rounding).
inline bool nearRgba(uint32_t actual, uint32_t expected, int tolerance = 2)
{
    for (int shift = 0; shift < 32; shift += 8) {
        int a = int((actual >> shift) & 0xff);
        int e = int((expected >> shift) & 0xff);
        if (a - e > tolerance || e - a > tolerance) {
            return false;
        }
    }
    return true;
}

#endif //PIXELKILN_TESTUTIL_H
