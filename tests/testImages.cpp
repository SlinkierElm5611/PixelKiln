//
// Created by Stefan Balta on 2026-09-21.
//

#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

// Uploads and downloads are bit exact copies for every color format (odd sizes on purpose).
TEST(image_roundtrip_formats)
{
    PixelKiln kiln;
    struct Format {
        ImageFormat format;
        uint32_t texelSize;
    };
    const Format formats[] = {
        {IMAGE_FORMAT_RGBA8_UNORM, 4}, {IMAGE_FORMAT_RGBA8_SRGB, 4}, {IMAGE_FORMAT_BGRA8_UNORM, 4},
        {IMAGE_FORMAT_R8_UNORM, 1}, {IMAGE_FORMAT_R32_FLOAT, 4}, {IMAGE_FORMAT_RG32_FLOAT, 8},
        {IMAGE_FORMAT_RGBA16_FLOAT, 8}, {IMAGE_FORMAT_RGBA32_FLOAT, 16}, {IMAGE_FORMAT_R32_UINT, 4},
    };
    const uint32_t width = 13, height = 7;
    for (const Format &format : formats) {
        const size_t size = size_t(width) * height * format.texelSize;
        std::vector<uint8_t> texels(size);
        for (size_t i = 0; i < size; i++) {
            // Keep floats finite: the top byte of each 32-bit word stays below 0x7f.
            texels[i] = uint8_t((i * 37 + format.format) % 0x7f);
        }
        uint64_t image = kiln.createImage({width, height, format.format, IMAGE_USAGE_SAMPLED});
        kiln.uploadImage(image, texels.data(), size);
        std::vector<uint8_t> result(size);
        kiln.downloadImage(image, result.data(), size);
        if (result != texels) {
            reportFailure(__FILE__, __LINE__, "roundtrip mismatch for format " + std::to_string(format.format));
        }
        kiln.destroyImage(image);
    }
}

TEST(image_reupload)
{
    PixelKiln kiln;
    const uint32_t size = 8;
    uint64_t image = kiln.createImage({size, size, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    std::vector<uint32_t> first(size * size, packRgba(1, 2, 3, 4));
    std::vector<uint32_t> second(size * size, packRgba(200, 150, 100, 50));
    kiln.uploadImage(image, first.data(), size * size * 4);
    std::vector<uint8_t> pixels = downloadPixels(kiln, image, size, size);
    CHECK_EQ(pixelAt(pixels, size, 3, 5), first[0]);
    kiln.uploadImage(image, second.data(), size * size * 4);
    pixels = downloadPixels(kiln, image, size, size);
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            CHECK_EQ(pixelAt(pixels, size, x, y), second[0]);
        }
    }
}
