module;

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Extrinsic.RHI.PipelineRegistry;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;

export namespace Extrinsic::RHI
{
    struct ShaderModuleId
    {
        std::string Path;
        std::uint64_t Generation = 0;

        bool operator==(const ShaderModuleId&) const = default;
    };

    struct PipelineStateKey
    {
        Topology PrimitiveTopology = Topology::TriangleList;
        RasterizerDesc Rasterizer{};
        DepthStencilDesc DepthStencil{};
        std::array<ColorBlendDesc, MaxColorTargets> ColorBlend{};
        std::uint32_t ColorTargetCount = 1;
        std::array<Format, MaxColorTargets> ColorTargetFormats{};
        Format DepthTargetFormat = Format::Undefined;
        std::uint32_t PushConstantSize = 0;

        [[nodiscard]] bool operator==(const PipelineStateKey& rhs) const noexcept
        {
            if (PrimitiveTopology != rhs.PrimitiveTopology ||
                Rasterizer.Culling != rhs.Rasterizer.Culling ||
                Rasterizer.Winding != rhs.Rasterizer.Winding ||
                Rasterizer.Fill != rhs.Rasterizer.Fill ||
                Rasterizer.DepthBiasConstant != rhs.Rasterizer.DepthBiasConstant ||
                Rasterizer.DepthBiasSlope != rhs.Rasterizer.DepthBiasSlope ||
                DepthStencil.DepthTestEnable != rhs.DepthStencil.DepthTestEnable ||
                DepthStencil.DepthWriteEnable != rhs.DepthStencil.DepthWriteEnable ||
                DepthStencil.DepthFunc != rhs.DepthStencil.DepthFunc ||
                DepthStencil.StencilEnable != rhs.DepthStencil.StencilEnable ||
                ColorTargetCount != rhs.ColorTargetCount ||
                DepthTargetFormat != rhs.DepthTargetFormat ||
                PushConstantSize != rhs.PushConstantSize)
            {
                return false;
            }

            for (std::uint32_t i = 0; i < MaxColorTargets; ++i)
            {
                const auto& lhsBlend = ColorBlend[i];
                const auto& rhsBlend = rhs.ColorBlend[i];
                if (lhsBlend.Enable != rhsBlend.Enable ||
                    lhsBlend.SrcColorFactor != rhsBlend.SrcColorFactor ||
                    lhsBlend.DstColorFactor != rhsBlend.DstColorFactor ||
                    lhsBlend.ColorOp != rhsBlend.ColorOp ||
                    lhsBlend.SrcAlphaFactor != rhsBlend.SrcAlphaFactor ||
                    lhsBlend.DstAlphaFactor != rhsBlend.DstAlphaFactor ||
                    lhsBlend.AlphaOp != rhsBlend.AlphaOp ||
                    ColorTargetFormats[i] != rhs.ColorTargetFormats[i])
                {
                    return false;
                }
            }
            return true;
        }
    };

    struct PipelineKey
    {
        ShaderModuleId Vertex{};
        ShaderModuleId Fragment{};
        ShaderModuleId Compute{};
        PipelineStateKey State{};

        bool operator==(const PipelineKey&) const = default;
    };

    struct PipelineRegistryDiagnostics
    {
        std::uint32_t CacheHitCount = 0;
        std::uint32_t CacheMissCount = 0;
        std::uint32_t MissingShaderCount = 0;
        std::uint32_t InvalidKeyCount = 0;
        std::uint32_t PipelineCreationFailureCount = 0;
        std::uint32_t ReloadInvalidationCount = 0;
        std::uint32_t LivePipelineCount = 0;
    };

    [[nodiscard]] PipelineStateKey MakePipelineStateKey(const PipelineDesc& desc);
    [[nodiscard]] PipelineKey MakePipelineKey(const PipelineDesc& desc,
                                              std::uint64_t vertexGeneration = 0,
                                              std::uint64_t fragmentGeneration = 0,
                                              std::uint64_t computeGeneration = 0);

    class PipelineRegistry
    {
    public:
        explicit PipelineRegistry(PipelineManager& pipelines);
        ~PipelineRegistry();

        PipelineRegistry(const PipelineRegistry&) = delete;
        PipelineRegistry& operator=(const PipelineRegistry&) = delete;

        [[nodiscard]] Core::Expected<PipelineHandle> GetOrCreatePipeline(const PipelineKey& key,
                                                                         const PipelineDesc& desc);
        std::uint32_t InvalidateShaderPath(std::string_view path);
        void Clear();

        [[nodiscard]] PipelineRegistryDiagnostics GetDiagnostics() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}



