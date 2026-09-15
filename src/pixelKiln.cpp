//
// Created by Stefan Balta on 2026-07-12.
//

#include "pixelKiln.h"
#include "pixelKilnImpl.h"

uint64_t PixelKiln::loadComputeProgram(const ComputeProgram &program) {
    return m_impl->loadComputeProgram(program);
}

PixelKiln::PixelKiln() : PixelKiln(Config())
{
}

PixelKiln::PixelKiln(Config config)
{
    m_impl = std::make_unique<PixelKilnImpl>(config);
}

PixelKiln::~PixelKiln()
{
}
