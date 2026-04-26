module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Graphics.RenderGraph:Compiler;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Handles;
import :Pass;
import :Resources;
import :Barriers;

namespace Extrinsic::Graphics
{
    export struct ResourceLifetime
    {
        bool HasUse = false;
        std::uint32_t FirstUsePass = 0;
        std::uint32_t LastUsePass = 0;
    };

    export struct CompiledRenderGraph
    {
        std::uint32_t PassCount = 0;
        std::uint32_t CulledPassCount = 0;
        std::uint32_t ResourceCount = 0;
        std::uint32_t EdgeCount = 0;
        std::vector<std::uint32_t> TopologicalOrder{};
        std::vector<std::uint32_t> TopologicalLayerByPass{};
        std::vector<std::string> PassNames{};
        std::vector<ResourceLifetime> TextureLifetimes{};
        std::vector<ResourceLifetime> BufferLifetimes{};
        std::vector<RHI::TextureHandle> TextureHandles{};
        std::vector<RHI::BufferHandle> BufferHandles{};
        std::vector<bool> TextureImported{};
        std::vector<bool> BufferImported{};
        std::vector<BarrierPacket> BarrierPackets{};
        std::string Diagnostic{};
    };

    export class RenderGraphCompiler final
    {
    public:
        [[nodiscard]] static Core::Expected<CompiledRenderGraph> Compile(
            std::span<const RenderPassRecord> passes,
            std::span<const TextureResourceDesc> textures,
            std::span<const BufferResourceDesc> buffers);
    };
}
