//
// Created by Stefan Balta on 2026-07-12.
//

#include "PixelKiln.h"
#include "PixelKilnImpl.h"

PixelKiln::PixelKiln()
{
    m_impl = std::make_unique<PixelKilnImpl>();
}

PixelKiln::~PixelKiln()
{
}
