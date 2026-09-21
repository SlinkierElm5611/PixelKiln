//
// Created by Stefan Balta on 2026-08-23.
//

#ifndef PIXELKILN_SHADER_H
#define PIXELKILN_SHADER_H
#include <cstdint>

struct Shader {
    const uint32_t* spirv;
    uint32_t spirvSize; // in bytes
};

#endif //PIXELKILN_SHADER_H
