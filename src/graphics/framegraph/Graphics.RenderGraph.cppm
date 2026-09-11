// Render-graph composition and pure lifetime placement shared with device-backed realization.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>
#include <string>

export module Extrinsic.Graphics.RenderGraph;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

export import :Resources;
export import :Pass;
export import :Barriers;
export import :Compiler;
export import :TransientAllocator;
export import :Executor;

namespace Extrinsic::Graphics
{
    // Pass fields are execution ranks; consumers map them to compiled pass identities.
    export struct TransientPlacementItem
    {
        std::uint32_t ResourceIndex = 0u;
        std::uint32_t FirstUsePass = 0u;
        std::uint32_t LastUsePass = 0u;
        std::uint64_t SizeBytes = 0u;
        std::uint64_t AlignmentBytes = 1u;
    };

    export struct TransientAliasReuseHazard
    {
        std::uint32_t PreviousResourceIndex = 0u;
        std::uint32_t ResourceIndex = 0u;
        std::uint32_t PassIndex = 0u;
        std::uint32_t BlockIndex = 0u;
        std::uint64_t OffsetBytes = 0u;
        std::uint64_t SizeBytes = 0u;
    };

    export struct TransientPlacementPlan
    {
        std::vector<TransientResourcePlacement> Placements{};
        std::vector<TransientAliasReuseHazard> AliasReuseHazards{};
        std::uint64_t PeakBytes = 0u;
    };

    // Items are ordered by (FirstUsePass, ResourceIndex) with inclusive lifetimes.
    // Size/alignment are caller-provided estimates or validated device requirements;
    // the caller owns final block alignment and memory-type compatibility.
    export [[nodiscard]] TransientPlacementPlan BuildTransientPlacementPlan(
        std::span<const TransientPlacementItem> items, bool aliasingEnabled);

    export class RenderGraph final
    {
    public:
        RenderGraph();
        ~RenderGraph();

        RenderGraph(RenderGraph&&) noexcept;
        RenderGraph& operator=(RenderGraph&&) noexcept;

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        [[nodiscard]] PassRef AddPass(std::string name);
        [[nodiscard]] PassRef AddPass(std::string name, bool sideEffect);
        [[nodiscard]] PassRef AddPass(std::string name,
                                      std::move_only_function<void(RenderGraphBuilder&)> setup);
        [[nodiscard]] PassRef AddPass(std::string name,
                                      std::move_only_function<void(RenderGraphBuilder&)> setup,
                                      bool sideEffect);
        [[nodiscard]] TextureRef ImportBackbuffer(std::string name, RHI::TextureHandle handle);
        [[nodiscard]] TextureRef ImportTexture(std::string name,
                                               RHI::TextureHandle handle,
                                               TextureState initial,
                                               TextureState finalState = TextureState::Present,
                                               bool allowImportedWrites = false);
        [[nodiscard]] BufferRef ImportBuffer(std::string name,
                                             RHI::BufferHandle handle,
                                             BufferState initial,
                                             BufferState finalState = BufferState::Undefined);
        [[nodiscard]] TextureRef CreateTexture(std::string name, const RHI::TextureDesc& desc);
        [[nodiscard]] BufferRef CreateBuffer(std::string name, const RHI::BufferDesc& desc);

        [[nodiscard]] Core::Expected<CompiledRenderGraph> Compile();
        [[nodiscard]] const RenderGraphValidationResult& GetLastCompileValidationResult() const;
        void SetTransientAliasingEnabled(bool enabled);
        [[nodiscard]] bool IsTransientAliasingEnabled() const;
        [[nodiscard]] Core::Result ValidateTextureRef(TextureRef ref) const;
        [[nodiscard]] Core::Result ValidateBufferRef(BufferRef ref) const;
        [[nodiscard]] const TextureResourceDesc* GetTextureDesc(TextureRef ref) const;
        [[nodiscard]] const BufferResourceDesc* GetBufferDesc(BufferRef ref) const;
        [[nodiscard]] const TextureResourceDesc* GetTextureDescByIndex(std::uint32_t index) const;
        [[nodiscard]] const BufferResourceDesc* GetBufferDescByIndex(std::uint32_t index) const;
        [[nodiscard]] Core::Result SetPassId(PassRef ref, FramePassId id);
        [[nodiscard]] Core::Result SetTextureResourceId(TextureRef ref, FrameResourceId id);
        [[nodiscard]] Core::Result SetBufferResourceId(BufferRef ref, FrameResourceId id);
        void Reset();

        [[nodiscard]] std::uint32_t GetPassCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
