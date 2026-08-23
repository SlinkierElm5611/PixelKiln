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
    vk::PhysicalDevice m_physicalDevice;
    vk::PhysicalDeviceProperties m_physicalDeviceProperties;
    vk::Device m_device;
    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
public:
    PixelKilnImpl(Config config);
    ~PixelKilnImpl();
};

#endif //PIXELKILN_PIXELKILNIMPL_H
