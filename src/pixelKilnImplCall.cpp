//
// Created by Stefan Balta on 2026-09-21.
//

#include "pixelKilnImpl.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

#include "vulkanTranslate.h"

uint64_t PixelKilnImpl::call(const ProgramCall &call) {
    collectGarbage();
    Program &program = getProgram(call.program);
    if (call.type != program.type) {
        throw std::invalid_argument("PixelKiln: call type does not match the program's type");
    }
    if (call.bindings.size() != program.bindings.size()) {
        throw std::invalid_argument("PixelKiln: a call needs one binding per program uniform binding");
    }
    const vk::PhysicalDeviceLimits &limits = m_physicalDeviceProperties.limits;

    // Validate everything before anything is allocated or recorded.
    struct UsedImage {
        uint64_t handle;
        Image* image;
        vk::ImageLayout layout; // layout the call needs it in
        bool target = false;
        bool discard = false; // cleared target, its old contents can be dropped
    };
    std::vector<Buffer*> usedBuffers;
    std::vector<UsedImage> usedImages;
    auto findImage = [&](uint64_t handle) -> UsedImage* {
        for (UsedImage &used : usedImages) {
            if (used.handle == handle) {
                return &used;
            }
        }
        return nullptr;
    };
    auto useImage = [&](uint64_t handle, Image &image, vk::ImageLayout layout) {
        checkSwapchainImage(image);
        UsedImage* used = findImage(handle);
        if (!used) {
            usedImages.push_back({handle, &image, layout});
        } else if (used->layout != layout) {
            throw std::invalid_argument("PixelKiln: an image can't be used in two different ways by the same call");
        }
    };
    const uint64_t uniformAlignment = limits.minUniformBufferOffsetAlignment;
    std::vector<uint64_t> uniformOffsets(call.bindings.size(), 0); // relative to the call's ring region
    uint64_t uniformSize = 0;
    bool hasDescriptors = false;
    for (size_t i = 0; i < call.bindings.size(); i++) {
        const CallBinding &binding = call.bindings[i];
        switch (program.bindings[i]) {
            case UNIFORM_BINDING_TYPE_BUFFER:
                if (!binding.data || binding.size == 0) {
                    throw std::invalid_argument("PixelKiln: a uniform buffer binding needs data and a size");
                }
                if (binding.size > limits.maxUniformBufferRange) {
                    throw std::invalid_argument("PixelKiln: uniform data is larger than this device's maxUniformBufferRange");
                }
                uniformOffsets[i] = alignUp(uniformSize, uniformAlignment);
                uniformSize = uniformOffsets[i] + binding.size;
                break;
            case UNIFORM_BINDING_TYPE_STORAGE_BUFFER: {
                Buffer &buffer = getBuffer(binding.resource);
                if (buffer.size > limits.maxStorageBufferRange) {
                    throw std::invalid_argument("PixelKiln: storage buffer is larger than this device's maxStorageBufferRange");
                }
                usedBuffers.push_back(&buffer);
                break;
            }
            case UNIFORM_BINDING_TYPE_SAMPLER: {
                Image &image = getImage(binding.resource);
                if (!(image.desc.usage & IMAGE_USAGE_SAMPLED)) {
                    throw std::invalid_argument("PixelKiln: sampler binding needs an image created with IMAGE_USAGE_SAMPLED");
                }
                if (binding.sampler.filter == SAMPLER_FILTER_LINEAR &&
                    !(formatFeatures(image.desc.format) & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
                    throw std::invalid_argument("PixelKiln: this device can't linearly filter the image's format");
                }
                getSampler(binding.sampler);
                useImage(binding.resource, image, vk::ImageLayout::eShaderReadOnlyOptimal);
                break;
            }
            case UNIFORM_BINDING_TYPE_STORAGE_IMAGE: {
                Image &image = getImage(binding.resource);
                if (!(image.desc.usage & IMAGE_USAGE_STORAGE)) {
                    throw std::invalid_argument("PixelKiln: storage image binding needs an image created with IMAGE_USAGE_STORAGE");
                }
                useImage(binding.resource, image, vk::ImageLayout::eGeneral);
                break;
            }
            default:
                continue; // EMPTY
        }
        hasDescriptors = true;
    }
    if (uniformSize > UNIFORM_RING_SIZE) {
        throw std::invalid_argument("PixelKiln: a call's uniform data is larger than the uniform ring");
    }

    vk::Extent2D extent{};
    if (program.type == PROGRAM_TYPE_RASTER_DRAW) {
        if (call.colorTargets.size() != program.colorFormats.size()) {
            throw std::invalid_argument("PixelKiln: a draw needs one color target per program color format");
        }
        // discard: the call overwrites all of it (cleared targets, resolve targets), its old contents are dropped.
        auto useTarget = [&](uint64_t handle, ImageFormat format, ImageUsage usage, uint32_t samples, bool discard,
                             vk::ImageLayout layout) {
            Image &image = getImage(handle);
            if (!(image.desc.usage & usage) || image.desc.format != format) {
                throw std::invalid_argument("PixelKiln: target image usage or format does not match the program");
            }
            if (image.desc.samples != samples) {
                throw std::invalid_argument(samples == 1 ? "PixelKiln: a resolve target must be a single sample image"
                                                         : "PixelKiln: target sample count does not match the program");
            }
            if (extent.width == 0) {
                extent = vk::Extent2D(image.desc.width, image.desc.height);
            } else if (extent.width != image.desc.width || extent.height != image.desc.height) {
                throw std::invalid_argument("PixelKiln: all targets of a draw must be the same size");
            }
            if (UsedImage* used = findImage(handle)) {
                throw std::invalid_argument(used->target ? "PixelKiln: an image can only be one target of a call"
                                                         : "PixelKiln: an image can't be used in two different ways by "
                                                           "the same call");
            }
            checkSwapchainImage(image);
            usedImages.push_back({handle, &image, layout, true, discard});
        };
        for (size_t i = 0; i < call.colorTargets.size(); i++) {
            const ColorTarget &target = call.colorTargets[i];
            useTarget(target.image, program.colorFormats[i], IMAGE_USAGE_COLOR_TARGET, program.samples, target.clear,
                      vk::ImageLayout::eColorAttachmentOptimal);
            if (target.resolveImage) {
                if (program.samples == 1) {
                    throw std::invalid_argument("PixelKiln: only multisampled targets can be resolved");
                }
                useTarget(target.resolveImage, program.colorFormats[i], IMAGE_USAGE_COLOR_TARGET, 1, true,
                          vk::ImageLayout::eColorAttachmentOptimal);
            }
        }
        if (program.depthFormat != IMAGE_FORMAT_UNDEFINED) {
            useTarget(call.depthTarget.image, program.depthFormat, IMAGE_USAGE_DEPTH_TARGET, program.samples,
                      call.depthTarget.clear, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        } else if (call.depthTarget.image != 0) {
            throw std::invalid_argument("PixelKiln: depth target given for a program without a depth format");
        }
        if (call.vertexBuffers.size() != program.vertexBufferCount) {
            throw std::invalid_argument("PixelKiln: a draw needs one vertex buffer per vertex layout buffer");
        }
        for (uint64_t vertexBuffer : call.vertexBuffers) {
            usedBuffers.push_back(&getBuffer(vertexBuffer));
        }
        if (call.indexType != INDEX_TYPE_NONE) {
            toVkIndexType(call.indexType);
            usedBuffers.push_back(&getBuffer(call.indexBuffer));
        }
    } else {
        if (call.groupCountX > limits.maxComputeWorkGroupCount[0] ||
            call.groupCountY > limits.maxComputeWorkGroupCount[1] ||
            call.groupCountZ > limits.maxComputeWorkGroupCount[2]) {
            throw std::invalid_argument("PixelKiln: group count exceeds this device's maxComputeWorkGroupCount");
        }
    }

    // Step 1: the uniform data goes straight into the ring the GPU reads it from.
    const uint64_t ticket = m_allValue + 1;
    uint64_t uniformBase = 0;
    if (uniformSize > 0) {
        uniformBase = allocateRing(m_uniformRing, uniformSize, uniformAlignment);
        for (size_t i = 0; i < call.bindings.size(); i++) {
            if (program.bindings[i] == UNIFORM_BINDING_TYPE_BUFFER) {
                std::memcpy(static_cast<char*>(m_uniformRing.staging.mapped) + uniformBase + uniformOffsets[i],
                            call.bindings[i].data, call.bindings[i].size);
            }
        }
    }

    // Step 2: descriptors, pushed while recording or written to a set allocated for this call.
    vk::DescriptorSet descriptorSet;
    std::vector<vk::DescriptorBufferInfo> bufferInfos;
    std::vector<vk::DescriptorImageInfo> imageInfos;
    std::vector<vk::WriteDescriptorSet> writes;
    if (hasDescriptors) {
        if (!program.pushDescriptors) {
            descriptorSet = allocateDescriptorSet(program.setLayout);
        }
        bufferInfos.reserve(call.bindings.size());
        imageInfos.reserve(call.bindings.size());
        for (size_t i = 0; i < call.bindings.size(); i++) {
            const CallBinding &binding = call.bindings[i];
            if (program.bindings[i] == UNIFORM_BINDING_TYPE_EMPTY) {
                continue;
            }
            vk::WriteDescriptorSet write{};
            write.dstSet = descriptorSet;
            write.dstBinding = static_cast<uint32_t>(i);
            write.descriptorCount = 1;
            write.descriptorType = toVkDescriptorType(program.bindings[i]);
            switch (program.bindings[i]) {
                case UNIFORM_BINDING_TYPE_BUFFER:
                    bufferInfos.emplace_back(m_uniformRing.staging.buffer, uniformBase + uniformOffsets[i],
                                             binding.size);
                    write.pBufferInfo = &bufferInfos.back();
                    break;
                case UNIFORM_BINDING_TYPE_STORAGE_BUFFER:
                    bufferInfos.emplace_back(m_buffers.at(binding.resource).buffer, 0, VK_WHOLE_SIZE);
                    write.pBufferInfo = &bufferInfos.back();
                    break;
                case UNIFORM_BINDING_TYPE_SAMPLER:
                    imageInfos.emplace_back(getSampler(binding.sampler), m_images.at(binding.resource).view,
                                            vk::ImageLayout::eShaderReadOnlyOptimal);
                    write.pImageInfo = &imageInfos.back();
                    break;
                default: // STORAGE_IMAGE
                    imageInfos.emplace_back(nullptr, m_images.at(binding.resource).view, vk::ImageLayout::eGeneral);
                    write.pImageInfo = &imageInfos.back();
                    break;
            }
            writes.push_back(write);
        }
        if (descriptorSet) {
            m_device.updateDescriptorSets(writes, nullptr);
        }
    }
    auto bindDescriptors = [&](vk::CommandBuffer commandBuffer, vk::PipelineBindPoint bindPoint) {
        if (descriptorSet) {
            commandBuffer.bindDescriptorSets(bindPoint, program.pipelineLayout, 0, descriptorSet, nullptr);
        } else if (!writes.empty()) {
            m_cmdPushDescriptorSet(commandBuffer, static_cast<VkPipelineBindPoint>(bindPoint), program.pipelineLayout,
                                   0, static_cast<uint32_t>(writes.size()),
                                   reinterpret_cast<const VkWriteDescriptorSet*>(writes.data()));
        }
    };

    // A swapchain image used for the first time since it was acquired: the batch waits for the acquire. Calls already
    // in the batch are submitted first, so they don't wait for the window as well.
    std::vector<Swapchain*> acquiredSwapchains;
    for (const UsedImage &used : usedImages) {
        if (used.image->swapchain) {
            Swapchain &swapchain = m_swapchains.at(used.image->swapchain);
            if (swapchain.pendingAcquire >= 0) {
                acquiredSwapchains.push_back(&swapchain);
            }
        }
    }
    if (!acquiredSwapchains.empty() && m_batch.callCount > 0) {
        flushBatch();
    }

    // Step 3: record the program into the open batch.
    vk::CommandBuffer commandBuffer = batchCommands();

    // Orders this call after everything earlier on the all queue (e.g. a compute writing a storage buffer that this
    // draw reads as vertices) and moves images into the layouts this call needs.
    vk::MemoryBarrier2 memoryBarrier{};
    memoryBarrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    memoryBarrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
    memoryBarrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    memoryBarrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    std::vector<vk::ImageMemoryBarrier2> imageBarriers;
    for (const UsedImage &used : usedImages) {
        Image &image = *used.image;
        vk::ImageLayout oldLayout = used.discard ? vk::ImageLayout::eUndefined : image.layout;
        if (oldLayout != used.layout) {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = used.layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange = vk::ImageSubresourceRange(image.aspect, 0, 1, 0, 1);
            imageBarriers.push_back(barrier);
        }
        image.layout = used.layout;
    }
    vk::DependencyInfo dependency{};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memoryBarrier;
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependency.pImageMemoryBarriers = imageBarriers.data();
    commandBuffer.pipelineBarrier2(dependency);

    if (program.type == PROGRAM_TYPE_COMPUTE) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, program.pipeline);
        bindDescriptors(commandBuffer, vk::PipelineBindPoint::eCompute);
        commandBuffer.dispatch(call.groupCountX, call.groupCountY, call.groupCountZ);
    } else {
        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        for (size_t i = 0; i < call.colorTargets.size(); i++) {
            const ColorTarget &target = call.colorTargets[i];
            vk::RenderingAttachmentInfo attachment{};
            attachment.imageView = m_images.at(target.image).view;
            attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            attachment.loadOp = target.clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            attachment.storeOp = target.store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
            attachment.clearValue.color = vk::ClearColorValue(std::array<float, 4>{
                target.clearColor[0], target.clearColor[1], target.clearColor[2], target.clearColor[3]});
            if (target.resolveImage) {
                // Integer samples can't be averaged, those resolve to sample 0.
                attachment.resolveMode = isIntegerFormat(program.colorFormats[i]) ? vk::ResolveModeFlagBits::eSampleZero
                                                                                 : vk::ResolveModeFlagBits::eAverage;
                attachment.resolveImageView = m_images.at(target.resolveImage).view;
                attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            }
            colorAttachments.push_back(attachment);
        }
        const bool hasDepth = program.depthFormat != IMAGE_FORMAT_UNDEFINED;
        vk::RenderingAttachmentInfo depthAttachment{};
        if (hasDepth) {
            depthAttachment.imageView = m_images.at(call.depthTarget.image).view;
            depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthAttachment.loadOp = call.depthTarget.clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            depthAttachment.storeOp = call.depthTarget.store ? vk::AttachmentStoreOp::eStore
                                                             : vk::AttachmentStoreOp::eDontCare;
            depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(call.depthTarget.clearDepth, 0);
        }
        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D({0, 0}, extent);
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();
        renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;
        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, program.pipeline);
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D({0, 0}, extent));
        bindDescriptors(commandBuffer, vk::PipelineBindPoint::eGraphics);
        if (!call.vertexBuffers.empty()) {
            std::vector<vk::Buffer> vertexBuffers;
            for (uint64_t vertexBuffer : call.vertexBuffers) {
                vertexBuffers.push_back(m_buffers.at(vertexBuffer).buffer);
            }
            std::vector<vk::DeviceSize> offsets(vertexBuffers.size(), 0);
            commandBuffer.bindVertexBuffers(0, vertexBuffers, offsets);
        }
        if (call.indexType != INDEX_TYPE_NONE) {
            commandBuffer.bindIndexBuffer(m_buffers.at(call.indexBuffer).buffer, 0, toVkIndexType(call.indexType));
            commandBuffer.drawIndexed(call.indexCount, call.instanceCount, 0, 0, 0);
        } else {
            commandBuffer.draw(call.vertexCount, call.instanceCount, 0, 0);
        }
        commandBuffer.endRendering();
    }

    // The batch waits for any pending upload into a resource this call uses.
    m_allValue = ticket;
    m_batch.callCount++;
    for (Buffer* buffer : usedBuffers) {
        m_batch.waitTransfer = std::max(m_batch.waitTransfer, buffer->lastTransferUse);
        buffer->lastAllUse = ticket;
    }
    for (const UsedImage &used : usedImages) {
        m_batch.waitTransfer = std::max(m_batch.waitTransfer, used.image->lastTransferUse);
        used.image->lastAllUse = ticket;
    }
    for (Swapchain* swapchain : acquiredSwapchains) {
        AcquireSemaphore &acquire = swapchain->acquireSemaphores[static_cast<size_t>(swapchain->pendingAcquire)];
        m_batch.acquireWaits.push_back(acquire.semaphore);
        acquire.allValue = ticket;
        swapchain->pendingAcquire = -1;
    }
    program.lastAllUse = ticket;
    if (descriptorSet) {
        m_descriptorPool.lastAllUse = ticket;
    }
    if (uniformSize > 0) {
        m_uniformRing.regions.push_back({uniformBase, uniformSize, ticket});
    }

    // Submitted right away while the all queue has nothing to do, otherwise batched with the calls that follow until
    // the queue runs dry, the batch is full, or something needs the results.
    if (m_batch.callCount >= MAX_BATCH_CALLS || completedValue(QUEUE_ALL) >= m_allSubmitted) {
        flushBatch();
    }
    return ticket;
}
