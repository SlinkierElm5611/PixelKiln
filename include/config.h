//
// Created by Stefan Balta on 2026-07-12.
//

#ifndef PIXELKILN_CONFIG_H
#define PIXELKILN_CONFIG_H
#include <string>

#include "gpuType.h"

struct Config
{
    std::string applicationName = "PixelKiln";
    GpuType gpuType = DEDICATED;
    bool enableValidation = false; // enables the Khronos validation layer if installed, messages go to stderr
};

#endif //PIXELKILN_CONFIG_H
