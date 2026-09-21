//
// Created by Stefan Balta on 2026-07-12.
//

// Renders a textured, tinted quad into an offscreen image with a depth buffer, then draws a full screen background
// triangle behind it with a second call. Downloads the image, checks the pixels and writes headless.ppm.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "pixelKiln.h"

static const uint32_t quadVertSpirv[] =
#include "quad.vert.h"
;
static const uint32_t quadFragSpirv[] =
#include "quad.frag.h"
;
static const uint32_t backgroundVertSpirv[] =
#include "background.vert.h"
;
static const uint32_t backgroundFragSpirv[] =
#include "background.frag.h"
;

static const uint32_t SIZE = 64;

struct Vertex {
    float x, y, z;
    float u, v;
};

static int failures = 0;

static void checkPixel(const std::vector<uint8_t> &pixels, uint32_t x, uint32_t y, int r, int g, int b)
{
    const uint8_t* p = &pixels[(y * SIZE + x) * 4];
    auto matches = [](int a, int b) { return std::abs(a - b) <= 2; };
    if (!matches(p[0], r) || !matches(p[1], g) || !matches(p[2], b) || p[3] != 255) {
        std::printf("pixel (%u, %u) is (%d, %d, %d, %d), expected (%d, %d, %d, 255)\n", x, y, p[0], p[1], p[2], p[3],
                    r, g, b);
        failures++;
    }
}

int main()
{
    PixelKiln kiln;

    RasterDrawProgram quadProgram{};
    quadProgram.vertexShader = {quadVertSpirv, sizeof(quadVertSpirv)};
    quadProgram.fragmentShader = {quadFragSpirv, sizeof(quadFragSpirv)};
    quadProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_SAMPLER};
    quadProgram.vertexLayout.buffers = {{sizeof(Vertex)}};
    quadProgram.vertexLayout.attributes = {
        {0, 0, VERTEX_FORMAT_FLOAT3, 0},
        {1, 0, VERTEX_FORMAT_FLOAT2, 3 * sizeof(float)},
    };
    quadProgram.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    quadProgram.depthFormat = IMAGE_FORMAT_D32_FLOAT;
    uint64_t quad = kiln.loadRasterDrawProgram(quadProgram);

    RasterDrawProgram backgroundProgram{};
    backgroundProgram.vertexShader = {backgroundVertSpirv, sizeof(backgroundVertSpirv)};
    backgroundProgram.fragmentShader = {backgroundFragSpirv, sizeof(backgroundFragSpirv)};
    backgroundProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    backgroundProgram.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    backgroundProgram.depthFormat = IMAGE_FORMAT_D32_FLOAT;
    uint64_t background = kiln.loadRasterDrawProgram(backgroundProgram);

    // Quad covering the middle half of the target, in front of the background.
    const Vertex vertices[] = {
        {-0.5f, -0.5f, 0.5f, 0.0f, 0.0f},
        {0.5f, -0.5f, 0.5f, 1.0f, 0.0f},
        {0.5f, 0.5f, 0.5f, 1.0f, 1.0f},
        {-0.5f, 0.5f, 0.5f, 0.0f, 1.0f},
    };
    const uint16_t indices[] = {0, 1, 2, 2, 3, 0};
    uint64_t vertexBuffer = kiln.createBuffer(sizeof(vertices));
    uint64_t indexBuffer = kiln.createBuffer(sizeof(indices));
    kiln.uploadBuffer(vertexBuffer, vertices, sizeof(vertices));
    kiln.uploadBuffer(indexBuffer, indices, sizeof(indices));

    // 2x2 texture: red, green / blue, white.
    const uint8_t texels[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    uint64_t texture = kiln.createImage({2, 2, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    kiln.uploadImage(texture, texels, sizeof(texels));

    uint64_t color = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
    uint64_t depth = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET});

    std::vector<uint8_t> pixels(SIZE * SIZE * 4);
    // Two frames with different tints, the second one reuses the target after it was downloaded.
    const float tints[2][4] = {{1.0f, 0.5f, 1.0f, 1.0f}, {0.5f, 1.0f, 1.0f, 1.0f}};
    for (int frame = 0; frame < 2; frame++) {
        ProgramCall quadCall{};
        quadCall.type = PROGRAM_TYPE_RASTER_DRAW;
        quadCall.program = quad;
        quadCall.bindings = {
            {.data = tints[frame], .size = sizeof(tints[frame])},
            {.resource = texture, .sampler = {SAMPLER_FILTER_NEAREST, SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}},
        };
        quadCall.colorTargets = {{color, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
        quadCall.depthTarget = {depth, true, 1.0f};
        quadCall.vertexBuffers = {vertexBuffer};
        quadCall.indexBuffer = indexBuffer;
        quadCall.indexType = INDEX_TYPE_UINT16;
        quadCall.indexCount = 6;
        kiln.call(quadCall);

        // Drawn second, but behind the quad: only visible outside it.
        const float gray[4] = {0.25f, 0.25f, 0.25f, 1.0f};
        ProgramCall backgroundCall{};
        backgroundCall.type = PROGRAM_TYPE_RASTER_DRAW;
        backgroundCall.program = background;
        backgroundCall.bindings = {{.data = gray, .size = sizeof(gray)}};
        backgroundCall.colorTargets = {{color, false}};
        backgroundCall.depthTarget = {depth, false};
        backgroundCall.vertexCount = 3;
        kiln.call(backgroundCall);

        kiln.downloadImage(color, pixels.data(), pixels.size());

        const int r = int(tints[frame][0] * 255.0f + 0.5f);
        const int g = int(tints[frame][1] * 255.0f + 0.5f);
        checkPixel(pixels, 8, 8, 64, 64, 64);     // background
        checkPixel(pixels, 60, 40, 64, 64, 64);   // background
        checkPixel(pixels, 20, 20, r, 0, 0);      // red texel
        checkPixel(pixels, 43, 20, 0, g, 0);      // green texel
        checkPixel(pixels, 20, 43, 0, 0, 255);    // blue texel
        checkPixel(pixels, 43, 43, r, g, 255);    // white texel
    }

    if (FILE* file = std::fopen("headless.ppm", "wb")) {
        std::fprintf(file, "P6\n%u %u\n255\n", SIZE, SIZE);
        for (uint32_t i = 0; i < SIZE * SIZE; i++) {
            std::fwrite(&pixels[i * 4], 1, 3, file);
        }
        std::fclose(file);
    }

    kiln.destroyImage(depth);
    kiln.destroyImage(color);
    kiln.destroyImage(texture);
    kiln.destroyBuffer(indexBuffer);
    kiln.destroyBuffer(vertexBuffer);
    kiln.unloadProgram(background);
    kiln.unloadProgram(quad);

    if (failures > 0) {
        std::printf("HeadlessRender: %d pixel checks failed\n", failures);
        return 1;
    }
    std::printf("HeadlessRender: ok, wrote headless.ppm\n");
    return 0;
}
