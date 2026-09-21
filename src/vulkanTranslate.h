//
// Created by Stefan Balta on 2026-09-21.
//

#ifndef PIXELKILN_VULKANTRANSLATE_H
#define PIXELKILN_VULKANTRANSLATE_H
#include <cstdint>
#include <stdexcept>

#include <vulkan/vulkan.hpp>

#include "cullMode.h"
#include "imageFormat.h"
#include "indexType.h"
#include "primitiveTopology.h"
#include "samplerDesc.h"
#include "uniformBindings.h"
#include "vertexLayout.h"

// Translation from the public PixelKiln enums to Vulkan. Out of range values throw std::invalid_argument.

inline vk::Format toVkFormat(ImageFormat format)
{
    switch (format) {
        case IMAGE_FORMAT_UNDEFINED: return vk::Format::eUndefined;
        case IMAGE_FORMAT_RGBA8_UNORM: return vk::Format::eR8G8B8A8Unorm;
        case IMAGE_FORMAT_RGBA8_SRGB: return vk::Format::eR8G8B8A8Srgb;
        case IMAGE_FORMAT_BGRA8_UNORM: return vk::Format::eB8G8R8A8Unorm;
        case IMAGE_FORMAT_BGRA8_SRGB: return vk::Format::eB8G8R8A8Srgb;
        case IMAGE_FORMAT_R8_UNORM: return vk::Format::eR8Unorm;
        case IMAGE_FORMAT_R32_FLOAT: return vk::Format::eR32Sfloat;
        case IMAGE_FORMAT_RG32_FLOAT: return vk::Format::eR32G32Sfloat;
        case IMAGE_FORMAT_RGBA16_FLOAT: return vk::Format::eR16G16B16A16Sfloat;
        case IMAGE_FORMAT_RGBA32_FLOAT: return vk::Format::eR32G32B32A32Sfloat;
        case IMAGE_FORMAT_R32_UINT: return vk::Format::eR32Uint;
        case IMAGE_FORMAT_D16_UNORM: return vk::Format::eD16Unorm;
        case IMAGE_FORMAT_D32_FLOAT: return vk::Format::eD32Sfloat;
        default: throw std::invalid_argument("PixelKiln: invalid ImageFormat");
    }
}

// The swapchain formats PixelKiln can hand out, IMAGE_FORMAT_UNDEFINED for anything else.
inline ImageFormat fromVkSwapchainFormat(vk::Format format)
{
    switch (format) {
        case vk::Format::eB8G8R8A8Unorm: return IMAGE_FORMAT_BGRA8_UNORM;
        case vk::Format::eB8G8R8A8Srgb: return IMAGE_FORMAT_BGRA8_SRGB;
        case vk::Format::eR8G8B8A8Unorm: return IMAGE_FORMAT_RGBA8_UNORM;
        case vk::Format::eR8G8B8A8Srgb: return IMAGE_FORMAT_RGBA8_SRGB;
        default: return IMAGE_FORMAT_UNDEFINED;
    }
}

inline bool isDepthFormat(ImageFormat format)
{
    return format == IMAGE_FORMAT_D16_UNORM || format == IMAGE_FORMAT_D32_FLOAT;
}

inline uint32_t texelSize(ImageFormat format)
{
    switch (format) {
        case IMAGE_FORMAT_R8_UNORM: return 1;
        case IMAGE_FORMAT_D16_UNORM: return 2;
        case IMAGE_FORMAT_RGBA8_UNORM:
        case IMAGE_FORMAT_RGBA8_SRGB:
        case IMAGE_FORMAT_BGRA8_UNORM:
        case IMAGE_FORMAT_BGRA8_SRGB:
        case IMAGE_FORMAT_R32_FLOAT:
        case IMAGE_FORMAT_R32_UINT:
        case IMAGE_FORMAT_D32_FLOAT: return 4;
        case IMAGE_FORMAT_RG32_FLOAT:
        case IMAGE_FORMAT_RGBA16_FLOAT: return 8;
        case IMAGE_FORMAT_RGBA32_FLOAT: return 16;
        default: throw std::invalid_argument("PixelKiln: invalid ImageFormat");
    }
}

inline vk::Format toVkFormat(VertexFormat format)
{
    switch (format) {
        case VERTEX_FORMAT_FLOAT: return vk::Format::eR32Sfloat;
        case VERTEX_FORMAT_FLOAT2: return vk::Format::eR32G32Sfloat;
        case VERTEX_FORMAT_FLOAT3: return vk::Format::eR32G32B32Sfloat;
        case VERTEX_FORMAT_FLOAT4: return vk::Format::eR32G32B32A32Sfloat;
        case VERTEX_FORMAT_INT: return vk::Format::eR32Sint;
        case VERTEX_FORMAT_INT2: return vk::Format::eR32G32Sint;
        case VERTEX_FORMAT_INT3: return vk::Format::eR32G32B32Sint;
        case VERTEX_FORMAT_INT4: return vk::Format::eR32G32B32A32Sint;
        case VERTEX_FORMAT_UINT: return vk::Format::eR32Uint;
        case VERTEX_FORMAT_UINT2: return vk::Format::eR32G32Uint;
        case VERTEX_FORMAT_UINT3: return vk::Format::eR32G32B32Uint;
        case VERTEX_FORMAT_UINT4: return vk::Format::eR32G32B32A32Uint;
        case VERTEX_FORMAT_UNORM8X4: return vk::Format::eR8G8B8A8Unorm;
        default: throw std::invalid_argument("PixelKiln: invalid VertexFormat");
    }
}

inline vk::PrimitiveTopology toVkTopology(PrimitiveTopology topology)
{
    switch (topology) {
        case PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return vk::PrimitiveTopology::eTriangleList;
        case PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return vk::PrimitiveTopology::eTriangleStrip;
        case PRIMITIVE_TOPOLOGY_LINE_LIST: return vk::PrimitiveTopology::eLineList;
        case PRIMITIVE_TOPOLOGY_LINE_STRIP: return vk::PrimitiveTopology::eLineStrip;
        case PRIMITIVE_TOPOLOGY_POINT_LIST: return vk::PrimitiveTopology::ePointList;
        default: throw std::invalid_argument("PixelKiln: invalid PrimitiveTopology");
    }
}

inline vk::CullModeFlags toVkCullMode(CullMode cullMode)
{
    switch (cullMode) {
        case CULL_MODE_NONE: return vk::CullModeFlagBits::eNone;
        case CULL_MODE_BACK: return vk::CullModeFlagBits::eBack;
        case CULL_MODE_FRONT: return vk::CullModeFlagBits::eFront;
        default: throw std::invalid_argument("PixelKiln: invalid CullMode");
    }
}

inline vk::DescriptorType toVkDescriptorType(UniformBindingType type)
{
    switch (type) {
        case UNIFORM_BINDING_TYPE_BUFFER: return vk::DescriptorType::eUniformBuffer;
        case UNIFORM_BINDING_TYPE_SAMPLER: return vk::DescriptorType::eCombinedImageSampler;
        case UNIFORM_BINDING_TYPE_STORAGE_IMAGE: return vk::DescriptorType::eStorageImage;
        case UNIFORM_BINDING_TYPE_STORAGE_BUFFER: return vk::DescriptorType::eStorageBuffer;
        default: throw std::invalid_argument("PixelKiln: invalid UniformBindingType");
    }
}

inline vk::Filter toVkFilter(SamplerFilter filter)
{
    switch (filter) {
        case SAMPLER_FILTER_NEAREST: return vk::Filter::eNearest;
        case SAMPLER_FILTER_LINEAR: return vk::Filter::eLinear;
        default: throw std::invalid_argument("PixelKiln: invalid SamplerFilter");
    }
}

inline vk::SamplerAddressMode toVkAddressMode(SamplerAddressMode addressMode)
{
    switch (addressMode) {
        case SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return vk::SamplerAddressMode::eClampToEdge;
        case SAMPLER_ADDRESS_MODE_REPEAT: return vk::SamplerAddressMode::eRepeat;
        case SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return vk::SamplerAddressMode::eMirroredRepeat;
        default: throw std::invalid_argument("PixelKiln: invalid SamplerAddressMode");
    }
}

inline vk::IndexType toVkIndexType(IndexType indexType)
{
    switch (indexType) {
        case INDEX_TYPE_UINT16: return vk::IndexType::eUint16;
        case INDEX_TYPE_UINT32: return vk::IndexType::eUint32;
        default: throw std::invalid_argument("PixelKiln: invalid IndexType");
    }
}

#endif //PIXELKILN_VULKANTRANSLATE_H
