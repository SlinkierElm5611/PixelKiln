//
// Created by Stefan Balta on 2026-09-21.
//

#include "pixelKilnImpl.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "vulkanTranslate.h"

// Orders this transfer submission after earlier transfer submissions (they may run concurrently on the same queue),
// e.g. two uploads into the same buffer, or an upload after a download of it. Transfer stages only, so it is legal on
// transfer-only queue families.
static void transferBarrier(vk::CommandBuffer commandBuffer)
{
    vk::MemoryBarrier2 barrier{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllTransfer;
    barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
    vk::DependencyInfo dependency{};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    commandBuffer.pipelineBarrier2(dependency);
}

// Makes a transfer write to a readback buffer visible to the host once the submission has completed.
static void hostBarrier(vk::CommandBuffer commandBuffer)
{
    vk::MemoryBarrier2 barrier{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eHost;
    barrier.dstAccessMask = vk::AccessFlagBits2::eHostRead;
    vk::DependencyInfo dependency{};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    commandBuffer.pipelineBarrier2(dependency);
}

// Layout transition on the transfer queue, transfer stages only. Earlier use on the all queue is covered by the
// submission's wait on the all timeline, which waits at the same transfer stages.
static void transferImageBarrier(vk::CommandBuffer commandBuffer, vk::Image image, vk::ImageAspectFlags aspect,
                                 vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 dstAccess)
{
    vk::ImageMemoryBarrier2 barrier{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllTransfer;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = vk::ImageSubresourceRange(aspect, 0, 1, 0, 1);
    vk::DependencyInfo dependency{};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    commandBuffer.pipelineBarrier2(dependency);
}

uint32_t PixelKilnImpl::findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags required,
                                       vk::MemoryPropertyFlags preferred) {
    for (vk::MemoryPropertyFlags flags : {required | preferred, required}) {
        for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
            if ((typeBits & (1u << i)) && (m_memoryProperties.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
    }
    throw std::runtime_error("PixelKiln: no suitable memory type");
}

vk::DeviceMemory PixelKilnImpl::allocateMemory(vk::MemoryRequirements requirements, vk::MemoryPropertyFlags required,
                                               vk::MemoryPropertyFlags preferred, bool* coherent) {
    uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, required, preferred);
    if (coherent) {
        *coherent = static_cast<bool>(m_memoryProperties.memoryTypes[memoryType].propertyFlags &
                                      vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    vk::MemoryAllocateInfo allocateInfo{};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    return m_device.allocateMemory(allocateInfo);
}

// Resources used by both queues are shared concurrently when the queues are in different families, so no queue
// family ownership transfers are needed.
void PixelKilnImpl::applySharingMode(vk::BufferCreateInfo &info, uint32_t* families) {
    if (m_allFamily != m_transferFamily) {
        families[0] = m_allFamily;
        families[1] = m_transferFamily;
        info.sharingMode = vk::SharingMode::eConcurrent;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    } else {
        info.sharingMode = vk::SharingMode::eExclusive;
    }
}

void PixelKilnImpl::applySharingMode(vk::ImageCreateInfo &info, uint32_t* families) {
    if (m_allFamily != m_transferFamily) {
        families[0] = m_allFamily;
        families[1] = m_transferFamily;
        info.sharingMode = vk::SharingMode::eConcurrent;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    } else {
        info.sharingMode = vk::SharingMode::eExclusive;
    }
}

PixelKilnImpl::Buffer PixelKilnImpl::createDeviceBuffer(uint64_t size, vk::BufferUsageFlags usage) {
    Buffer buffer;
    buffer.size = size;
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    uint32_t families[2];
    applySharingMode(bufferInfo, families);
    buffer.buffer = m_device.createBuffer(bufferInfo);
    try {
        buffer.memory = allocateMemory(m_device.getBufferMemoryRequirements(buffer.buffer), {},
                                       vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_device.bindBufferMemory(buffer.buffer, buffer.memory, 0);
    } catch (...) {
        m_device.destroyBuffer(buffer.buffer);
        m_device.freeMemory(buffer.memory);
        throw;
    }
    return buffer;
}

// Staging buffers are only touched by the transfer queue. Uploads use coherent memory, readbacks prefer cached
// memory and are invalidated before reading when it isn't coherent (common on discrete GPUs).
PixelKilnImpl::StagingBuffer PixelKilnImpl::createStagingBuffer(uint64_t size, bool readback) {
    StagingBuffer staging;
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = readback ? vk::BufferUsageFlagBits::eTransferDst : vk::BufferUsageFlagBits::eTransferSrc;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    staging.buffer = m_device.createBuffer(bufferInfo);
    try {
        vk::MemoryRequirements requirements = m_device.getBufferMemoryRequirements(staging.buffer);
        if (readback) {
            staging.memory = allocateMemory(requirements, vk::MemoryPropertyFlagBits::eHostVisible,
                                            vk::MemoryPropertyFlagBits::eHostCached, &staging.coherent);
        } else {
            staging.memory = allocateMemory(requirements, vk::MemoryPropertyFlagBits::eHostVisible |
                                                          vk::MemoryPropertyFlagBits::eHostCoherent, {});
        }
        m_device.bindBufferMemory(staging.buffer, staging.memory, 0);
        staging.mapped = m_device.mapMemory(staging.memory, 0, VK_WHOLE_SIZE);
    } catch (...) {
        destroyStagingBuffer(staging);
        throw;
    }
    return staging;
}

void PixelKilnImpl::destroyStagingBuffer(const StagingBuffer &staging) {
    m_device.destroyBuffer(staging.buffer);
    m_device.freeMemory(staging.memory); // implicitly unmaps
}

void PixelKilnImpl::readStagingBuffer(const StagingBuffer &staging, void* data, uint64_t size) {
    if (!staging.coherent) {
        vk::MappedMemoryRange range{};
        range.memory = staging.memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        m_device.invalidateMappedMemoryRanges(range);
    }
    std::memcpy(data, staging.mapped, size);
}

PixelKilnImpl::Buffer &PixelKilnImpl::getBuffer(uint64_t buffer) {
    auto it = m_buffers.find(buffer);
    if (it == m_buffers.end()) {
        throw std::invalid_argument("PixelKiln: unknown buffer handle");
    }
    return it->second;
}

PixelKilnImpl::Image &PixelKilnImpl::getImage(uint64_t image) {
    auto it = m_images.find(image);
    if (it == m_images.end()) {
        throw std::invalid_argument("PixelKiln: unknown image handle");
    }
    return it->second;
}

vk::FormatFeatureFlags PixelKilnImpl::formatFeatures(ImageFormat format) {
    return m_physicalDevice.getFormatProperties(toVkFormat(format)).optimalTilingFeatures;
}

uint64_t PixelKilnImpl::createBuffer(uint64_t size) {
    collectGarbage();
    if (size == 0) {
        throw std::invalid_argument("PixelKiln: buffer size must be greater than 0");
    }
    Buffer buffer = createDeviceBuffer(size, vk::BufferUsageFlagBits::eVertexBuffer |
                                             vk::BufferUsageFlagBits::eIndexBuffer |
                                             vk::BufferUsageFlagBits::eStorageBuffer |
                                             vk::BufferUsageFlagBits::eTransferSrc |
                                             vk::BufferUsageFlagBits::eTransferDst);
    uint64_t handle = m_nextHandle++;
    m_buffers[handle] = buffer;
    return handle;
}

void PixelKilnImpl::destroyBuffer(uint64_t buffer) {
    collectGarbage();
    Buffer destroyed = getBuffer(buffer);
    m_buffers.erase(buffer);
    vk::Device device = m_device;
    deferDestroy(destroyed.lastAllUse, destroyed.lastTransferUse, [device, destroyed]() {
        device.destroyBuffer(destroyed.buffer);
        device.freeMemory(destroyed.memory);
    });
}

void PixelKilnImpl::uploadBuffer(uint64_t buffer, const void* data, uint64_t size, uint64_t offset) {
    collectGarbage();
    Buffer &target = getBuffer(buffer);
    if (!data || size == 0) {
        throw std::invalid_argument("PixelKiln: uploadBuffer needs data and a size");
    }
    if (offset > target.size || size > target.size - offset) {
        throw std::invalid_argument("PixelKiln: uploadBuffer range is outside the buffer");
    }
    StagingBuffer staging = createStagingBuffer(size, false);
    std::memcpy(staging.mapped, data, size);
    uint64_t value;
    try {
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        transferBarrier(commandBuffer);
        vk::BufferCopy region{0, offset, size};
        commandBuffer.copyBuffer(staging.buffer, target.buffer, region);
        // Waits for the all queue to be done with the buffer (e.g. the call reading its previous contents).
        value = submitCommands(QUEUE_TRANSFER, commandBuffer, target.lastAllUse);
    } catch (...) {
        destroyStagingBuffer(staging);
        throw;
    }
    target.lastTransferUse = value;
    vk::Device device = m_device;
    deferDestroy(0, value, [device, staging]() {
        device.destroyBuffer(staging.buffer);
        device.freeMemory(staging.memory);
    });
}

void PixelKilnImpl::downloadBuffer(uint64_t buffer, void* data, uint64_t size, uint64_t offset) {
    collectGarbage();
    Buffer &source = getBuffer(buffer);
    if (!data || size == 0) {
        throw std::invalid_argument("PixelKiln: downloadBuffer needs data and a size");
    }
    if (offset > source.size || size > source.size - offset) {
        throw std::invalid_argument("PixelKiln: downloadBuffer range is outside the buffer");
    }
    StagingBuffer staging = createStagingBuffer(size, true);
    uint64_t value;
    try {
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        transferBarrier(commandBuffer);
        vk::BufferCopy region{offset, 0, size};
        commandBuffer.copyBuffer(source.buffer, staging.buffer, region);
        hostBarrier(commandBuffer);
        value = submitCommands(QUEUE_TRANSFER, commandBuffer, source.lastAllUse);
    } catch (...) {
        destroyStagingBuffer(staging);
        throw;
    }
    source.lastTransferUse = value;
    waitValue(QUEUE_TRANSFER, value);
    readStagingBuffer(staging, data, size);
    destroyStagingBuffer(staging);
}

uint64_t PixelKilnImpl::createImage(const ImageDesc &desc) {
    collectGarbage();
    if (desc.width == 0 || desc.height == 0) {
        throw std::invalid_argument("PixelKiln: image width and height must be greater than 0");
    }
    if (desc.format == IMAGE_FORMAT_UNDEFINED) {
        throw std::invalid_argument("PixelKiln: image format can't be IMAGE_FORMAT_UNDEFINED");
    }
    const ImageUsageFlags allUsages = IMAGE_USAGE_SAMPLED | IMAGE_USAGE_STORAGE | IMAGE_USAGE_COLOR_TARGET |
                                      IMAGE_USAGE_DEPTH_TARGET;
    if (desc.usage == 0 || (desc.usage & ~allUsages)) {
        throw std::invalid_argument("PixelKiln: invalid image usage");
    }
    const bool depth = isDepthFormat(desc.format);
    if (depth && (desc.usage & (IMAGE_USAGE_STORAGE | IMAGE_USAGE_COLOR_TARGET))) {
        throw std::invalid_argument("PixelKiln: depth images can only be sampled or used as depth targets");
    }
    if (!depth && (desc.usage & IMAGE_USAGE_DEPTH_TARGET)) {
        throw std::invalid_argument("PixelKiln: IMAGE_USAGE_DEPTH_TARGET needs a depth format");
    }

    vk::Format format = toVkFormat(desc.format);
    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    if (desc.usage & IMAGE_USAGE_SAMPLED) usage |= vk::ImageUsageFlagBits::eSampled;
    if (desc.usage & IMAGE_USAGE_STORAGE) usage |= vk::ImageUsageFlagBits::eStorage;
    if (desc.usage & IMAGE_USAGE_COLOR_TARGET) usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (desc.usage & IMAGE_USAGE_DEPTH_TARGET) usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;

    vk::ImageFormatProperties formatProperties;
    try {
        formatProperties = m_physicalDevice.getImageFormatProperties(format, vk::ImageType::e2D,
                                                                     vk::ImageTiling::eOptimal, usage);
    } catch (const vk::FormatNotSupportedError &) {
        throw std::invalid_argument("PixelKiln: image format and usage combination is not supported by this device");
    }
    if (desc.width > formatProperties.maxExtent.width || desc.height > formatProperties.maxExtent.height) {
        throw std::invalid_argument("PixelKiln: image is larger than this device supports");
    }

    Image image;
    image.desc = desc;
    image.aspect = depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = format;
    imageInfo.extent = vk::Extent3D(desc.width, desc.height, 1);
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = usage;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    uint32_t families[2];
    applySharingMode(imageInfo, families);
    image.image = m_device.createImage(imageInfo);
    try {
        image.memory = allocateMemory(m_device.getImageMemoryRequirements(image.image), {},
                                      vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_device.bindImageMemory(image.image, image.memory, 0);
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = image.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = vk::ImageSubresourceRange(image.aspect, 0, 1, 0, 1);
        image.view = m_device.createImageView(viewInfo);
    } catch (...) {
        m_device.destroyImage(image.image);
        m_device.freeMemory(image.memory);
        throw;
    }
    uint64_t handle = m_nextHandle++;
    m_images[handle] = image;
    return handle;
}

void PixelKilnImpl::destroyImage(uint64_t image) {
    collectGarbage();
    Image destroyed = getImage(image);
    if (destroyed.swapchain) {
        throw std::invalid_argument("PixelKiln: swapchain images belong to their swapchain and can't be destroyed");
    }
    m_images.erase(image);
    vk::Device device = m_device;
    deferDestroy(destroyed.lastAllUse, destroyed.lastTransferUse, [device, destroyed]() {
        device.destroyImageView(destroyed.view);
        device.destroyImage(destroyed.image);
        device.freeMemory(destroyed.memory);
    });
}

// Whole image copies only: they always satisfy minImageTransferGranularity on transfer-only queues. Depth aspects
// can't be copied on queues without graphics, so depth images are render targets only.
void PixelKilnImpl::uploadImage(uint64_t image, const void* data, uint64_t size) {
    collectGarbage();
    Image &target = getImage(image);
    if (isDepthFormat(target.desc.format)) {
        throw std::invalid_argument("PixelKiln: depth images can't be uploaded");
    }
    if (target.swapchain) {
        throw std::invalid_argument("PixelKiln: swapchain images can't be uploaded to");
    }
    const uint64_t expectedSize = uint64_t(target.desc.width) * target.desc.height * texelSize(target.desc.format);
    if (!data || size != expectedSize) {
        throw std::invalid_argument("PixelKiln: uploadImage needs data of exactly width * height * texel size bytes");
    }
    StagingBuffer staging = createStagingBuffer(size, false);
    std::memcpy(staging.mapped, data, size);
    uint64_t value;
    try {
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        // The whole image is overwritten, so its old contents can be discarded.
        transferImageBarrier(commandBuffer, target.image, target.aspect, vk::ImageLayout::eUndefined,
                             vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite);
        vk::BufferImageCopy region{};
        region.imageSubresource = vk::ImageSubresourceLayers(target.aspect, 0, 0, 1);
        region.imageExtent = vk::Extent3D(target.desc.width, target.desc.height, 1);
        commandBuffer.copyBufferToImage(staging.buffer, target.image, vk::ImageLayout::eTransferDstOptimal, region);
        value = submitCommands(QUEUE_TRANSFER, commandBuffer, target.lastAllUse);
    } catch (...) {
        destroyStagingBuffer(staging);
        throw;
    }
    target.layout = vk::ImageLayout::eTransferDstOptimal;
    target.lastTransferUse = value;
    vk::Device device = m_device;
    deferDestroy(0, value, [device, staging]() {
        device.destroyBuffer(staging.buffer);
        device.freeMemory(staging.memory);
    });
}

void PixelKilnImpl::downloadImage(uint64_t image, void* data, uint64_t size) {
    collectGarbage();
    Image &source = getImage(image);
    if (isDepthFormat(source.desc.format)) {
        throw std::invalid_argument("PixelKiln: depth images can't be downloaded");
    }
    if (source.swapchain) {
        checkSwapchainImage(source);
        const Swapchain &swapchain = m_swapchains.at(source.swapchain);
        if (swapchain.pendingAcquire >= 0) {
            throw std::invalid_argument("PixelKiln: render to a swapchain image before downloading it");
        }
        if (!(swapchain.usage & vk::ImageUsageFlagBits::eTransferSrc)) {
            throw std::invalid_argument("PixelKiln: this window's swapchain images can't be downloaded");
        }
    }
    const uint64_t expectedSize = uint64_t(source.desc.width) * source.desc.height * texelSize(source.desc.format);
    if (!data || size != expectedSize) {
        throw std::invalid_argument("PixelKiln: downloadImage needs room for exactly width * height * texel size bytes");
    }
    StagingBuffer staging = createStagingBuffer(size, true);
    uint64_t value;
    try {
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        transferImageBarrier(commandBuffer, source.image, source.aspect, source.layout,
                             vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy region{};
        region.imageSubresource = vk::ImageSubresourceLayers(source.aspect, 0, 0, 1);
        region.imageExtent = vk::Extent3D(source.desc.width, source.desc.height, 1);
        commandBuffer.copyImageToBuffer(source.image, vk::ImageLayout::eTransferSrcOptimal, staging.buffer, region);
        hostBarrier(commandBuffer);
        value = submitCommands(QUEUE_TRANSFER, commandBuffer, source.lastAllUse);
    } catch (...) {
        destroyStagingBuffer(staging);
        throw;
    }
    source.layout = vk::ImageLayout::eTransferSrcOptimal;
    source.lastTransferUse = value;
    waitValue(QUEUE_TRANSFER, value);
    readStagingBuffer(staging, data, size);
    destroyStagingBuffer(staging);
}

void PixelKilnImpl::createUniformRing() {
    m_uniformStaging = createStagingBuffer(UNIFORM_RING_SIZE, false);
    m_uniformBuffer = createDeviceBuffer(UNIFORM_RING_SIZE, vk::BufferUsageFlagBits::eUniformBuffer |
                                                            vk::BufferUsageFlagBits::eTransferDst);
}

void PixelKilnImpl::retireUniformRegions() {
    if (m_uniformRegions.empty()) {
        return;
    }
    uint64_t completed = completedValue(QUEUE_ALL);
    while (!m_uniformRegions.empty() && m_uniformRegions.front().allValue <= completed) {
        m_uniformReuseValue = std::max(m_uniformReuseValue, m_uniformRegions.front().allValue);
        m_uniformRegions.pop_front();
    }
}

uint64_t PixelKilnImpl::allocateUniforms(uint64_t size) {
    if (size > UNIFORM_RING_SIZE) {
        throw std::invalid_argument("PixelKiln: a call's uniform data is larger than the uniform ring");
    }
    const uint64_t alignment = m_physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
    while (true) {
        retireUniformRegions();
        uint64_t offset = alignUp(m_uniformHead, alignment);
        if (offset + size > UNIFORM_RING_SIZE) {
            offset = 0;
        }
        bool overlaps = false;
        for (const auto& region : m_uniformRegions) {
            if (offset < region.offset + region.size && region.offset < offset + size) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            m_uniformHead = offset + size;
            return offset;
        }
        // The ring is full: wait for the oldest call still using it.
        waitValue(QUEUE_ALL, m_uniformRegions.front().allValue);
    }
}

vk::Sampler PixelKilnImpl::getSampler(const SamplerDesc &desc) {
    vk::Filter filter = toVkFilter(desc.filter);
    vk::SamplerAddressMode addressMode = toVkAddressMode(desc.addressMode);
    uint32_t key = uint32_t(desc.filter) * SAMPLER_ADDRESS_MODE_COUNT + uint32_t(desc.addressMode);
    auto it = m_samplers.find(key);
    if (it != m_samplers.end()) {
        return it->second;
    }
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    vk::Sampler sampler = m_device.createSampler(samplerInfo);
    m_samplers[key] = sampler;
    return sampler;
}

PixelKilnImpl::DescriptorPool PixelKilnImpl::acquireDescriptorPool() {
    uint64_t completed = completedValue(QUEUE_ALL);
    for (size_t i = 0; i < m_retiredDescriptorPools.size(); i++) {
        if (m_retiredDescriptorPools[i].lastAllUse <= completed) {
            DescriptorPool pool = m_retiredDescriptorPools[i];
            m_retiredDescriptorPools.erase(m_retiredDescriptorPools.begin() + static_cast<std::ptrdiff_t>(i));
            m_device.resetDescriptorPool(pool.pool);
            pool.lastAllUse = 0;
            return pool;
        }
    }
    vk::DescriptorPoolSize poolSizes[] = {
        {vk::DescriptorType::eUniformBuffer, DESCRIPTORS_PER_TYPE},
        {vk::DescriptorType::eStorageBuffer, DESCRIPTORS_PER_TYPE},
        {vk::DescriptorType::eCombinedImageSampler, DESCRIPTORS_PER_TYPE},
        {vk::DescriptorType::eStorageImage, DESCRIPTORS_PER_TYPE},
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = DESCRIPTOR_POOL_SETS;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    DescriptorPool pool;
    pool.pool = m_device.createDescriptorPool(poolInfo);
    return pool;
}

// One set per call. A full pool is parked until the GPU is done with every set allocated from it, then reset.
vk::DescriptorSet PixelKilnImpl::allocateDescriptorSet(vk::DescriptorSetLayout layout) {
    if (!m_descriptorPool.pool) {
        m_descriptorPool = acquireDescriptorPool();
    }
    vk::DescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout;
    for (int attempt = 0; attempt < 2; attempt++) {
        allocateInfo.descriptorPool = m_descriptorPool.pool;
        try {
            return m_device.allocateDescriptorSets(allocateInfo)[0];
        } catch (const vk::OutOfPoolMemoryError &) {
        } catch (const vk::FragmentedPoolError &) {
        }
        m_retiredDescriptorPools.push_back(m_descriptorPool);
        m_descriptorPool = acquireDescriptorPool();
    }
    throw std::runtime_error("PixelKiln: failed to allocate a descriptor set");
}
