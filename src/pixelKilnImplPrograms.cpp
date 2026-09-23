//
// Created by Stefan Balta on 2026-09-21.
//

#include "pixelKilnImpl.h"

#include <stdexcept>

#include "vulkanTranslate.h"

PixelKilnImpl::Program &PixelKilnImpl::getProgram(uint64_t program) {
    auto it = m_programs.find(program);
    if (it == m_programs.end()) {
        throw std::invalid_argument("PixelKiln: unknown program handle");
    }
    return it->second;
}

// Binding i of set 0 is uniformBindings[i], EMPTY entries leave a hole.
vk::DescriptorSetLayout PixelKilnImpl::createSetLayout(const UniformBindings &bindings, vk::ShaderStageFlags stages,
                                                       bool &pushDescriptors) {
    std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
    uint32_t counts[UNIFORM_BINDING_TYPE_COUNT] = {};
    for (size_t i = 0; i < bindings.size(); i++) {
        if (bindings[i] == UNIFORM_BINDING_TYPE_EMPTY) {
            continue;
        }
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = static_cast<uint32_t>(i);
        binding.descriptorType = toVkDescriptorType(bindings[i]);
        binding.descriptorCount = 1;
        binding.stageFlags = stages;
        if (++counts[bindings[i]] > DESCRIPTORS_PER_TYPE) {
            throw std::invalid_argument("PixelKiln: too many uniform bindings of one type in a program");
        }
        layoutBindings.push_back(binding);
    }
    pushDescriptors = !layoutBindings.empty() && layoutBindings.size() <= m_maxPushDescriptors;
    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    if (pushDescriptors) {
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
    }
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();
    return m_device.createDescriptorSetLayout(layoutInfo);
}

vk::ShaderModule PixelKilnImpl::createShaderModule(const Shader &shader) {
    if (!shader.spirv || shader.spirvSize == 0 || shader.spirvSize % 4 != 0) {
        throw std::invalid_argument("PixelKiln: shader needs SPIR-V code with a size in bytes that is a multiple of 4");
    }
    vk::ShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.codeSize = shader.spirvSize;
    shaderModuleCreateInfo.pCode = shader.spirv;
    return m_device.createShaderModule(shaderModuleCreateInfo);
}

uint64_t PixelKilnImpl::loadComputeProgram(const ComputeProgram &program) {
    collectGarbage();
    if (program.pushConstantSize > m_physicalDeviceProperties.limits.maxPushConstantsSize) {
        throw std::invalid_argument("PixelKiln: push constant size is too large for this device");
    }
    Program loaded;
    loaded.type = PROGRAM_TYPE_COMPUTE;
    loaded.bindings = program.uniformBindings;
    loaded.pushConstantSize = program.pushConstantSize;
    loaded.pushConstantStages = vk::ShaderStageFlagBits::eCompute;
    vk::ShaderModule shaderModule = createShaderModule(program.computeShader);
    try {
        loaded.setLayout = createSetLayout(program.uniformBindings, vk::ShaderStageFlagBits::eCompute,
                                           loaded.pushDescriptors);
        vk::PushConstantRange pushConstantRange{loaded.pushConstantStages, 0, loaded.pushConstantSize};
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &loaded.setLayout;
        if (loaded.pushConstantSize > 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;
        }
        loaded.pipelineLayout = m_device.createPipelineLayout(layoutInfo);

        vk::PipelineShaderStageCreateInfo shaderStageCreateInfo{};
        shaderStageCreateInfo.module = shaderModule;
        shaderStageCreateInfo.pName = "main";
        shaderStageCreateInfo.stage = vk::ShaderStageFlagBits::eCompute;
        shaderStageCreateInfo.pSpecializationInfo = nullptr;
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shaderStageCreateInfo;
        pipelineInfo.layout = loaded.pipelineLayout;
        loaded.pipeline = m_device.createComputePipeline(nullptr, pipelineInfo).value;
    } catch (...) {
        m_device.destroyShaderModule(shaderModule);
        m_device.destroyPipelineLayout(loaded.pipelineLayout);
        m_device.destroyDescriptorSetLayout(loaded.setLayout);
        throw;
    }
    m_device.destroyShaderModule(shaderModule);
    uint64_t handle = m_nextHandle++;
    m_programs[handle] = loaded;
    return handle;
}

uint64_t PixelKilnImpl::loadRasterDrawProgram(const RasterDrawProgram &program) {
    collectGarbage();
    const vk::PhysicalDeviceLimits &limits = m_physicalDeviceProperties.limits;
    if (program.colorFormats.empty() && program.depthFormat == IMAGE_FORMAT_UNDEFINED) {
        throw std::invalid_argument("PixelKiln: a raster draw program needs at least one color or depth target");
    }
    if (program.pushConstantSize > limits.maxPushConstantsSize) {
        throw std::invalid_argument("PixelKiln: push constant size is too large for this device");
    }
    if (program.colorFormats.size() > limits.maxColorAttachments) {
        throw std::invalid_argument("PixelKiln: too many color targets for this device");
    }
    std::vector<vk::Format> colorFormats;
    for (ImageFormat format : program.colorFormats) {
        if (format == IMAGE_FORMAT_UNDEFINED || isDepthFormat(format)) {
            throw std::invalid_argument("PixelKiln: colorFormats must be color formats");
        }
        vk::FormatFeatureFlags required = vk::FormatFeatureFlagBits::eColorAttachment;
        if (program.blendEnable) {
            required |= vk::FormatFeatureFlagBits::eColorAttachmentBlend;
        }
        if ((formatFeatures(format) & required) != required) {
            throw std::invalid_argument("PixelKiln: color format can't be rendered (or blended) to on this device");
        }
        colorFormats.push_back(toVkFormat(format));
    }
    const bool hasDepth = program.depthFormat != IMAGE_FORMAT_UNDEFINED;
    if (hasDepth) {
        if (!isDepthFormat(program.depthFormat)) {
            throw std::invalid_argument("PixelKiln: depthFormat must be a depth format");
        }
        if (!(formatFeatures(program.depthFormat) & vk::FormatFeatureFlagBits::eDepthStencilAttachment)) {
            throw std::invalid_argument("PixelKiln: depth format can't be rendered to on this device");
        }
    }
    const vk::SampleCountFlagBits samples = toVkSampleCount(program.samples);
    if (program.samples > 1) {
        for (ImageFormat format : program.colorFormats) {
            if (!(getSupportedSampleCounts(format) & program.samples)) {
                throw std::invalid_argument("PixelKiln: this device can't render the program's sample count to one "
                                            "of its color formats");
            }
        }
        if (hasDepth && !(getSupportedSampleCounts(program.depthFormat) & program.samples)) {
            throw std::invalid_argument("PixelKiln: this device can't render the program's sample count to its depth "
                                        "format");
        }
    }

    const VertexLayout &vertexLayout = program.vertexLayout;
    if (vertexLayout.buffers.size() > limits.maxVertexInputBindings ||
        vertexLayout.attributes.size() > limits.maxVertexInputAttributes) {
        throw std::invalid_argument("PixelKiln: too many vertex buffers or attributes for this device");
    }
    std::vector<vk::VertexInputBindingDescription> vertexBindings;
    for (size_t i = 0; i < vertexLayout.buffers.size(); i++) {
        if (vertexLayout.buffers[i].stride > limits.maxVertexInputBindingStride) {
            throw std::invalid_argument("PixelKiln: vertex buffer stride is too large for this device");
        }
        vk::VertexInputBindingDescription binding{};
        binding.binding = static_cast<uint32_t>(i);
        binding.stride = vertexLayout.buffers[i].stride;
        binding.inputRate = vertexLayout.buffers[i].perInstance ? vk::VertexInputRate::eInstance
                                                                : vk::VertexInputRate::eVertex;
        vertexBindings.push_back(binding);
    }
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes;
    for (const VertexAttribute &attribute : vertexLayout.attributes) {
        if (attribute.buffer >= vertexLayout.buffers.size()) {
            throw std::invalid_argument("PixelKiln: vertex attribute refers to a vertex buffer that isn't in the layout");
        }
        if (attribute.location >= limits.maxVertexInputAttributes ||
            attribute.offset > limits.maxVertexInputAttributeOffset) {
            throw std::invalid_argument("PixelKiln: vertex attribute location or offset is too large for this device");
        }
        vk::VertexInputAttributeDescription description{};
        description.location = attribute.location;
        description.binding = attribute.buffer;
        description.format = toVkFormat(attribute.format);
        description.offset = attribute.offset;
        vertexAttributes.push_back(description);
    }
    vk::PrimitiveTopology topology = toVkTopology(program.topology);
    vk::CullModeFlags cullMode = toVkCullMode(program.cullMode);

    Program loaded;
    loaded.type = PROGRAM_TYPE_RASTER_DRAW;
    loaded.bindings = program.uniformBindings;
    loaded.colorFormats = program.colorFormats;
    loaded.depthFormat = program.depthFormat;
    loaded.samples = program.samples;
    loaded.vertexBufferCount = static_cast<uint32_t>(vertexLayout.buffers.size());
    loaded.pushConstantSize = program.pushConstantSize;
    loaded.pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::ShaderModule vertexModule = createShaderModule(program.vertexShader);
    vk::ShaderModule fragmentModule;
    try {
        fragmentModule = createShaderModule(program.fragmentShader);
        loaded.setLayout = createSetLayout(program.uniformBindings,
                                           vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                           loaded.pushDescriptors);
        vk::PushConstantRange pushConstantRange{loaded.pushConstantStages, 0, loaded.pushConstantSize};
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &loaded.setLayout;
        if (loaded.pushConstantSize > 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;
        }
        loaded.pipelineLayout = m_device.createPipelineLayout(layoutInfo);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
        vertexInput.pVertexBindingDescriptions = vertexBindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = topology;

        // Viewport and scissor are dynamic, set to the target size by each call.
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterization{};
        rasterization.polygonMode = vk::PolygonMode::eFill;
        rasterization.cullMode = cullMode;
        rasterization.frontFace = vk::FrontFace::eCounterClockwise;
        rasterization.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo multisample{};
        multisample.rasterizationSamples = samples;
        multisample.alphaToCoverageEnable = program.alphaToCoverage;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = program.depthTest;
        depthStencil.depthWriteEnable = program.depthWrite;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;

        std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(colorFormats.size());
        for (auto& blend : blendAttachments) {
            blend.blendEnable = program.blendEnable;
            blend.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            blend.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            blend.colorBlendOp = vk::BlendOp::eAdd;
            blend.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            blend.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            blend.alphaBlendOp = vk::BlendOp::eAdd;
            blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        }
        vk::PipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        colorBlend.pAttachments = blendAttachments.data();

        vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = hasDepth ? toVkFormat(program.depthFormat) : vk::Format::eUndefined;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = hasDepth ? &depthStencil : nullptr;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = loaded.pipelineLayout;
        loaded.pipeline = m_device.createGraphicsPipeline(nullptr, pipelineInfo).value;
    } catch (...) {
        m_device.destroyShaderModule(vertexModule);
        m_device.destroyShaderModule(fragmentModule);
        m_device.destroyPipelineLayout(loaded.pipelineLayout);
        m_device.destroyDescriptorSetLayout(loaded.setLayout);
        throw;
    }
    m_device.destroyShaderModule(vertexModule);
    m_device.destroyShaderModule(fragmentModule);
    uint64_t handle = m_nextHandle++;
    m_programs[handle] = loaded;
    return handle;
}

void PixelKilnImpl::unloadProgram(uint64_t program) {
    collectGarbage();
    Program unloaded = getProgram(program);
    m_programs.erase(program);
    vk::Device device = m_device;
    deferDestroy(unloaded.lastAllUse, 0, [device, unloaded]() {
        device.destroyPipeline(unloaded.pipeline);
        device.destroyPipelineLayout(unloaded.pipelineLayout);
        device.destroyDescriptorSetLayout(unloaded.setLayout);
    });
}
