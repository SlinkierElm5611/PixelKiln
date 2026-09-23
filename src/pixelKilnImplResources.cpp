//
// Created by Stefan Balta on 2026-09-21.
//

#include "pixelKilnImpl.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "vulkanTranslate.h"

void PixelKilnImpl::transferBarrier(vk::CommandBuffer commandBuffer)
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

// Staging offsets are multiples of every texel size, and of 4 as transfer-only queues require for image copies.
static uint64_t stagingAlignment(const vk::PhysicalDeviceLimits &limits)
{
    return std::max<uint64_t>(16, limits.optimalBufferCopyOffsetAlignment);
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

static void checkAllocation(VkResult result)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error("PixelKiln: allocating GPU memory failed (VkResult " + std::to_string(result) + ")");
    }
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
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkBuffer created = VK_NULL_HANDLE;
    checkAllocation(vmaCreateBuffer(m_allocator, &static_cast<const VkBufferCreateInfo &>(bufferInfo), &allocationInfo,
                                    &created, &buffer.allocation, nullptr));
    buffer.buffer = created;
    return buffer;
}

PixelKilnImpl::StagingBuffer PixelKilnImpl::createMappedBuffer(uint64_t size, vk::BufferUsageFlags usage,
                                                              VmaMemoryUsage memoryUsage,
                                                              VmaAllocationCreateFlags hostAccess) {
    StagingBuffer staging;
    staging.size = size;
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = hostAccess | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (hostAccess & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) {
        allocationInfo.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    VkBuffer created = VK_NULL_HANDLE;
    VmaAllocationInfo allocated{};
    checkAllocation(vmaCreateBuffer(m_allocator, &static_cast<const VkBufferCreateInfo &>(bufferInfo), &allocationInfo,
                                    &created, &staging.allocation, &allocated));
    staging.buffer = created;
    staging.mapped = allocated.pMappedData;
    return staging;
}

// Staging buffers are only touched by the transfer queue. Uploads use coherent memory, readbacks prefer cached
// memory and are invalidated before reading when it isn't coherent (common on discrete GPUs).
PixelKilnImpl::StagingBuffer PixelKilnImpl::createStagingBuffer(uint64_t size, bool readback) {
    if (readback) {
        return createMappedBuffer(size, vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    }
    return createMappedBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
}

// Calls write their uniforms straight into this ring and the GPU reads them from there: no staging copy and no
// transfer submission per call. Device local host-visible memory (resizable BAR, unified memory) when available.
void PixelKilnImpl::createUniformRing() {
    m_uniformRing.staging = createMappedBuffer(UNIFORM_RING_SIZE, vk::BufferUsageFlagBits::eUniformBuffer,
                                               VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
}

void PixelKilnImpl::destroyStagingBuffer(const StagingBuffer &staging) {
    if (staging.buffer) { // also called on never created buffers, possibly before the allocator exists
        vmaDestroyBuffer(m_allocator, staging.buffer, staging.allocation);
    }
}

void PixelKilnImpl::readStagingBuffer(const StagingBuffer &staging, uint64_t offset, void* data, uint64_t size) {
    vmaInvalidateAllocation(m_allocator, staging.allocation, offset, size); // nothing to do for coherent memory
    std::memcpy(data, static_cast<const char*>(staging.mapped) + offset, size);
}

void PixelKilnImpl::reserveReadback(uint64_t size) {
    if (m_readback.size >= size) {
        return;
    }
    uint64_t capacity = READBACK_MIN_SIZE;
    while (capacity < size) {
        capacity *= 2;
    }
    destroyStagingBuffer(m_readback);
    m_readback = {};
    m_readback = createStagingBuffer(std::min(capacity, READBACK_MAX_SIZE), true);
}

uint64_t PixelKilnImpl::allocateRing(Ring &ring, uint64_t size, uint64_t alignment) {
    if (size > ring.staging.size) {
        throw std::logic_error("PixelKiln: ring allocation larger than the ring");
    }
    bool retired = false;
    while (true) {
        std::optional<uint64_t> offset;
        if (ring.regions.empty()) {
            offset = 0;
        } else {
            const RingRegion &oldest = ring.regions.front();
            const RingRegion &newest = ring.regions.back();
            const uint64_t head = alignUp(newest.offset + newest.size, alignment);
            if (oldest.offset <= newest.offset) {
                // Live regions don't wrap around: free space after the newest and before the oldest.
                if (head + size <= ring.staging.size) {
                    offset = head;
                } else if (size <= oldest.offset) {
                    offset = 0;
                }
            } else if (head + size <= oldest.offset) {
                offset = head; // they do: free space between the newest and the oldest
            }
        }
        if (offset) {
            return *offset;
        }
        // Full: drop the regions whose submissions completed, then wait for the oldest one if that wasn't enough.
        if (retired) {
            waitValue(ring.queue, ring.regions.front().value);
        }
        const uint64_t completed = completedValue(ring.queue);
        while (!ring.regions.empty() && ring.regions.front().value <= completed) {
            ring.regions.pop_front();
        }
        retired = true;
    }
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
    if (static_cast<unsigned>(format) >= IMAGE_FORMAT_COUNT) {
        throw std::invalid_argument("PixelKiln: invalid ImageFormat");
    }
    return m_formatFeatures[format];
}

uint64_t PixelKilnImpl::createBuffer(uint64_t size, const char* debugName) {
    collectGarbage();
    if (size == 0) {
        throw std::invalid_argument("PixelKiln: buffer size must be greater than 0");
    }
    Buffer buffer = createDeviceBuffer(size, vk::BufferUsageFlagBits::eVertexBuffer |
                                             vk::BufferUsageFlagBits::eIndexBuffer |
                                             vk::BufferUsageFlagBits::eStorageBuffer |
                                             vk::BufferUsageFlagBits::eIndirectBuffer |
                                             vk::BufferUsageFlagBits::eTransferSrc |
                                             vk::BufferUsageFlagBits::eTransferDst);
    setDebugName(vk::ObjectType::eBuffer, reinterpret_cast<uint64_t>(static_cast<VkBuffer>(buffer.buffer)), debugName);
    uint64_t handle = m_nextHandle++;
    m_buffers[handle] = buffer;
    return handle;
}

void PixelKilnImpl::destroyBuffer(uint64_t buffer) {
    collectGarbage();
    Buffer destroyed = getBuffer(buffer);
    m_buffers.erase(buffer);
    VmaAllocator allocator = m_allocator;
    deferDestroy(destroyed.lastAllUse, destroyed.lastTransferUse, [allocator, destroyed]() {
        vmaDestroyBuffer(allocator, destroyed.buffer, destroyed.allocation);
    });
}

PixelKilnImpl::Ring &PixelKilnImpl::uploadRing() {
    if (!m_uploadRing.staging.buffer) {
        m_uploadRing.staging = createStagingBuffer(UPLOAD_RING_SIZE, false);
    }
    return m_uploadRing;
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
    Ring &ring = uploadRing();
    // Each piece waits for the all queue to be done with the buffer (e.g. the call reading its previous contents).
    const uint64_t waitAll = submittedValue(target.lastAllUse);
    // Large uploads are staged a piece at a time, later pieces wait for ring space as earlier ones land.
    for (uint64_t done = 0; done < size; done += UPLOAD_CHUNK_SIZE) {
        const uint64_t chunk = std::min(UPLOAD_CHUNK_SIZE, size - done);
        const uint64_t stagingOffset = allocateRing(ring, chunk, stagingAlignment(m_physicalDeviceProperties.limits));
        std::memcpy(static_cast<char*>(ring.staging.mapped) + stagingOffset, static_cast<const char*>(data) + done,
                    chunk);
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        transferBarrier(commandBuffer);
        vk::BufferCopy region{stagingOffset, offset + done, chunk};
        commandBuffer.copyBuffer(ring.staging.buffer, target.buffer, region);
        target.lastTransferUse = submitCommands(QUEUE_TRANSFER, commandBuffer, waitAll);
        ring.regions.push_back({stagingOffset, chunk, target.lastTransferUse});
    }
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
    reserveReadback(std::min(size, READBACK_MAX_SIZE));
    const uint64_t waitAll = submittedValue(source.lastAllUse);
    // Downloads larger than the readback buffer are read back a piece at a time.
    for (uint64_t done = 0; done < size; done += m_readback.size) {
        const uint64_t chunk = std::min(m_readback.size, size - done);
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        transferBarrier(commandBuffer);
        vk::BufferCopy region{offset + done, 0, chunk};
        commandBuffer.copyBuffer(source.buffer, m_readback.buffer, region);
        hostBarrier(commandBuffer);
        source.lastTransferUse = submitCommands(QUEUE_TRANSFER, commandBuffer, waitAll);
        waitValue(QUEUE_TRANSFER, source.lastTransferUse);
        readStagingBuffer(m_readback, 0, static_cast<char*>(data) + done, chunk);
    }
}

uint64_t PixelKilnImpl::createImage(const ImageDesc &desc, const char* debugName) {
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
    const vk::SampleCountFlagBits samples = toVkSampleCount(desc.samples);
    const bool multisampled = desc.samples > 1;
    if (multisampled && (desc.usage & IMAGE_USAGE_STORAGE)) {
        throw std::invalid_argument("PixelKiln: multisampled images can't be storage images");
    }
    if (multisampled && !(desc.usage & (IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_DEPTH_TARGET))) {
        throw std::invalid_argument("PixelKiln: multisampled images must be color or depth targets");
    }

    // Depth and multisampled images are never copied (uploads and downloads reject them), so only the all queue
    // touches them: no transfer usage, and exclusive sharing, which keeps compression available on some GPUs.
    const bool transferable = !depth && !multisampled;
    vk::Format format = toVkFormat(desc.format);
    vk::ImageUsageFlags usage;
    if (transferable) usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
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
    if (!(formatProperties.sampleCounts & samples)) {
        throw std::invalid_argument("PixelKiln: this device doesn't support that sample count for the image's format "
                                    "and usage");
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
    imageInfo.samples = samples;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = usage;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    uint32_t families[2];
    if (transferable) {
        applySharingMode(imageInfo, families);
    }
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (desc.usage & (IMAGE_USAGE_COLOR_TARGET | IMAGE_USAGE_DEPTH_TARGET)) {
        // Render targets get memory of their own, as VMA recommends (large, often recreated on resize).
        allocationInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }
    VkImage created = VK_NULL_HANDLE;
    checkAllocation(vmaCreateImage(m_allocator, &static_cast<const VkImageCreateInfo &>(imageInfo), &allocationInfo,
                                   &created, &image.allocation, nullptr));
    image.image = created;
    try {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = image.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = vk::ImageSubresourceRange(image.aspect, 0, 1, 0, 1);
        image.view = m_device.createImageView(viewInfo);
    } catch (...) {
        vmaDestroyImage(m_allocator, image.image, image.allocation);
        throw;
    }
    setDebugName(vk::ObjectType::eImage, reinterpret_cast<uint64_t>(static_cast<VkImage>(image.image)), debugName);
    setDebugName(vk::ObjectType::eImageView, reinterpret_cast<uint64_t>(static_cast<VkImageView>(image.view)), debugName);
    uint64_t handle = m_nextHandle++;
    m_images[handle] = image;
    return handle;
}

uint32_t PixelKilnImpl::getSupportedSampleCounts(ImageFormat format) {
    if (format == IMAGE_FORMAT_UNDEFINED) {
        throw std::invalid_argument("PixelKiln: image format can't be IMAGE_FORMAT_UNDEFINED");
    }
    const vk::PhysicalDeviceLimits &limits = m_physicalDeviceProperties.limits;
    const bool depth = isDepthFormat(format);
    const vk::ImageUsageFlags usage = depth ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                                            : vk::ImageUsageFlagBits::eColorAttachment;
    vk::ImageFormatProperties properties;
    try {
        properties = m_physicalDevice.getImageFormatProperties(toVkFormat(format), vk::ImageType::e2D,
                                                               vk::ImageTiling::eOptimal, usage);
    } catch (const vk::FormatNotSupportedError &) {
        return 0;
    }
    vk::SampleCountFlags framebuffer = depth ? limits.framebufferDepthSampleCounts
                                     : isIntegerFormat(format) ? m_integerColorSampleCounts
                                                               : limits.framebufferColorSampleCounts;
    return static_cast<uint32_t>(properties.sampleCounts & framebuffer);
}

void PixelKilnImpl::destroyImage(uint64_t image) {
    collectGarbage();
    Image destroyed = getImage(image);
    if (destroyed.swapchain) {
        throw std::invalid_argument("PixelKiln: swapchain images belong to their swapchain and can't be destroyed");
    }
    m_images.erase(image);
    vk::Device device = m_device;
    VmaAllocator allocator = m_allocator;
    deferDestroy(destroyed.lastAllUse, destroyed.lastTransferUse, [device, allocator, destroyed]() {
        device.destroyImageView(destroyed.view);
        vmaDestroyImage(allocator, destroyed.image, destroyed.allocation);
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
    if (target.desc.samples > 1) {
        throw std::invalid_argument("PixelKiln: multisampled images can't be uploaded");
    }
    const uint64_t expectedSize = uint64_t(target.desc.width) * target.desc.height * texelSize(target.desc.format);
    if (!data || size != expectedSize) {
        throw std::invalid_argument("PixelKiln: uploadImage needs data of exactly width * height * texel size bytes");
    }
    // Small images go through the upload ring. Large ones (usually loaded once) get their own staging buffer.
    const bool useRing = size <= UPLOAD_CHUNK_SIZE;
    StagingBuffer staging;
    uint64_t stagingOffset = 0;
    if (useRing) {
        Ring &ring = uploadRing();
        stagingOffset = allocateRing(ring, size, stagingAlignment(m_physicalDeviceProperties.limits));
        staging = ring.staging;
    } else {
        staging = createStagingBuffer(size, false);
    }
    std::memcpy(static_cast<char*>(staging.mapped) + stagingOffset, data, size);
    const uint64_t waitAll = submittedValue(target.lastAllUse);
    uint64_t value;
    try {
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        // The whole image is overwritten, so its old contents can be discarded.
        transferImageBarrier(commandBuffer, target.image, target.aspect, vk::ImageLayout::eUndefined,
                             vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite);
        vk::BufferImageCopy region{};
        region.bufferOffset = stagingOffset;
        region.imageSubresource = vk::ImageSubresourceLayers(target.aspect, 0, 0, 1);
        region.imageExtent = vk::Extent3D(target.desc.width, target.desc.height, 1);
        commandBuffer.copyBufferToImage(staging.buffer, target.image, vk::ImageLayout::eTransferDstOptimal, region);
        value = submitCommands(QUEUE_TRANSFER, commandBuffer, waitAll);
    } catch (...) {
        if (!useRing) {
            destroyStagingBuffer(staging);
        }
        throw;
    }
    target.layout = vk::ImageLayout::eTransferDstOptimal;
    target.lastTransferUse = value;
    if (useRing) {
        m_uploadRing.regions.push_back({stagingOffset, size, value});
    } else {
        VmaAllocator allocator = m_allocator;
        deferDestroy(0, value, [allocator, staging]() {
            vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
        });
    }
}

void PixelKilnImpl::downloadImage(uint64_t image, void* data, uint64_t size) {
    collectGarbage();
    Image &source = getImage(image);
    if (isDepthFormat(source.desc.format)) {
        throw std::invalid_argument("PixelKiln: depth images can't be downloaded");
    }
    if (source.desc.samples > 1) {
        throw std::invalid_argument("PixelKiln: multisampled images can't be downloaded, resolve them into a single "
                                    "sample image (ColorTarget::resolveImage)");
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
    // Through the readback buffer, or a staging buffer of its own when larger than it may grow.
    const bool useReadback = size <= READBACK_MAX_SIZE;
    StagingBuffer staging;
    if (useReadback) {
        reserveReadback(size);
        staging = m_readback;
    } else {
        staging = createStagingBuffer(size, true);
    }
    const uint64_t waitAll = submittedValue(source.lastAllUse);
    uint64_t value;
    try {
        vk::CommandBuffer commandBuffer = beginCommands(QUEUE_TRANSFER);
        transferBarrier(commandBuffer); // after the previous download into the readback buffer
        transferImageBarrier(commandBuffer, source.image, source.aspect, source.layout,
                             vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead);
        vk::BufferImageCopy region{};
        region.imageSubresource = vk::ImageSubresourceLayers(source.aspect, 0, 0, 1);
        region.imageExtent = vk::Extent3D(source.desc.width, source.desc.height, 1);
        commandBuffer.copyImageToBuffer(source.image, vk::ImageLayout::eTransferSrcOptimal, staging.buffer, region);
        hostBarrier(commandBuffer);
        value = submitCommands(QUEUE_TRANSFER, commandBuffer, waitAll);
    } catch (...) {
        if (!useReadback) {
            destroyStagingBuffer(staging);
        }
        throw;
    }
    source.layout = vk::ImageLayout::eTransferSrcOptimal;
    source.lastTransferUse = value;
    waitValue(QUEUE_TRANSFER, value);
    readStagingBuffer(staging, 0, data, size);
    if (!useReadback) {
        destroyStagingBuffer(staging);
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
