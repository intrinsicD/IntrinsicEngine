module;

#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Graphics.RenderGraph:Compiler;

import Extrinsic.Core.Error;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;
import :Pass;
import :Resources;
import :Barriers;

namespace Extrinsic::Graphics
{
    export enum class RenderGraphValidationSeverity : std::uint8_t
    {
        Info = 0,
        Warning,
        Error,
    };

    export enum class RenderGraphValidationCode : std::uint16_t
    {
        MissingTextureProducer = 0,
        MissingBufferProducer,
        TransientTextureWithoutProducer,
        TransientBufferWithoutProducer,
        LoadWithoutGuaranteedWriter,
        UnauthorizedImportedTextureWrite,
        UnauthorizedImportedBufferWrite,
        BackbufferWrittenByNonFinalizer,
        ImportedTextureFinalStateMismatch,
        RenderPassColorWriteMissing,
        RenderPassDepthAccessMissing,
        CycleDetected,
        InvalidExplicitDependency,
        InvalidTextureAccess,
        InvalidBufferAccess,
        CrossQueueCycle,
        DuplicatePassId,
        DuplicateResourceId,
    };

    export struct RenderGraphValidationFinding
    {
        RenderGraphValidationSeverity Severity = RenderGraphValidationSeverity::Info;
        RenderGraphValidationCode Code = RenderGraphValidationCode::MissingTextureProducer;
        std::string Message{};
        std::uint32_t PassIndex = std::numeric_limits<std::uint32_t>::max();
        std::string PassName{};
        std::uint32_t ResourceIndex = std::numeric_limits<std::uint32_t>::max();
        bool IsTextureResource = true;
        std::string ResourceName{};
    };

    export struct RenderGraphValidationResult
    {
        std::vector<RenderGraphValidationFinding> Findings{};

        [[nodiscard]] bool HasErrors() const;
        [[nodiscard]] bool HasWarnings() const;
        [[nodiscard]] std::size_t CountBySeverity(RenderGraphValidationSeverity severity) const;
    };

    export enum class ImportedResourceWritePolicy : std::uint8_t
    {
        Disallow = 0,
        AllowFinalizerOnly,
        AllowAny,
    };

    export struct ImportedResourceAuthorization
    {
        std::uint32_t ResourceIndex = 0;
        bool IsTexture = true;
        ImportedResourceWritePolicy Policy = ImportedResourceWritePolicy::Disallow;
        std::vector<std::string> AuthorizedWriterPassNames{};
    };

    export struct CompiledRenderPassAttachment
    {
        std::uint32_t PassIndex = 0;
        std::uint32_t ResourceIndex = 0;
        std::uint32_t AttachmentIndex = 0;
        bool IsTextureResource = true;
        bool IsDepthAttachment = false;
        RHI::LoadOp Load = RHI::LoadOp::Clear;
        RHI::StoreOp Store = RHI::StoreOp::Store;
        RHI::Format Format = RHI::Format::Undefined;
        // BUG-016: carry the recipe-declared color clear value through
        // compilation so the executor's render-pass scope clears to the
        // recipe color (e.g. the default-recipe blue scene background) instead
        // of a hardcoded black. Ignored for depth attachments.
        float ClearR = 0.0f;
        float ClearG = 0.0f;
        float ClearB = 0.0f;
        float ClearA = 1.0f;
    };

    export struct CompiledPassDeclarations
    {
        std::uint32_t PassIndex = 0;
        std::vector<std::uint32_t> ReadTextures{};
        std::vector<std::uint32_t> WriteTextures{};
        std::vector<std::uint32_t> ReadBuffers{};
        std::vector<std::uint32_t> WriteBuffers{};

        [[nodiscard]] bool DeclaresTextureRead(TextureRef ref) const;
        [[nodiscard]] bool DeclaresTextureWrite(TextureRef ref) const;
        [[nodiscard]] bool DeclaresBufferRead(BufferRef ref) const;
        [[nodiscard]] bool DeclaresBufferWrite(BufferRef ref) const;

        [[nodiscard]] Core::Result RequireTextureRead(TextureRef ref) const;
        [[nodiscard]] Core::Result RequireTextureWrite(TextureRef ref) const;
        [[nodiscard]] Core::Result RequireBufferRead(BufferRef ref) const;
        [[nodiscard]] Core::Result RequireBufferWrite(BufferRef ref) const;
    };

    export struct ResourceLifetime
    {
        bool HasUse = false;
        std::uint32_t FirstUsePass = 0;
        std::uint32_t LastUsePass = 0;
    };

    export struct CrossQueueTimelineSignal
    {
        std::uint32_t PassIndex = 0;
        RenderQueue Queue = RenderQueue::Graphics;
        std::uint64_t Value = 0;
    };

    export struct CrossQueueTimelineWait
    {
        std::uint32_t PassIndex = 0;
        RenderQueue Queue = RenderQueue::Graphics;
        std::uint32_t SignalPassIndex = 0;
        RenderQueue SignalQueue = RenderQueue::Graphics;
        std::uint64_t Value = 0;
    };

    export struct CrossQueueTimelineEdge
    {
        std::uint32_t SignalPassIndex = 0;
        std::uint32_t WaitPassIndex = 0;
        RenderQueue SignalQueue = RenderQueue::Graphics;
        RenderQueue WaitQueue = RenderQueue::Graphics;
        std::uint64_t Value = 0;
    };

    export struct CompiledRenderGraph
    {
        // Keep vector-heavy special members out-of-line so Clang module importers
        // do not synthesize layout-sensitive copies independently.
        CompiledRenderGraph();
        CompiledRenderGraph(const CompiledRenderGraph& other);
        CompiledRenderGraph(CompiledRenderGraph&& other) noexcept;
        CompiledRenderGraph& operator=(const CompiledRenderGraph& other);
        CompiledRenderGraph& operator=(CompiledRenderGraph&& other) noexcept;
        ~CompiledRenderGraph();

        std::uint32_t PassCount = 0;
        std::uint32_t CulledPassCount = 0;
        std::uint32_t ResourceCount = 0;
        std::uint32_t TransientTextureCount = 0;
        std::uint32_t TransientBufferCount = 0;
        std::uint32_t EdgeCount = 0;
        std::uint32_t QueueHandoffEdgeCount = 0;
        std::uint32_t CrossQueueTimelineEdgeCount = 0;
        std::uint32_t CrossQueueOwnershipTransferCount = 0;
        std::uint64_t TransientMemoryEstimateBytes = 0;
        std::vector<std::uint32_t> TopologicalOrder{};
        std::vector<std::uint32_t> TopologicalLayerByPass{};
        std::vector<std::string> PassNames{};
        std::vector<RenderQueue> PassQueues{};
        std::vector<bool> PassSideEffects{};
        std::vector<CompiledPassDeclarations> PassDeclarations{};
        std::vector<std::string> TextureNames{};
        std::vector<std::string> BufferNames{};
        std::vector<ResourceLifetime> TextureLifetimes{};
        std::vector<ResourceLifetime> BufferLifetimes{};
        std::vector<TextureState> TextureInitialStates{};
        std::vector<TextureState> TextureFinalStates{};
        std::vector<BufferState> BufferInitialStates{};
        std::vector<BufferState> BufferFinalStates{};
        std::vector<RHI::TextureHandle> TextureHandles{};
        std::vector<RHI::BufferHandle> BufferHandles{};
        std::vector<bool> TextureImported{};
        std::vector<bool> TextureIsBackbuffer{};
        std::vector<bool> BufferImported{};
        std::vector<QueueSharingMode> TextureQueueSharingModes{};
        std::vector<QueueSharingMode> BufferQueueSharingModes{};
        std::vector<CompiledRenderPassAttachment> RenderPassAttachments{};
        std::vector<CrossQueueTimelineSignal> CrossQueueTimelineSignals{};
        std::vector<CrossQueueTimelineWait> CrossQueueTimelineWaits{};
        std::vector<CrossQueueTimelineEdge> CrossQueueTimelineEdges{};
        std::vector<BarrierPacket> BarrierPackets{};
        std::vector<RenderGraphValidationFinding> ValidationFindings{};
    };

    export struct QueuePartitionedPass
    {
        std::uint32_t PassIndex = 0;
        std::uint32_t TopologicalRank = 0;
        RenderQueue Requested = RenderQueue::Graphics;
        RenderQueue Resolved = RenderQueue::Graphics;
        bool Demoted = false;
    };

    export struct QueuePartition
    {
        std::vector<QueuePartitionedPass> Graphics{};
        std::vector<QueuePartitionedPass> AsyncCompute{};
        std::vector<QueuePartitionedPass> Transfer{};
        std::uint32_t QueueAffinityDemotedCount = 0;
    };

    export struct QueueSubmitTimelineSignal
    {
        std::uint32_t PassIndex = 0;
        RenderQueue RequestedQueue = RenderQueue::Graphics;
        RenderQueue Queue = RenderQueue::Graphics;
        std::uint64_t Value = 0;
    };

    export struct QueueSubmitTimelineWait
    {
        std::uint32_t PassIndex = 0;
        RenderQueue RequestedQueue = RenderQueue::Graphics;
        RenderQueue Queue = RenderQueue::Graphics;
        std::uint32_t SignalPassIndex = 0;
        RenderQueue RequestedSignalQueue = RenderQueue::Graphics;
        RenderQueue SignalQueue = RenderQueue::Graphics;
        std::uint64_t Value = 0;
    };

    export struct QueueSubmitBatch
    {
        RenderQueue Queue = RenderQueue::Graphics;
        std::vector<std::uint32_t> PassIndices{};
        std::vector<QueueSubmitTimelineWait> Waits{};
        std::vector<QueueSubmitTimelineSignal> Signals{};
    };

    export struct QueueSubmitPlan
    {
        std::vector<QueueSubmitBatch> Batches{};
        std::uint32_t QueueAffinityDemotedCount = 0;
        std::uint32_t OmittedSameQueueTimelineEdgeCount = 0;
    };

    export class RenderGraphCompiler final
    {
    public:
        [[nodiscard]] static Core::Expected<CompiledRenderGraph> Compile(
            std::span<const RenderPassRecord> passes,
            std::span<const TextureResourceDesc> textures,
            std::span<const BufferResourceDesc> buffers);
        [[nodiscard]] static const RenderGraphValidationResult& GetLastCompileValidationResult();
    };

    export [[nodiscard]] std::string BuildRenderGraphDebugDump(const CompiledRenderGraph& compiled);
    export [[nodiscard]] QueuePartition PartitionPassesByQueue(
        std::span<const RenderQueue> passQueues,
        std::span<const std::uint32_t> livePasses,
        std::span<const std::uint32_t> topologicalRankByPass,
        RHI::QueueCapabilityProfile profile);
    export [[nodiscard]] QueuePartition PartitionPassesByQueue(
        const CompiledRenderGraph& compiled,
        RHI::QueueCapabilityProfile profile);
    export [[nodiscard]] QueueSubmitPlan BuildQueueSubmitPlan(
        const CompiledRenderGraph& compiled,
        RHI::QueueCapabilityProfile profile);
    export [[nodiscard]] RenderGraphValidationResult ValidateCompiledGraph(
        const CompiledRenderGraph& compiled,
        std::span<const ImportedResourceAuthorization> authorizations = {});
}
