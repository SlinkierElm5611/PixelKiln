//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_RASTERDRAWPROGRAM_H
#define PIXELKILN_RASTERDRAWPROGRAM_H
#include <vector>

#include "cullMode.h"
#include "imageFormat.h"
#include "primitiveTopology.h"
#include "shader.h"
#include "uniformBindings.h"
#include "vertexLayout.h"

struct RasterDrawProgram {
    Shader vertexShader;
    Shader fragmentShader;
    UniformBindings uniformBindings; // visible to the vertex and fragment stages
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    CullMode cullMode = CULL_MODE_NONE;
    bool blendEnable = false; // src-alpha / one-minus-src-alpha on every color target
    std::vector<ImageFormat> colorFormats; // one per color target
    ImageFormat depthFormat = IMAGE_FORMAT_UNDEFINED; // UNDEFINED means no depth target
    bool depthTest = true; // compare op LESS, only used when depthFormat is set
    bool depthWrite = true;
    uint32_t samples = 1; // samples per pixel of every target (ImageDesc::samples), more than 1 for MSAA
    bool alphaToCoverage = false; // the alpha of color output 0 decides how many samples a fragment covers
};

#endif //PIXELKILN_RASTERDRAWPROGRAM_H
