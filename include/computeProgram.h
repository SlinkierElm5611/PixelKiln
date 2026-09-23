//
// Created by Stefan Balta on 2026-09-15.
//

#ifndef PIXELKILN_COMPUTEPROGRAM_H
#define PIXELKILN_COMPUTEPROGRAM_H
#include "shader.h"
#include "uniformBindings.h"
struct ComputeProgram {
    Shader computeShader;
    UniformBindings uniformBindings;
    uint32_t pushConstantSize = 0; // bytes visible to the shader as a push_constant block, 0 for none
};
#endif //PIXELKILN_COMPUTEPROGRAM_H
