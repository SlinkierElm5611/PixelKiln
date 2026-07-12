//
// Created by Stefan Balta on 2026-07-12.
//

#ifndef PIXELKILN_PIXELKILNIMPL_H
#define PIXELKILN_PIXELKILNIMPL_H
#include "config.h"
#include <vulkan/vulkan.hpp>

class PixelKilnImpl
{
private:
    Config m_config;
    vk::Instance m_instance;
    void createInstance();
public:
    PixelKilnImpl(Config config);
    ~PixelKilnImpl();
};

#endif //PIXELKILN_PIXELKILNIMPL_H
