//
// Created by Stefan Balta on 2026-09-21.
//

#include "pixelKilnImpl.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

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
    std::vector<Buffer*> usedBuffers;
    std::unordered_map<uint64_t, vk::ImageLayout> usedImages; // image handle -> layout the call needs it in
    std::unordered_set<uint64_t> clearedImages; // targets that are cleared, their old contents can be dropped
    auto useImage = [&](uint64_t handle, vk::ImageLayout layout) {
        auto [it, inserted] = usedImages.emplace(handle, layout);
        if (!inserted && it->second != layout) {
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
                useImage(binding.resource, vk::ImageLayout::eShaderReadOnlyOptimal);
                break;
            }
            case UNIFORM_BINDING_TYPE_STORAGE_IMAGE: {
                Image &image = getImage(binding.resource);
                if (!(image.desc.usage & IMAGE_USAGE_STORAGE)) {
                    throw std::invalid_argument("PixelKiln: storage image binding needs an image created with IMAGE_USAGE_STORAGE");
                }
                useImage(binding.resource, vk::ImageLayout::eGeneral);
                break;
            }
            default:
                continue; // EMPTY
        }
        hasDescriptors = true;
    }

    vk::Extent2D extent{};
    if (program.type == PROGRAM_TYPE_RASTER_DRAW) {
        if (call.colorTargets.size() != program.colorFormats.size()) {
            throw std::invalid_argument("PixelKiln: a draw needs one color target per program color format");
        }
        auto useTarget = [&](uint64_t handle, ImageFormat format, ImageUsage usage, bool clear, vk::ImageLayout layout) {
            Image &image = getImage(handle);
            if (!(image.desc.usage & usage) || image.desc.format != format) {
                throw std::invalid_argument("PixelKiln: target image usage or format does not match the program");
            }
            if (extent.width == 0) {
                extent = vk::Extent2D(image.desc.width, image.desc.height);
            } else if (extent.width != image.desc.width || extent.height != image.desc.height) {
                throw std::invalid_argument("PixelKiln: all targets of a draw must be the same size");
            }
            useImage(handle, layout);
            if (clear) {
                clearedImages.insert(handle);
            }
        };
        for (size_t i = 0; i < call.colorTargets.size(); i++) {
            useTarget(call.colorTargets[i].image, program.colorFormats[i], IMAGE_USAGE_COLOR_TARGET,
                      call.colorTargets[i].clear, vk::ImageLayout::eColorAttachmentOptimal);
        }
        if (program.depthFormat != IMAGE_FORMAT_UNDEFINED) {
            useTarget(call.depthTarget.image, program.depthFormat, IMAGE_USAGE_DEPTH_TARGET, call.depthTarget.clear,
                      vk::ImageLayout::eDepthStencilAttachmentOptimal);
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

    // Step 1: stage the uniform data and upload it on the transfer queue.
    const uint64_t uniformRegionSize = alignUp(uniformSize, uniformAlignment);
    uint64_t uniformBase = 0;
    uint64_t uniformUpload = 0;
    if (uniformSize > 0) {
        uniformBase = allocateUniforms(uniformRegionSize);
        for (size_t i = 0; i < call.bindings.size(); i++) {
            if (program.bindings[i] == UNIFORM_BINDING_TYPE_BUFFER) {
                std::memcpy(static_cast<char*>(m_uniformStaging.mapped) + uniformBase + uniformOffsets[i],
                            call.bindings[i].data, call.bindings[i].size);
            }
        }
        vk::CommandBuffer transfer = beginCommands(QUEUE_TRANSFER);
        vk::BufferCopy region{uniformBase, uniformBase, uniformSize};
        transfer.copyBuffer(m_uniformStaging.buffer, m_uniformBuffer.buffer, region);
        // The region was last read by a call that has completed (m_uniformReuseValue or earlier); waiting on it
        // orders the overwrite after that read without stalling.
        uniformUpload = submitCommands(QUEUE_TRANSFER, transfer, m_uniformReuseValue);
    }

    // Step 2: record the program on the all queue.
    vk::DescriptorSet descriptorSet;
    if (hasDescriptors) {
        descriptorSet = allocateDescriptorSet(program.setLayout);
        std::vector<vk::DescriptorBufferInfo> bufferInfos;
        std::vector<vk::DescriptorImageInfo> imageInfos;
        bufferInfos.reserve(call.bindings.size());
        imageInfos.reserve(call.bindings.size());
        std::vector<vk::WriteDescriptorSet> writes;
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
                    bufferInfos.emplace_back(m_uniformBuffer.buffer, uniformBase + uniformOffsets[i], binding.size);
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
        m_device.updateDescriptorSets(writes, nullptr);
    }

    vk::CommandBuffer commandBuffer = beginCommands(QUEUE_ALL);

    // Orders this call after everything earlier on the all queue (e.g. a compute writing a storage buffer that this
    // draw reads as vertices) and moves images into the layouts this call needs.
    vk::MemoryBarrier2 memoryBarrier{};
    memoryBarrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    memoryBarrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
    memoryBarrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    memoryBarrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    std::vector<vk::ImageMemoryBarrier2> imageBarriers;
    for (const auto& [handle, layout] : usedImages) {
        Image &image = m_images.at(handle);
        vk::ImageLayout oldLayout = clearedImages.count(handle) ? vk::ImageLayout::eUndefined : image.layout;
        if (oldLayout != layout) {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange = vk::ImageSubresourceRange(image.aspect, 0, 1, 0, 1);
            imageBarriers.push_back(barrier);
        }
        image.layout = layout;
    }
    vk::DependencyInfo dependency{};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memoryBarrier;
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependency.pImageMemoryBarriers = imageBarriers.data();
    commandBuffer.pipelineBarrier2(dependency);

    if (program.type == PROGRAM_TYPE_COMPUTE) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, program.pipeline);
        if (descriptorSet) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, program.pipelineLayout, 0,
                                             descriptorSet, nullptr);
        }
        commandBuffer.dispatch(call.groupCountX, call.groupCountY, call.groupCountZ);
    } else {
        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        for (const ColorTarget &target : call.colorTargets) {
            vk::RenderingAttachmentInfo attachment{};
            attachment.imageView = m_images.at(target.image).view;
            attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            attachment.loadOp = target.clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            attachment.storeOp = vk::AttachmentStoreOp::eStore;
            attachment.clearValue.color = vk::ClearColorValue(std::array<float, 4>{
                target.clearColor[0], target.clearColor[1], target.clearColor[2], target.clearColor[3]});
            colorAttachments.push_back(attachment);
        }
        const bool hasDepth = program.depthFormat != IMAGE_FORMAT_UNDEFINED;
        vk::RenderingAttachmentInfo depthAttachment{};
        if (hasDepth) {
            depthAttachment.imageView = m_images.at(call.depthTarget.image).view;
            depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthAttachment.loadOp = call.depthTarget.clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
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
        if (descriptorSet) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, program.pipelineLayout, 0,
                                             descriptorSet, nullptr);
        }
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

    // Step 3: submit, waiting for this call's uniform upload and any pending upload into a resource it uses.
    uint64_t waitTransfer = uniformUpload;
    for (Buffer* buffer : usedBuffers) {
        waitTransfer = std::max(waitTransfer, buffer->lastTransferUse);
    }
    for (const auto& [handle, layout] : usedImages) {
        waitTransfer = std::max(waitTransfer, m_images.at(handle).lastTransferUse);
    }
    uint64_t ticket = submitCommands(QUEUE_ALL, commandBuffer, waitTransfer);

    for (Buffer* buffer : usedBuffers) {
        buffer->lastAllUse = ticket;
    }
    for (const auto& [handle, layout] : usedImages) {
        m_images.at(handle).lastAllUse = ticket;
    }
    program.lastAllUse = ticket;
    if (descriptorSet) {
        m_descriptorPool.lastAllUse = ticket;
    }
    if (uniformSize > 0) {
        m_uniformRegions.push_back({uniformBase, uniformRegionSize, ticket});
    }
    return ticket;
}
