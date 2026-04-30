module;

#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Pipelines;

export import Extrinsic.RHI.CommandContext;
export import Extrinsic.RHI.Descriptors;
export import Extrinsic.RHI.Types;

namespace Extrinsic::Backends::Vulkan
{
    export struct VulkanPipeline
    {
        VkPipeline          Pipeline   = VK_NULL_HANDLE;
        VkPipelineLayout    Layout     = VK_NULL_HANDLE;
        VkPipelineBindPoint BindPoint  = VK_PIPELINE_BIND_POINT_GRAPHICS;
        bool                OwnsLayout = false;
    };

    export [[nodiscard]] VkFormat              ToVkFormat(RHI::Format f);
    export [[nodiscard]] VkImageLayout         ToVkImageLayout(RHI::TextureLayout l);
    export [[nodiscard]] VkFilter              ToVkFilter(RHI::FilterMode m);
    export [[nodiscard]] VkSamplerMipmapMode   ToVkMipmapMode(RHI::MipmapMode m);
    export [[nodiscard]] VkSamplerAddressMode  ToVkAddressMode(RHI::AddressMode m);
    export [[nodiscard]] VkCompareOp           ToVkCompareOp(RHI::CompareOp o);
    export [[nodiscard]] VkPrimitiveTopology   ToVkTopology(RHI::Topology t);
    export [[nodiscard]] VkCullModeFlags       ToVkCullMode(RHI::CullMode c);
    export [[nodiscard]] VkFrontFace           ToVkFrontFace(RHI::FrontFace f);
    export [[nodiscard]] VkPolygonMode         ToVkFillMode(RHI::FillMode f);
    export [[nodiscard]] VkBlendFactor         ToVkBlendFactor(RHI::BlendFactor b);
    export [[nodiscard]] VkBlendOp             ToVkBlendOp(RHI::BlendOp o);
    export [[nodiscard]] VkCompareOp           ToVkDepthOp(RHI::DepthOp o);
    export [[nodiscard]] VkImageAspectFlags    AspectFromFormat(VkFormat f);
    export [[nodiscard]] VkBufferUsageFlags    ToVkBufferUsage(RHI::BufferUsage u);
    export [[nodiscard]] VkImageUsageFlags     ToVkTextureUsage(RHI::TextureUsage u);
    export [[nodiscard]] VkAccessFlags2        ToVkAccess(RHI::MemoryAccess a);
    export [[nodiscard]] VkPipelineStageFlags2 ToVkStage(RHI::MemoryAccess a);
    export [[nodiscard]] VkPresentModeKHR      ToVkPresentMode(RHI::PresentMode m,
                                                               const std::vector<VkPresentModeKHR>& available);
    export [[nodiscard]] VkIndexType           ToVkIndexType(RHI::IndexType t);
}

