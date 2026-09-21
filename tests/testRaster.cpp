//
// Created by Stefan Balta on 2026-09-21.
//

#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t positionVertSpirv[] =
#include "position.vert.h"
;
static const uint32_t solidFragSpirv[] =
#include "solid.frag.h"
;
static const uint32_t twoTargetsFragSpirv[] =
#include "twoTargets.frag.h"
;
static const uint32_t texturedVertSpirv[] =
#include "textured.vert.h"
;
static const uint32_t texturedFragSpirv[] =
#include "textured.frag.h"
;
static const uint32_t backgroundVertSpirv[] =
#include "background.vert.h"
;
static const uint32_t instancedVertSpirv[] =
#include "instanced.vert.h"
;
static const uint32_t triangleSpirv[] =
#include "triangle.comp.h"
;

static const uint32_t SIZE = 64;

// position.vert + solid.frag: vec4 positions from vertex buffer 0, color from uniform binding 0.
static RasterDrawProgram solidProgram(std::vector<ImageFormat> colorFormats)
{
    RasterDrawProgram program{};
    program.vertexShader = shaderFrom(positionVertSpirv);
    program.fragmentShader = shaderFrom(solidFragSpirv);
    program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    program.vertexLayout.buffers = {{4 * sizeof(float)}};
    program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    program.colorFormats = colorFormats;
    return program;
}

static uint64_t fullScreenTriangle(PixelKiln &kiln)
{
    const float positions[] = {-1.0f, -1.0f, 0.0f, 1.0f, 3.0f, -1.0f, 0.0f, 1.0f, -1.0f, 3.0f, 0.0f, 1.0f};
    uint64_t buffer = kiln.createBuffer(sizeof(positions));
    kiln.uploadBuffer(buffer, positions, sizeof(positions));
    return buffer;
}

static uint64_t colorTarget(PixelKiln &kiln)
{
    return kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
}

// A textured quad in front, then a background triangle drawn afterwards but behind it (depth test, LOAD ops).
TEST(raster_textured_indexed_depth)
{
    PixelKiln kiln;
    RasterDrawProgram quadProgram{};
    quadProgram.vertexShader = shaderFrom(texturedVertSpirv);
    quadProgram.fragmentShader = shaderFrom(texturedFragSpirv);
    quadProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_SAMPLER};
    quadProgram.vertexLayout.buffers = {{5 * sizeof(float)}};
    quadProgram.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT3, 0}, {1, 0, VERTEX_FORMAT_FLOAT2, 3 * sizeof(float)}};
    quadProgram.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    quadProgram.depthFormat = IMAGE_FORMAT_D32_FLOAT;
    uint64_t quad = kiln.loadRasterDrawProgram(quadProgram);

    RasterDrawProgram backgroundProgram{};
    backgroundProgram.vertexShader = shaderFrom(backgroundVertSpirv);
    backgroundProgram.fragmentShader = shaderFrom(solidFragSpirv);
    backgroundProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    backgroundProgram.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    backgroundProgram.depthFormat = IMAGE_FORMAT_D32_FLOAT;
    uint64_t background = kiln.loadRasterDrawProgram(backgroundProgram);

    const float vertices[] = {
        -0.5f, -0.5f, 0.5f, 0.0f, 0.0f,
        0.5f, -0.5f, 0.5f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 1.0f, 1.0f,
        -0.5f, 0.5f, 0.5f, 0.0f, 1.0f,
    };
    const uint16_t indices[] = {0, 1, 2, 2, 3, 0};
    uint64_t vertexBuffer = kiln.createBuffer(sizeof(vertices));
    uint64_t indexBuffer = kiln.createBuffer(sizeof(indices));
    kiln.uploadBuffer(vertexBuffer, vertices, sizeof(vertices));
    kiln.uploadBuffer(indexBuffer, indices, sizeof(indices));
    const uint32_t texels[] = {packRgba(255, 0, 0, 255), packRgba(0, 255, 0, 255),
                               packRgba(0, 0, 255, 255), packRgba(255, 255, 255, 255)};
    uint64_t texture = kiln.createImage({2, 2, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    kiln.uploadImage(texture, texels, sizeof(texels));
    uint64_t color = colorTarget(kiln);
    uint64_t depth = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET});

    // Two frames: the second reuses the targets after the first was downloaded.
    const float tints[2][4] = {{1.0f, 0.5f, 1.0f, 1.0f}, {0.5f, 1.0f, 1.0f, 1.0f}};
    for (int frame = 0; frame < 2; frame++) {
        ProgramCall quadCall{};
        quadCall.type = PROGRAM_TYPE_RASTER_DRAW;
        quadCall.program = quad;
        quadCall.bindings = {{.data = tints[frame], .size = sizeof(tints[frame])},
                             {.resource = texture, .sampler = {SAMPLER_FILTER_NEAREST}}};
        quadCall.colorTargets = {{color, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
        quadCall.depthTarget = {depth, true, 1.0f};
        quadCall.vertexBuffers = {vertexBuffer};
        quadCall.indexBuffer = indexBuffer;
        quadCall.indexType = INDEX_TYPE_UINT16;
        quadCall.indexCount = 6;
        kiln.call(quadCall);

        const float gray[4] = {0.25f, 0.25f, 0.25f, 1.0f};
        ProgramCall backgroundCall{};
        backgroundCall.type = PROGRAM_TYPE_RASTER_DRAW;
        backgroundCall.program = background;
        backgroundCall.bindings = {{.data = gray, .size = sizeof(gray)}};
        backgroundCall.colorTargets = {{color, false}};
        backgroundCall.depthTarget = {depth, false};
        backgroundCall.vertexCount = 3;
        kiln.call(backgroundCall);

        std::vector<uint8_t> pixels = downloadPixels(kiln, color, SIZE, SIZE);
        const uint32_t r = uint32_t(tints[frame][0] * 255.0f + 0.5f);
        const uint32_t g = uint32_t(tints[frame][1] * 255.0f + 0.5f);
        CHECK(nearRgba(pixelAt(pixels, SIZE, 8, 8), packRgba(64, 64, 64, 255)));
        CHECK(nearRgba(pixelAt(pixels, SIZE, 60, 40), packRgba(64, 64, 64, 255)));
        CHECK(nearRgba(pixelAt(pixels, SIZE, 20, 20), packRgba(r, 0, 0, 255)));
        CHECK(nearRgba(pixelAt(pixels, SIZE, 43, 20), packRgba(0, g, 0, 255)));
        CHECK(nearRgba(pixelAt(pixels, SIZE, 20, 43), packRgba(0, 0, 255, 255)));
        CHECK(nearRgba(pixelAt(pixels, SIZE, 43, 43), packRgba(r, g, 255, 255)));
    }
}

TEST(raster_multiple_color_targets)
{
    PixelKiln kiln;
    RasterDrawProgram program = solidProgram({IMAGE_FORMAT_RGBA8_UNORM, IMAGE_FORMAT_RGBA8_UNORM});
    program.fragmentShader = shaderFrom(twoTargetsFragSpirv);
    uint64_t draw = kiln.loadRasterDrawProgram(program);
    uint64_t triangle = fullScreenTriangle(kiln);
    uint64_t first = colorTarget(kiln);
    uint64_t second = colorTarget(kiln);

    const float color[4] = {0.2f, 0.4f, 0.6f, 1.0f};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = draw;
    call.bindings = {{.data = color, .size = sizeof(color)}};
    call.colorTargets = {{.image = first}, {.image = second}};
    call.vertexBuffers = {triangle};
    call.vertexCount = 3;
    kiln.call(call);

    std::vector<uint8_t> pixels = downloadPixels(kiln, first, SIZE, SIZE);
    CHECK(nearRgba(pixelAt(pixels, SIZE, 5, 5), packRgba(51, 102, 153, 255)));
    CHECK(nearRgba(pixelAt(pixels, SIZE, 60, 60), packRgba(51, 102, 153, 255)));
    pixels = downloadPixels(kiln, second, SIZE, SIZE);
    CHECK(nearRgba(pixelAt(pixels, SIZE, 5, 5), packRgba(204, 153, 102, 255)));
    CHECK(nearRgba(pixelAt(pixels, SIZE, 60, 60), packRgba(204, 153, 102, 255)));
}

TEST(raster_blending)
{
    PixelKiln kiln;
    RasterDrawProgram program = solidProgram({IMAGE_FORMAT_RGBA8_UNORM});
    program.blendEnable = true;
    uint64_t draw = kiln.loadRasterDrawProgram(program);
    uint64_t triangle = fullScreenTriangle(kiln);
    uint64_t color = colorTarget(kiln);

    const float halfRed[4] = {1.0f, 0.0f, 0.0f, 0.5f};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = draw;
    call.bindings = {{.data = halfRed, .size = sizeof(halfRed)}};
    call.colorTargets = {{color, true, {0.0f, 0.0f, 1.0f, 1.0f}}};
    call.vertexBuffers = {triangle};
    call.vertexCount = 3;
    kiln.call(call);

    std::vector<uint8_t> pixels = downloadPixels(kiln, color, SIZE, SIZE);
    CHECK(nearRgba(pixelAt(pixels, SIZE, 32, 32), packRgba(128, 0, 128, 255)));
}

// Per instance vertex buffer + 32 bit indices: four small squares, one per quadrant.
TEST(raster_instancing_uint32_indices)
{
    PixelKiln kiln;
    RasterDrawProgram program = solidProgram({IMAGE_FORMAT_RGBA8_UNORM});
    program.vertexShader = shaderFrom(instancedVertSpirv);
    program.vertexLayout.buffers = {{2 * sizeof(float)}, {2 * sizeof(float), true}};
    program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT2, 0}, {1, 1, VERTEX_FORMAT_FLOAT2, 0}};
    uint64_t draw = kiln.loadRasterDrawProgram(program);

    const float corners[] = {-0.25f, -0.25f, 0.25f, -0.25f, 0.25f, 0.25f, -0.25f, 0.25f};
    const float offsets[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
    const uint32_t indices[] = {0, 1, 2, 2, 3, 0};
    uint64_t cornerBuffer = kiln.createBuffer(sizeof(corners));
    uint64_t offsetBuffer = kiln.createBuffer(sizeof(offsets));
    uint64_t indexBuffer = kiln.createBuffer(sizeof(indices));
    kiln.uploadBuffer(cornerBuffer, corners, sizeof(corners));
    kiln.uploadBuffer(offsetBuffer, offsets, sizeof(offsets));
    kiln.uploadBuffer(indexBuffer, indices, sizeof(indices));
    uint64_t color = colorTarget(kiln);

    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = draw;
    call.bindings = {{.data = white, .size = sizeof(white)}};
    call.colorTargets = {{color, true, {0.0f, 0.0f, 0.0f, 1.0f}}};
    call.vertexBuffers = {cornerBuffer, offsetBuffer};
    call.indexBuffer = indexBuffer;
    call.indexType = INDEX_TYPE_UINT32;
    call.indexCount = 6;
    call.instanceCount = 4;
    kiln.call(call);

    std::vector<uint8_t> pixels = downloadPixels(kiln, color, SIZE, SIZE);
    const uint32_t on = packRgba(255, 255, 255, 255);
    const uint32_t off = packRgba(0, 0, 0, 255);
    CHECK_EQ(pixelAt(pixels, SIZE, 16, 16), on);
    CHECK_EQ(pixelAt(pixels, SIZE, 48, 16), on);
    CHECK_EQ(pixelAt(pixels, SIZE, 16, 48), on);
    CHECK_EQ(pixelAt(pixels, SIZE, 48, 48), on);
    CHECK_EQ(pixelAt(pixels, SIZE, 32, 32), off);
    CHECK_EQ(pixelAt(pixels, SIZE, 32, 16), off);
    CHECK_EQ(pixelAt(pixels, SIZE, 2, 2), off);
}

// A compute call writes the vertices, the next draw reads them as a vertex buffer.
TEST(raster_vertex_buffer_written_by_compute)
{
    PixelKiln kiln;
    uint64_t generate = kiln.loadComputeProgram({shaderFrom(triangleSpirv), {UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    uint64_t draw = kiln.loadRasterDrawProgram(solidProgram({IMAGE_FORMAT_RGBA8_UNORM}));
    uint64_t vertices = kiln.createBuffer(3 * 4 * sizeof(float));
    uint64_t color = colorTarget(kiln);

    ProgramCall compute{};
    compute.type = PROGRAM_TYPE_COMPUTE;
    compute.program = generate;
    compute.bindings = {{.resource = vertices}};
    kiln.call(compute);

    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = draw;
    call.bindings = {{.data = green, .size = sizeof(green)}};
    call.colorTargets = {{color, true, {1.0f, 0.0f, 0.0f, 1.0f}}};
    call.vertexBuffers = {vertices};
    call.vertexCount = 3;
    kiln.call(call);

    std::vector<uint8_t> pixels = downloadPixels(kiln, color, SIZE, SIZE);
    int wrong = 0;
    for (uint32_t y = 0; y < SIZE; y++) {
        for (uint32_t x = 0; x < SIZE; x++) {
            wrong += pixelAt(pixels, SIZE, x, y) != packRgba(0, 255, 0, 255);
        }
    }
    CHECK_EQ(wrong, 0);
}
