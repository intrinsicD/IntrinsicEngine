module;

#include <algorithm>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Pipelines;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §2  Mapping tables  (RHI enums → Vulkan enums)
// =============================================================================

VkFormat ToVkFormat(RHI::Format f)
{
    switch (f)
    {
    case RHI::Format::Undefined:          return VK_FORMAT_UNDEFINED;
    case RHI::Format::R8_UNORM:           return VK_FORMAT_R8_UNORM;
    case RHI::Format::RG8_UNORM:          return VK_FORMAT_R8G8_UNORM;
    case RHI::Format::RGBA8_UNORM:        return VK_FORMAT_R8G8B8A8_UNORM;
    case RHI::Format::RGBA8_SRGB:         return VK_FORMAT_R8G8B8A8_SRGB;
    case RHI::Format::BGRA8_UNORM:        return VK_FORMAT_B8G8R8A8_UNORM;
    case RHI::Format::BGRA8_SRGB:         return VK_FORMAT_B8G8R8A8_SRGB;
    case RHI::Format::R16_FLOAT:          return VK_FORMAT_R16_SFLOAT;
    case RHI::Format::RG16_FLOAT:         return VK_FORMAT_R16G16_SFLOAT;
    case RHI::Format::RGBA16_FLOAT:       return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RHI::Format::R16_UINT:           return VK_FORMAT_R16_UINT;
    case RHI::Format::R16_UNORM:          return VK_FORMAT_R16_UNORM;
    case RHI::Format::R32_FLOAT:          return VK_FORMAT_R32_SFLOAT;
    case RHI::Format::RG32_FLOAT:         return VK_FORMAT_R32G32_SFLOAT;
    case RHI::Format::RGB32_FLOAT:        return VK_FORMAT_R32G32B32_SFLOAT;
    case RHI::Format::RGBA32_FLOAT:       return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RHI::Format::R32_UINT:           return VK_FORMAT_R32_UINT;
    case RHI::Format::R32_SINT:           return VK_FORMAT_R32_SINT;
    case RHI::Format::D16_UNORM:          return VK_FORMAT_D16_UNORM;
    case RHI::Format::D32_FLOAT:          return VK_FORMAT_D32_SFLOAT;
    case RHI::Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
    case RHI::Format::D32_FLOAT_S8_UINT:  return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case RHI::Format::BC1_UNORM:          return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case RHI::Format::BC3_UNORM:          return VK_FORMAT_BC3_UNORM_BLOCK;
    case RHI::Format::BC5_UNORM:          return VK_FORMAT_BC5_UNORM_BLOCK;
    case RHI::Format::BC7_UNORM:          return VK_FORMAT_BC7_UNORM_BLOCK;
    case RHI::Format::BC7_SRGB:           return VK_FORMAT_BC7_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

VkImageLayout ToVkImageLayout(RHI::TextureLayout l)
{
    switch (l)
    {
    case RHI::TextureLayout::Undefined:         return VK_IMAGE_LAYOUT_UNDEFINED;
    case RHI::TextureLayout::General:           return VK_IMAGE_LAYOUT_GENERAL;
    case RHI::TextureLayout::ColorAttachment:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case RHI::TextureLayout::DepthAttachment:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case RHI::TextureLayout::DepthReadOnly:     return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case RHI::TextureLayout::ShaderReadOnly:    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case RHI::TextureLayout::TransferSrc:       return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case RHI::TextureLayout::TransferDst:       return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case RHI::TextureLayout::Present:           return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkFilter ToVkFilter(RHI::FilterMode m)
{
    return m == RHI::FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

VkSamplerMipmapMode ToVkMipmapMode(RHI::MipmapMode m)
{
    return m == RHI::MipmapMode::Linear
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkSamplerAddressMode ToVkAddressMode(RHI::AddressMode m)
{
    switch (m)
    {
    case RHI::AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RHI::AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RHI::AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case RHI::AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkCompareOp ToVkCompareOp(RHI::CompareOp o)
{
    switch (o)
    {
    case RHI::CompareOp::Never:        return VK_COMPARE_OP_NEVER;
    case RHI::CompareOp::Less:         return VK_COMPARE_OP_LESS;
    case RHI::CompareOp::Equal:        return VK_COMPARE_OP_EQUAL;
    case RHI::CompareOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHI::CompareOp::Greater:      return VK_COMPARE_OP_GREATER;
    case RHI::CompareOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case RHI::CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHI::CompareOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

VkCompareOp ToVkDepthOp(RHI::DepthOp o)
{
    switch (o)
    {
    case RHI::DepthOp::Never:        return VK_COMPARE_OP_NEVER;
    case RHI::DepthOp::Less:         return VK_COMPARE_OP_LESS;
    case RHI::DepthOp::Equal:        return VK_COMPARE_OP_EQUAL;
    case RHI::DepthOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHI::DepthOp::Greater:      return VK_COMPARE_OP_GREATER;
    case RHI::DepthOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case RHI::DepthOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHI::DepthOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

VkPrimitiveTopology ToVkTopology(RHI::Topology t)
{
    switch (t)
    {
    case RHI::Topology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case RHI::Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case RHI::Topology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case RHI::Topology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case RHI::Topology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags ToVkCullMode(RHI::CullMode c)
{
    switch (c)
    {
    case RHI::CullMode::None:  return VK_CULL_MODE_NONE;
    case RHI::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RHI::CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkFrontFace ToVkFrontFace(RHI::FrontFace f)
{
    return f == RHI::FrontFace::CounterClockwise
        ? VK_FRONT_FACE_COUNTER_CLOCKWISE
        : VK_FRONT_FACE_CLOCKWISE;
}

VkPolygonMode ToVkFillMode(RHI::FillMode f)
{
    return f == RHI::FillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
}

VkBlendFactor ToVkBlendFactor(RHI::BlendFactor b)
{
    switch (b)
    {
    case RHI::BlendFactor::Zero:              return VK_BLEND_FACTOR_ZERO;
    case RHI::BlendFactor::One:               return VK_BLEND_FACTOR_ONE;
    case RHI::BlendFactor::SrcColor:          return VK_BLEND_FACTOR_SRC_COLOR;
    case RHI::BlendFactor::OneMinusSrcColor:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case RHI::BlendFactor::SrcAlpha:          return VK_BLEND_FACTOR_SRC_ALPHA;
    case RHI::BlendFactor::OneMinusSrcAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case RHI::BlendFactor::DstAlpha:          return VK_BLEND_FACTOR_DST_ALPHA;
    case RHI::BlendFactor::OneMinusDstAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ZERO;
}

VkBlendOp ToVkBlendOp(RHI::BlendOp o)
{
    switch (o)
    {
    case RHI::BlendOp::Add:             return VK_BLEND_OP_ADD;
    case RHI::BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case RHI::BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case RHI::BlendOp::Min:             return VK_BLEND_OP_MIN;
    case RHI::BlendOp::Max:             return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkImageAspectFlags AspectFromFormat(VkFormat f)
{
    switch (f)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}


VkIndexType ToVkIndexType(RHI::IndexType t)
{
    switch (t)
    {
    case RHI::IndexType::Uint16: return VK_INDEX_TYPE_UINT16;
    case RHI::IndexType::Uint32: return VK_INDEX_TYPE_UINT32;
    }
    return VK_INDEX_TYPE_UINT32;
}

VkBufferUsageFlags ToVkBufferUsage(RHI::BufferUsage u)
{
    VkBufferUsageFlags flags = 0;
    if (RHI::HasUsage(u, RHI::BufferUsage::Vertex))      flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::Index))       flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::Uniform))     flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::Storage))
    {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
               | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    if (RHI::HasUsage(u, RHI::BufferUsage::Indirect))    flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (RHI::HasUsage(u, RHI::BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkImageUsageFlags ToVkTextureUsage(RHI::TextureUsage u)
{
    VkImageUsageFlags flags = 0;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::Sampled))     flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::Storage))     flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::ColorTarget)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::DepthTarget)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (static_cast<uint32_t>(u) & static_cast<uint32_t>(RHI::TextureUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkAccessFlags2 ToVkAccess(RHI::MemoryAccess a)
{
    VkAccessFlags2 flags = 0;
    const auto u = static_cast<uint8_t>(a);
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndirectRead))  flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndexRead))     flags |= VK_ACCESS_2_INDEX_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::ShaderRead))    flags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::ShaderWrite))   flags |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::TransferRead))  flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::TransferWrite)) flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::HostRead))      flags |= VK_ACCESS_2_HOST_READ_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::HostWrite))     flags |= VK_ACCESS_2_HOST_WRITE_BIT;
    return flags;
}

VkPipelineStageFlags2 ToVkStage(RHI::MemoryAccess a)
{
    const auto u = static_cast<uint8_t>(a);
    if (u == 0) return VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 flags = 0;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndirectRead))  flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    if (u & static_cast<uint8_t>(RHI::MemoryAccess::IndexRead))     flags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    if (u & (static_cast<uint8_t>(RHI::MemoryAccess::ShaderRead) |
             static_cast<uint8_t>(RHI::MemoryAccess::ShaderWrite)))
        flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
               | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
               | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if (u & (static_cast<uint8_t>(RHI::MemoryAccess::TransferRead) |
             static_cast<uint8_t>(RHI::MemoryAccess::TransferWrite)))
        flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    if (u & (static_cast<uint8_t>(RHI::MemoryAccess::HostRead) |
             static_cast<uint8_t>(RHI::MemoryAccess::HostWrite)))
        flags |= VK_PIPELINE_STAGE_2_HOST_BIT;
    return flags;
}

VkPresentModeKHR ToVkPresentMode(RHI::PresentMode m,
                                   const std::vector<VkPresentModeKHR>& available)
{
    auto has = [&](VkPresentModeKHR mode) {
        return std::find(available.begin(), available.end(), mode) != available.end();
    };
    switch (m)
    {
    case RHI::PresentMode::LowLatency:
        if (has(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
        break;
    case RHI::PresentMode::Uncapped:
        if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
        break;
    case RHI::PresentMode::Throttled:
        if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        break;
    default: break;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // always supported
}

} // namespace Extrinsic::Backends::Vulkan

