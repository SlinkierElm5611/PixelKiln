//
// Created by Stefan Balta on 2026-09-22.
//

// A spinning trefoil knot, drawn with MSAA into a multisampled color and depth target that is resolved straight into
// the window. The multisampled targets are never stored, only the resolved image is.
//   M      toggle MSAA to compare the edges
//   Space  pause

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "exampleWindow.h"

static const uint32_t knotVertSpirv[] =
#include "knot.vert.h"
;
static const uint32_t knotFragSpirv[] =
#include "knot.frag.h"
;

struct Vertex {
    float position[3];
    float normal[3];
    float along;
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // column major
};

static Mat4 operator*(const Mat4 &a, const Mat4 &b)
{
    Mat4 result;
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
            result.m[column * 4 + row] = sum;
        }
    }
    return result;
}

static Mat4 rotation(float angle, int axis)
{
    Mat4 result;
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    result.m[a * 4 + a] = std::cos(angle);
    result.m[a * 4 + b] = std::sin(angle);
    result.m[b * 4 + a] = -std::sin(angle);
    result.m[b * 4 + b] = std::cos(angle);
    return result;
}

// Right handed view space looking down -z to Vulkan clip space: y down, depth 0 at the near plane and 1 at the far.
static Mat4 perspective(float fovY, float aspect, float nearPlane, float farPlane)
{
    const float f = 1.0f / std::tan(fovY / 2.0f);
    Mat4 result;
    result.m[0] = f / aspect;
    result.m[5] = -f;
    result.m[10] = farPlane / (nearPlane - farPlane);
    result.m[11] = -1.0f;
    result.m[14] = nearPlane * farPlane / (nearPlane - farPlane);
    result.m[15] = 0.0f;
    return result;
}

struct KnotParams {
    Mat4 model;
    Mat4 viewProjection;
    float light[4];
    float eye[4];
};

static void knotPoint(float t, float point[3])
{
    const float radius = 2.0f + std::cos(3.0f * t);
    point[0] = radius * std::cos(2.0f * t);
    point[1] = radius * std::sin(2.0f * t);
    point[2] = -std::sin(3.0f * t);
}

// A tube around the (2, 3) torus knot. Its tangent is never vertical, so the frame around it can be built from the
// z axis without twisting. The last ring repeats the first one with along = 1, so the colors don't wrap around
// within a single segment.
static void buildKnot(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
{
    const uint32_t segments = 720, sides = 24;
    const float tubeRadius = 0.42f;
    for (uint32_t i = 0; i <= segments; i++) {
        const float t = 6.2831853f * float(i) / float(segments);
        float center[3], ahead[3], behind[3];
        knotPoint(t, center);
        knotPoint(t + 0.001f, ahead);
        knotPoint(t - 0.001f, behind);
        float tangent[3] = {ahead[0] - behind[0], ahead[1] - behind[1], ahead[2] - behind[2]};
        float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
        for (float &c : tangent) {
            c /= length;
        }
        float normal[3] = {tangent[1], -tangent[0], 0.0f}; // tangent x z
        length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1]);
        normal[0] /= length;
        normal[1] /= length;
        const float binormal[3] = {normal[1] * tangent[2] - normal[2] * tangent[1],
                                   normal[2] * tangent[0] - normal[0] * tangent[2],
                                   normal[0] * tangent[1] - normal[1] * tangent[0]};
        for (uint32_t j = 0; j < sides; j++) {
            const float angle = 6.2831853f * float(j) / float(sides);
            Vertex vertex{};
            for (int c = 0; c < 3; c++) {
                vertex.normal[c] = std::cos(angle) * normal[c] + std::sin(angle) * binormal[c];
                vertex.position[c] = center[c] + tubeRadius * vertex.normal[c];
            }
            vertex.along = float(i) / float(segments);
            vertices.push_back(vertex);
        }
    }
    for (uint32_t i = 0; i < segments; i++) {
        for (uint32_t j = 0; j < sides; j++) {
            const uint32_t a = i * sides + j, b = i * sides + (j + 1) % sides;
            const uint32_t c = (i + 1) * sides + j, d = (i + 1) * sides + (j + 1) % sides;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

int main(int argc, char** argv)
{
    PixelKiln kiln;
    ExampleWindow window(kiln, "PixelKiln - Torus Knot", 1100, 800, argc, argv);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    buildKnot(vertices, indices);
    uint64_t vertexBuffer = kiln.createBuffer(vertices.size() * sizeof(Vertex));
    uint64_t indexBuffer = kiln.createBuffer(indices.size() * sizeof(uint32_t));
    kiln.uploadBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(Vertex));
    kiln.uploadBuffer(indexBuffer, indices.data(), indices.size() * sizeof(uint32_t));

    // The most samples up to 8 that both the window's format and the depth format support.
    const ImageFormat depthFormat = IMAGE_FORMAT_D32_FLOAT;
    const uint32_t counts = kiln.getSupportedSampleCounts(window.format()) & kiln.getSupportedSampleCounts(depthFormat);
    uint32_t samples = 1;
    for (uint32_t count : {8u, 4u, 2u}) {
        if (counts & count) {
            samples = count;
            break;
        }
    }

    RasterDrawProgram knotProgram{};
    knotProgram.vertexShader = {knotVertSpirv, sizeof(knotVertSpirv)};
    knotProgram.fragmentShader = {knotFragSpirv, sizeof(knotFragSpirv)};
    knotProgram.uniformBindings = {UNIFORM_BINDING_TYPE_BUFFER};
    knotProgram.vertexLayout.buffers = {{sizeof(Vertex)}};
    knotProgram.vertexLayout.attributes = {{0, 0, VERTEX_FORMAT_FLOAT3, 0},
                                           {1, 0, VERTEX_FORMAT_FLOAT3, 3 * sizeof(float)},
                                           {2, 0, VERTEX_FORMAT_FLOAT, 6 * sizeof(float)}};
    knotProgram.colorFormats = {window.format()};
    knotProgram.depthFormat = depthFormat;
    uint64_t aliasedKnot = kiln.loadRasterDrawProgram(knotProgram);
    knotProgram.samples = samples;
    uint64_t smoothKnot = samples > 1 ? kiln.loadRasterDrawProgram(knotProgram) : aliasedKnot;

    bool msaa = samples > 1;
    bool paused = false;
    float angle = 0.0f;
    std::printf("MSAA: %ux (M to toggle)\n", msaa ? samples : 1);

    // Targets follow the window size: a multisampled color + depth pair, and a single sample depth image for when
    // MSAA is off and the knot is drawn straight into the window.
    uint64_t color = 0, depth = 0, aliasedDepth = 0;
    uint32_t targetWidth = 0, targetHeight = 0;
    while (window.nextFrame()) {
        if (window.keyPressed(GLFW_KEY_M) && samples > 1) {
            msaa = !msaa;
            std::printf("MSAA: %ux\n", msaa ? samples : 1);
        }
        if (window.keyPressed(GLFW_KEY_SPACE)) {
            paused = !paused;
        }
        uint64_t image = window.acquire();
        if (image == 0) {
            continue;
        }
        if (window.width() != targetWidth || window.height() != targetHeight) {
            for (uint64_t target : {color, depth, aliasedDepth}) {
                if (target) {
                    kiln.destroyImage(target);
                }
            }
            targetWidth = window.width();
            targetHeight = window.height();
            color = kiln.createImage({targetWidth, targetHeight, window.format(), IMAGE_USAGE_COLOR_TARGET, samples});
            depth = kiln.createImage({targetWidth, targetHeight, depthFormat, IMAGE_USAGE_DEPTH_TARGET, samples});
            aliasedDepth = kiln.createImage({targetWidth, targetHeight, depthFormat, IMAGE_USAGE_DEPTH_TARGET});
        }
        if (!paused) {
            angle += window.deltaTime();
        }

        KnotParams params{};
        params.model = rotation(angle * 0.31f, 0) * rotation(angle * 0.53f, 1) * rotation(angle * 0.17f, 2);
        Mat4 view;
        view.m[14] = -9.5f;
        params.viewProjection = perspective(0.75f, window.aspect(), 0.5f, 30.0f) * view;
        const float light[3] = {0.4f, 0.75f, 0.55f};
        const float lightLength = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
        for (int c = 0; c < 3; c++) {
            params.light[c] = light[c] / lightLength;
        }
        params.light[3] = angle * 0.05f;
        params.eye[2] = 9.5f;

        const float background[4] = {0.03f, 0.035f, 0.05f, 1.0f};
        ProgramCall call{};
        call.type = PROGRAM_TYPE_RASTER_DRAW;
        call.bindings = {{.data = &params, .size = sizeof(params)}};
        call.vertexBuffers = {vertexBuffer};
        call.indexBuffer = indexBuffer;
        call.indexType = INDEX_TYPE_UINT32;
        call.indexCount = uint32_t(indices.size());
        if (msaa) {
            // Resolved into the window at the end of the call; the samples themselves are never written to memory.
            call.program = smoothKnot;
            call.colorTargets = {{.image = color, .clearColor = {background[0], background[1], background[2], 1.0f},
                                  .resolveImage = image, .store = false}};
            call.depthTarget = {.image = depth, .store = false};
        } else {
            call.program = aliasedKnot;
            call.colorTargets = {{.image = image, .clearColor = {background[0], background[1], background[2], 1.0f}}};
            call.depthTarget = {.image = aliasedDepth, .store = false};
        }
        kiln.call(call);

        window.present();
    }
    return 0;
}
