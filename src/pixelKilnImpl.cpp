//
// Created by Stefan Balta on 2026-07-12.
//

#include "pixelKilnImpl.h"
#include "surface.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "vulkanTranslate.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                    VkDebugUtilsMessageTypeFlagsEXT,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    std::fprintf(stderr, "PixelKiln validation: %s\n", data->pMessage);
    return VK_FALSE;
}

void PixelKilnImpl::createInstance()
{
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = m_config.applicationName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "PixelKiln";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<vk::ExtensionProperties> availableExtensions = vk::enumerateInstanceExtensionProperties();
    auto hasExtension = [&](const char* name) {
        return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                           [&](const vk::ExtensionProperties &e) { return std::strcmp(e.extensionName.data(), name) == 0; });
    };
    std::vector<const char*> extensions;
    std::vector<const char*> layers;
    vk::InstanceCreateInfo createInfo{};
    // Needed to see portability drivers such as MoltenVK, not present (and not needed) on most Windows/Linux loaders.
    if (hasExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
    // Window surfaces, when the loader supports them. Headless machines simply don't get swapchains.
    if (hasExtension(VK_KHR_SURFACE_EXTENSION_NAME)) {
        for (const char* extension : surfaceInstanceExtensions()) {
            if (hasExtension(extension)) {
                extensions.push_back(extension);
            }
        }
        m_surfaceSupport = true;
    }
    bool debugUtils = false;
    if (m_config.enableValidation) {
        std::vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();
        bool hasValidation = std::any_of(availableLayers.begin(), availableLayers.end(), [](const vk::LayerProperties &l) {
            return std::strcmp(l.layerName.data(), "VK_LAYER_KHRONOS_validation") == 0;
        });
        if (hasValidation) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            std::fprintf(stderr, "PixelKiln: validation requested but VK_LAYER_KHRONOS_validation is not installed\n");
        }
        if (hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugUtils = true;
        }
    }
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    m_instance = vk::createInstance(createInfo);

    if (debugUtils) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            m_instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = debugCallback;
            create(m_instance, &messengerInfo, nullptr, &m_debugMessenger);
        }
    }
}

void PixelKilnImpl::selectPhysicalDevice() {
    std::vector<vk::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
    vk::PhysicalDeviceType preferredType = m_config.gpuType == INTEGRATED ? vk::PhysicalDeviceType::eIntegratedGpu
                                                                           : vk::PhysicalDeviceType::eDiscreteGpu;
    const vk::QueueFlags graphicsCompute = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
    int bestScore = -1;
    for (const auto& device : devices) {
        vk::PhysicalDeviceProperties properties = device.getProperties();
        if (properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }
        auto features = device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features,
                                            vk::PhysicalDeviceVulkan13Features>();
        if (!features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore ||
            !features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering ||
            !features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) {
            continue;
        }
        std::vector<vk::QueueFamilyProperties> families = device.getQueueFamilyProperties();
        bool hasAllFamily = std::any_of(families.begin(), families.end(), [&](const vk::QueueFamilyProperties &f) {
            return (f.queueFlags & graphicsCompute) == graphicsCompute && f.queueCount > 0;
        });
        if (!hasAllFamily) {
            continue;
        }
        int score = 0;
        if (properties.deviceType == preferredType) {
            score = 3;
        } else if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ||
                   properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
            score = 2;
        } else if (properties.deviceType == vk::PhysicalDeviceType::eVirtualGpu) {
            score = 1;
        }
        if (score > bestScore) {
            bestScore = score;
            m_physicalDevice = device;
            m_physicalDeviceProperties = properties;
            m_integerColorSampleCounts = device.getProperties2<vk::PhysicalDeviceProperties2,
                                                               vk::PhysicalDeviceVulkan12Properties>()
                                             .get<vk::PhysicalDeviceVulkan12Properties>()
                                             .framebufferIntegerColorSampleCounts;
        }
    }
    if (bestScore < 0) {
        throw std::runtime_error("PixelKiln: no Vulkan 1.3 device with timeline semaphores, dynamic rendering and "
                                 "synchronization2 found");
    }
    m_memoryProperties = m_physicalDevice.getMemoryProperties();
    for (int format = IMAGE_FORMAT_UNDEFINED + 1; format < IMAGE_FORMAT_COUNT; format++) {
        m_formatFeatures[format] =
            m_physicalDevice.getFormatProperties(toVkFormat(static_cast<ImageFormat>(format))).optimalTilingFeatures;
    }
}

void PixelKilnImpl::createDevice() {
    std::vector<vk::QueueFamilyProperties> families = m_physicalDevice.getQueueFamilyProperties();
    const vk::QueueFlags graphicsCompute = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
    const uint32_t familyCount = static_cast<uint32_t>(families.size());

    m_allFamily = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; i++) {
        if ((families[i].queueFlags & graphicsCompute) == graphicsCompute && families[i].queueCount > 0) {
            m_allFamily = i;
            break;
        }
    }

    // Transfer queue preference: dedicated transfer family (NVIDIA/AMD copy engines), compute only family,
    // a second queue in the all family, any other family, and finally the all queue itself.
    m_transferFamily = UINT32_MAX;
    uint32_t transferIndex = 0;
    for (uint32_t i = 0; i < familyCount && m_transferFamily == UINT32_MAX; i++) {
        vk::QueueFlags flags = families[i].queueFlags;
        if ((flags & vk::QueueFlagBits::eTransfer) && !(flags & graphicsCompute) && families[i].queueCount > 0) {
            m_transferFamily = i;
        }
    }
    for (uint32_t i = 0; i < familyCount && m_transferFamily == UINT32_MAX; i++) {
        vk::QueueFlags flags = families[i].queueFlags;
        if ((flags & vk::QueueFlagBits::eCompute) && !(flags & vk::QueueFlagBits::eGraphics) && families[i].queueCount > 0) {
            m_transferFamily = i;
        }
    }
    if (m_transferFamily == UINT32_MAX && families[m_allFamily].queueCount >= 2) {
        m_transferFamily = m_allFamily;
        transferIndex = 1;
    }
    for (uint32_t i = 0; i < familyCount && m_transferFamily == UINT32_MAX; i++) {
        if (i != m_allFamily && (families[i].queueFlags & graphicsCompute) && families[i].queueCount > 0) {
            m_transferFamily = i;
        }
    }
    if (m_transferFamily == UINT32_MAX) {
        // Only one queue: hold two references to it and keep treating them as separate queues.
        m_transferFamily = m_allFamily;
    }

    float queuePriorities[2] = {1.0f, 1.0f};
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.queueFamilyIndex = m_allFamily;
    queueCreateInfo.queueCount = m_transferFamily == m_allFamily ? transferIndex + 1 : 1;
    queueCreateInfo.pQueuePriorities = queuePriorities;
    queueCreateInfos.push_back(queueCreateInfo);
    if (m_transferFamily != m_allFamily) {
        queueCreateInfo.queueFamilyIndex = m_transferFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    std::vector<const char*> extensions;
    std::vector<vk::ExtensionProperties> availableExtensions = m_physicalDevice.enumerateDeviceExtensionProperties();
    for (const auto& extension : availableExtensions) {
        // Must be enabled whenever the device exposes it (MoltenVK).
        if (std::strcmp(extension.extensionName.data(), "VK_KHR_portability_subset") == 0) {
            extensions.push_back("VK_KHR_portability_subset");
        }
        if (m_surfaceSupport && std::strcmp(extension.extensionName.data(), VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            m_swapchainSupport = true;
        }
        if (std::strcmp(extension.extensionName.data(), VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0) {
            extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
            auto properties = m_physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                                              vk::PhysicalDevicePushDescriptorPropertiesKHR>();
            m_maxPushDescriptors = properties.get<vk::PhysicalDevicePushDescriptorPropertiesKHR>().maxPushDescriptors;
        }
    }

    vk::PhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    vk::PhysicalDeviceVulkan12Features features12{};
    features12.timelineSemaphore = VK_TRUE;
    features12.pNext = &features13;

    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.pNext = &features12;
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = extensions.data();
    m_device = m_physicalDevice.createDevice(deviceCreateInfo);
    if (m_maxPushDescriptors > 0) {
        // Extension commands aren't exported by the loader.
        m_cmdPushDescriptorSet = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
            m_device.getProcAddr("vkCmdPushDescriptorSetKHR"));
        if (!m_cmdPushDescriptorSet) {
            m_maxPushDescriptors = 0;
        }
    }

    m_allQueue = m_device.getQueue(m_allFamily, 0);
    m_transferQueue = m_device.getQueue(m_transferFamily, transferIndex);
}

void PixelKilnImpl::createSyncObjects() {
    vk::SemaphoreTypeCreateInfo semaphoreType{};
    semaphoreType.semaphoreType = vk::SemaphoreType::eTimeline;
    semaphoreType.initialValue = 0;
    vk::SemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.pNext = &semaphoreType;
    m_allTimeline = m_device.createSemaphore(semaphoreInfo);
    m_transferTimeline = m_device.createSemaphore(semaphoreInfo);

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient;
    poolInfo.queueFamilyIndex = m_allFamily;
    m_allCommandPool = m_device.createCommandPool(poolInfo);
    poolInfo.queueFamilyIndex = m_transferFamily;
    m_transferCommandPool = m_device.createCommandPool(poolInfo);
}

uint64_t PixelKilnImpl::completedValue(QueueKind queue) {
    if (queue == QUEUE_TRANSFER) {
        return m_device.getSemaphoreCounterValue(m_transferTimeline);
    }
    uint64_t completed = m_device.getSemaphoreCounterValue(m_allTimeline);
    while (!m_allSignals.empty() && m_allSignals.front() <= completed) {
        m_allRetiredSignal = m_allSignals.front();
        m_allSignals.pop_front();
    }
    return completed;
}

uint64_t PixelKilnImpl::submittedValue(uint64_t allValue) {
    if (allValue == 0) {
        return 0;
    }
    if (allValue > m_allSubmitted) {
        flushBatch();
    }
    // A signal also covers every earlier submission on the queue, so any signal >= the ticket will do.
    auto signal = std::lower_bound(m_allSignals.begin(), m_allSignals.end(), allValue);
    if (signal != m_allSignals.end()) {
        return *signal;
    }
    if (m_allRetiredSignal < allValue) {
        throw std::logic_error("PixelKiln: waiting for a ticket that was never submitted");
    }
    return m_allRetiredSignal;
}

void PixelKilnImpl::waitValue(QueueKind queue, uint64_t value) {
    if (queue == QUEUE_ALL) {
        value = submittedValue(value);
    }
    if (value == 0) {
        return;
    }
    vk::Semaphore semaphore = queue == QUEUE_ALL ? m_allTimeline : m_transferTimeline;
    vk::SemaphoreWaitInfo waitInfo{};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore;
    waitInfo.pValues = &value;
    if (m_device.waitSemaphores(waitInfo, UINT64_MAX) != vk::Result::eSuccess) {
        throw std::runtime_error("PixelKiln: waiting for the GPU failed");
    }
}

vk::CommandBuffer PixelKilnImpl::beginCommands(QueueKind queue) {
    std::vector<CommandBuffer> &commandBuffers = queue == QUEUE_ALL ? m_allCommandBuffers : m_transferCommandBuffers;
    uint64_t completed = completedValue(queue);
    CommandBuffer* free = nullptr;
    for (auto& commandBuffer : commandBuffers) {
        if (commandBuffer.value <= completed) {
            free = &commandBuffer;
            break;
        }
    }
    if (!free) {
        vk::CommandBufferAllocateInfo allocateInfo{};
        allocateInfo.commandPool = queue == QUEUE_ALL ? m_allCommandPool : m_transferCommandPool;
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = 1;
        commandBuffers.push_back({m_device.allocateCommandBuffers(allocateInfo)[0], 0});
        free = &commandBuffers.back();
    }
    free->value = UINT64_MAX;
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    free->commandBuffer.begin(beginInfo); // implicitly resets it
    return free->commandBuffer;
}

uint64_t PixelKilnImpl::submitCommands(QueueKind queue, vk::CommandBuffer commandBuffer, uint64_t waitValue,
                                      const std::vector<vk::Semaphore> &binaryWaits, vk::Semaphore binarySignal) {
    commandBuffer.end();
    const bool transfer = queue == QUEUE_TRANSFER;
    // The transfer queue may be a transfer-only family, so its semaphore stages stay within transfer. This also keeps
    // its signal from waiting on draws/dispatches when both queue references point to the same queue.
    const vk::PipelineStageFlags2 stages = transfer ? vk::PipelineStageFlagBits2::eAllTransfer
                                                    : vk::PipelineStageFlagBits2::eAllCommands;
    const uint64_t value = transfer ? m_transferValue + 1 : m_allValue;

    std::vector<vk::SemaphoreSubmitInfo> waitInfos;
    if (waitValue > 0) {
        vk::SemaphoreSubmitInfo waitInfo{};
        waitInfo.semaphore = transfer ? m_allTimeline : m_transferTimeline;
        waitInfo.value = waitValue;
        waitInfo.stageMask = stages;
        waitInfos.push_back(waitInfo);
    }
    for (vk::Semaphore semaphore : binaryWaits) {
        vk::SemaphoreSubmitInfo waitInfo{};
        waitInfo.semaphore = semaphore;
        waitInfo.stageMask = stages;
        waitInfos.push_back(waitInfo);
    }
    std::vector<vk::SemaphoreSubmitInfo> signalInfos(1);
    signalInfos[0].semaphore = transfer ? m_transferTimeline : m_allTimeline;
    signalInfos[0].value = value;
    signalInfos[0].stageMask = stages;
    if (binarySignal) {
        vk::SemaphoreSubmitInfo signalInfo{};
        signalInfo.semaphore = binarySignal;
        signalInfo.stageMask = stages;
        signalInfos.push_back(signalInfo);
    }
    vk::CommandBufferSubmitInfo commandBufferInfo{};
    commandBufferInfo.commandBuffer = commandBuffer;

    vk::SubmitInfo2 submitInfo{};
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferInfo;
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();
    (transfer ? m_transferQueue : m_allQueue).submit2(submitInfo);
    if (transfer) {
        m_transferValue = value;
    } else {
        m_allSubmitted = value;
        m_allSignals.push_back(value);
    }

    std::vector<CommandBuffer> &commandBuffers = transfer ? m_transferCommandBuffers : m_allCommandBuffers;
    for (auto& entry : commandBuffers) {
        if (entry.commandBuffer == commandBuffer) {
            entry.value = value;
        }
    }
    return value;
}

vk::CommandBuffer PixelKilnImpl::batchCommands() {
    if (!m_batch.commandBuffer) {
        m_batch.commandBuffer = beginCommands(QUEUE_ALL);
    }
    return m_batch.commandBuffer;
}

void PixelKilnImpl::flushBatch(vk::Semaphore binarySignal) {
    if (!m_batch.commandBuffer && !binarySignal) {
        return;
    }
    vk::CommandBuffer commandBuffer = batchCommands();
    Batch batch = std::move(m_batch);
    m_batch = {};
    if (m_allValue == m_allSubmitted) {
        m_allValue++; // nothing took a ticket (a call threw after opening the batch), the signal still needs a value
    }
    submitCommands(QUEUE_ALL, commandBuffer, batch.waitTransfer, batch.acquireWaits, binarySignal);
}

void PixelKilnImpl::deferDestroy(uint64_t allValue, uint64_t transferValue, std::function<void()> destroy) {
    m_pendingDestroys.push_back({allValue, transferValue, std::move(destroy)});
}

void PixelKilnImpl::collectGarbage() {
    if (m_pendingDestroys.empty()) {
        return;
    }
    uint64_t completedAll = completedValue(QUEUE_ALL);
    uint64_t completedTransfer = completedValue(QUEUE_TRANSFER);
    std::vector<PendingDestroy> remaining;
    for (auto& pending : m_pendingDestroys) {
        if (pending.allValue <= completedAll && pending.transferValue <= completedTransfer) {
            pending.destroy();
        } else {
            remaining.push_back(std::move(pending));
        }
    }
    m_pendingDestroys.swap(remaining);
}

void PixelKilnImpl::flush() {
    flushBatch();
    collectGarbage();
}

bool PixelKilnImpl::isComplete(uint64_t ticket) {
    // A pending ticket is submitted now, otherwise polling it would never see it complete.
    if (ticket <= m_allValue) {
        submittedValue(ticket);
    }
    return completedValue(QUEUE_ALL) >= ticket;
}

void PixelKilnImpl::wait(uint64_t ticket) {
    if (ticket > m_allValue) {
        throw std::invalid_argument("PixelKiln: wait on a ticket that was never returned by call()");
    }
    waitValue(QUEUE_ALL, ticket);
    collectGarbage();
}

void PixelKilnImpl::waitIdle() {
    waitValue(QUEUE_ALL, m_allValue);
    waitValue(QUEUE_TRANSFER, m_transferValue);
    collectGarbage();
}

void PixelKilnImpl::destroyAll() {
    if (m_device) {
        if (m_batch.commandBuffer) {
            try {
                flushBatch(); // may hold swapchain acquire waits, which must not stay pending
            } catch (...) {
            }
        }
        for (auto& [handle, swapchain] : m_swapchains) {
            teardownSwapchain(swapchain);
        }
        m_swapchains.clear();
        m_device.waitIdle();
        for (auto& pending : m_pendingDestroys) {
            pending.destroy();
        }
        m_pendingDestroys.clear();
        for (auto& [handle, program] : m_programs) {
            m_device.destroyPipeline(program.pipeline);
            m_device.destroyPipelineLayout(program.pipelineLayout);
            m_device.destroyDescriptorSetLayout(program.setLayout);
        }
        m_programs.clear();
        for (auto& [handle, buffer] : m_buffers) {
            m_device.destroyBuffer(buffer.buffer);
            m_device.freeMemory(buffer.memory);
        }
        m_buffers.clear();
        for (auto& [handle, image] : m_images) {
            m_device.destroyImageView(image.view);
            m_device.destroyImage(image.image);
            m_device.freeMemory(image.memory);
        }
        m_images.clear();
        for (auto& [key, sampler] : m_samplers) {
            m_device.destroySampler(sampler);
        }
        m_samplers.clear();
        m_device.destroyDescriptorPool(m_descriptorPool.pool);
        for (auto& pool : m_retiredDescriptorPools) {
            m_device.destroyDescriptorPool(pool.pool);
        }
        m_retiredDescriptorPools.clear();
        destroyStagingBuffer(m_uniformRing.staging);
        destroyStagingBuffer(m_uploadRing.staging);
        destroyStagingBuffer(m_readback);
        m_device.destroyCommandPool(m_allCommandPool);
        m_device.destroyCommandPool(m_transferCommandPool);
        m_device.destroySemaphore(m_allTimeline);
        m_device.destroySemaphore(m_transferTimeline);
        m_device.destroy();
        m_device = nullptr;
    }
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            m_instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) {
            destroy(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance) {
        m_instance.destroy();
        m_instance = nullptr;
    }
}

PixelKilnImpl::PixelKilnImpl(Config config)
{
    m_config = config;
    try {
        createInstance();
        selectPhysicalDevice();
        createDevice();
        createSyncObjects();
        createUniformRing();
    } catch (...) {
        destroyAll();
        throw;
    }
}

PixelKilnImpl::~PixelKilnImpl()
{
    destroyAll();
}
