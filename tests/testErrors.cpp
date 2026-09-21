//
// Created by Stefan Balta on 2026-09-21.
//

// Misuse throws std::invalid_argument before any GPU work is recorded, and the kiln keeps working afterwards.

#include <cstdint>
#include <vector>

#include "testFramework.h"
#include "testUtil.h"

static const uint32_t storeSpirv[] =
#include "store.comp.h"
;
static const uint32_t positionVertSpirv[] =
#include "position.vert.h"
;
static const uint32_t solidFragSpirv[] =
#include "solid.frag.h"
;
static const uint32_t readbackSpirv[] =
#include "readback.comp.h"
;

TEST(errors_resources)
{
    PixelKiln kiln;
    uint64_t buffer = kiln.createBuffer(64);
    uint32_t data[32] = {};
    CHECK_THROWS_INVALID(kiln.createBuffer(0));
    CHECK_THROWS_INVALID(kiln.uploadBuffer(buffer, data, 128));
    CHECK_THROWS_INVALID(kiln.uploadBuffer(buffer, data, 8, 60));
    CHECK_THROWS_INVALID(kiln.uploadBuffer(buffer, nullptr, 8));
    CHECK_THROWS_INVALID(kiln.uploadBuffer(buffer, data, 0));
    CHECK_THROWS_INVALID(kiln.downloadBuffer(buffer, data, 8, UINT64_MAX));
    CHECK_THROWS_INVALID(kiln.uploadBuffer(12345, data, 8));
    CHECK_THROWS_INVALID(kiln.destroyBuffer(12345));

    CHECK_THROWS_INVALID(kiln.createImage({0, 4, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED}));
    CHECK_THROWS_INVALID(kiln.createImage({4, 4, IMAGE_FORMAT_UNDEFINED, IMAGE_USAGE_SAMPLED}));
    CHECK_THROWS_INVALID(kiln.createImage({4, 4, static_cast<ImageFormat>(99), IMAGE_USAGE_SAMPLED}));
    CHECK_THROWS_INVALID(kiln.createImage({4, 4, IMAGE_FORMAT_RGBA8_UNORM, 0}));
    CHECK_THROWS_INVALID(kiln.createImage({4, 4, IMAGE_FORMAT_RGBA8_UNORM, 1u << 10}));
    CHECK_THROWS_INVALID(kiln.createImage({4, 4, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_COLOR_TARGET}));
    CHECK_THROWS_INVALID(kiln.createImage({4, 4, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_DEPTH_TARGET}));
    CHECK_THROWS_INVALID(kiln.createImage({1u << 30, 4, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED}));

    uint64_t image = kiln.createImage({4, 4, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    uint64_t depth = kiln.createImage({4, 4, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET});
    std::vector<uint8_t> pixels(64);
    CHECK_THROWS_INVALID(kiln.uploadImage(image, pixels.data(), 63));
    CHECK_THROWS_INVALID(kiln.uploadImage(image, nullptr, 64));
    CHECK_THROWS_INVALID(kiln.downloadImage(image, pixels.data(), 65));
    CHECK_THROWS_INVALID(kiln.uploadImage(depth, pixels.data(), 64));
    CHECK_THROWS_INVALID(kiln.downloadImage(depth, pixels.data(), 64));
    // Handles of one kind are never valid as another.
    CHECK_THROWS_INVALID(kiln.destroyBuffer(image));
    CHECK_THROWS_INVALID(kiln.destroyImage(buffer));

    CHECK_THROWS_INVALID(kiln.wait(1u << 30));

    // Still works.
    const uint32_t values[4] = {1, 2, 3, 4};
    kiln.uploadBuffer(buffer, values, sizeof(values));
    std::vector<uint32_t> result = download<uint32_t>(kiln, buffer, 4);
    CHECK_EQ(result[3], 4u);
    kiln.destroyBuffer(buffer);
    CHECK_THROWS_INVALID(kiln.destroyBuffer(buffer));
}

TEST(errors_programs)
{
    PixelKiln kiln;
    CHECK_THROWS_INVALID(kiln.loadComputeProgram({{storeSpirv, 3}, {}}));
    CHECK_THROWS_INVALID(kiln.loadComputeProgram({{nullptr, 64}, {}}));
    CHECK_THROWS_INVALID(kiln.loadComputeProgram({shaderFrom(storeSpirv), {static_cast<UniformBindingType>(77)}}));
    CHECK_THROWS_INVALID(kiln.unloadProgram(4242));

    RasterDrawProgram program{};
    program.vertexShader = shaderFrom(positionVertSpirv);
    program.fragmentShader = shaderFrom(solidFragSpirv);
    program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    program.vertexLayout.buffers = {{16}};
    program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program)); // no targets

    program.colorFormats = {IMAGE_FORMAT_D32_FLOAT};
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program));
    program.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    program.depthFormat = IMAGE_FORMAT_RGBA8_UNORM;
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program));
    program.depthFormat = IMAGE_FORMAT_UNDEFINED;
    program.vertexLayout.attributes = {{0, 3, VERTEX_FORMAT_FLOAT4, 0}};
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program));
    program.vertexLayout.attributes = {{0, 0, static_cast<VertexFormat>(99), 0}};
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program));
    program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    program.topology = static_cast<PrimitiveTopology>(42);
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program));
    program.topology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    program.cullMode = static_cast<CullMode>(42);
    CHECK_THROWS_INVALID(kiln.loadRasterDrawProgram(program));
    program.cullMode = CULL_MODE_BACK;
    uint64_t draw = kiln.loadRasterDrawProgram(program);
    kiln.unloadProgram(draw);
    CHECK_THROWS_INVALID(kiln.unloadProgram(draw));
}

TEST(errors_compute_calls)
{
    PixelKiln kiln;
    uint64_t program = kiln.loadComputeProgram({shaderFrom(storeSpirv),
                                                {UNIFORM_BINDING_TYPE_BUFFER, UNIFORM_BINDING_TYPE_STORAGE_BUFFER}});
    uint64_t readback = kiln.loadComputeProgram({shaderFrom(readbackSpirv), {UNIFORM_BINDING_TYPE_SAMPLER,
                                                                             UNIFORM_BINDING_TYPE_STORAGE_BUFFER,
                                                                             UNIFORM_BINDING_TYPE_BUFFER}});
    uint64_t output = kiln.createBuffer(64);
    uint64_t storageOnly = kiln.createImage({8, 8, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_STORAGE});
    uint32_t params[2] = {3, 42};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_COMPUTE;
    call.program = program;
    call.bindings = {{.data = params, .size = sizeof(params)}, {.resource = output}};

    auto broken = [&](auto change) {
        ProgramCall copy = call;
        change(copy);
        return copy;
    };
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.program = 9999; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.type = PROGRAM_TYPE_RASTER_DRAW; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.bindings.pop_back(); })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.bindings.push_back({}); })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.bindings[0].data = nullptr; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.bindings[0].size = 0; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.bindings[1].resource = 12345; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.bindings[1].resource = storageOnly; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.groupCountX = UINT32_MAX; })));

    // Sampling an image that wasn't created with IMAGE_USAGE_SAMPLED, or with an invalid sampler.
    uint32_t width = 8;
    ProgramCall sample{};
    sample.type = PROGRAM_TYPE_COMPUTE;
    sample.program = readback;
    sample.bindings = {{.resource = storageOnly}, {.resource = output}, {.data = &width, .size = sizeof(width)}};
    CHECK_THROWS_INVALID(kiln.call(sample));
    uint64_t sampled = kiln.createImage({8, 8, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    sample.bindings[0].resource = sampled;
    sample.bindings[0].sampler.filter = static_cast<SamplerFilter>(9);
    CHECK_THROWS_INVALID(kiln.call(sample));

    // Still works after all of that.
    kiln.call(call);
    CHECK_EQ(download<uint32_t>(kiln, output, 4)[3], 42u);
}

TEST(errors_raster_calls)
{
    PixelKiln kiln;
    RasterDrawProgram program{};
    program.vertexShader = shaderFrom(positionVertSpirv);
    program.fragmentShader = shaderFrom(solidFragSpirv);
    program.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    program.vertexLayout.buffers = {{16}};
    program.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT4, 0}};
    program.colorFormats = {IMAGE_FORMAT_RGBA8_UNORM};
    program.depthFormat = IMAGE_FORMAT_D32_FLOAT;
    uint64_t draw = kiln.loadRasterDrawProgram(program);

    const float positions[] = {-1.0f, -1.0f, 0.5f, 1.0f, 3.0f, -1.0f, 0.5f, 1.0f, -1.0f, 3.0f, 0.5f, 1.0f};
    uint64_t triangle = kiln.createBuffer(sizeof(positions));
    kiln.uploadBuffer(triangle, positions, sizeof(positions));
    uint64_t color = kiln.createImage({16, 16, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_SAMPLED});
    uint64_t otherSize = kiln.createImage({8, 8, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
    uint64_t otherFormat = kiln.createImage({16, 16, IMAGE_FORMAT_BGRA8_UNORM, IMAGE_USAGE_COLOR_TARGET});
    uint64_t notTarget = kiln.createImage({16, 16, IMAGE_FORMAT_RGBA8_UNORM, IMAGE_USAGE_SAMPLED});
    uint64_t depth = kiln.createImage({16, 16, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET});
    uint64_t smallDepth = kiln.createImage({8, 8, IMAGE_FORMAT_D32_FLOAT, IMAGE_USAGE_DEPTH_TARGET});

    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ProgramCall call{};
    call.type = PROGRAM_TYPE_RASTER_DRAW;
    call.program = draw;
    call.bindings = {{.data = white, .size = sizeof(white)}};
    call.colorTargets = {{color}};
    call.depthTarget = {depth};
    call.vertexBuffers = {triangle};
    call.vertexCount = 3;

    auto broken = [&](auto change) {
        ProgramCall copy = call;
        change(copy);
        return copy;
    };
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.type = PROGRAM_TYPE_COMPUTE; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.colorTargets.clear(); })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.colorTargets.push_back({color}); })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.colorTargets[0].image = otherSize; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.colorTargets[0].image = otherFormat; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.colorTargets[0].image = notTarget; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.colorTargets[0].image = depth; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.depthTarget.image = 0; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.depthTarget.image = smallDepth; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) { c.depthTarget.image = color; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.vertexBuffers.clear(); })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.vertexBuffers = {777}; })));
    CHECK_THROWS_INVALID(kiln.call(broken([](ProgramCall &c) { c.indexType = INDEX_TYPE_UINT16; })));
    CHECK_THROWS_INVALID(kiln.call(broken([&](ProgramCall &c) {
        c.indexType = static_cast<IndexType>(7);
        c.indexBuffer = triangle;
    })));

    // Still works.
    kiln.call(call);
    std::vector<uint8_t> pixels = downloadPixels(kiln, color, 16, 16);
    CHECK_EQ(pixelAt(pixels, 16, 8, 8), packRgba(255, 255, 255, 255));
}
