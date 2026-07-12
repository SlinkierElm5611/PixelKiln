//
// Created by Stefan Balta on 2026-07-12.
//

#include "pixelKiln.h"
#include "pixelKilnImpl.h"

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
