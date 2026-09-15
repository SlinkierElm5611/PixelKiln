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
};
#endif //PIXELKILN_COMPUTEPROGRAM_H
