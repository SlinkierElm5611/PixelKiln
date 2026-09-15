//
// Created by Stefan Balta on 2026-07-12.
//

#ifndef PIXELKILN_PIXELKILN_H
#define PIXELKILN_PIXELKILN_H

#include <memory>

#include "computeProgram.h"
#include "config.h"

class PixelKilnImpl;

class PixelKiln
{
private:
    std::unique_ptr<PixelKilnImpl> m_impl;
public:
    uint64_t loadComputeProgram(const ComputeProgram &program);
    PixelKiln();
    PixelKiln(Config config);
    ~PixelKiln();
};


#endif //PIXELKILN_PIXELKILN_H
