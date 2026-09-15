//
// Created by Stefan Balta on 2026-07-12.
//

#include "pixelKilnImpl.h"

void PixelKilnImpl::createInstance()
{
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = m_config.applicationName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "PixelKiln";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    m_instance = vk::createInstance(createInfo);
}

void PixelKilnImpl::selectPhysicalDevice() {
    std::vector<vk::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
    for (const auto& device : devices) {
        // TODO add proper device selection logic later
        m_physicalDevice = device;
        m_physicalDeviceProperties = device.getProperties();
        break;
    }
}

void PixelKilnImpl::createDevice() {
    vk::DeviceQueueCreateInfo queueCreateInfo{};
    float queuePriority = 1.0f;
    queueCreateInfo.queueFamilyIndex = 0;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    m_device = m_physicalDevice.createDevice(deviceCreateInfo);
}

uint64_t PixelKilnImpl::loadComputeProgram(const ComputeProgram &program) {
    // TODO: implement compute pipeline creation and storage
    return m_pipelineIDCounter++;
}
PixelKilnImpl::PixelKilnImpl(Config config)
{
    m_config = config;
    createInstance();
    selectPhysicalDevice();
    createDevice();
}

PixelKilnImpl::~PixelKilnImpl()
{
    m_device.destroy();
    m_instance.destroy();
}
