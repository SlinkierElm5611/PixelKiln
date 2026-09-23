//
// Created by Stefan Balta on 2026-09-22.
//

// Multisampled rendering: resolves, the samples themselves, depth, store ops, alpha to coverage and integer targets.
// Each test uses the largest of 8x, 4x and 2x that the device supports, and is skipped when it supports none.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t positionVertSpirv[] =
#include "position.vert.h"
;
static const uint32_t solidFragSpirv[] =
#include "solid.frag.h"
;
static const uint32_t backgroundVertSpirv[] =
#include "background.vert.h"
;
static const uint32_t msaaCoverageSpirv[] =
#include "msaaCoverage.comp.h"
;
static const uint32_t uintValueFragSpirv[] =
#include "uintValue.frag.h"
;

static const uint32_t SIZE = 64;
static const float WHITE[4] = {1.0f, 1.0f, 1.0f, 1.0f};

static uint32_t msaaSamples(PixelKiln &kiln, ImageFormat format, ImageFormat depthFormat = IMAGE_FORMAT_UNDEFINED)
{
    uint32_t counts = kiln.getSupportedSampleCounts(format);
    if (depthFormat != IMAGE_FORMAT_UNDEFINED) {
        counts &= kiln.getSupportedSampleCounts(depthFormat);
    }
    for (uint32_t samples : {8u, 4u, 2u}) {
        if (counts & samples) {
            return samples;
        }
    }
    std::printf("  skipped: this device can't multisample the format\n");
    return 0;
}

// A triangle over the left part of the target at depth 0.3. Its edges cross the target at angles that aren't
// multiples of 45 degrees, so the pixels along them are partially covered. (2, 4) is inside, (61, 61) outside.
static uint64_t edgeTriangle(PixelKiln &kiln)
{
    const float positions[] = {-1.0f, -1.0f, 0.3f, 1.0f, 1.0f, 0.8f, 0.3f, 1.0f, -1.0f, 1.0f, 0.3f, 1.0f};
    uint64_t buffer = kiln.createBuffer(sizeof(positions));
    kiln.uploadBuffer(buffer, positions, sizeof(positions));
    return buffer;
}
static const uint32_t INSIDE = 4 * SIZE + 2;
static const uint32_t OUTSIDE = (SIZE - 3) * SIZE + SIZE - 3;

// position.vert + solid.frag: vec4 positions from vertex buffer 0, color from uniform binding 0.
static RasterDrawProgram edgeProgram(uint32_t samples, ImageFormat depthFormat = IMAGE_FORMAT_UNDEFINED)
{
    RasterDrawProgram program{};
    program.vertexShader = shaderFrom(positionVertSpirv);
    program.fragmentShader = shaderFrom(solidFragSpirv);
    program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    program.vertexLayout.buffers = {{4 * sizeof(float)}};
    program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    program.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    program.depthFormat = depthFormat;
    program.samples = samples;
    return program;
}

static ProgramCall edgeCall(uint64_t program, uint64_t triangle, const ColorTarget &target)
{
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = program;
    call.bindings = {{.data = WHITE, .size = sizeof(WHITE)}};
    call.colorTargets = {target};
    call.vertexBuffers = {triangle};
    call.vertexCount = 3;
    return call;
}

// The red channel of every pixel of an RGBA8 image.
static std::vector<uint32_t> reds(PixelKiln &kiln, uint64_t image)
{
    std::vector<uint8_t> pixels = downloadPixels(kiln, image, SIZE, SIZE);
    std::vector<uint32_t> values(size_t(SIZE) * SIZE);
    for (size_t i = 0; i < values.size(); i++) {
        values[i] = pixels[i * 4];
    }
    return values;
}

// Without multisampling every pixel is either in or out. A resolved multisampled target gives the partially covered
// pixels along the edges the average of their samples: a multiple of 255 / samples.
TEST(msaa_resolve_antialiases_edges)
{
    PixelKiln kiln;
    const uint32_t samples = msaaSamples(kiln, IMAGE_FORMAT_RGBA8_UNORM);
    if (!samples) {
        return;
    }
    uint64_t triangle = edgeTriangle(kiln);
    uint64_t aliased = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
    uint64_t multisampled = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET, samples});
    uint64_t resolved = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});

    uint64_t single = kiln.loadRasterDrawProgram(edgeProgram(1));
    kiln.call(edgeCall(single, triangle, {.image = aliased, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}));
    int partial = 0;
    for (uint32_t red : reds(kiln, aliased)) {
        partial += red != 0 && red != 255;
    }
    CHECK_EQ(partial, 0);

    uint64_t msaa = kiln.loadRasterDrawProgram(edgeProgram(samples));
    kiln.call(edgeCall(msaa, triangle, {.image = multisampled, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                                        .resolveImage = resolved, .store = false}));
    std::vector<uint32_t> values = reds(kiln, resolved);
    partial = 0;
    int offGrid = 0;
    for (uint32_t red : values) {
        partial += red != 0 && red != 255;
        const uint32_t covered = (red * samples + 127) / 255; // nearest whole number of covered samples
        const uint32_t expected = covered * 255 / samples;
        offGrid += red > expected + 2 || red + 2 < expected;
    }
    CHECK(partial >= int(SIZE) / 2); // at least the long edge, which crosses the whole target
    CHECK_EQ(offGrid, 0);
    CHECK_EQ(values[INSIDE], 255u);
    CHECK_EQ(values[OUTSIDE], 0u);
}

// The samples themselves, read back with texelFetch from a sampler2DMS: each resolved pixel is the average of its
// samples, and a stored multisampled target keeps them after being resolved.
TEST(msaa_samples_match_resolve)
{
    PixelKiln kiln;
    const uint32_t samples = msaaSamples(kiln, IMAGE_FORMAT_RGBA8_UNORM);
    if (!samples) {
        return;
    }
    uint64_t triangle = edgeTriangle(kiln);
    uint64_t multisampled = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM,
                                              IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_SAMPLED, samples});
    uint64_t resolved = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
    uint64_t msaa = kiln.loadRasterDrawProgram(edgeProgram(samples));
    kiln.call(edgeCall(msaa, triangle, {.image = multisampled, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                                        .resolveImage = resolved}));

    uint64_t coverage = kiln.loadComputeProgram({shaderFrom(msaaCoverageSpirv), {UNIFORM_BINDING_TYPE_SAMPLER,
                                                                               UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                                               UNIFORM_BINDING_TYPE_BUFFER}});
    uint64_t counts = kiln.createBuffer(SIZE * SIZE * sizeof(uint32_t));
    const uint32_t params[2] = {SIZE, samples};
    ProgramCall count{};
    count.type = PROGRAM_TYPE_COMPUTE;
    count.program = coverage;
    count.bindings = {{.resource = multisampled, .sampler = {SAMPLER_FILTER_NEAREST}}, {.resource = counts},
                      {.data = params, .size = sizeof(params)}};
    count.groupCountX = SIZE / 8;
    count.groupCountY = SIZE / 8;
    kiln.call(count);

    std::vector<uint32_t> covered = download<uint32_t>(kiln, counts, SIZE * SIZE);
    std::vector<uint32_t> values = reds(kiln, resolved);
    int partial = 0;
    int mismatched = 0;
    for (size_t i = 0; i < covered.size(); i++) {
        partial += covered[i] != 0 && covered[i] != samples;
        const uint32_t expected = covered[i] * 255 / samples;
        mismatched += values[i] > expected + 2 || values[i] + 2 < expected;
    }
    CHECK(partial >= int(SIZE) / 2);
    CHECK_EQ(mismatched, 0);
    CHECK_EQ(covered[INSIDE], samples);
    CHECK_EQ(covered[OUTSIDE], 0u);
}

// Multisampled depth across two calls: the near triangle first, then a far full screen triangle that loads both
// targets and resolves without storing them. The near triangle stays in front, its edges blend the two colors, and
// since every sample is covered by one of them nothing is left black.
TEST(msaa_depth_across_calls)
{
    PixelKiln kiln;
    const uint32_t samples = msaaSamples(kiln, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_FORMAT_D32_FLOAT);
    if (!samples) {
        return;
    }
    uint64_t color = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET, samples});
    uint64_t depth = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET, samples});
    uint64_t resolved = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
    uint64_t triangle = edgeTriangle(kiln);
    uint64_t nearProgram = kiln.loadRasterDrawProgram(edgeProgram(samples, IMAGE_FORMAT_D32_FLOAT));
    RasterDrawProgram background = edgeProgram(samples, IMAGE_FORMAT_D32_FLOAT);
    background.vertexShader = shaderFrom(backgroundVertSpirv); // full screen at depth 0.9
    background.vertexLayout = {};
    uint64_t farProgram = kiln.loadRasterDrawProgram(background);

    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    ProgramCall first = edgeCall(nearProgram, triangle, {.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}});
    first.bindings = {{.data = red, .size = sizeof(red)}};
    first.depthTarget = {depth, true, 1.0f};
    kiln.call(first);

    ProgramCall second{};
    second.type = PROGRAM_TYPE_RASTER_DRAW;
    second.program = farProgram;
    second.bindings = {{.data = green, .size = sizeof(green)}};
    second.colorTargets = {{.image = color, .clear = false, .resolveImage = resolved, .store = false}};
    second.depthTarget = {.image = depth, .clear = false, .store = false};
    second.vertexCount = 3;
    kiln.call(second);

    std::vector<uint8_t> pixels = downloadPixels(kiln, resolved, SIZE, SIZE);
    CHECK_EQ(pixelAt(pixels, SIZE, 2, 4), packRgba(255, 0, 0, 255));
    CHECK_EQ(pixelAt(pixels, SIZE, SIZE - 3, SIZE - 3), packRgba(0, 255, 0, 255));
    int blended = 0;
    int wrong = 0;
    for (uint32_t y = 0; y < SIZE; y++) {
        for (uint32_t x = 0; x < SIZE; x++) {
            const uint32_t pixel = pixelAt(pixels, SIZE, x, y);
            const uint32_t r = pixel & 0xff, g = (pixel >> 8) & 0xff, b = (pixel >> 16) & 0xff;
            blended += r != 0 && g != 0;
            wrong += r + g < 253 || r + g > 257 || b != 0;
        }
    }
    CHECK(blended >= int(SIZE) / 2);
    CHECK_EQ(wrong, 0);
}

// Alpha to coverage: a fragment with alpha a covers about a * samples of its pixel's samples.
TEST(msaa_alpha_to_coverage)
{
    PixelKiln kiln;
    const uint32_t samples = msaaSamples(kiln, IMAGE_FORMAT_RGBA8_UNORM);
    if (!samples) {
        return;
    }
    RasterDrawProgram program = edgeProgram(samples);
    program.vertexShader = shaderFrom(backgroundVertSpirv);
    program.vertexLayout = {};
    program.alphaToCoverage = true;
    uint64_t draw = kiln.loadRasterDrawProgram(program);
    uint64_t color = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET, samples});
    uint64_t resolved = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});

    // Average red over the resolved image for a full screen white fragment with the given alpha, over black.
    auto average = [&](float alpha) {
        const float white[4] = {1.0f, 1.0f, 1.0f, alpha};
        ProgramCall call{};
        call.type = PROGRAM_TYPE_RASTER_DRAW;
        call.program = draw;
        call.bindings = {{.data = white, .size = sizeof(white)}};
        call.colorTargets = {{.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .resolveImage = resolved,
                              .store = false}};
        call.vertexCount = 3;
        kiln.call(call);
        uint64_t sum = 0;
        for (uint32_t red : reds(kiln, resolved)) {
            sum += red;
        }
        return double(sum) / double(SIZE * SIZE);
    };
    CHECK(average(0.0f) == 0.0);
    CHECK(average(1.0f) == 255.0);
    const double half = average(0.5f); // how coverage is dithered is up to the implementation
    CHECK(half > 64.0 && half < 192.0);
}

// Integer samples can't be averaged: an integer target resolves to one of its samples, never to a blend.
TEST(msaa_integer_target)
{
    PixelKiln kiln;
    const uint32_t samples = msaaSamples(kiln, IMAGE_FORMAT_R32_UINT);
    if (!samples) {
        return;
    }
    RasterDrawProgram program = edgeProgram(samples);
    program.fragmentShader = shaderFrom(uintValueFragSpirv);
    program.colorFormats = {IMAGE_FORMAT_R32_UINT};
    uint64_t draw = kiln.loadRasterDrawProgram(program);
    uint64_t color = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_R32_UINT, IMAGE_USAGE_COLOR_TARGET, samples});
    uint64_t resolved = kiln.createImage({SIZE, SIZE, IMAGE_FORMAT_R32_UINT, IMAGE_USAGE_COLOR_TARGET});
    uint64_t triangle = edgeTriangle(kiln);

    const uint32_t value = 7;
    ProgramCall call = edgeCall(draw, triangle, {.image = color, .resolveImage = resolved, .store = false});
    call.bindings = {{.data = &value, .size = sizeof(value)}};
    kiln.call(call);

    std::vector<uint32_t> values(size_t(SIZE) * SIZE);
    kiln.downloadImage(resolved, values.data(), values.size() * sizeof(uint32_t));
    int blends = 0;
    for (uint32_t v : values) {
        blends += v != 0 && v != value;
    }
    CHECK_EQ(blends, 0);
    CHECK_EQ(values[INSIDE], value);
    CHECK_EQ(values[OUTSIDE], 0u);
}
