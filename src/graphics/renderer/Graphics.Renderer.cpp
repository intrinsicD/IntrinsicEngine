module;

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

module Extrinsic.Graphics.Renderer;

import Extrinsic.Core.Error;
import Extrinsic.Core.Telemetry;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.UvView;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.VisualizationPropertyBufferResidency;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.HZB;
import Extrinsic.Graphics.LightClusters;
import Extrinsic.Graphics.Reconstruction;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.Pass.DepthPrepass;
import Extrinsic.Graphics.Pass.Deferred.GBuffers;
import Extrinsic.Graphics.Pass.Deferred.Lighting;
import Extrinsic.Graphics.Pass.Forward.Surface;
import Extrinsic.Graphics.Pass.Forward.Line;
import Extrinsic.Graphics.Pass.Forward.Point;
import Extrinsic.Graphics.Pass.Shadows;
import Extrinsic.Graphics.Pass.Selection.EntityId;
import Extrinsic.Graphics.Pass.Selection.FaceId;
import Extrinsic.Graphics.Pass.Selection.EdgeId;
import Extrinsic.Graphics.Pass.Selection.PointId;
import Extrinsic.Graphics.Pass.Selection.Outline;
import Extrinsic.Graphics.Pass.PostProcess.Bloom;
import Extrinsic.Graphics.Pass.PostProcess.FXAA;
import Extrinsic.Graphics.Pass.PostProcess.Histogram;
import Extrinsic.Graphics.Pass.PostProcess.SMAA;
import Extrinsic.Graphics.Pass.PostProcess.ToneMap;
import Extrinsic.Graphics.Pass.Present;
// GRAPHICS-076 Slice B — canonical default-recipe `Pass.DebugView`.
// Imported alongside the canonical `Pass.Present` above so the renderer
// can own a `DebugViewPass` instance bound to a renderer-owned
// `DebugViewSystem`. The system module is imported explicitly so the
// renderer can construct + drive the system itself (Pass.DebugView only
// holds a reference and does not re-export the system module).
import Extrinsic.Graphics.Pass.DebugView;
import Extrinsic.Graphics.DebugViewSystem;
// GRAPHICS-079 Slice A — canonical default-recipe `Pass.ImGui`. The
// renderer owns a `std::optional<ImGuiPass>` bound to the engine-owned
// `ImGuiOverlaySystem` handed in via `SetImGuiOverlaySystem`; the overlay
// system module is already imported transitively through the renderer's
// public surface, but the pass module is imported explicitly here so the
// executor can construct and drive the consumer route.
import Extrinsic.Graphics.Pass.ImGui;
import Extrinsic.Graphics.ImGuiUploadHelper;
// GRAPHICS-077 Slice A — `TransientDebugSurfacePass` shell. Slice B
// promotes the triangle lane from `SkippedUnavailable` to `Recorded`
// by creating two pipeline variants (depth-tested + always-on-top)
// and driving a backend-local upload helper that packs the sanitized
// `DebugTrianglePacket` span into a host-visible vertex buffer per
// frame; Slice C extends to line + point lanes.
import Extrinsic.Graphics.Pass.TransientDebug.Surface;
import Extrinsic.Graphics.TransientDebugUploadHelper;
// GRAPHICS-078 Slices A + B + C — `VisualizationOverlayPass` shell +
// vector-field + isoline lane wiring. Slice B added the vector-field
// lane pipelines + upload; Slice C completes the isoline lane
// pipelines + upload, mirroring the GRAPHICS-077 transient-debug
// pattern. The renderer constructs `VisualizationOverlayUploadHelper`
// against the live `BufferManager` and routes
// `RecordVisualizationOverlayPass` through it for both lanes.
import Extrinsic.Graphics.Pass.VisualizationOverlay;
import Extrinsic.Graphics.VisualizationOverlayUploadHelper;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderCommandRouter;
import Extrinsic.Graphics.RenderSubsystemRegistry;
import Extrinsic.Graphics.RenderPrepPipeline;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.SharedRenderRecipeExecution;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Core.Logging;

namespace Extrinsic::Graphics
{
    namespace
    {
        // GRAPHICS-075 Slice E.2 — `Histogram.Readback` per-slot size in
        // bytes. Matches the recipe-side `PostProcess.Histogram` buffer
        // declaration (`256 * sizeof(std::uint32_t)`) and the GPU shader's
        // `bins[256]` storage block (`assets/shaders/post_histogram.comp`).
        // The renderer-owned host-visible readback buffer holds
        // `kHistogramReadbackSlotBytes * frames-in-flight` bytes total so
        // each in-flight frame copies into its own slot without aliasing.
        constexpr std::uint64_t kHistogramReadbackSlotBytes =
            256ull * sizeof(std::uint32_t);
        // BUG-026 — `Picking.Readback` per-slot layout. Each in-flight frame
        // slot holds the three single-pixel samples the PickingPass copies at
        // the pick pixel plus 4 pad bytes for a 16-byte stride:
        //   +0  EntityId            (R32_UINT;  0 = background sentinel)
        //   +4  EncodedSelectionId  (R32_UINT;  domain<<28 | payload)
        //   +8  SceneDepth          (R32_SFLOAT bits; 1.0 = depth clear)
        //   +12 pad
        constexpr std::uint64_t kPickingReadbackSlotStride          = 16ull;
        constexpr std::uint64_t kPickingReadbackEntityIdOffset      = 0ull;
        constexpr std::uint64_t kPickingReadbackEncodedIdOffset     = 4ull;
        constexpr std::uint64_t kPickingReadbackDepthOffset         = 8ull;
        constexpr std::uint32_t kFrameSampledDescriptorSlotDefault = 0u;
        constexpr std::uint32_t kFrameSampledDescriptorSlotDebugView = 1u;
        constexpr std::uint32_t kFrameSampledDescriptorSlotPresent = 2u;
        constexpr std::uint32_t kFrameSampledDescriptorSlotSelectionOutline = 3u;
        // GRAPHICS-119 Slice C.6: pass callbacks route shared renderer state
        // through guarded helpers, so accepted parallel command contexts may
        // record on scheduler workers while serial submit order stays fixed.
        constexpr bool kRenderGraphParallelRecordWorkerFanOutEnabled = true;

        struct FrameRecipeContributionCacheKey
        {
            FrameRecipePassKind Kind{FrameRecipePassKind::Culling};
            FramePassId Id{};
            std::string Name{};
            bool Enabled{false};
            bool PreserveDisabledDeclaration{false};
            bool FinalizesBackbuffer{false};
            RenderQueue Queue{RenderQueue::Graphics};
            FramePassId AnchorPassId{};
            FrameRecipeContributionAnchorPlacement AnchorPlacement{
                FrameRecipeContributionAnchorPlacement::After};
            std::vector<FrameResourceId> Reads{};
            std::vector<FrameResourceId> Writes{};

            [[nodiscard]] bool operator==(const FrameRecipeContributionCacheKey&) const = default;
        };

        struct RenderGraphCompileCacheKey
        {
            FrameRecipeFeatures Features{};
            FrameRecipeImports Imports{};
            FrameRecipeSizing Sizing{};
            FrameRecipeAAOptions AAOptions{};
            FrameRecipeShadowSizing ShadowSizing{};
            FrameRecipeTemporalOptions TemporalOptions{};
            std::vector<FrameRecipeContributionCacheKey> Contributions{};
        };

        struct RenderGraphCompileCacheEntry
        {
            RenderGraphCompileCacheKey Key{};
            CompiledRenderGraph Compiled{};
            FrameRecipeIntrospection Recipe{};
        };

        [[nodiscard]] bool FrameRecipeFeaturesEqual(const FrameRecipeFeatures& lhs,
                                                    const FrameRecipeFeatures& rhs) noexcept
        {
            return std::tie(lhs.LightingPath,
                            lhs.EnableDepthPrepass,
                            lhs.EnableHZBBuild,
                            lhs.EnableClusterGridBuild,
                            lhs.EnableClusterLightAssignment,
                            lhs.EnablePicking,
                            lhs.EnableShadows,
                            lhs.EnableSelectionOutline,
                            lhs.EnableDebugView,
                            lhs.EnablePostProcess,
                            lhs.EnableAntiAliasing,
                            lhs.EnableImGui,
                            lhs.EnableTransientDebugSurface,
                            lhs.EnableVisualizationOverlay,
                            lhs.EnableUvView) ==
                   std::tie(rhs.LightingPath,
                            rhs.EnableDepthPrepass,
                            rhs.EnableHZBBuild,
                            rhs.EnableClusterGridBuild,
                            rhs.EnableClusterLightAssignment,
                            rhs.EnablePicking,
                            rhs.EnableShadows,
                            rhs.EnableSelectionOutline,
                            rhs.EnableDebugView,
                            rhs.EnablePostProcess,
                            rhs.EnableAntiAliasing,
                            rhs.EnableImGui,
                            rhs.EnableTransientDebugSurface,
                            rhs.EnableVisualizationOverlay,
                            rhs.EnableUvView);
        }

        [[nodiscard]] bool FrameRecipeImportsEqual(const FrameRecipeImports& lhs,
                                                   const FrameRecipeImports& rhs) noexcept
        {
            return std::make_tuple(lhs.Backbuffer.IsValid(),
                                   lhs.SceneTable.IsValid(),
                                   lhs.InstanceStatic.IsValid(),
                                   lhs.InstanceDynamic.IsValid(),
                                   lhs.EntityConfig.IsValid(),
                                   lhs.GeometryRecords.IsValid(),
                                   lhs.Bounds.IsValid(),
                                   lhs.Lights.IsValid(),
                                   lhs.MaterialBuffer.IsValid(),
                                   lhs.SurfaceOpaqueIndexedArgs.IsValid(),
                                   lhs.SurfaceOpaqueCount.IsValid(),
                                   lhs.LinesIndexedArgs.IsValid(),
                                   lhs.LinesCount.IsValid(),
                                   lhs.LineQuadsNonIndexedArgs.IsValid(),
                                   lhs.LineQuadsCount.IsValid(),
                                   lhs.PointsNonIndexedArgs.IsValid(),
                                   lhs.PointsCount.IsValid(),
                                   lhs.ShadowAtlas.IsValid(),
                                   lhs.PickingReadback.IsValid(),
                                   lhs.HistogramReadback.IsValid(),
                                   lhs.HZBCurrent.IsValid(),
                                   lhs.ClusterGridAABBs.IsValid(),
                                   lhs.ClusterLightHeaders.IsValid(),
                                   lhs.ClusterLightIndices.IsValid(),
                                   lhs.ClusterLightCounter.IsValid(),
                                   lhs.UvViewColor.IsValid(),
                                   lhs.UvViewColorInitialized) ==
                   std::make_tuple(rhs.Backbuffer.IsValid(),
                                   rhs.SceneTable.IsValid(),
                                   rhs.InstanceStatic.IsValid(),
                                   rhs.InstanceDynamic.IsValid(),
                                   rhs.EntityConfig.IsValid(),
                                   rhs.GeometryRecords.IsValid(),
                                   rhs.Bounds.IsValid(),
                                   rhs.Lights.IsValid(),
                                   rhs.MaterialBuffer.IsValid(),
                                   rhs.SurfaceOpaqueIndexedArgs.IsValid(),
                                   rhs.SurfaceOpaqueCount.IsValid(),
                                   rhs.LinesIndexedArgs.IsValid(),
                                   rhs.LinesCount.IsValid(),
                                   rhs.LineQuadsNonIndexedArgs.IsValid(),
                                   rhs.LineQuadsCount.IsValid(),
                                   rhs.PointsNonIndexedArgs.IsValid(),
                                   rhs.PointsCount.IsValid(),
                                   rhs.ShadowAtlas.IsValid(),
                                   rhs.PickingReadback.IsValid(),
                                   rhs.HistogramReadback.IsValid(),
                                   rhs.HZBCurrent.IsValid(),
                                   rhs.ClusterGridAABBs.IsValid(),
                                   rhs.ClusterLightHeaders.IsValid(),
                                   rhs.ClusterLightIndices.IsValid(),
                                   rhs.ClusterLightCounter.IsValid(),
                                   rhs.UvViewColor.IsValid(),
                                   rhs.UvViewColorInitialized);
        }

        [[nodiscard]] bool FrameRecipeSizingEqual(const FrameRecipeSizing& lhs,
                                                  const FrameRecipeSizing& rhs) noexcept
        {
            return std::tie(lhs.Width, lhs.Height, lhs.BackbufferFormat, lhs.DepthFormat) ==
                   std::tie(rhs.Width, rhs.Height, rhs.BackbufferFormat, rhs.DepthFormat);
        }

        [[nodiscard]] bool FrameRecipeAAOptionsEqual(const FrameRecipeAAOptions& lhs,
                                                     const FrameRecipeAAOptions& rhs) noexcept
        {
            return std::make_tuple(lhs.Mode,
                                   lhs.InputWidth,
                                   lhs.InputHeight,
                                   lhs.ReconstructionHistoryPrevious.IsValid(),
                                   lhs.ReconstructionHistoryCurrent.IsValid()) ==
                   std::make_tuple(rhs.Mode,
                                   rhs.InputWidth,
                                   rhs.InputHeight,
                                   rhs.ReconstructionHistoryPrevious.IsValid(),
                                   rhs.ReconstructionHistoryCurrent.IsValid());
        }

        [[nodiscard]] bool FrameRecipeShadowSizingEqual(const FrameRecipeShadowSizing& lhs,
                                                        const FrameRecipeShadowSizing& rhs) noexcept
        {
            return std::tie(lhs.AtlasResolution, lhs.CascadeCount) ==
                   std::tie(rhs.AtlasResolution, rhs.CascadeCount);
        }

        [[nodiscard]] bool FrameRecipeTemporalOptionsEqual(const FrameRecipeTemporalOptions& lhs,
                                                           const FrameRecipeTemporalOptions& rhs) noexcept
        {
            return std::tie(lhs.NoJitterNoHistory, lhs.EnableMotionVectors) ==
                   std::tie(rhs.NoJitterNoHistory, rhs.EnableMotionVectors);
        }

        [[nodiscard]] bool RenderGraphCompileCacheKeyEqual(const RenderGraphCompileCacheKey& lhs,
                                                           const RenderGraphCompileCacheKey& rhs)
        {
            return FrameRecipeFeaturesEqual(lhs.Features, rhs.Features) &&
                   FrameRecipeImportsEqual(lhs.Imports, rhs.Imports) &&
                   FrameRecipeSizingEqual(lhs.Sizing, rhs.Sizing) &&
                   FrameRecipeAAOptionsEqual(lhs.AAOptions, rhs.AAOptions) &&
                   FrameRecipeShadowSizingEqual(lhs.ShadowSizing, rhs.ShadowSizing) &&
                   FrameRecipeTemporalOptionsEqual(lhs.TemporalOptions, rhs.TemporalOptions) &&
                   lhs.Contributions == rhs.Contributions;
        }

        [[nodiscard]] RenderGraphCompileCacheKey BuildRenderGraphCompileCacheKey(
            const FrameRecipeFeatures& features,
            const FrameRecipeImports& imports,
            const FrameRecipeSizing& sizing,
            const FrameRecipeAAOptions& aaOptions,
            const FrameRecipeShadowSizing& shadowSizing,
            const FrameRecipeTemporalOptions temporalOptions,
            const std::vector<FrameRecipePassContribution>& contributions)
        {
            RenderGraphCompileCacheKey key{
                .Features = features,
                .Imports = imports,
                .Sizing = sizing,
                .AAOptions = aaOptions,
                .ShadowSizing = shadowSizing,
                .TemporalOptions = temporalOptions,
            };
            key.Contributions.reserve(contributions.size());
            for (const FrameRecipePassContribution& contribution : contributions)
            {
                key.Contributions.push_back(FrameRecipeContributionCacheKey{
                    .Kind = contribution.Kind,
                    .Id = contribution.Id,
                    .Name = std::string{contribution.Name},
                    .Enabled = contribution.Enabled,
                    .PreserveDisabledDeclaration = contribution.PreserveDisabledDeclaration,
                    .FinalizesBackbuffer = contribution.FinalizesBackbuffer,
                    .Queue = contribution.Queue,
                    .AnchorPassId = contribution.Anchor.PassId,
                    .AnchorPlacement = contribution.Anchor.Placement,
                    .Reads = contribution.Reads,
                    .Writes = contribution.Writes,
                });
            }
            return key;
        }

        void ApplyResolvedDebugViewRead(std::vector<FrameRecipePassContribution>& contributions,
                                        const DebugViewResolvedSelection& resolved)
        {
            if (!resolved.Enabled || !resolved.SelectedResourceId.IsValid())
            {
                return;
            }

            for (FrameRecipePassContribution& contribution : contributions)
            {
                if (contribution.Kind == FrameRecipePassKind::DebugView)
                {
                    contribution.Reads = {resolved.SelectedResourceId};
                    return;
                }
            }
        }

        [[nodiscard]] std::optional<std::uint32_t> FindCompiledTextureIndexByResourceId(
            const CompiledRenderGraph& compiled,
            const FrameResourceId id) noexcept
        {
            if (!id.IsValid())
            {
                return std::nullopt;
            }

            for (std::uint32_t index = 0u; index < compiled.TextureResourceIds.size(); ++index)
            {
                if (compiled.TextureResourceIds[index] == id)
                {
                    return index;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint32_t> FindCompiledBufferIndexByResourceId(
            const CompiledRenderGraph& compiled,
            const FrameResourceId id) noexcept
        {
            if (!id.IsValid())
            {
                return std::nullopt;
            }

            for (std::uint32_t index = 0u; index < compiled.BufferResourceIds.size(); ++index)
            {
                if (compiled.BufferResourceIds[index] == id)
                {
                    return index;
                }
            }
            return std::nullopt;
        }

        void RebindCompiledGraphImports(CompiledRenderGraph& compiled,
                                        const FrameRecipeImports& imports,
                                        const FrameRecipeAAOptions& aaOptions)
        {
            auto bindTexture = [&compiled](const FrameRecipeResourceKind kind,
                                           const RHI::TextureHandle handle)
            {
                const std::optional<std::uint32_t> index =
                    FindCompiledTextureIndexByResourceId(compiled, ToFrameResourceId(kind));
                if (!index.has_value() || *index >= compiled.TextureHandles.size() ||
                    *index >= compiled.TextureImported.size() || !compiled.TextureImported[*index])
                {
                    return;
                }
                compiled.TextureHandles[*index] = handle;
            };

            auto bindBuffer = [&compiled](const FrameRecipeResourceKind kind,
                                          const RHI::BufferHandle handle)
            {
                const std::optional<std::uint32_t> index =
                    FindCompiledBufferIndexByResourceId(compiled, ToFrameResourceId(kind));
                if (!index.has_value() || *index >= compiled.BufferHandles.size() ||
                    *index >= compiled.BufferImported.size() || !compiled.BufferImported[*index])
                {
                    return;
                }
                compiled.BufferHandles[*index] = handle;
            };

            bindTexture(FrameRecipeResourceKind::Backbuffer, imports.Backbuffer);
            bindTexture(FrameRecipeResourceKind::ShadowAtlas, imports.ShadowAtlas);
            bindTexture(FrameRecipeResourceKind::HZBCurrent, imports.HZBCurrent);
            bindTexture(FrameRecipeResourceKind::ReconstructionHistoryPrevious,
                        aaOptions.ReconstructionHistoryPrevious);
            bindTexture(FrameRecipeResourceKind::ReconstructionHistoryCurrent,
                        aaOptions.ReconstructionHistoryCurrent);
            bindTexture(FrameRecipeResourceKind::UvViewColor, imports.UvViewColor);

            bindBuffer(FrameRecipeResourceKind::SceneTable, imports.SceneTable);
            bindBuffer(FrameRecipeResourceKind::InstanceStatic, imports.InstanceStatic);
            bindBuffer(FrameRecipeResourceKind::InstanceDynamic, imports.InstanceDynamic);
            bindBuffer(FrameRecipeResourceKind::EntityConfig, imports.EntityConfig);
            bindBuffer(FrameRecipeResourceKind::GeometryRecords, imports.GeometryRecords);
            bindBuffer(FrameRecipeResourceKind::Bounds, imports.Bounds);
            bindBuffer(FrameRecipeResourceKind::Lights, imports.Lights);
            bindBuffer(FrameRecipeResourceKind::MaterialBuffer, imports.MaterialBuffer);
            bindBuffer(FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs,
                       imports.SurfaceOpaqueIndexedArgs);
            bindBuffer(FrameRecipeResourceKind::SurfaceOpaqueCount,
                       imports.SurfaceOpaqueCount);
            bindBuffer(FrameRecipeResourceKind::LinesIndexedArgs,
                       imports.LinesIndexedArgs);
            bindBuffer(FrameRecipeResourceKind::LinesCount,
                       imports.LinesCount);
            bindBuffer(FrameRecipeResourceKind::LineQuadsNonIndexedArgs,
                       imports.LineQuadsNonIndexedArgs);
            bindBuffer(FrameRecipeResourceKind::LineQuadsCount,
                       imports.LineQuadsCount);
            bindBuffer(FrameRecipeResourceKind::PointsNonIndexedArgs,
                       imports.PointsNonIndexedArgs);
            bindBuffer(FrameRecipeResourceKind::PointsCount,
                       imports.PointsCount);
            bindBuffer(FrameRecipeResourceKind::PickingReadback,
                       imports.PickingReadback);
            bindBuffer(FrameRecipeResourceKind::HistogramReadback,
                       imports.HistogramReadback);
            bindBuffer(FrameRecipeResourceKind::ClusterGridAABBs,
                       imports.ClusterGridAABBs);
            bindBuffer(FrameRecipeResourceKind::ClusterLightHeaders,
                       imports.ClusterLightHeaders);
            bindBuffer(FrameRecipeResourceKind::ClusterLightIndices,
                       imports.ClusterLightIndices);
            bindBuffer(FrameRecipeResourceKind::ClusterLightCounter,
                       imports.ClusterLightCounter);
        }

        [[nodiscard]] constexpr std::uint32_t FrameSampledDescriptorSlotForPass(
            const FramePassId passId) noexcept
        {
            return passId == ToFramePassId(FrameRecipePassKind::Present)
                ? kFrameSampledDescriptorSlotPresent
                : kFrameSampledDescriptorSlotDefault;
        }

        bool BindFrameSampledTextureByResourceId(RHI::ICommandContext& cmd,
                                                 const CompiledRenderGraph& compiled,
                                                 const FrameResourceId id,
                                                 const std::uint32_t descriptorSlot)
        {
            const std::optional<std::uint32_t> textureIndex =
                FindCompiledTextureIndexByResourceId(compiled, id);
            if (!textureIndex.has_value() || *textureIndex >= compiled.TextureHandles.size())
            {
                return false;
            }

            const RHI::TextureHandle texture = compiled.TextureHandles[*textureIndex];
            if (!texture.IsValid())
            {
                return false;
            }

            cmd.BindFrameSampledTextureAt(texture, descriptorSlot);
            return true;
        }

        bool BindFrameSampledDeclaredTexture(RHI::ICommandContext& cmd,
                                             const CompiledRenderGraph& compiled,
                                             const std::uint32_t textureIndex,
                                             const std::uint32_t descriptorSlot)
        {
            if (textureIndex >= compiled.TextureHandles.size())
            {
                return false;
            }

            if (textureIndex < compiled.TextureResourceIds.size() &&
                compiled.TextureResourceIds[textureIndex].IsValid())
            {
                return BindFrameSampledTextureByResourceId(
                    cmd,
                    compiled,
                    compiled.TextureResourceIds[textureIndex],
                    descriptorSlot);
            }

            const RHI::TextureHandle texture = compiled.TextureHandles[textureIndex];
            if (!texture.IsValid())
            {
                return false;
            }

            cmd.BindFrameSampledTextureAt(texture, descriptorSlot);
            return true;
        }

        [[nodiscard]] bool IsContractOutputDiagnostic(
            const RenderingContractDiagnosticCode code) noexcept
        {
            switch (code)
            {
            case RenderingContractDiagnosticCode::MissingRendererOutput:
            case RenderingContractDiagnosticCode::UnsupportedOutput:
            case RenderingContractDiagnosticCode::UnsupportedReadbackRequest:
            case RenderingContractDiagnosticCode::UndeclaredArtifactOutput:
                return true;
            case RenderingContractDiagnosticCode::None:
            case RenderingContractDiagnosticCode::EmptyRendererId:
            case RenderingContractDiagnosticCode::UnknownRendererPurpose:
            case RenderingContractDiagnosticCode::MissingSupportedSnapshotScope:
            case RenderingContractDiagnosticCode::MissingSupportedSnapshotKind:
            case RenderingContractDiagnosticCode::MissingUpdateMode:
            case RenderingContractDiagnosticCode::EmptySnapshotId:
            case RenderingContractDiagnosticCode::SnapshotRendererMismatch:
            case RenderingContractDiagnosticCode::UnsupportedSnapshotScope:
            case RenderingContractDiagnosticCode::UnsupportedSnapshotKind:
            case RenderingContractDiagnosticCode::InvalidSnapshotState:
            case RenderingContractDiagnosticCode::StaleSnapshot:
            case RenderingContractDiagnosticCode::MissingSnapshotData:
            case RenderingContractDiagnosticCode::DegradedSnapshot:
            case RenderingContractDiagnosticCode::EmptyBindingRole:
            case RenderingContractDiagnosticCode::MissingRequiredBinding:
            case RenderingContractDiagnosticCode::UnsupportedBindingCapability:
            case RenderingContractDiagnosticCode::EmptyRecipeId:
            case RenderingContractDiagnosticCode::UnknownRecipeSlot:
            case RenderingContractDiagnosticCode::UnsupportedRecipeCapability:
            case RenderingContractDiagnosticCode::DisallowedRecipeBinding:
            case RenderingContractDiagnosticCode::EmptyViewRecipeId:
            case RenderingContractDiagnosticCode::InvalidViewport:
            case RenderingContractDiagnosticCode::InvalidRenderScale:
            case RenderingContractDiagnosticCode::EmptyArtifactId:
            case RenderingContractDiagnosticCode::ArtifactRendererMismatch:
            case RenderingContractDiagnosticCode::ArtifactSnapshotMissing:
            case RenderingContractDiagnosticCode::ArtifactViewRecipeMissing:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool IsSharedUnsupportedDiagnostic(
            const SharedRecipeDiagnosticCode code) noexcept
        {
            switch (code)
            {
            case SharedRecipeDiagnosticCode::UnsupportedProduct:
            case SharedRecipeDiagnosticCode::MissingRendererCapability:
            case SharedRecipeDiagnosticCode::ProductNotProduced:
                return true;
            case SharedRecipeDiagnosticCode::None:
            case SharedRecipeDiagnosticCode::EmptyVisibilityInput:
            case SharedRecipeDiagnosticCode::EmptyLightingInput:
            case SharedRecipeDiagnosticCode::InvalidRenderable:
            case SharedRecipeDiagnosticCode::MissingGeometry:
            case SharedRecipeDiagnosticCode::MissingInstance:
            case SharedRecipeDiagnosticCode::NonFiniteBounds:
            case SharedRecipeDiagnosticCode::NotVisible:
            case SharedRecipeDiagnosticCode::UnsupportedRenderDomain:
            case SharedRecipeDiagnosticCode::StaleInput:
            case SharedRecipeDiagnosticCode::DegradedOutput:
            case SharedRecipeDiagnosticCode::InvalidLight:
            case SharedRecipeDiagnosticCode::UnsupportedLight:
            case SharedRecipeDiagnosticCode::MissingEnvironment:
            case SharedRecipeDiagnosticCode::FallbackUsed:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool IsSharedDegradedFallbackDiagnostic(
            const SharedRecipeDiagnosticCode code) noexcept
        {
            switch (code)
            {
            case SharedRecipeDiagnosticCode::EmptyVisibilityInput:
            case SharedRecipeDiagnosticCode::EmptyLightingInput:
            case SharedRecipeDiagnosticCode::StaleInput:
            case SharedRecipeDiagnosticCode::DegradedOutput:
            case SharedRecipeDiagnosticCode::MissingEnvironment:
            case SharedRecipeDiagnosticCode::FallbackUsed:
                return true;
            case SharedRecipeDiagnosticCode::None:
            case SharedRecipeDiagnosticCode::InvalidRenderable:
            case SharedRecipeDiagnosticCode::MissingGeometry:
            case SharedRecipeDiagnosticCode::MissingInstance:
            case SharedRecipeDiagnosticCode::NonFiniteBounds:
            case SharedRecipeDiagnosticCode::NotVisible:
            case SharedRecipeDiagnosticCode::UnsupportedRenderDomain:
            case SharedRecipeDiagnosticCode::UnsupportedProduct:
            case SharedRecipeDiagnosticCode::InvalidLight:
            case SharedRecipeDiagnosticCode::UnsupportedLight:
            case SharedRecipeDiagnosticCode::MissingRendererCapability:
            case SharedRecipeDiagnosticCode::ProductNotProduced:
                return false;
            }
            return false;
        }

        [[nodiscard]] const RecipeExtensionSlotDescriptor* FindRecipeSlot(
            const RenderRecipeDescriptor& recipe,
            const std::string_view stableName) noexcept
        {
            const auto it = std::find_if(recipe.Slots.begin(),
                                         recipe.Slots.end(),
                                         [stableName](const RecipeExtensionSlotDescriptor& slot) {
                                             return slot.StableName == stableName;
                                         });
            return it == recipe.Slots.end() ? nullptr : &*it;
        }

        [[nodiscard]] bool ContainsCapability(
            const std::vector<RendererCapability>& capabilities,
            const RendererCapability capability) noexcept
        {
            return std::find(capabilities.begin(), capabilities.end(), capability) != capabilities.end();
        }

        [[nodiscard]] bool RendererSupportsCapabilities(
            const RendererDescriptor& renderer,
            const std::vector<RendererCapability>& required) noexcept
        {
            return std::all_of(required.begin(),
                               required.end(),
                               [&renderer](const RendererCapability capability) {
                                   return ContainsCapability(renderer.SupportedCapabilities, capability);
                               });
        }

        void AddFrameRecipeOverrideDiagnostic(
            FrameRecipeOverrideProjection& projection,
            const FrameRecipeOverrideDiagnosticCode code,
            std::string subject,
            std::string message)
        {
            projection.Diagnostics.push_back(FrameRecipeOverrideDiagnostic{
                .Code = code,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        [[nodiscard]] bool IsDisableMappedSlot(const std::string_view stableName) noexcept
        {
            return stableName == "postprocess" ||
                   stableName == "debug-view" ||
                   stableName == "picking" ||
                   stableName == "lighting";
        }

        void DisableMappedFrameRecipeSlot(FrameRecipeFeatures& features,
                                          const std::string_view stableName,
                                          std::uint32_t& disabledSlotCount) noexcept
        {
            if (stableName == "postprocess")
            {
                if (features.EnablePostProcess || features.EnableAntiAliasing)
                {
                    ++disabledSlotCount;
                }
                features.EnablePostProcess = false;
                features.EnableAntiAliasing = false;
                return;
            }
            if (stableName == "debug-view")
            {
                if (features.EnableDebugView)
                {
                    ++disabledSlotCount;
                }
                features.EnableDebugView = false;
                return;
            }
            if (stableName == "picking")
            {
                if (features.EnablePicking)
                {
                    ++disabledSlotCount;
                }
                features.EnablePicking = false;
                return;
            }
            if (stableName == "lighting")
            {
                if (features.LightingPath != FrameRecipeLightingPath::Forward ||
                    features.EnableClusterGridBuild ||
                    features.EnableClusterLightAssignment)
                {
                    ++disabledSlotCount;
                }
                features.LightingPath = FrameRecipeLightingPath::Forward;
                features.EnableClusterGridBuild = false;
                features.EnableClusterLightAssignment = false;
            }
        }

        void AppendContractDiagnostics(RenderGraphContractIntegrationStats& stats,
                                       const RenderingContractValidationResult& result)
        {
            for (const RenderingContractDiagnostic& diagnostic : result.Diagnostics)
            {
                std::string message{ToString(diagnostic.Code)};
                if (!diagnostic.Subject.empty())
                {
                    message += ": ";
                    message += diagnostic.Subject;
                }
                if (!diagnostic.Message.empty())
                {
                    message += " - ";
                    message += diagnostic.Message;
                }
                stats.Diagnostics.push_back(std::move(message));
                if (IsContractOutputDiagnostic(diagnostic.Code))
                {
                    ++stats.MissingOutputDiagnosticCount;
                }
            }
        }

        void AppendSharedDiagnostics(RenderGraphContractIntegrationStats& stats,
                                     const std::vector<SharedRecipeDiagnostic>& diagnostics)
        {
            for (const SharedRecipeDiagnostic& diagnostic : diagnostics)
            {
                std::string message{ToString(diagnostic.Code)};
                if (!diagnostic.Subject.empty())
                {
                    message += ": ";
                    message += diagnostic.Subject;
                }
                if (!diagnostic.Message.empty())
                {
                    message += " - ";
                    message += diagnostic.Message;
                }
                stats.Diagnostics.push_back(std::move(message));
                if (IsSharedUnsupportedDiagnostic(diagnostic.Code))
                {
                    ++stats.UnsupportedProductDiagnosticCount;
                }
                if (IsSharedDegradedFallbackDiagnostic(diagnostic.Code))
                {
                    ++stats.DegradedFallbackDiagnosticCount;
                }
            }
        }

        [[nodiscard]] RenderArtifactMetadata MakeDeclaredOutputArtifact(
            const CurrentRendererContract& contract,
            const ViewOutputDescriptor& output)
        {
            return RenderArtifactMetadata{
                .ArtifactId = contract.ViewOutput.RecipeId + "." + output.Name,
                .RendererId = contract.Renderer.Id,
                .SnapshotId = contract.Snapshot.Id,
                .ViewOutputRecipeId = contract.ViewOutput.RecipeId,
                .SourceRevisions = contract.Snapshot.SourceRevisions,
                .Status = RenderArtifactStatus::Declared,
                .Lifetime = RenderArtifactLifetime::Transient,
                .Purpose = output.Name,
            };
        }

        [[nodiscard]] std::vector<SharedRecipeProductKind> MergeSharedProducts(
            const VisibilityRecipeExecutionResult& visibility,
            const LightingRecipeExecutionResult& lighting)
        {
            std::vector<SharedRecipeProductKind> products = visibility.Products;
            products.insert(products.end(), lighting.Products.begin(), lighting.Products.end());
            return products;
        }

        [[nodiscard]] SharedRecipeRendererProductDeclaration MakeCurrentRendererSharedProductDeclaration(
            const RendererDescriptor& renderer)
        {
            return SharedRecipeRendererProductDeclaration{
                .Renderer = renderer,
                .ConsumedProducts = {
                    SharedRecipeProductKind::VisibleItemSet,
                    SharedRecipeProductKind::GroupingKeys,
                    SharedRecipeProductKind::BatchGroups,
                    SharedRecipeProductKind::InstanceGroups,
                    SharedRecipeProductKind::LodSelections,
                    SharedRecipeProductKind::SpatialPartitions,
                    SharedRecipeProductKind::LightSet,
                    SharedRecipeProductKind::EnvironmentMap,
                    SharedRecipeProductKind::ShadowIntent,
                    SharedRecipeProductKind::Fallbacks,
                },
            };
        }

        void PopulateCurrentRendererContractIntegrationStats(
            RenderGraphFrameStats& frameStats,
            const RenderWorld& renderWorld,
            const std::uint64_t frameIndex,
            const bool readbackRequested)
        {
            RenderGraphContractIntegrationStats stats{};
            const CurrentRendererContract contract = MakeCurrentRendererContract(
                renderWorld,
                CurrentRendererSnapshotOptions{.FrameIndex = frameIndex},
                CurrentRendererOutputOptions{.ReadbackRequested = readbackRequested});
            const VisibilityRecipeExecutionResult visibility =
                ExecuteVisibilityRecipe(renderWorld, contract.Snapshot);
            const LightingRecipeExecutionResult lighting =
                ExecuteLightingRecipe(renderWorld, contract.Snapshot);
            const std::vector<SharedRecipeProductKind> products =
                MergeSharedProducts(visibility, lighting);
            const SharedRecipeCompatibilityResult sharedCompatibility =
                CheckSharedRecipeCompatibility(
                    MakeCurrentRendererSharedProductDeclaration(contract.Renderer),
                    std::span<const SharedRecipeProductKind>{products.data(), products.size()});

            stats.Evaluated = true;
            stats.ContractCompatible = IsCompatible(contract.Diagnostics);
            stats.SharedProductsCompatible = sharedCompatibility.Compatible();
            stats.ArtifactMetadataValid = true;
            stats.RendererId = contract.Renderer.Id;
            stats.SnapshotId = contract.Snapshot.Id;
            stats.RecipeId = contract.Recipe.RecipeId;
            stats.ViewOutputRecipeId = contract.ViewOutput.RecipeId;
            stats.SnapshotSourceRevisionCount =
                static_cast<std::uint32_t>(contract.Snapshot.SourceRevisions.size());
            stats.BindingIntentCount =
                static_cast<std::uint32_t>(contract.Bindings.Intents.size());
            stats.RecipeSlotCount =
                static_cast<std::uint32_t>(contract.Recipe.Slots.size());
            stats.ViewOutputCount =
                static_cast<std::uint32_t>(contract.ViewOutput.Outputs.size());
            stats.VisibilityProductCount =
                static_cast<std::uint32_t>(visibility.Products.size());
            stats.VisibilityVisibleItemCount =
                static_cast<std::uint32_t>(visibility.VisibleItems.size());
            stats.VisibilityRejectedItemCount =
                static_cast<std::uint32_t>(visibility.RejectedItems.size());
            stats.LightingProductCount =
                static_cast<std::uint32_t>(lighting.Products.size());
            stats.LightingResolvedLightCount =
                static_cast<std::uint32_t>(lighting.Lights.size());
            stats.LightingIntentCount =
                static_cast<std::uint32_t>(lighting.Intents.size());

            AppendContractDiagnostics(stats, contract.Diagnostics);
            AppendSharedDiagnostics(stats, visibility.Diagnostics);
            AppendSharedDiagnostics(stats, lighting.Diagnostics);
            AppendSharedDiagnostics(stats, sharedCompatibility.Diagnostics);

            for (const ViewOutputDescriptor& output : contract.ViewOutput.Outputs)
            {
                RenderArtifactMetadata artifact =
                    MakeDeclaredOutputArtifact(contract, output);
                const RenderingContractValidationResult artifactValidation =
                    ValidateRenderArtifactMetadata(contract.Renderer,
                                                   contract.ViewOutput,
                                                   artifact);
                if (!IsCompatible(artifactValidation))
                {
                    stats.ArtifactMetadataValid = false;
                    stats.ArtifactPublicationFailureDiagnosticCount +=
                        CountBySeverity(artifactValidation,
                                        RenderingContractDiagnosticSeverity::Error);
                    AppendContractDiagnostics(stats, artifactValidation);
                }
                stats.DeclaredArtifacts.push_back(std::move(artifact));
            }
            stats.DeclaredArtifactCount =
                static_cast<std::uint32_t>(stats.DeclaredArtifacts.size());
            frameStats.Contract = std::move(stats);
        }

        void FinalizeCurrentRendererContractIntegrationStats(RenderGraphFrameStats& stats)
        {
            if (!stats.Contract.Evaluated)
            {
                return;
            }

            const bool frameProducedOutputs =
                stats.Execute.Succeeded && stats.Execute.DeviceOperational;
            for (RenderArtifactMetadata& artifact : stats.Contract.DeclaredArtifacts)
            {
                if (!frameProducedOutputs)
                {
                    artifact.Status = RenderArtifactStatus::Failed;
                    artifact.Diagnostics.push_back(
                        "render graph did not produce this declared output");
                    ++stats.Contract.ArtifactPublicationFailureDiagnosticCount;
                    continue;
                }
                if (artifact.Purpose == "readback" &&
                    stats.DefaultRecipeBackbufferReadbackCopyCount == 0u)
                {
                    artifact.Status = RenderArtifactStatus::Missing;
                    artifact.Diagnostics.push_back(
                        "readback output was declared but no backbuffer copy recorded");
                    ++stats.Contract.MissingOutputDiagnosticCount;
                    continue;
                }
                artifact.Status = RenderArtifactStatus::Available;
                artifact.Lifetime = RenderArtifactLifetime::Transient;
            }
        }

        [[nodiscard]] constexpr bool IsReconstructionAAMode(const FrameRecipeAAMode mode) noexcept
        {
            return mode == FrameRecipeAAMode::TAA ||
                   mode == FrameRecipeAAMode::ExternalReconstructor;
        }

        [[nodiscard]] RHI::TextureLayout ToTextureLayout(const TextureBarrierState state)
        {
            switch (state)
            {
            case TextureBarrierState::Undefined:           return RHI::TextureLayout::Undefined;
            case TextureBarrierState::ColorAttachmentWrite: return RHI::TextureLayout::ColorAttachment;
            case TextureBarrierState::ColorAttachmentRead:  return RHI::TextureLayout::ColorAttachment;
            case TextureBarrierState::DepthWrite:           return RHI::TextureLayout::DepthAttachment;
            case TextureBarrierState::DepthRead:            return RHI::TextureLayout::DepthReadOnly;
            case TextureBarrierState::ShaderRead:           return RHI::TextureLayout::ShaderReadOnly;
            case TextureBarrierState::ShaderWrite:          return RHI::TextureLayout::General;
            case TextureBarrierState::TransferSrc:          return RHI::TextureLayout::TransferSrc;
            case TextureBarrierState::TransferDst:          return RHI::TextureLayout::TransferDst;
            case TextureBarrierState::Present:              return RHI::TextureLayout::Present;
            }
            return RHI::TextureLayout::Undefined;
        }

        [[nodiscard]] RHI::MemoryAccess ToMemoryAccess(const TextureBarrierState state)
        {
            switch (state)
            {
            case TextureBarrierState::Undefined:            return RHI::MemoryAccess::None;
            case TextureBarrierState::ColorAttachmentWrite: return RHI::MemoryAccess::ColorAttachmentWrite;
            case TextureBarrierState::ColorAttachmentRead:  return RHI::MemoryAccess::ColorAttachmentRead;
            case TextureBarrierState::DepthRead:            return RHI::MemoryAccess::DepthStencilRead;
            case TextureBarrierState::DepthWrite:           return RHI::MemoryAccess::DepthStencilWrite;
            case TextureBarrierState::ShaderRead:           return RHI::MemoryAccess::ShaderRead;
            case TextureBarrierState::ShaderWrite:          return RHI::MemoryAccess::ShaderWrite;
            case TextureBarrierState::TransferSrc:          return RHI::MemoryAccess::TransferRead;
            case TextureBarrierState::TransferDst:          return RHI::MemoryAccess::TransferWrite;
            case TextureBarrierState::Present:              return RHI::MemoryAccess::None;
            }
            return RHI::MemoryAccess::None;
        }

        [[nodiscard]] RHI::MemoryAccess ToMemoryAccess(const BufferBarrierState state)
        {
            switch (state)
            {
            case BufferBarrierState::Undefined:     return RHI::MemoryAccess::None;
            case BufferBarrierState::IndirectRead:  return RHI::MemoryAccess::IndirectRead;
            case BufferBarrierState::IndexRead:     return RHI::MemoryAccess::IndexRead;
            case BufferBarrierState::VertexRead:    return RHI::MemoryAccess::ShaderRead;
            case BufferBarrierState::ShaderRead:    return RHI::MemoryAccess::ShaderRead;
            case BufferBarrierState::ShaderWrite:   return RHI::MemoryAccess::ShaderWrite;
            case BufferBarrierState::TransferSrc:    return RHI::MemoryAccess::TransferRead;
            case BufferBarrierState::TransferDst:    return RHI::MemoryAccess::TransferWrite;
            case BufferBarrierState::HostReadback:   return RHI::MemoryAccess::HostRead;
            }
            return RHI::MemoryAccess::None;
        }

        [[nodiscard]] constexpr std::uint64_t AlignUpForRendererPlacement(
            const std::uint64_t value,
            const std::uint64_t alignment) noexcept
        {
            if (alignment <= 1u)
            {
                return value;
            }
            const std::uint64_t remainder = value % alignment;
            return remainder == 0u ? value : value + (alignment - remainder);
        }

        inline constexpr std::uint32_t kInvalidRendererTransientPlacementResource =
            std::numeric_limits<std::uint32_t>::max();

        struct RendererTransientPlacementItem
        {
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t FirstUsePass = 0u;
            std::uint32_t LastUsePass = 0u;
            RHI::ResourceMemoryRequirements Requirements{};
        };

        struct RendererTransientAliasReuseHazard
        {
            std::uint32_t PreviousResourceIndex = 0u;
            std::uint32_t ResourceIndex = 0u;
            std::uint32_t PassIndex = 0u;
            std::uint32_t BlockIndex = 0u;
            std::uint64_t OffsetBytes = 0u;
            std::uint64_t SizeBytes = 0u;
        };

        struct RendererTransientPlacementPlan
        {
            std::vector<TransientResourcePlacement> Placements{};
            std::vector<RendererTransientAliasReuseHazard> AliasReuseHazards{};
            std::uint64_t NaiveBytes = 0u;
            std::uint64_t PeakBytes = 0u;
            std::uint64_t BlockAlignmentBytes = 1u;
            std::uint32_t MemoryTypeBits = 0u;
            bool IsValid = true;
        };

        [[nodiscard]] RendererTransientPlacementPlan BuildRendererTransientPlacementPlan(
            std::vector<RendererTransientPlacementItem> items,
            const bool aliasingEnabled)
        {
            struct ActiveRange
            {
                std::uint32_t ResourceIndex = 0u;
                std::uint32_t LastUsePass = 0u;
                std::uint32_t BlockIndex = 0u;
                std::uint64_t OffsetBytes = 0u;
                std::uint64_t SizeBytes = 0u;
            };

            struct FreeRange
            {
                std::uint32_t BlockIndex = 0u;
                std::uint64_t OffsetBytes = 0u;
                std::uint64_t SizeBytes = 0u;
                std::uint32_t PreviousResourceIndex = kInvalidRendererTransientPlacementResource;
            };

            RendererTransientPlacementPlan plan{};
            plan.Placements.reserve(items.size());

            if (items.empty())
            {
                return plan;
            }

            std::ranges::sort(items, [](const RendererTransientPlacementItem& lhs,
                                        const RendererTransientPlacementItem& rhs)
            {
                return std::tie(lhs.FirstUsePass, lhs.ResourceIndex) <
                       std::tie(rhs.FirstUsePass, rhs.ResourceIndex);
            });

            plan.MemoryTypeBits = items.front().Requirements.MemoryTypeBits;
            for (const RendererTransientPlacementItem& item : items)
            {
                if (!item.Requirements.IsValid() || item.Requirements.DedicatedAllocationRequired)
                {
                    plan.IsValid = false;
                    return plan;
                }
                plan.NaiveBytes += item.Requirements.SizeBytes;
                plan.BlockAlignmentBytes =
                    std::max(plan.BlockAlignmentBytes, item.Requirements.AlignmentBytes);
                plan.MemoryTypeBits &= item.Requirements.MemoryTypeBits;
            }

            if (plan.MemoryTypeBits == 0u)
            {
                plan.IsValid = false;
                return plan;
            }

            std::vector<ActiveRange> activeRanges{};
            std::vector<FreeRange> freeRanges{};
            std::uint64_t blockSize = 0u;

            auto sortFreeRanges = [&]() {
                std::ranges::sort(freeRanges, [](const FreeRange& lhs, const FreeRange& rhs) {
                    return std::tuple{lhs.BlockIndex, lhs.OffsetBytes, lhs.SizeBytes, lhs.PreviousResourceIndex} <
                           std::tuple{rhs.BlockIndex, rhs.OffsetBytes, rhs.SizeBytes, rhs.PreviousResourceIndex};
                });
            };

            for (const RendererTransientPlacementItem& item : items)
            {
                for (std::size_t activeIndex = 0u; activeIndex < activeRanges.size();)
                {
                    const ActiveRange& active = activeRanges[activeIndex];
                    if (active.LastUsePass < item.FirstUsePass)
                    {
                        if (aliasingEnabled && active.SizeBytes != 0u)
                        {
                            freeRanges.push_back(FreeRange{
                                .BlockIndex = active.BlockIndex,
                                .OffsetBytes = active.OffsetBytes,
                                .SizeBytes = active.SizeBytes,
                                .PreviousResourceIndex = active.ResourceIndex,
                            });
                        }
                        activeRanges.erase(activeRanges.begin() + static_cast<std::ptrdiff_t>(activeIndex));
                        continue;
                    }
                    ++activeIndex;
                }

                sortFreeRanges();

                bool placedInFreeRange = false;
                std::uint32_t blockIndex = 0u;
                std::uint64_t offsetBytes = 0u;

                if (aliasingEnabled && item.Requirements.SizeBytes != 0u)
                {
                    for (std::size_t rangeIndex = 0u; rangeIndex < freeRanges.size(); ++rangeIndex)
                    {
                        const FreeRange range = freeRanges[rangeIndex];
                        const std::uint64_t alignedOffset =
                            AlignUpForRendererPlacement(range.OffsetBytes, item.Requirements.AlignmentBytes);
                        const std::uint64_t rangeEnd = range.OffsetBytes + range.SizeBytes;
                        if (alignedOffset > rangeEnd ||
                            item.Requirements.SizeBytes > rangeEnd - alignedOffset)
                        {
                            continue;
                        }

                        blockIndex = range.BlockIndex;
                        offsetBytes = alignedOffset;
                        placedInFreeRange = true;
                        freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(rangeIndex));

                        if (range.OffsetBytes < alignedOffset)
                        {
                            freeRanges.push_back(FreeRange{
                                .BlockIndex = range.BlockIndex,
                                .OffsetBytes = range.OffsetBytes,
                                .SizeBytes = alignedOffset - range.OffsetBytes,
                                .PreviousResourceIndex = range.PreviousResourceIndex,
                            });
                        }

                        const std::uint64_t allocationEnd =
                            alignedOffset + item.Requirements.SizeBytes;
                        if (allocationEnd < rangeEnd)
                        {
                            freeRanges.push_back(FreeRange{
                                .BlockIndex = range.BlockIndex,
                                .OffsetBytes = allocationEnd,
                                .SizeBytes = rangeEnd - allocationEnd,
                                .PreviousResourceIndex = range.PreviousResourceIndex,
                            });
                        }

                        if (range.PreviousResourceIndex != kInvalidRendererTransientPlacementResource)
                        {
                            plan.AliasReuseHazards.push_back(RendererTransientAliasReuseHazard{
                                .PreviousResourceIndex = range.PreviousResourceIndex,
                                .ResourceIndex = item.ResourceIndex,
                                .PassIndex = item.FirstUsePass,
                                .BlockIndex = blockIndex,
                                .OffsetBytes = offsetBytes,
                                .SizeBytes = item.Requirements.SizeBytes,
                            });
                        }
                        break;
                    }
                }

                if (!placedInFreeRange)
                {
                    offsetBytes =
                        AlignUpForRendererPlacement(blockSize, item.Requirements.AlignmentBytes);
                    blockSize = offsetBytes + item.Requirements.SizeBytes;
                }

                plan.Placements.push_back(TransientResourcePlacement{
                    .ResourceIndex = item.ResourceIndex,
                    .BlockIndex = blockIndex,
                    .OffsetBytes = offsetBytes,
                    .SizeBytes = item.Requirements.SizeBytes,
                    .AlignmentBytes = item.Requirements.AlignmentBytes,
                    .FirstUsePass = item.FirstUsePass,
                    .LastUsePass = item.LastUsePass,
                });

                activeRanges.push_back(ActiveRange{
                    .ResourceIndex = item.ResourceIndex,
                    .LastUsePass = item.LastUsePass,
                    .BlockIndex = blockIndex,
                    .OffsetBytes = offsetBytes,
                    .SizeBytes = item.Requirements.SizeBytes,
                });
            }

            plan.PeakBytes = AlignUpForRendererPlacement(blockSize, plan.BlockAlignmentBytes);
            std::ranges::sort(plan.Placements, [](const TransientResourcePlacement& lhs,
                                                  const TransientResourcePlacement& rhs)
            {
                return lhs.ResourceIndex < rhs.ResourceIndex;
            });
            return plan;
        }

        [[nodiscard]] BarrierPacket& FindOrCreateRendererBarrierPacket(
            std::vector<BarrierPacket>& packets,
            const std::uint32_t passIndex,
            const BarrierPacketStage stage)
        {
            const auto it = std::ranges::find_if(packets, [passIndex, stage](const BarrierPacket& packet) {
                return packet.PassIndex == passIndex && packet.Stage == stage;
            });
            if (it != packets.end())
            {
                return *it;
            }

            packets.push_back(BarrierPacket{
                .Kind = BarrierKind::AliasReuse,
                .PassIndex = passIndex,
                .Stage = stage,
            });
            return packets.back();
        }

        void SortRendererBarrierPackets(std::vector<BarrierPacket>& packets)
        {
            std::ranges::sort(packets, [](const BarrierPacket& lhs, const BarrierPacket& rhs) {
                return std::tuple{lhs.PassIndex, BarrierPacketStageSortKey(lhs.Stage)} <
                       std::tuple{rhs.PassIndex, BarrierPacketStageSortKey(rhs.Stage)};
            });
        }

        [[nodiscard]] constexpr RHI::MemoryAccess AliasReuseBeforeAccess() noexcept
        {
            return RHI::MemoryAccess::ShaderWrite |
                   RHI::MemoryAccess::TransferWrite |
                   RHI::MemoryAccess::ColorAttachmentWrite |
                   RHI::MemoryAccess::DepthStencilWrite;
        }

        [[nodiscard]] constexpr RHI::MemoryAccess AliasReuseAfterAccess() noexcept
        {
            return RHI::MemoryAccess::IndirectRead |
                   RHI::MemoryAccess::IndexRead |
                   RHI::MemoryAccess::ShaderRead |
                   RHI::MemoryAccess::ShaderWrite |
                   RHI::MemoryAccess::TransferRead |
                   RHI::MemoryAccess::TransferWrite |
                   RHI::MemoryAccess::ColorAttachmentRead |
                   RHI::MemoryAccess::ColorAttachmentWrite |
                   RHI::MemoryAccess::DepthStencilRead |
                   RHI::MemoryAccess::DepthStencilWrite;
        }

        void SubmitBarrierPacket(RHI::ICommandContext& cmd,
                                 const CompiledRenderGraph& graph,
                                 const BarrierPacket& packet,
                                 const RHI::QueueCapabilityProfile& frameGraphProfile)
        {
            // BUG-015: a queue-family ownership transfer (QFOT) is only real when
            // the device's framegraph queue profile actually schedules the
            // producer and consumer onto *different* queues. The promoted device
            // reports a graphics-only profile and demotes every pass onto the
            // graphics queue, so any compiled release/acquire pair must collapse
            // to a plain barrier here (IGNORED families). Otherwise single-queue
            // submission records a QFOT acquire with no matching release
            // (UNASSIGNED-VkBufferMemoryBarrier-buffer-00004) plus duplicate
            // release/acquire warnings (-00001/-00003). Resolving against the
            // profile (not bound Vulkan families) keeps this correct regardless of
            // backend queue-family wiring.
            constexpr std::uint32_t kNoQueueFamilyTransfer = static_cast<std::uint32_t>(-1);
            const auto isLiveCrossQueueTransfer =
                [&frameGraphProfile](const QueueOwnershipTransfer& transfer) noexcept
            {
                return IsLiveCrossQueueOwnershipTransfer(transfer, frameGraphProfile);
            };

            std::vector<RHI::TextureBarrierDesc> textureBarriers;
            textureBarriers.reserve(packet.TextureBarriers.size());
            for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
            {
                const bool liveTransfer = isLiveCrossQueueTransfer(barrier.OwnershipTransfer);
                textureBarriers.push_back(RHI::TextureBarrierDesc{
                    .Texture = graph.TextureHandles[barrier.TextureIndex],
                    .BeforeLayout = ToTextureLayout(barrier.Before),
                    .AfterLayout = ToTextureLayout(barrier.After),
                    .BeforeAccess = ToMemoryAccess(barrier.Before),
                    .AfterAccess = ToMemoryAccess(barrier.After),
                    .SrcQueueFamily = liveTransfer ? barrier.OwnershipTransfer.SourceQueueFamily
                                                   : kNoQueueFamilyTransfer,
                    .DstQueueFamily = liveTransfer ? barrier.OwnershipTransfer.DestinationQueueFamily
                                                   : kNoQueueFamilyTransfer,
                });
            }

            std::vector<RHI::BufferBarrierDesc> bufferBarriers;
            bufferBarriers.reserve(packet.BufferBarriers.size());
            for (const BufferBarrierPacket& barrier : packet.BufferBarriers)
            {
                const bool liveTransfer = isLiveCrossQueueTransfer(barrier.OwnershipTransfer);
                bufferBarriers.push_back(RHI::BufferBarrierDesc{
                    .Buffer = graph.BufferHandles[barrier.BufferIndex],
                    .BeforeAccess = ToMemoryAccess(barrier.Before),
                    .AfterAccess = ToMemoryAccess(barrier.After),
                    .SrcQueueFamily = liveTransfer ? barrier.OwnershipTransfer.SourceQueueFamily
                                                   : kNoQueueFamilyTransfer,
                    .DstQueueFamily = liveTransfer ? barrier.OwnershipTransfer.DestinationQueueFamily
                                                   : kNoQueueFamilyTransfer,
                });
            }

            std::vector<RHI::MemoryBarrierDesc> memoryBarriers;
            memoryBarriers.reserve(packet.TextureAliasReuseBarriers.size() +
                                   packet.BufferAliasReuseBarriers.size());
            for (const TextureAliasReuseBarrierPacket& barrier : packet.TextureAliasReuseBarriers)
            {
                (void)barrier;
                memoryBarriers.push_back(RHI::MemoryBarrierDesc{
                    .BeforeAccess = AliasReuseBeforeAccess(),
                    .AfterAccess = AliasReuseAfterAccess(),
                });
            }
            for (const BufferAliasReuseBarrierPacket& barrier : packet.BufferAliasReuseBarriers)
            {
                (void)barrier;
                memoryBarriers.push_back(RHI::MemoryBarrierDesc{
                    .BeforeAccess = AliasReuseBeforeAccess(),
                    .AfterAccess = AliasReuseAfterAccess(),
                });
            }

            if (textureBarriers.empty() && bufferBarriers.empty() && memoryBarriers.empty())
            {
                return;
            }

            cmd.SubmitBarriers(RHI::BarrierBatchDesc{
                .TextureBarriers = textureBarriers,
                .BufferBarriers = bufferBarriers,
                .MemoryBarriers = memoryBarriers,
            });
        }

        constexpr float kCameraInverseDeterminantEpsilon = 0.000001f;
        constexpr float kClusterDefaultVerticalFovRadians = 1.5707963267948966f;
        constexpr float kClusterDefaultNearZ = 0.1f;
        constexpr float kClusterDefaultFarZ = 1000.0f;
        constexpr float kMinLineWidth = 0.5f;
        constexpr float kMaxLineWidth = 32.0f;
        constexpr float kMinPointRadius = 0.0001f;
        constexpr float kMaxPointRadius = 1.0f;

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] bool IsValidDebugLine(const DebugLinePacket& line) noexcept
        {
            return IsFinite(line.Start) && IsFinite(line.End) && IsFinite(line.Color) &&
                   std::isfinite(line.Width) && line.Width > 0.f;
        }

        [[nodiscard]] bool IsValidDebugPoint(const DebugPointPacket& point) noexcept
        {
            return IsFinite(point.Position) && IsFinite(point.Color) &&
                   std::isfinite(point.Radius) && point.Radius > 0.f;
        }

        [[nodiscard]] bool IsValidDebugTriangle(const DebugTrianglePacket& triangle) noexcept
        {
            return IsFinite(triangle.A) && IsFinite(triangle.B) &&
                   IsFinite(triangle.C) && IsFinite(triangle.Color);
        }

        [[nodiscard]] bool IsFinite(const glm::mat4& value) noexcept
        {
            return IsFinite(value[0]) && IsFinite(value[1]) &&
                   IsFinite(value[2]) && IsFinite(value[3]);
        }

        [[nodiscard]] bool IsValidTransformGizmo(const TransformGizmoRenderPacket& gizmo) noexcept
        {
            return IsFinite(gizmo.Transform) &&
                   IsFinite(gizmo.XAxisColor) && IsFinite(gizmo.YAxisColor) && IsFinite(gizmo.ZAxisColor) &&
                   std::isfinite(gizmo.AxisLength) && gizmo.AxisLength > 0.f;
        }

        [[nodiscard]] bool IsInvertibleFiniteMatrix(const glm::mat4& value) noexcept
        {
            const float determinant = glm::determinant(value);
            return IsFinite(value) && std::isfinite(determinant) &&
                   std::abs(determinant) > kCameraInverseDeterminantEpsilon;
        }
    }

    [[nodiscard]] FrameRecipeOverrideProjection ProjectFrameRecipeOverride(
        const FrameRecipeFeatures& derivedDefaults,
        const FrameRecipeOverride& recipeOverride)
    {
        FrameRecipeOverrideProjection projection{
            .Features = derivedDefaults,
        };
        const RendererDescriptor renderer = MakeCurrentRendererDescriptor();
        const RenderRecipeDescriptor baseRecipe = MakeCurrentRendererRecipeDescriptor();

        if (recipeOverride.Recipe.RecipeId.empty())
        {
            AddFrameRecipeOverrideDiagnostic(projection,
                                             FrameRecipeOverrideDiagnosticCode::EmptyRecipeId,
                                             "recipe.recipeId",
                                             "frame-recipe override must carry a non-empty recipe id");
        }
        if (!recipeOverride.Recipe.FixedCoreName.empty() &&
            recipeOverride.Recipe.FixedCoreName != baseRecipe.FixedCoreName)
        {
            AddFrameRecipeOverrideDiagnostic(projection,
                                             FrameRecipeOverrideDiagnosticCode::FixedCoreMutation,
                                             recipeOverride.Recipe.FixedCoreName,
                                             "frame-recipe override cannot replace the fixed frame core");
        }

        for (const RecipeExtensionSlotDescriptor& slot : recipeOverride.Recipe.Slots)
        {
            const RecipeExtensionSlotDescriptor* baseSlot =
                FindRecipeSlot(baseRecipe, slot.StableName);
            if (baseSlot == nullptr)
            {
                AddFrameRecipeOverrideDiagnostic(projection,
                                                 FrameRecipeOverrideDiagnosticCode::UnknownSlot,
                                                 slot.StableName,
                                                 "frame-recipe override references an undeclared slot");
                continue;
            }
            if (slot.Kind != baseSlot->Kind || baseSlot->Kind == RecipeSlotKind::FixedCore)
            {
                if (slot.StableName != baseSlot->StableName ||
                    slot.Kind != baseSlot->Kind ||
                    slot.SchemaId != baseSlot->SchemaId)
                {
                    AddFrameRecipeOverrideDiagnostic(projection,
                                                     FrameRecipeOverrideDiagnosticCode::FixedCoreMutation,
                                                     slot.StableName,
                                                     "frame-recipe override cannot mutate the fixed frame core");
                }
            }
            if (!RendererSupportsCapabilities(renderer, slot.RequiredCapabilities))
            {
                AddFrameRecipeOverrideDiagnostic(projection,
                                                 FrameRecipeOverrideDiagnosticCode::UnsupportedCapability,
                                                 slot.StableName,
                                                 "frame-recipe override requires a renderer capability that is unavailable");
            }
        }

        for (const std::string& stableName : recipeOverride.DisabledExtensionSlots)
        {
            const RecipeExtensionSlotDescriptor* baseSlot = FindRecipeSlot(baseRecipe, stableName);
            if (baseSlot == nullptr)
            {
                AddFrameRecipeOverrideDiagnostic(projection,
                                                 FrameRecipeOverrideDiagnosticCode::UnknownSlot,
                                                 stableName,
                                                 "frame-recipe override disables an undeclared slot");
                continue;
            }
            if (baseSlot->Kind == RecipeSlotKind::FixedCore)
            {
                AddFrameRecipeOverrideDiagnostic(projection,
                                                 FrameRecipeOverrideDiagnosticCode::FixedCoreSlotDisabled,
                                                 stableName,
                                                 "frame-recipe override cannot disable the fixed frame core");
                continue;
            }
            if (!IsDisableMappedSlot(stableName))
            {
                AddFrameRecipeOverrideDiagnostic(projection,
                                                 FrameRecipeOverrideDiagnosticCode::UnsupportedSlotDisable,
                                                 stableName,
                                                 "frame-recipe override can only disable slots with live feature gates");
                continue;
            }
            DisableMappedFrameRecipeSlot(projection.Features,
                                         stableName,
                                         projection.DisabledSlotCount);
        }

        if (!projection.Diagnostics.empty())
        {
            projection.Features = derivedDefaults;
            projection.DisabledSlotCount = 0u;
            projection.Applied = false;
            return projection;
        }

        projection.Applied = !recipeOverride.DisabledExtensionSlots.empty();
        return projection;
    }

    void IRenderer::SubmitUvViewRequest(UvViewRequest request)
    {
        (void)request;
    }

    UvViewOutput IRenderer::GetUvViewOutput() const
    {
        return UvViewOutput{
            .Status = UvViewStatus::CpuFallbackNonOperational,
            .ActiveMode = UvViewActiveMode::CpuLayout,
            .Diagnostic = "Renderer backend does not provide the optional GPU UV view.",
        };
    }

    class NullRenderer final : public IRenderer
    {
    private:
        static constexpr std::uint32_t kRuntimeSnapshotStorageSlots = 4u;

        struct RuntimeSnapshotStorage
        {
            std::vector<VisualizationSyncRecord>           VisualizationSyncRecords;
            std::vector<VisualizationPropertyBufferUploadDescriptor> VisualizationPropertyBuffers;
            std::vector<VisualizationPropertyBufferAddress> VisualizationPropertyBufferAddresses;
            std::vector<std::vector<std::byte>>             VisualizationPropertyBufferPayloads;
            std::vector<VisualizationAttributeBufferPacket> VisualizationAttributeBuffers;
            std::vector<ScalarAttributePacket>              VisualizationScalars;
            std::vector<ColorAttributePacket>               VisualizationColors;
            std::vector<VectorFieldOverlayPacket>           VisualizationVectorFields;
            std::vector<IsolineOverlayPacket>               VisualizationIsolines;
            std::vector<HtexPatchPreviewAtlasPacket>        VisualizationHtexAtlases;
            std::vector<FragmentBakeAtlasPacket>            VisualizationFragmentBakeAtlases;
            VisualizationDiagnostics                        VisualizationDiagnostics{};
            VisualizationPropertyBufferDiagnostics          VisualizationPropertyBufferDiagnostics{};
            VisualizationOverlaySummary                     VisualizationOverlaySummary{};
            std::vector<TransformSyncRecord>                TransformSyncRecords;
            std::vector<LightSnapshot>                      LightSnapshots;
            std::vector<DebugLinePacket>                    DebugLinePackets;
            std::vector<DebugPointPacket>                   DebugPointPackets;
            std::vector<DebugTrianglePacket>                DebugTrianglePackets;
            std::vector<TransformGizmoRenderPacket>         TransformGizmoPackets;
            std::vector<RenderableSnapshot>                 RenderableSnapshots;
            std::vector<std::uint32_t>                      SelectionSelectedStableIds;
            std::uint32_t                                   SelectionHoveredStableId{0u};
            bool                                            SelectionHasHovered{false};
            std::uint32_t                                   InvalidSnapshotRecordCount{0u};

            void Clear()
            {
                VisualizationSyncRecords.clear();
                VisualizationPropertyBuffers.clear();
                VisualizationPropertyBufferAddresses.clear();
                VisualizationPropertyBufferPayloads.clear();
                VisualizationAttributeBuffers.clear();
                VisualizationScalars.clear();
                VisualizationColors.clear();
                VisualizationVectorFields.clear();
                VisualizationIsolines.clear();
                VisualizationHtexAtlases.clear();
                VisualizationFragmentBakeAtlases.clear();
                VisualizationDiagnostics = {};
                VisualizationPropertyBufferDiagnostics = {};
                VisualizationOverlaySummary = {};
                TransformSyncRecords.clear();
                LightSnapshots.clear();
                DebugLinePackets.clear();
                DebugPointPackets.clear();
                DebugTrianglePackets.clear();
                TransformGizmoPackets.clear();
                RenderableSnapshots.clear();
                SelectionSelectedStableIds.clear();
                SelectionHoveredStableId = 0u;
                SelectionHasHovered = false;
                InvalidSnapshotRecordCount = 0u;
            }
        };

        struct GpuProfilePassMetadata
        {
            std::uint32_t Ordinal{0u};
            std::string Name{};
            FramePassId Id{};
            RHI::QueueAffinity Queue{RHI::QueueAffinity::Graphics};
            RenderCommandPassStatus CommandStatus{
                RenderCommandPassStatus::SkippedUnavailable};
            RHI::ProfilerScopeToken Token{};
        };

        struct ActiveGpuProfileFrame
        {
            RHI::IProfiler* Profiler{nullptr};
            RHI::ProfilerFrameKey Frame{};
            std::vector<GpuProfilePassMetadata> Passes{};
            std::vector<std::uint32_t> MetadataIndexByPass{};
            std::array<bool, 2u> OpenQueues{};
            bool ExecuteSucceeded{false};
        };

        struct SubmittedGpuProfileFrame
        {
            RHI::ProfilerFrameKey Frame{};
            std::vector<GpuProfilePassMetadata> Passes{};
        };

        [[nodiscard]] static std::uint32_t NormalizeRuntimeSnapshotSlot(
            const std::uint32_t storageSlot) noexcept
        {
            return storageSlot < kRuntimeSnapshotStorageSlots ? storageSlot : 0u;
        }

        [[nodiscard]] RuntimeSnapshotStorage& RuntimeSnapshotSlot(
            const std::uint32_t storageSlot) noexcept
        {
            return m_RuntimeSnapshotSlots[NormalizeRuntimeSnapshotSlot(storageSlot)];
        }

        [[nodiscard]] const RuntimeSnapshotStorage& RuntimeSnapshotSlot(
            const std::uint32_t storageSlot) const noexcept
        {
            return m_RuntimeSnapshotSlots[NormalizeRuntimeSnapshotSlot(storageSlot)];
        }

        [[nodiscard]] RuntimeSnapshotStorage& ActiveRuntimeSnapshotStorage() noexcept
        {
            return RuntimeSnapshotSlot(m_ActiveRuntimeSnapshotReadSlot);
        }

        [[nodiscard]] const RuntimeSnapshotStorage& ActiveRuntimeSnapshotStorage() const noexcept
        {
            return RuntimeSnapshotSlot(m_ActiveRuntimeSnapshotReadSlot);
        }

        void ClearRuntimeSnapshotSlots()
        {
            for (RuntimeSnapshotStorage& storage : m_RuntimeSnapshotSlots)
                storage.Clear();
        }

    public:
        NullRenderer()
        {
            m_RenderGraph.SetTransientAliasingEnabled(false);
            RegisterCommandRoutes();
        }

        void Initialize(RHI::IDevice& device) override
        {
            m_Device = &device;
            m_BackbufferFormat = device.GetBackbufferFormat();
            m_Subsystems.Initialize(device);
            // GRAPHICS-077 Slice B — backend-local transient-debug upload
            // helper. Constructed alongside the BufferManager so per-frame
            // `UploadTriangles(...)` can lease a single growing host-
            // visible vertex buffer. Reset in `Shutdown()` before the
            // BufferManager so its `BufferLease` destructor observes a
            // live manager. The default in-renderer implementation is
            // CPU-functional against `MockDevice` for the contract gate;
            // the Vulkan-tuned concrete implementation lands with
            // GRAPHICS-077 Slice D.
            m_TransientDebugUploadHelper =
                std::make_unique<TransientDebugUploadHelper>(device, *m_Subsystems.BufferManager());
            // GRAPHICS-078 Slice B — backend-local visualization-overlay
            // upload helper. Same lifetime contract as the transient-
            // debug helper above: constructed alongside the
            // BufferManager so per-frame `UploadVectorFields(...)` can
            // lease a single growing host-visible vertex buffer; reset
            // in `Shutdown()` before the BufferManager so the helper's
            // internal `BufferLease` destructor observes a live
            // manager. The default in-renderer implementation is
            // CPU-functional against `MockDevice` for the contract
            // gate; the Vulkan-tuned concrete implementation lands
            // with GRAPHICS-078 Slice D.
            m_VisualizationOverlayUploadHelper =
                std::make_unique<VisualizationOverlayUploadHelper>(device, *m_Subsystems.BufferManager());
            // GRAPHICS-084 — graphics-owned visualization property-buffer
            // residency. Constructed beside the other upload helpers so
            // runtime/editor code can submit copied CPU property arrays while
            // the renderer owns BufferManager leases and publishes BDAs into
            // visualization packets before validation.
            m_VisualizationPropertyBufferResidency =
                std::make_unique<VisualizationPropertyBufferResidency>(
                    device, *m_Subsystems.BufferManager());
            // GRAPHICS-079 Slice C — backend-neutral ImGui upload helper.
            // Mirrors the transient-debug / visualization-overlay helpers:
            // one growing host-visible vertex buffer and one growing index
            // buffer owned by the renderer, reset in Shutdown before the
            // BufferManager is destroyed.
            m_ImGuiUploadHelper =
                std::make_unique<ImGuiUploadHelper>(device, *m_Subsystems.BufferManager());
            // GRAPHICS-074 Slice A — `EntityIdPass` is selection-system-bound
            // and consumes the `SurfaceOpaque` cull bucket via
            // `EntityIdPass::Execute(...)`. The pass must be emplaced before
            // the operational publisher creates the EntityId selection
            // pipeline and calls `SetPipeline(...)` — same publisher-before-
            // first-frame invariant as the forward / shadow / deferred passes
            // below — otherwise the first frame would observe a `has_value()`
            // lease but a default-constructed pipeline handle on the pass
            // itself, and `Execute()` would early-return on
            // `!m_Pipeline.IsValid()` while the executor still reported
            // `Recorded`.
            m_SelectionEntityIdPass.emplace(*m_Subsystems.SelectionSystemRegistry());
            // GRAPHICS-074 Slice B — Face/Edge/Point ID selection passes
            // share the same publisher-before-first-frame invariant as the
            // EntityId pass above: each is emplaced here so the operational
            // publisher can `SetPipeline(...)` on the pass instance before
            // any executor branch reaches `Execute(...)`. Same fail-closed
            // semantics: `has_value()` lease + default pipeline handle on
            // the pass would silently early-return inside `Execute()` while
            // the executor still reported `Recorded`.
            m_SelectionFaceIdPass.emplace(*m_Subsystems.SelectionSystemRegistry());
            m_SelectionEdgeIdPass.emplace(*m_Subsystems.SelectionSystemRegistry());
            m_SelectionPointIdPass.emplace(*m_Subsystems.SelectionSystemRegistry());
            // GRAPHICS-074 Slice C — `SelectionOutlinePass` is selection-system-
            // bound and renders a fullscreen overlay into the current present
            // source. Same publisher-before-first-frame invariant as the
            // other selection passes above: emplaced here so the operational
            // publisher can `SetPipeline(...)` on the pass instance before
            // any executor branch reaches `Execute(...)`. `Execute()` early-
            // returns when the pipeline handle or `SelectionSystem` is not
            // initialised, so a missing pipeline yields `SkippedUnavailable`
            // on the executor taxonomy rather than a silently-recorded no-op.
            m_SelectionOutlinePass.emplace(*m_Subsystems.SelectionSystemRegistry());
            // GRAPHICS-070/071/073 — default-recipe forward surface/line/point/
            // shadow passes own their system-bound instances plus pipeline
            // leases created from `InitializeOperationalPassResources()`. The
            // passes must be emplaced before that publisher runs so the
            // initial operational `Initialize()` path can call
            // `SetPipeline(...)` on each pass — otherwise the first frame
            // would see a `has_value()` lease but a default-constructed
            // pipeline handle on the pass itself, and `Execute()` would
            // early-return on `!m_Pipeline.IsValid()` while the executor still
            // reported `Recorded`. Pipeline leases + `SetPipeline()` are
            // routed through the same code path on
            // `RebuildOperationalResources()` so the post-operational-
            // transition reset (GRAPHICS-018R) republishes them byte-identical.
            m_ForwardSurfacePass.emplace(*m_Subsystems.ForwardSystemRegistry());
            m_ForwardLinePass.emplace(*m_Subsystems.ForwardSystemRegistry());
            m_ForwardPointPass.emplace(*m_Subsystems.ForwardSystemRegistry());
            // GRAPHICS-073 Slice A — ShadowSystem must be live before the
            // operational publisher creates the depth-only shadow pipeline
            // and calls `SetPipeline(...)` on `m_ShadowPass`.
            // GRAPHICS-073 Slice B — ShadowSystem now owns the depth atlas +
            // `sampler2DShadow`-bindable sampler. The managers are emplaced
            // earlier in this function (line 207), so the system can hold
            // long-lived references and lazily allocate the atlas when
            // `SetParams(...)` enables shadows. The atlas is *not* reallocated
            // by `RebuildOperationalResources()` so the imported handle stays
            // byte-identical across rebuilds.
            m_ShadowPass.emplace(*m_Subsystems.ShadowSystemRegistry());
            // GRAPHICS-038A/B — retained HZB ping-pong resource + build pass
            // target. The system owns two renderer-retained textures through
            // TextureManager; ExecuteFrame allocates/resizes them for the
            // current viewport before importing `HZB.Current` into the recipe.
            m_HZBSystem.emplace();
            m_HZBSystem->Initialize(device, *m_Subsystems.TextureManager());
            // GRAPHICS-040C — retained temporal reconstruction history. The
            // recipe imports the ping-pong images only when TAA/external
            // reconstruction is selected; allocation is lazy per viewport in
            // ExecuteFrame(), matching HZB's retained-resource cadence.
            m_ReconstructionHistorySystem.emplace();
            m_ReconstructionHistorySystem->Initialize(device, *m_Subsystems.TextureManager());
            // GRAPHICS-072 Slice A — DeferredSystem and its `DeferredGBufferPass`
            // must be live before the operational publisher runs so the
            // initial `Initialize()` path can call `SetPipeline(...)` on the
            // GBuffer pass. The same invariant the forward / shadow passes
            // follow above: `has_value()` lease but an unset pipeline handle
            // on the pass would silently early-return inside `Execute()` while
            // the executor still reported `Recorded`.
            m_DeferredGBufferPass.emplace(*m_Subsystems.DeferredSystemRegistry());
            // GRAPHICS-072 Slice B — DeferredLightingPass must be live before
            // the operational publisher runs so the initial `Initialize()`
            // path can call `SetPipeline(...)` on the lighting pass. Same
            // invariant as the GBuffer pass above: `has_value()` lease but an
            // unset pipeline handle on the pass would silently early-return
            // inside `Execute()` while the executor still reported `Recorded`.
            // Slice C: the pass also takes the `ShadowSystem&` so
            // `Execute(...)` can publish the atlas bindless index through the
            // pushed `DeferredLightingPushConstants::ShadowAtlasBindlessIndex`
            // field. The system has already been emplaced + Initialize'd
            // above (m_Subsystems.ShadowSystemRegistry()), so the reference is live before the
            // operational publisher runs.
            m_DeferredLightingPass.emplace(*m_Subsystems.DeferredSystemRegistry(), *m_Subsystems.ShadowSystemRegistry());
            // GRAPHICS-075 Slice A — `PostProcessSystem` must be live before
            // the operational publisher runs so the initial `Initialize()`
            // path can call `SetPipeline(...)` on `m_PostProcessToneMapPass`.
            // Same invariant the forward / shadow / deferred / selection
            // passes follow above: a `has_value()` lease but a default-
            // constructed pipeline handle on the pass would silently early-
            // return inside `PostProcessToneMapPass::Execute()` while the
            // executor still reported `Recorded`.
            //
            // GRAPHICS-075 Slice D.2b — uses the device-aware Initialize
            // overload so PostProcessSystem allocates + uploads its retained
            // SMAA `AreaTex` / `SearchTex` LUT textures and the exposure-
            // adaptation history buffer up-front when the device is
            // operational. The overload is idempotent and no-ops when the
            // device is non-operational; the RebuildOperationalResources()
            // path below re-invokes it so a device that becomes operational
            // later picks up the allocation without a Shutdown()+Initialize()
            // round-trip.
            m_PostProcessToneMapPass.emplace(*m_Subsystems.PostProcessSystemRegistry());
            // GRAPHICS-075 Slice B.1 — same lifetime contract as the
            // tonemap pass above: emplace after `m_Subsystems.PostProcessSystemRegistry()` is
            // initialised and before the operational publisher runs, so
            // the initial `Initialize()` path can call
            // `SetDownsamplePipeline(...)` / `SetUpsamplePipeline(...)` on
            // `m_PostProcessBloomPass`.
            m_PostProcessBloomPass.emplace(*m_Subsystems.PostProcessSystemRegistry());
            // GRAPHICS-075 Slice C — same lifetime contract as the bloom
            // pass above: emplace after `m_Subsystems.PostProcessSystemRegistry()` is
            // initialised and before the operational publisher runs, so
            // the initial `Initialize()` path can call `SetPipeline(...)`
            // on `m_PostProcessFXAAPass`. The FXAA leg is gated by
            // `PostProcessSettings::AntiAliasing == FXAA` inside the pass
            // body (which `IsStageEnabled` already enforces); the helper
            // still reports `Recorded` under the umbrella's accumulator
            // when the stage is disabled, mirroring the bloom helper's
            // "structurally-recorded no-op" taxonomy.
            m_PostProcessFXAAPass.emplace(*m_Subsystems.PostProcessSystemRegistry());
            // GRAPHICS-075 Slice D.2a — SMAA pass shares the same lifetime
            // contract as the bloom + FXAA passes above. Mutually
            // exclusive with FXAA per `PostProcessSettings::AntiAliasing`;
            // `IsStageEnabled(SMAA)` short-circuits the per-stage Execute
            // calls to no-op when AA is `None` or `FXAA`, while the
            // per-stage umbrella helpers still report `Recorded` under
            // their `"PostProcessAA{Edge,Blend,Resolve}Pass"`
            // accumulators. Per-stage pipeline leases (edge / blend /
            // resolve) are bound in `InitializeOperationalPassResources`.
            m_PostProcessSMAAPass.emplace(*m_Subsystems.PostProcessSystemRegistry());
            // GRAPHICS-075 Slice E.1 — same lifetime contract as the
            // tonemap + bloom + FXAA + SMAA passes above; emplaced after
            // `m_Subsystems.PostProcessSystemRegistry()` is initialised and before the
            // operational publisher runs so the initial `Initialize()`
            // path can call `SetPipeline(...)` on
            // `m_PostProcessHistogramPass`.
            m_PostProcessHistogramPass.emplace(*m_Subsystems.PostProcessSystemRegistry());
            // GRAPHICS-076 Slice B — `DebugViewSystem` is a renderer-owned
            // CPU-only system (resource inspection / deterministic
            // selection / fallback diagnostics) that the canonical
            // `Pass.DebugView` records against. Initialize is called
            // synchronously so the system's resolved-selection state is
            // valid from the first frame; the per-frame
            // `SetSettings({.Enabled = ...}) + ResolveSelection(recipe)`
            // pair runs inside `ExecuteFrame()` after the
            // `FrameRecipeIntrospection` for the current frame is
            // computed. The pass is emplaced immediately after the
            // system so the operational publisher's
            // `SetPipeline(...)` call observes a fully-constructed
            // `DebugViewPass` with the `DebugViewSystem&` reference
            // bound; the same publisher-before-first-frame invariant
            // the selection / forward / deferred / postprocess passes
            // above all follow.
            m_DebugViewSystem.emplace();
            m_DebugViewSystem->Initialize();
            m_DebugViewPass.emplace(*m_DebugViewSystem);
            if (device.IsOperational())
            {
                [[maybe_unused]] const bool passResourcesReady = InitializeOperationalPassResources(device);
            }
            if (m_ImGuiOverlaySystem != nullptr && m_Subsystems.TextureManager() && m_Subsystems.SamplerManager())
            {
                m_ImGuiOverlaySystem->InitializeGpuResources(
                    device,
                    *m_Subsystems.TextureManager(),
                    *m_Subsystems.SamplerManager());
            }
            // CullingSystem::Initialize requires a shader path — concrete
            // renderers supply it.  NullRenderer skips the cull dispatch.
        }

        bool RebuildOperationalResources(RHI::IDevice& device) override
        {
            m_Device = &device;
            if (!device.IsOperational())
            {
                m_CullingOutputAvailable = false;
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    "Renderer operational-resource rebuild requires an operational device.";
                return false;
            }
            m_BackbufferFormat = device.GetBackbufferFormat();
            if (!m_Subsystems.PipelineManager() || !m_Subsystems.TextureManager() ||
                !m_Subsystems.CullingSystemRegistry())
            {
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    "Renderer operational-resource rebuild requires initialized renderer systems.";
                return false;
            }

            if (!m_Subsystems.RebuildOperationalResources(device))
            {
                const RenderSubsystemRegistryDiagnostics diagnostics = m_Subsystems.GetDiagnostics();
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    diagnostics.LastRebuildFailedMissingRequiredSubsystem
                        ? "Renderer operational-resource rebuild requires initialized renderer systems."
                        : "Renderer operational-resource rebuild failed while recreating registry resources.";
                return false;
            }
            if (m_ReconstructionHistorySystem.has_value())
            {
                m_ReconstructionHistorySystem->Initialize(device, *m_Subsystems.TextureManager());
            }
            if (m_ImGuiOverlaySystem != nullptr && m_Subsystems.TextureManager() && m_Subsystems.SamplerManager())
            {
                m_ImGuiOverlaySystem->InitializeGpuResources(
                    device,
                    *m_Subsystems.TextureManager(),
                    *m_Subsystems.SamplerManager());
            }

            const bool passResourcesReady = InitializeOperationalPassResources(device);
            m_RenderGraph.Reset();
            m_RenderGraphCompileCache.reset();
            m_LastRenderGraphStats.LifecycleDiagnostic = m_CullingOutputAvailable
                ? std::string{}
                : std::string{"Renderer operational-resource rebuild completed with culling unavailable."};
            return passResourcesReady;
        }

        void Shutdown() override
        {
            ReleaseAllFrameTransientResources();
            m_Device = nullptr;
            ClearRuntimeSnapshotSlots();
            m_Subsystems.ShutdownSystems();
            // GRAPHICS-076 Slice B — drop the renderer-owned
            // `DebugViewSystem` alongside the other CPU-only systems
            // above. The pass + pipeline-lease resets below land before
            // `m_DebugViewSystem.reset()` later in this function so the
            // optional destructor never observes a dangling system
            // reference.
            if (m_DebugViewSystem) m_DebugViewSystem->Shutdown();
            if (m_HZBSystem)       m_HZBSystem->Shutdown();
            if (m_ReconstructionHistorySystem) m_ReconstructionHistorySystem->Shutdown();

            // GRAPHICS-070/071/072/073/074 — drop forward, deferred GBuffer,
            // shadow, and EntityId selection passes before resetting their
            // system dependencies below so optional destructors do not observe
            // a dangling reference.
            m_ForwardSurfacePass.reset();
            m_ForwardLinePass.reset();
            m_ForwardPointPass.reset();
            m_ShadowPass.reset();
            m_DeferredGBufferPass.reset();
            m_DeferredLightingPass.reset();
            m_SelectionEntityIdPass.reset();
            m_SelectionFaceIdPass.reset();
            m_SelectionEdgeIdPass.reset();
            m_SelectionPointIdPass.reset();
            m_SelectionOutlinePass.reset();
            // GRAPHICS-075 Slice A — reset the tonemap pass before its system
            // dependency below so the optional destructor does not observe a
            // dangling `PostProcessSystem&`. Same lifetime contract as the
            // selection / forward / deferred / shadow passes above.
            m_PostProcessToneMapPass.reset();
            // GRAPHICS-075 Slice B.1 — bloom pass shares the same lifetime
            // contract as the tonemap pass above.
            m_PostProcessBloomPass.reset();
            // GRAPHICS-075 Slice C — FXAA pass shares the same lifetime
            // contract as the bloom + tonemap passes above.
            m_PostProcessFXAAPass.reset();
            // GRAPHICS-075 Slice D.1 — SMAA pass shares the same lifetime
            // contract as the bloom + tonemap + FXAA passes above; drop
            // before `m_Subsystems.PostProcessSystemRegistry()` is reset below so the optional
            // destructor does not observe a dangling reference.
            m_PostProcessSMAAPass.reset();
            // GRAPHICS-075 Slice E.1 — histogram pass shares the same
            // lifetime contract as the SMAA / FXAA / bloom / tonemap
            // passes above; drop before `m_Subsystems.PostProcessSystemRegistry()` is reset
            // below so the optional destructor does not observe a
            // dangling reference.
            m_PostProcessHistogramPass.reset();
            // GRAPHICS-076 Slice B — reset the canonical `DebugViewPass`
            // before `m_DebugViewSystem` is destroyed below so the
            // optional destructor does not observe a dangling
            // `DebugViewSystem&`. Mirrors the lifetime contract the
            // selection / forward / deferred / postprocess passes follow
            // for their system-bound `Execute(...)` references.
            m_DebugViewPass.reset();
            m_DebugViewSystem.reset();
            // GRAPHICS-079 Slice A — reset the canonical `ImGuiPass` consumer
            // before teardown so its optional destructor does not observe a
            // dangling `ImGuiOverlaySystem&`. Keep the borrowed overlay pointer
            // long enough to release Slice C's manager-backed font-atlas leases
            // before the managers are reset below.
            m_ImGuiPass.reset();
            m_HZBSystem      .reset();
            m_ReconstructionHistorySystem.reset();
            m_DepthPrepassPipelineLease.reset();
            m_DefaultDebugSurfacePipelineLease.reset();
            // GRAPHICS-076 Slice A — drop the canonical default-recipe
            // present pipeline lease before `m_Subsystems.PipelineManager()` is destroyed
            // below.
            m_PresentPipelineLease.reset();
            // GRAPHICS-076 Slice B — drop the canonical default-recipe
            // `Pass.DebugView` pipeline lease alongside the present
            // lease above; same teardown ordering contract (lease reset
            // before `m_Subsystems.PipelineManager()` is destroyed below).
            m_DebugViewPipelineLease.reset();
            // GRAPHICS-079 Slice A — drop the canonical default-recipe
            // `Pass.ImGui` pipeline lease alongside the debug-view lease
            // above; same teardown ordering contract (lease reset before
            // `m_Subsystems.PipelineManager()` is destroyed below).
            m_ImGuiPipelineLease.reset();
            m_ImGuiRgba8PipelineLease.reset();
            // GRAPHICS-077 Slices B + C — drop the canonical default-
            // recipe transient-debug pipeline leases (triangle + line +
            // point lanes, depth-tested + always-on-top per lane)
            // alongside the debug-view lease above; same teardown
            // ordering contract (leases reset before
            // `m_Subsystems.PipelineManager()` is destroyed below).
            m_TransientDebugTrianglePipelineLeaseDepthTested.reset();
            m_TransientDebugTrianglePipelineLeaseAlwaysOnTop.reset();
            m_TransientDebugLinePipelineLeaseDepthTested.reset();
            m_TransientDebugLinePipelineLeaseAlwaysOnTop.reset();
            m_TransientDebugPointPipelineLeaseDepthTested.reset();
            m_TransientDebugPointPipelineLeaseAlwaysOnTop.reset();
            // GRAPHICS-078 Slices B + C — drop the canonical default-
            // recipe visualization-overlay pipeline leases (vector-
            // field + isoline lanes, depth-tested + always-on-top each)
            // alongside the transient-debug leases above; same teardown
            // ordering contract (leases reset before `m_Subsystems.PipelineManager()`
            // is destroyed below).
            m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested.reset();
            m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop.reset();
            m_VisualizationOverlayIsolinePipelineLeaseDepthTested.reset();
            m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop.reset();
            m_ForwardSurfacePipelineLease.reset();
            m_ForwardLinePipelineLease.reset();
            m_ForwardPointPipelineLease.reset();
            m_ShadowPipelineLease.reset();
            m_DeferredGBufferPipelineLease.reset();
            m_DeferredLightingPipelineLease.reset();
            m_SelectionEntityIdPipelineLease.reset();
            m_SelectionEntityIdOutlinePipelineLease.reset();
            m_SelectionFaceIdPipelineLease.reset();
            m_SelectionEdgeIdPipelineLease.reset();
            m_SelectionPointIdPipelineLease.reset();
            m_SelectionOutlinePipelineLease.reset();
            m_PostProcessToneMapPipelineLease.reset();
            // GRAPHICS-075 Slice B.1 — drop the bloom pipeline leases
            // alongside the tonemap lease before the BufferManager /
            // PipelineManager are torn down below.
            m_PostProcessBloomDownsamplePipelineLease.reset();
            m_PostProcessBloomUpsamplePipelineLease.reset();
            // GRAPHICS-075 Slice C — drop the FXAA pipeline lease alongside
            // the tonemap + bloom leases above; same teardown ordering
            // contract.
            m_PostProcessFXAAPipelineLease.reset();
            // GRAPHICS-075 Slice D.1 — drop the three SMAA pipeline leases
            // alongside the FXAA lease above; same teardown ordering
            // contract. The leases must reset before
            // `m_Subsystems.PipelineManager()` is torn down below since the lease
            // destructor calls back through the manager.
            m_PostProcessSMAAEdgePipelineLease.reset();
            m_PostProcessSMAABlendPipelineLease.reset();
            m_PostProcessSMAAResolvePipelineLease.reset();
            // GRAPHICS-075 Slice E.1 — drop the histogram pipeline lease
            // alongside the SMAA leases above; same teardown ordering
            // contract. The lease must reset before `m_Subsystems.PipelineManager()`
            // is torn down below since the lease destructor calls back
            // through the manager.
            m_PostProcessHistogramPipelineLease.reset();
            m_HZBBuildPipelineLease.reset();
            m_ClusterGridBuildPipelineLease.reset();
            m_ClusterLightAssignmentPipelineLease.reset();
            m_ClusterGridAABBBuffer.reset();
            m_ClusterLightHeaderBuffer.reset();
            m_ClusterLightIndexBuffer.reset();
            m_ClusterLightCounterBuffer.reset();
            m_ClusterGridDesc = {};
            m_ClusterGridProjection = {};
            // GRAPHICS-074 Slice D.1 — drop the renderer-owned
            // `Picking.Readback` lease before the BufferManager is torn
            // down so the lease's destructor still observes a live manager
            // (the manager's `Release` path calls `IDevice::DestroyBuffer`).
            m_PickingReadbackBuffer.reset();
            m_PickingReadbackBufferSize = 0u;
            // GRAPHICS-074 Slice D.3 — drop the per-slot picking metadata
            // alongside the buffer so a later `Initialize(device)` allocates
            // fresh bookkeeping against the new BufferManager. Pending
            // readbacks are simply discarded (the `SelectionSystem` is also
            // about to be torn down on the line above).
            m_PickingSlotPending.clear();
            m_PickingSlotIssuedFrame.clear();
            m_PickingSlotRequest.clear();
            m_PickingSlotInvalidated.clear();
            m_PickingSlotSequence.clear();
            m_PickingSlotDepthCopied.clear();
            // GRAPHICS-075 Slice E.2 — drop the renderer-owned
            // `Histogram.Readback` lease + per-slot metadata before the
            // BufferManager is torn down, mirroring the picking pattern.
            m_HistogramReadbackBuffer.reset();
            m_HistogramReadbackBufferSize = 0u;
            m_HistogramSlotPending.clear();
            m_HistogramSlotIssuedFrame.clear();
            m_HistogramSlotInvalidated.clear();
            // GRAPHICS-076 Slice A — zero the canonical present pass's
            // cached pipeline handle so a later `Initialize(device)` starts
            // from a clean fail-closed state instead of inheriting a stale
            // device handle from a previous operational lifecycle.
            m_PresentPass.SetPipeline(RHI::PipelineHandle{});
            // GRAPHICS-077 Slices B + C — zero the transient-debug
            // surface pass's cached per-lane pipeline handles
            // (triangle + line + point) so a later
            // `Initialize(device)` starts from a clean fail-closed
            // state.
            m_TransientDebugSurfacePass.SetTriangleDepthTestedPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetTriangleAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetLineDepthTestedPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetLineAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetPointDepthTestedPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetPointAlwaysOnTopPipeline(RHI::PipelineHandle{});
            // GRAPHICS-078 Slices B + C — zero the visualization-overlay
            // pass's cached vector-field + isoline pipeline handles
            // alongside the transient-debug handles above so a later
            // `Initialize(device)` starts from a clean fail-closed
            // state.
            m_VisualizationOverlayPass.SetVectorFieldDepthTestedPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetVectorFieldAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetIsolineDepthTestedPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetIsolineAlwaysOnTopPipeline(RHI::PipelineHandle{});
            if (m_ImGuiOverlaySystem != nullptr)
            {
                m_ImGuiOverlaySystem->ShutdownGpuResources();
            }
            m_ImGuiOverlaySystem = nullptr;
            m_RuntimeFrameCommandHooks.clear();
            // GRAPHICS-077 Slice B — drop the transient-debug upload
            // helper before the BufferManager is destroyed so the
            // helper's internal `BufferManager::BufferLease` destructor
            // observes a live manager.
            m_TransientDebugUploadHelper.reset();
            // GRAPHICS-078 Slice B — drop the visualization-overlay
            // upload helper alongside the transient-debug helper above;
            // same teardown ordering contract so its internal
            // `BufferManager::BufferLease` destructor observes a live
            // manager.
            m_VisualizationOverlayUploadHelper.reset();
            // GRAPHICS-084 — release property-buffer leases before the
            // BufferManager is destroyed, matching the upload-helper
            // lifetime contract above.
            m_VisualizationPropertyBufferResidency.reset();
            // GRAPHICS-079 Slice C — release the ImGui helper before the
            // BufferManager for the same lease-lifetime reason as the other
            // renderer-owned upload helpers.
            m_ImGuiUploadHelper.reset();
            m_Subsystems.ResetStorage();
            m_RenderGraph.Reset();
            m_RenderGraphCompileCache.reset();
            DiscardActiveGpuProfile();
            m_SubmittedGpuProfiles.clear();
            m_CurrentGpuProfile = {};
            m_LastGoodGpuProfile.reset();
            ClearGpuPassTelemetry();
            m_CullingOutputAvailable = false;
        }

        void Resize(std::uint32_t, std::uint32_t) override
        {
            m_RenderGraph.Reset();
            m_RenderGraphCompileCache.reset();
        }

        // ── Per-frame phases ──────────────────────────────────────────────

        bool BeginFrame(RHI::FrameHandle& outFrame) override
        {
            ResetFrameState();
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.LifecycleDiagnostic = "BeginFrame requires a live device.";
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::Unavailable,
                    "GPU profiling requires a live render device.");
                m_LastRenderGraphStats.GpuProfile =
                    m_CurrentGpuProfile;
                Core::Log::Error("[Graphics] BeginFrame failed: device missing");
                return false;
            }
            // GRAPHICS-074 Slice D.3 — drain completed picking-readback slots
            // before acquiring the next frame. We use
            // `IDevice::GetGlobalFrameNumber()` (the post-EndFrame counter)
            // as the "completed-frame number" proxy: stub/null backends
            // complete GPU work synchronously inside `EndFrame(...)` so any
            // slot whose `IssuedFrame < GlobalFrameNumber` has flushed; real
            // async backends (Vulkan) signal post-submit fences out of band
            // and a follow-up task may specialise this check via a
            // dedicated `GetCompletedFrameNumber()` / `HasFrameCompleted()`
            // IDevice seam once that lifecycle is plumbed. Each drained
            // slot decodes the `EntityId` word, the `EncodedSelectionId`
            // word (`GRAPHICS-012Q`), and the `SceneDepth` float sample
            // (BUG-026) at `slot * kPickingReadbackSlotStride` and is
            // routed to the `SelectionSystem` via `PublishPickResult`
            // (live hit) or `PublishNoHit` (zero EntityId, invalidated
            // request, or read failure).
            DrainCompletedPickingSlots();
            // GRAPHICS-075 Slice E.2 — drain completed histogram-readback
            // slots before acquiring the next frame. Mirrors the picking
            // drain pattern: uses `IDevice::GetGlobalFrameNumber()` as the
            // completed-frame proxy (the post-EndFrame counter), decodes
            // the 256 uint32 bins each slot copied, and forwards them to
            // `PostProcessSystem::PublishHistogramReadback(...)`. Slots
            // flagged `Invalidated` (e.g. by a `RebuildOperationalResources()`
            // device-lost recovery) are released without publishing so the
            // exposure-history mirror is never anchored to stale
            // pre-rebuild bytes.
            DrainCompletedHistogramSlots();
            const bool began = m_Device->BeginFrame(outFrame);
            if (began)
            {
                m_CurrentFrame = outFrame;
                const std::uint32_t framesInFlight = m_Device->GetFramesInFlight() == 0u
                    ? 1u
                    : m_Device->GetFramesInFlight();
                if (m_SubmittedGpuProfiles.size() < framesInFlight)
                {
                    m_SubmittedGpuProfiles.resize(framesInFlight);
                }
                // VulkanDevice has already completed this reused slot's
                // fence set and retired its query metadata. Resolution here
                // is exact-keyed and nonblocking; no other frame-path wait is
                // introduced for profiling.
                ResolveCompletedGpuProfile(outFrame.FrameIndex);
                m_LastRenderGraphStats.GpuProfile =
                    m_CurrentGpuProfile;
                std::lock_guard<std::mutex> uploadLock(m_DynamicUploadMutex);
                if (m_TransientDebugUploadHelper)
                {
                    m_TransientDebugUploadHelper->BeginFrame(outFrame.FrameIndex, framesInFlight);
                }
                if (m_VisualizationOverlayUploadHelper)
                {
                    m_VisualizationOverlayUploadHelper->BeginFrame(outFrame.FrameIndex, framesInFlight);
                }
                if (m_ImGuiUploadHelper)
                {
                    m_ImGuiUploadHelper->BeginFrame(outFrame.FrameIndex, framesInFlight);
                }
            }
            else
            {
                const RHI::IProfiler* profiler =
                    m_Device->GetProfiler();
                const RHI::ProfilerStatusSnapshot profilerStatus =
                    profiler != nullptr
                        ? profiler->GetStatus()
                        : RHI::ProfilerStatusSnapshot{};
                PublishStaleGpuProfileStatus(
                    profiler != nullptr
                        ? ProfileStatusForBackend(profilerStatus.Status)
                        : RenderGraphGpuProfileStatus::Unavailable,
                    profiler != nullptr &&
                            !profilerStatus.Diagnostic.empty()
                        ? profilerStatus.Diagnostic
                        : "GPU profile resolution is unavailable because "
                          "frame acquisition failed.");
                m_LastRenderGraphStats.GpuProfile =
                    m_CurrentGpuProfile;
            }
            return began;
        }

        // GRAPHICS-079 Slice A — receive the engine-owned `ImGuiOverlaySystem`
        // (runtime owns composition). The renderer-owned `ImGuiPass` borrows the
        // handed-in overlay so `RecordImGuiPass` reads the same submitted frame
        // the `RUNTIME-090` adapter produced. May be called before or after
        // `Initialize()`: if the pipeline lease already exists we bind it to the
        // freshly-constructed pass here; otherwise `InitializeOperationalPassResources`
        // binds it when the lease is created. `nullptr` detaches the consumer so
        // the route reports `SkippedUnavailable`.
        void SetImGuiOverlaySystem(ImGuiOverlaySystem* overlay) noexcept override
        {
            if (m_ImGuiOverlaySystem != nullptr && m_ImGuiOverlaySystem != overlay)
            {
                m_ImGuiOverlaySystem->ShutdownGpuResources();
            }
            m_ImGuiOverlaySystem = overlay;
            if (overlay == nullptr)
            {
                m_ImGuiPass.reset();
                return;
            }
            if (m_Device != nullptr && m_Subsystems.TextureManager().has_value() && m_Subsystems.SamplerManager().has_value())
            {
                overlay->InitializeGpuResources(*m_Device, *m_Subsystems.TextureManager(), *m_Subsystems.SamplerManager());
            }
            m_ImGuiPass.emplace(*overlay);
            if (m_Subsystems.PipelineManager().has_value() && m_ImGuiPipelineLease.has_value() &&
                m_ImGuiPipelineLease->IsValid())
            {
                m_ImGuiPass->SetPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(m_ImGuiPipelineLease->GetHandle()));
            }
        }

        bool HasImGuiOverlaySystem() const noexcept override
        {
            return m_ImGuiOverlaySystem != nullptr && m_ImGuiPass.has_value();
        }

        RuntimeFrameCommandHookHandle RegisterRuntimeFrameCommandHook(
            RuntimeFrameCommandHook hook) override
        {
            if (!hook)
                return {};

            RuntimeFrameCommandHookHandle handle{
                m_NextRuntimeFrameCommandHookHandle++};
            m_RuntimeFrameCommandHooks.push_back(RuntimeFrameCommandHookEntry{
                .Handle = handle,
                .Hook = std::move(hook),
            });
            return handle;
        }

        void UnregisterRuntimeFrameCommandHook(
            const RuntimeFrameCommandHookHandle handle) noexcept override
        {
            if (!handle.IsValid())
                return;
            std::erase_if(
                m_RuntimeFrameCommandHooks,
                [handle](const RuntimeFrameCommandHookEntry& entry) noexcept
                {
                    return entry.Handle == handle;
                });
        }

        void SubmitUvViewRequest(UvViewRequest request) override
        {
            if (m_Subsystems.UvViewSystem())
            {
                m_Subsystems.UvViewSystem()->Submit(std::move(request));
            }
        }

        [[nodiscard]] UvViewOutput GetUvViewOutput() const override
        {
            if (m_Subsystems.UvViewSystem())
            {
                return m_Subsystems.UvViewSystem()->GetOutput();
            }
            return IRenderer::GetUvViewOutput();
        }

        void SubmitRuntimeSnapshots(const RuntimeRenderSnapshotBatch& snapshots,
                                    const std::uint32_t storageSlot) override
        {
            RuntimeSnapshotStorage& storage = RuntimeSnapshotSlot(storageSlot);
            auto& m_TransformSyncRecords = storage.TransformSyncRecords;
            auto& m_LightSnapshots = storage.LightSnapshots;
            auto& m_VisualizationSyncRecords = storage.VisualizationSyncRecords;
            auto& m_VisualizationPropertyBuffers = storage.VisualizationPropertyBuffers;
            auto& m_VisualizationPropertyBufferAddresses = storage.VisualizationPropertyBufferAddresses;
            auto& m_VisualizationPropertyBufferPayloads = storage.VisualizationPropertyBufferPayloads;
            auto& m_VisualizationAttributeBuffers = storage.VisualizationAttributeBuffers;
            auto& m_VisualizationScalars = storage.VisualizationScalars;
            auto& m_VisualizationColors = storage.VisualizationColors;
            auto& m_VisualizationVectorFields = storage.VisualizationVectorFields;
            auto& m_VisualizationIsolines = storage.VisualizationIsolines;
            auto& m_VisualizationHtexAtlases = storage.VisualizationHtexAtlases;
            auto& m_VisualizationFragmentBakeAtlases = storage.VisualizationFragmentBakeAtlases;
            auto& m_VisualizationDiagnostics = storage.VisualizationDiagnostics;
            auto& m_VisualizationPropertyBufferDiagnostics = storage.VisualizationPropertyBufferDiagnostics;
            auto& m_VisualizationOverlaySummary = storage.VisualizationOverlaySummary;
            auto& m_DebugLinePackets = storage.DebugLinePackets;
            auto& m_DebugPointPackets = storage.DebugPointPackets;
            auto& m_DebugTrianglePackets = storage.DebugTrianglePackets;
            auto& m_TransformGizmoPackets = storage.TransformGizmoPackets;
            auto& m_RenderableSnapshots = storage.RenderableSnapshots;
            auto& m_SelectionSelectedStableIds = storage.SelectionSelectedStableIds;
            auto& m_SelectionHoveredStableId = storage.SelectionHoveredStableId;
            auto& m_SelectionHasHovered = storage.SelectionHasHovered;
            auto& m_InvalidSnapshotRecordCount = storage.InvalidSnapshotRecordCount;

            m_TransformSyncRecords.assign(snapshots.Transforms.begin(), snapshots.Transforms.end());
            m_LightSnapshots.assign(snapshots.Lights.begin(), snapshots.Lights.end());
            m_VisualizationSyncRecords.assign(snapshots.Visualizations.begin(), snapshots.Visualizations.end());
            m_VisualizationPropertyBuffers.clear();
            m_VisualizationPropertyBufferPayloads.clear();
            m_VisualizationPropertyBuffers.reserve(snapshots.VisualizationPropertyBuffers.size());
            m_VisualizationPropertyBufferPayloads.reserve(snapshots.VisualizationPropertyBuffers.size());
            for (const VisualizationPropertyBufferUploadDescriptor& descriptor :
                 snapshots.VisualizationPropertyBuffers)
            {
                std::vector<std::byte>& payload =
                    m_VisualizationPropertyBufferPayloads.emplace_back(
                        descriptor.Bytes.begin(), descriptor.Bytes.end());
                VisualizationPropertyBufferUploadDescriptor copied = descriptor;
                copied.Bytes = std::span<const std::byte>{payload.data(), payload.size()};
                m_VisualizationPropertyBuffers.push_back(std::move(copied));
            }
            m_VisualizationAttributeBuffers.assign(snapshots.VisualizationAttributeBuffers.begin(), snapshots.VisualizationAttributeBuffers.end());
            m_VisualizationScalars.assign(snapshots.VisualizationScalars.begin(), snapshots.VisualizationScalars.end());
            m_VisualizationColors.assign(snapshots.VisualizationColors.begin(), snapshots.VisualizationColors.end());
            m_VisualizationVectorFields.assign(snapshots.VisualizationVectorFields.begin(), snapshots.VisualizationVectorFields.end());
            m_VisualizationIsolines.assign(snapshots.VisualizationIsolines.begin(), snapshots.VisualizationIsolines.end());
            m_VisualizationHtexAtlases.assign(snapshots.VisualizationHtexAtlases.begin(), snapshots.VisualizationHtexAtlases.end());
            m_VisualizationFragmentBakeAtlases.assign(snapshots.VisualizationFragmentBakeAtlases.begin(), snapshots.VisualizationFragmentBakeAtlases.end());
            // RUNTIME-089 Slice B — copy the runtime selection snapshot identity
            // into stable storage; ExtractRenderWorld surfaces it as
            // RenderWorld::Selection.
            m_SelectionSelectedStableIds.assign(snapshots.SelectionSelectedStableIds.begin(),
                                                snapshots.SelectionSelectedStableIds.end());
            m_SelectionHoveredStableId = snapshots.SelectionHoveredStableId;
            m_SelectionHasHovered      = snapshots.SelectionHasHovered;

            if (m_VisualizationPropertyBufferResidency)
            {
                m_VisualizationPropertyBufferDiagnostics =
                    m_VisualizationPropertyBufferResidency->Update(
                        m_VisualizationPropertyBuffers);
                const std::span<const VisualizationPropertyBufferAddress> addresses =
                    m_VisualizationPropertyBufferResidency->GetLastAddresses();
                m_VisualizationPropertyBufferAddresses.assign(addresses.begin(), addresses.end());

                auto findAddress = [this](
                    const std::string_view explicitKey,
                    const std::string_view fallbackKey,
                    const VisualizationAttributeDomain domain,
                    const auto typeMatches)
                    -> const VisualizationPropertyBufferAddress*
                {
                    const std::string_view key =
                        explicitKey.empty() ? fallbackKey : explicitKey;
                    if (key.empty())
                    {
                        return nullptr;
                    }

                    const VisualizationPropertyBufferAddress* address =
                        m_VisualizationPropertyBufferResidency->Find(key);
                    if (address == nullptr || address->Domain != domain ||
                        !typeMatches(address->ValueType))
                    {
                        return nullptr;
                    }
                    return address;
                };

                for (VisualizationAttributeBufferPacket& packet :
                     m_VisualizationAttributeBuffers)
                {
                    if (packet.BufferBDA != 0u)
                    {
                        continue;
                    }
                    const VisualizationPropertyBufferAddress* address =
                        findAddress(packet.SourceBufferKey, packet.Name,
                                    packet.Domain,
                                    [valueType = packet.ValueType](
                                        const VisualizationValueType candidate)
                                    {
                                        return candidate == valueType;
                                    });
                    if (address != nullptr)
                    {
                        packet.BufferBDA = address->BufferBDA;
                    }
                }

                for (ScalarAttributePacket& packet : m_VisualizationScalars)
                {
                    if (packet.ScalarBufferBDA != 0u)
                    {
                        continue;
                    }
                    const VisualizationPropertyBufferAddress* address =
                        findAddress(packet.SourceBufferKey, packet.Name,
                                    packet.Domain,
                                    [](const VisualizationValueType candidate)
                                    {
                                        return candidate == VisualizationValueType::ScalarFloat ||
                                               candidate == VisualizationValueType::ScalarDouble;
                                    });
                    if (address != nullptr)
                    {
                        packet.ScalarBufferBDA = address->BufferBDA;
                    }
                }

                for (ColorAttributePacket& packet : m_VisualizationColors)
                {
                    if (packet.ColorBufferBDA != 0u)
                    {
                        continue;
                    }
                    const VisualizationPropertyBufferAddress* address =
                        findAddress(packet.SourceBufferKey, packet.Name,
                                    packet.Domain,
                                    [](const VisualizationValueType candidate)
                                    {
                                        return candidate == VisualizationValueType::Rgba8 ||
                                               candidate == VisualizationValueType::RgbaFloat4;
                                    });
                    if (address != nullptr)
                    {
                        packet.ColorBufferBDA = address->BufferBDA;
                    }
                }

                for (VectorFieldOverlayPacket& packet : m_VisualizationVectorFields)
                {
                    if (packet.PositionBufferBDA == 0u)
                    {
                        const VisualizationPropertyBufferAddress* address =
                            findAddress(packet.PositionBufferSourceKey, {},
                                        packet.Domain,
                                        [](const VisualizationValueType candidate)
                                        {
                                            return candidate == VisualizationValueType::VectorFloat3;
                                        });
                        if (address != nullptr)
                        {
                            packet.PositionBufferBDA = address->BufferBDA;
                        }
                    }
                    if (packet.VectorBufferBDA == 0u)
                    {
                        const VisualizationPropertyBufferAddress* address =
                            findAddress(packet.VectorBufferSourceKey, packet.Name,
                                        packet.Domain,
                                        [](const VisualizationValueType candidate)
                                        {
                                            return candidate == VisualizationValueType::VectorFloat3;
                                        });
                        if (address != nullptr)
                        {
                            packet.VectorBufferBDA = address->BufferBDA;
                        }
                    }
                }

                for (IsolineOverlayPacket& packet : m_VisualizationIsolines)
                {
                    if (packet.ScalarBufferBDA != 0u)
                    {
                        continue;
                    }
                    const VisualizationPropertyBufferAddress* address =
                        findAddress(packet.ScalarBufferSourceKey,
                                    packet.SourceScalarName,
                                    packet.Domain,
                                    [](const VisualizationValueType candidate)
                                    {
                                        return candidate == VisualizationValueType::ScalarFloat ||
                                               candidate == VisualizationValueType::ScalarDouble;
                                    });
                    if (address != nullptr)
                    {
                        packet.ScalarBufferBDA = address->BufferBDA;
                    }
                }
            }
            else
            {
                m_VisualizationPropertyBufferAddresses.clear();
                m_VisualizationPropertyBufferDiagnostics =
                    ValidateVisualizationPropertyBufferUploads(
                        m_VisualizationPropertyBuffers);
                if (!m_VisualizationPropertyBuffers.empty())
                {
                    m_VisualizationPropertyBufferDiagnostics.UploadDeferralCount +=
                        static_cast<std::uint32_t>(
                            m_VisualizationPropertyBuffers.size());
                    m_VisualizationPropertyBufferDiagnostics.HasErrors = true;
                }
            }

            const VisualizationPacketBatch visualizationBatch{
                .PropertyBuffers = m_VisualizationPropertyBuffers,
                .AttributeBuffers = m_VisualizationAttributeBuffers,
                .Scalars = m_VisualizationScalars,
                .Colors = m_VisualizationColors,
                .VectorFields = m_VisualizationVectorFields,
                .Isolines = m_VisualizationIsolines,
                .HtexAtlases = m_VisualizationHtexAtlases,
                .FragmentBakeAtlases = m_VisualizationFragmentBakeAtlases,
            };
            m_VisualizationDiagnostics = ValidateVisualizationPackets(visualizationBatch);
            m_VisualizationOverlaySummary = BuildVisualizationOverlaySummary(visualizationBatch);
            m_InvalidSnapshotRecordCount = 0;
            if (m_Subsystems.MaterialSystemRegistry())
            {
                m_Subsystems.MaterialSystemRegistry()->ResetPerFrameSubstitutionCounters();
            }

            m_DebugLinePackets.clear();
            m_DebugPointPackets.clear();
            m_DebugTrianglePackets.clear();
            m_TransformGizmoPackets.clear();
            m_DebugLinePackets.reserve(snapshots.DebugLines.size());
            m_DebugPointPackets.reserve(snapshots.DebugPoints.size());
            m_DebugTrianglePackets.reserve(snapshots.DebugTriangles.size());
            m_TransformGizmoPackets.reserve(snapshots.TransformGizmos.size());

            for (DebugLinePacket line : snapshots.DebugLines)
            {
                if (!IsValidDebugLine(line))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                line.Width = std::clamp(line.Width, kMinLineWidth, kMaxLineWidth);
                m_DebugLinePackets.push_back(line);
            }

            for (DebugPointPacket point : snapshots.DebugPoints)
            {
                if (!IsValidDebugPoint(point))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                point.Radius = std::clamp(point.Radius, kMinPointRadius, kMaxPointRadius);
                m_DebugPointPackets.push_back(point);
            }

            for (const DebugTrianglePacket& triangle : snapshots.DebugTriangles)
            {
                if (!IsValidDebugTriangle(triangle))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                m_DebugTrianglePackets.push_back(triangle);
            }

            // RUNTIME-082 Slice D follow-up — consume the snapshot batch's
            // SpatialDebug* spans by routing them through the existing
            // wireframe builders and merging the produced Debug{Line,Point,
            // Triangle}Packet records into the debug primitive collections
            // that ExtractRenderWorld surfaces as
            // `RenderWorld::DebugPrimitives`. Without this routing the
            // runtime adapter pump submits non-empty SpatialDebug* spans but
            // the renderer never builds the wireframe geometry, so no
            // visible debug output reaches the canonical debug-primitive
            // pass — the failure mode the original Slice D shipped with.
            //
            // Bounds vs HierarchyNodes: tree adapters (BVH/KDTree/Octree)
            // populate the two spans 1:1 with the same AABB list,
            // HierarchyNodes carrying additional `Depth` + `IsLeaf` for
            // depth-coded coloring. To avoid double-rendering the same
            // wireframe twice per node we prefer HierarchyNodes when both
            // are non-empty; bare-AABB adapters that emit only Bounds
            // still get a flat-color wireframe via the fallback branch.
            auto appendSpatialDebugPackets = [&](const SpatialDebugPacketResult& result) {
                for (DebugLinePacket line : result.Lines)
                {
                    if (!IsValidDebugLine(line))
                    {
                        ++m_InvalidSnapshotRecordCount;
                        continue;
                    }
                    line.Width = std::clamp(line.Width, kMinLineWidth, kMaxLineWidth);
                    m_DebugLinePackets.push_back(line);
                }
                for (DebugPointPacket point : result.Points)
                {
                    if (!IsValidDebugPoint(point))
                    {
                        ++m_InvalidSnapshotRecordCount;
                        continue;
                    }
                    point.Radius = std::clamp(point.Radius, kMinPointRadius, kMaxPointRadius);
                    m_DebugPointPackets.push_back(point);
                }
                for (const DebugTrianglePacket& triangle : result.Triangles)
                {
                    if (!IsValidDebugTriangle(triangle))
                    {
                        ++m_InvalidSnapshotRecordCount;
                        continue;
                    }
                    m_DebugTrianglePackets.push_back(triangle);
                }
            };

            const SpatialDebugVisualizerOptions spatialDebugOptions{};

            if (!snapshots.SpatialDebugHierarchyNodes.empty())
            {
                appendSpatialDebugPackets(
                    BuildSpatialDebugHierarchyWireframes(
                        snapshots.SpatialDebugHierarchyNodes,
                        spatialDebugOptions));
            }
            else if (!snapshots.SpatialDebugBounds.empty())
            {
                appendSpatialDebugPackets(
                    BuildSpatialDebugBoundsWireframes(
                        snapshots.SpatialDebugBounds,
                        spatialDebugOptions));
            }

            if (!snapshots.SpatialDebugSplitPlanes.empty())
            {
                appendSpatialDebugPackets(
                    BuildSpatialDebugSplitPlaneWireframes(
                        snapshots.SpatialDebugSplitPlanes,
                        spatialDebugOptions));
            }

            if (!snapshots.SpatialDebugConvexHullVertices.empty()
                && !snapshots.SpatialDebugConvexHullEdges.empty())
            {
                appendSpatialDebugPackets(
                    BuildSpatialDebugConvexHullWireframe(
                        snapshots.SpatialDebugConvexHullVertices,
                        snapshots.SpatialDebugConvexHullEdges,
                        spatialDebugOptions));
            }

            if (!snapshots.SpatialDebugPointMarkers.empty())
            {
                appendSpatialDebugPackets(
                    BuildSpatialDebugPointMarkers(
                        snapshots.SpatialDebugPointMarkers,
                        spatialDebugOptions));
            }

            for (TransformGizmoRenderPacket gizmo : snapshots.TransformGizmos)
            {
                if (!IsValidTransformGizmo(gizmo))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                gizmo.AxisLength = std::clamp(gizmo.AxisLength, 0.001f, 10000.f);
                m_TransformGizmoPackets.push_back(gizmo);
            }

            // GRAPHICS-031B Decision 7 path-(b): substitute missing / invalid
            // material slots with `kDefaultMaterialSlotIndex` (slot 0 =
            // `Material.DefaultDebugSurface`) and record the substitution
            // category through `MaterialSystem`. Mutate `m_TransformSyncRecords`
            // in place so the downstream `TransformSyncSystem::SyncGpuBuffer`
            // path observes the substituted slot, then mirror the records into
            // the immutable `m_RenderableSnapshots` span exposed via
            // `ExtractRenderWorld`.
            m_RenderableSnapshots.clear();
            m_RenderableSnapshots.reserve(m_TransformSyncRecords.size());
            const std::uint32_t materialCapacity =
                m_Subsystems.MaterialSystemRegistry() ? m_Subsystems.MaterialSystemRegistry()->GetCapacity() : 0u;
            for (TransformSyncRecord& record : m_TransformSyncRecords)
            {
                if (!record.Instance.IsValid())
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }

                if (!record.HasMaterialSlot)
                {
                    record.MaterialSlot = kDefaultMaterialSlotIndex;
                    record.HasMaterialSlot = true;
                    if (m_Subsystems.MaterialSystemRegistry())
                    {
                        m_Subsystems.MaterialSystemRegistry()->RecordMissingMaterialFallback();
                    }
                }
                else if (materialCapacity > 0u && record.MaterialSlot >= materialCapacity)
                {
                    record.MaterialSlot = kDefaultMaterialSlotIndex;
                    if (m_Subsystems.MaterialSystemRegistry())
                    {
                        m_Subsystems.MaterialSystemRegistry()->RecordInvalidMaterialSlot();
                    }
                }

                if (record.MaterialSlot == kDefaultMaterialSlotIndex && m_Subsystems.MaterialSystemRegistry())
                {
                    m_Subsystems.MaterialSystemRegistry()->RecordDefaultDebugSurfaceUse();
                }

                m_RenderableSnapshots.push_back(RenderableSnapshot{
                    .StableId = record.StableId,
                    .Instance = record.Instance,
                    .Model = record.Model,
                    .Bounds = record.Bounds,
                    .RenderFlags = record.RenderFlags,
                    .MaterialSlot = record.MaterialSlot,
                    .HasMaterialSlot = record.HasMaterialSlot,
                });
            }
        }

        RenderWorld ExtractRenderWorld(const RenderFrameInput& input,
                                       const std::uint32_t storageSlot) override
        {
            m_ActiveRuntimeSnapshotReadSlot = NormalizeRuntimeSnapshotSlot(storageSlot);
            const RuntimeSnapshotStorage& storage = ActiveRuntimeSnapshotStorage();
            const auto& m_LightSnapshots = storage.LightSnapshots;
            const auto& m_VisualizationAttributeBuffers = storage.VisualizationAttributeBuffers;
            const auto& m_VisualizationScalars = storage.VisualizationScalars;
            const auto& m_VisualizationColors = storage.VisualizationColors;
            const auto& m_VisualizationVectorFields = storage.VisualizationVectorFields;
            const auto& m_VisualizationIsolines = storage.VisualizationIsolines;
            const auto& m_VisualizationHtexAtlases = storage.VisualizationHtexAtlases;
            const auto& m_VisualizationFragmentBakeAtlases = storage.VisualizationFragmentBakeAtlases;
            const auto& m_VisualizationDiagnostics = storage.VisualizationDiagnostics;
            const auto& m_VisualizationPropertyBufferDiagnostics = storage.VisualizationPropertyBufferDiagnostics;
            const auto& m_VisualizationOverlaySummary = storage.VisualizationOverlaySummary;
            const auto& m_DebugLinePackets = storage.DebugLinePackets;
            const auto& m_DebugPointPackets = storage.DebugPointPackets;
            const auto& m_DebugTrianglePackets = storage.DebugTrianglePackets;
            const auto& m_TransformGizmoPackets = storage.TransformGizmoPackets;
            const auto& m_RenderableSnapshots = storage.RenderableSnapshots;
            const auto& m_SelectionSelectedStableIds = storage.SelectionSelectedStableIds;
            const auto& m_SelectionHoveredStableId = storage.SelectionHoveredStableId;
            const auto& m_SelectionHasHovered = storage.SelectionHasHovered;
            const auto& m_InvalidSnapshotRecordCount = storage.InvalidSnapshotRecordCount;

            m_HasExtractedRenderWorld = true;
            m_HasPreparedFrame = false;
            const PickPixelRequest pick = input.Pick.Pending
                ? input.Pick
                : PickPixelRequest{.X = 0u, .Y = 0u, .Pending = input.HasPendingPick};
            const CameraViewSnapshot camera = BuildCameraViewSnapshot(input.Camera, input.Viewport, pick);
            return RenderWorld{
                .Viewport       = input.Viewport,
                .Alpha          = input.Alpha,
                .HasPendingPick = input.HasPendingPick,
                .DebugOverlayEnabled = input.DebugOverlayEnabled,
                .EnableGpuProfiling = input.EnableGpuProfiling,
                .Camera = camera,
                .Renderables = m_RenderableSnapshots,
                .Lights = m_LightSnapshots,
                .PickRequest = PickRequestSnapshot{
                    .Pending = input.HasPendingPick || input.Pick.Pending,
                    .X = pick.X,
                    .Y = pick.Y,
                    .RayOrigin = camera.PickRayOrigin,
                    .RayDirection = camera.PickRayDirection,
                    .HasRay = camera.HasPickRay,
                    // RUNTIME-089 — carry the runtime selection correlation
                    // token to the picking slot.
                    .Sequence = pick.Sequence,
                },
                .Selection = SelectionSnapshot{
                    // RUNTIME-089 Slice B — identity from the runtime selection
                    // controller; outline styling keeps SelectionSnapshot's
                    // recipe defaults.
                    .SelectedStableIds = m_SelectionSelectedStableIds,
                    .HoveredStableId   = m_SelectionHoveredStableId,
                    .HasHovered        = m_SelectionHasHovered,
                },
                .DebugPrimitives = DebugPrimitiveSnapshot{
                    .Lines = m_DebugLinePackets,
                    .Points = m_DebugPointPackets,
                    .Triangles = m_DebugTrianglePackets,
                    .LineCount = static_cast<std::uint32_t>(m_DebugLinePackets.size()),
                    .PointCount = static_cast<std::uint32_t>(m_DebugPointPackets.size()),
                    .TriangleCount = static_cast<std::uint32_t>(m_DebugTrianglePackets.size()),
                    .HasTransientDebug = input.DebugOverlayEnabled ||
                        !m_DebugLinePackets.empty() ||
                        !m_DebugPointPackets.empty() ||
                        !m_DebugTrianglePackets.empty(),
                },
                .Gizmos = GizmoRenderSnapshot{
                    .TransformGizmos = m_TransformGizmoPackets,
                    .TransformGizmoCount = static_cast<std::uint32_t>(m_TransformGizmoPackets.size()),
                    .HasGizmos = !m_TransformGizmoPackets.empty(),
                },
                .Visualization = VisualizationSnapshot{
                    .AttributeBuffers = m_VisualizationAttributeBuffers,
                    .Scalars = m_VisualizationScalars,
                    .Colors = m_VisualizationColors,
                    .VectorFields = m_VisualizationVectorFields,
                    .Isolines = m_VisualizationIsolines,
                    .HtexAtlases = m_VisualizationHtexAtlases,
                    .FragmentBakeAtlases = m_VisualizationFragmentBakeAtlases,
                    .Diagnostics = m_VisualizationDiagnostics,
                    .PropertyBufferDiagnostics = m_VisualizationPropertyBufferDiagnostics,
                    .OverlaySummary = m_VisualizationOverlaySummary,
                    .HasVisualizationPackets = m_VisualizationDiagnostics.InputPacketCount > 0u,
                },
                .PostProcess = PostProcessSnapshot{
                    .Enabled = input.DebugOverlayEnabled,
                },
                .InvalidSnapshotRecordCount = m_InvalidSnapshotRecordCount,
            };
        }

        void PrepareFrame(RenderWorld& renderWorld) override
        {
            m_LastRenderPrepResult = {};
            if (!m_HasExtractedRenderWorld)
            {
                Core::Log::Warn("[Graphics] PrepareFrame called before ExtractRenderWorld");
                m_LastRenderPrepResult.Diagnostic = "PrepareFrame requires ExtractRenderWorld.";
                m_LastRenderGraphStats.LifecycleDiagnostic = m_LastRenderPrepResult.Diagnostic;
                m_HasPreparedFrame = false;
                return;
            }
            RuntimeSnapshotStorage* const activeSnapshot = &ActiveRuntimeSnapshotStorage();
            if (m_Subsystems.GpuWorldSystem())
            {
                m_Subsystems.GpuWorldSystem()->SetCamera(
                    BuildCameraUbo(renderWorld, m_CurrentFrame.FrameIndex));
            }

            RenderPrepPipelineInputs inputs{
                .PipelineManager = m_Subsystems.PipelineManager() ? &*m_Subsystems.PipelineManager() : nullptr,
                .Materials = m_Subsystems.MaterialSystemRegistry() ? &*m_Subsystems.MaterialSystemRegistry() : nullptr,
                .Colormaps = m_Subsystems.ColormapSystemRegistry() ? &*m_Subsystems.ColormapSystemRegistry() : nullptr,
                .VisualizationSync = m_Subsystems.VisualizationSyncSystemRegistry() ? &*m_Subsystems.VisualizationSyncSystemRegistry() : nullptr,
                .TransformSync = m_Subsystems.TransformSyncSystemRegistry() ? &*m_Subsystems.TransformSyncSystemRegistry() : nullptr,
                .Lights = m_Subsystems.LightSystemRegistry() ? &*m_Subsystems.LightSystemRegistry() : nullptr,
                .World = m_Subsystems.GpuWorldSystem() ? &*m_Subsystems.GpuWorldSystem() : nullptr,
                .Culling = m_Subsystems.CullingSystemRegistry() ? &*m_Subsystems.CullingSystemRegistry() : nullptr,
                .VisualizationSyncRecords = std::span<VisualizationSyncRecord>{activeSnapshot->VisualizationSyncRecords},
                .VisualizationPropertyBufferAddresses =
                    std::span<const VisualizationPropertyBufferAddress>{
                        activeSnapshot->VisualizationPropertyBufferAddresses},
                .VisualizationScalarPackets =
                    std::span<const ScalarAttributePacket>{
                        activeSnapshot->VisualizationScalars},
                .TransformSyncRecords = std::span<const TransformSyncRecord>{activeSnapshot->TransformSyncRecords},
                .LightSnapshots = std::span<const LightSnapshot>{activeSnapshot->LightSnapshots},
                .EnsureClusterLightResources = [this, &renderWorld]
                {
                    return EnsureClusterLightResources(renderWorld);
                },
            };

            m_LastRenderPrepResult = m_RenderPrepPipeline.Run(inputs);
            if (!m_LastRenderPrepResult.Succeeded)
            {
                Core::Log::Error("[Graphics] {}", m_LastRenderPrepResult.Diagnostic);
                m_LastRenderGraphStats.LifecycleDiagnostic = m_LastRenderPrepResult.Diagnostic;
                m_HasPreparedFrame = false;
                return;
            }

            m_HasPreparedFrame = true;
        }

        void ExecuteFrame(const RHI::FrameHandle& frame,
                          const RenderWorld& renderWorld) override
        {
            m_LastRenderGraphStats = {};
            ResetCommandRecordStats();
            if (!renderWorld.EnableGpuProfiling)
            {
                const RHI::IProfiler* profiler =
                    m_Device != nullptr
                        ? m_Device->GetProfiler()
                        : nullptr;
                const RHI::ProfilerStatusSnapshot profilerStatus =
                    profiler != nullptr
                        ? profiler->GetStatus()
                        : RHI::ProfilerStatusSnapshot{};
                const bool deviceLost =
                    profiler != nullptr &&
                    profilerStatus.Status ==
                        RHI::ProfilerBackendStatus::DeviceLost;
                PublishStaleGpuProfileStatus(
                    deviceLost
                        ? RenderGraphGpuProfileStatus::DeviceLost
                        : RenderGraphGpuProfileStatus::Disabled,
                    deviceLost
                        ? profilerStatus.Diagnostic
                        : "GPU profiling is disabled for this render "
                          "snapshot.");
            }
            else if (m_CurrentGpuProfile.Status ==
                     RenderGraphGpuProfileStatus::Disabled)
            {
                UpdateCurrentGpuProfileStatus(
                    RenderGraphGpuProfileStatus::NotReady,
                    "GPU profiling is requested; compiled-pass planning "
                    "has not started.");
            }
            m_LastRenderGraphStats.GpuProfile =
                m_CurrentGpuProfile;
            m_LastRenderGraphStats.VisualizationPropertyBuffers =
                renderWorld.Visualization.PropertyBufferDiagnostics;
            if (!m_HasPreparedFrame)
            {
                m_LastRenderGraphStats.Diagnostic = m_LastRenderPrepResult.Diagnostic.empty()
                    ? std::string{"ExecuteFrame requires successful PrepareFrame."}
                    : std::string{"ExecuteFrame requires successful PrepareFrame: "} + m_LastRenderPrepResult.Diagnostic;
                Core::Log::Warn("[Graphics] ExecuteFrame called before successful PrepareFrame");
                return;
            }
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph execute requires a live device.";
                Core::Log::Error("[Graphics] RenderGraph Execute() failed: device missing");
                return;
            }
            PopulateCurrentRendererContractIntegrationStats(
                m_LastRenderGraphStats,
                renderWorld,
                frame.FrameIndex,
                m_DefaultRecipeReadbackBuffer.IsValid());
            if (!m_LastRenderGraphStats.Contract.ContractCompatible ||
                !m_LastRenderGraphStats.Contract.SharedProductsCompatible ||
                !m_LastRenderGraphStats.Contract.ArtifactMetadataValid)
            {
                m_LastRenderGraphStats.Diagnostic =
                    "RenderGraph contract compatibility failed.";
                Core::Log::Error("[Graphics] RenderGraph contract compatibility failed");
                return;
            }
            const auto& surfaceOpaque = m_Subsystems.CullingSystemRegistry()->GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
            const auto& lines = m_Subsystems.CullingSystemRegistry()->GetBucket(RHI::GpuDrawBucketKind::Lines);
            const auto& lineQuads = m_Subsystems.CullingSystemRegistry()->GetBucket(RHI::GpuDrawBucketKind::LineQuads);
            const auto& points = m_Subsystems.CullingSystemRegistry()->GetBucket(RHI::GpuDrawBucketKind::Points);
            const FrameRecipeSizing sizing{
                .Width = renderWorld.Viewport.Width > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Width) : 1u,
                .Height = renderWorld.Viewport.Height > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Height) : 1u,
                .BackbufferFormat = m_BackbufferFormat,
                .DepthFormat = RHI::Format::D32_FLOAT,
            };

            FrameRecipeAAMode selectedAAMode = SelectedFrameRecipeAAMode();
            FrameRecipeTemporalOptions temporalOptions{};
            if (IsReconstructionAAMode(selectedAAMode))
            {
                const TemporalJitterSample jitter =
                    ComputeTemporalJitterSample(frame.FrameIndex, renderWorld.Viewport);
                m_LastRenderGraphStats.JitterOffsetX = jitter.NdcOffset.x;
                m_LastRenderGraphStats.JitterOffsetY = jitter.NdcOffset.y;
            }

            RHI::TextureHandle hzbCurrent{};
            if (m_HZBSystem.has_value() && m_Device->IsOperational())
            {
                const std::uint64_t frameNumber = m_Device->GetGlobalFrameNumber();
                const bool hzbAllocated =
                    m_HZBSystem->EnsureAllocated(sizing.Width, sizing.Height, frameNumber);
                m_HZBSystem->Tick(frameNumber, std::max(1u, m_Device->GetFramesInFlight()));
                if (hzbAllocated && m_HZBSystem->IsAllocated())
                {
                    hzbCurrent = m_HZBSystem->CurrentHZB();
                }
            }
            RHI::TextureHandle reconstructionHistoryPrevious{};
            RHI::TextureHandle reconstructionHistoryCurrent{};
            if (IsReconstructionAAMode(selectedAAMode) &&
                m_ReconstructionHistorySystem.has_value() &&
                m_Device->IsOperational())
            {
                const std::uint64_t frameNumber = m_Device->GetGlobalFrameNumber();
                const bool historyAllocated =
                    m_ReconstructionHistorySystem->EnsureAllocated(sizing.Width,
                                                                   sizing.Height,
                                                                   frameNumber);
                m_ReconstructionHistorySystem->Tick(frameNumber, std::max(1u, m_Device->GetFramesInFlight()));
                if (historyAllocated && m_ReconstructionHistorySystem->IsAllocated())
                {
                    reconstructionHistoryPrevious = m_ReconstructionHistorySystem->PreviousHistory();
                    reconstructionHistoryCurrent = m_ReconstructionHistorySystem->CurrentHistory();
                    temporalOptions.EnableMotionVectors = true;
                }
            }
            if (IsReconstructionAAMode(selectedAAMode) &&
                (!reconstructionHistoryPrevious.IsValid() || !reconstructionHistoryCurrent.IsValid()))
            {
                selectedAAMode = FrameRecipeAAMode::NoAA;
                temporalOptions = {};
            }
            UvView* uvView = m_Subsystems.UvViewSystem()
                ? &*m_Subsystems.UvViewSystem()
                : nullptr;
            if (uvView != nullptr)
            {
                uvView->Prepare(*m_Subsystems.GpuWorldSystem());
            }
            const FrameRecipeImports imports{
                .Backbuffer = m_Device->GetBackbufferHandle(frame),
                .SceneTable = m_Subsystems.GpuWorldSystem()->GetSceneTableBuffer(),
                .InstanceStatic = m_Subsystems.GpuWorldSystem()->GetInstanceStaticBuffer(),
                .InstanceDynamic = m_Subsystems.GpuWorldSystem()->GetInstanceDynamicBuffer(),
                .EntityConfig = m_Subsystems.GpuWorldSystem()->GetEntityConfigBuffer(),
                .GeometryRecords = m_Subsystems.GpuWorldSystem()->GetGeometryRecordBuffer(),
                .Bounds = m_Subsystems.GpuWorldSystem()->GetBoundsBuffer(),
                .Lights = m_Subsystems.GpuWorldSystem()->GetLightBuffer(),
                .MaterialBuffer = m_Subsystems.MaterialSystemRegistry()->GetBuffer(),
                .SurfaceOpaqueIndexedArgs = surfaceOpaque.IndexedArgsBuffer,
                .SurfaceOpaqueCount = surfaceOpaque.CountBuffer,
                .LinesIndexedArgs = lines.IndexedArgsBuffer,
                .LinesCount = lines.CountBuffer,
                .LineQuadsNonIndexedArgs = lineQuads.NonIndexedArgsBuffer,
                .LineQuadsCount = lineQuads.CountBuffer,
                .PointsNonIndexedArgs = points.NonIndexedArgsBuffer,
                .PointsCount = points.CountBuffer,
                // GRAPHICS-073 Slice B — when `ShadowSystem` has lazily
                // allocated its atlas (after `SetParams` enabled shadows),
                // hand the handle to the recipe so the imported atlas
                // replaces the Slice A transient `graph.CreateTexture(...)`
                // path. Stays invalid until the runtime publishes shadows
                // enabled, which keeps default-CPU/null fixtures on the
                // transient fallback.
                .ShadowAtlas = m_Subsystems.ShadowSystemRegistry() ? m_Subsystems.ShadowSystemRegistry()->GetAtlasTexture() : RHI::TextureHandle{},
                // GRAPHICS-074 Slice D.2 — hand the renderer-owned host-
                // visible `Picking.Readback` lease to the recipe so it is
                // imported (with `TransferDst → HostReadback`) rather than
                // allocated as a transient. The handle is invalid until
                // Slice D.1's operational publisher has run, which lines up
                // with `pickingActive` requiring an operational device.
                .PickingReadback = (m_PickingReadbackBuffer.has_value() && m_PickingReadbackBuffer->IsValid())
                                       ? m_PickingReadbackBuffer->GetHandle()
                                       : RHI::BufferHandle{},
                // GRAPHICS-075 Slice E.2 — hand the renderer-owned host-
                // visible `Histogram.Readback` lease to the recipe so the
                // executor can record `CopyBuffer(PostProcess.Histogram →
                // Histogram.Readback @ slot * 1024)` after the histogram
                // dispatch. The handle is invalid until the operational
                // publisher has run; `BuildDefaultFrameRecipe` falls back to
                // skipping the import + readback write when the handle is
                // not valid yet.
                .HistogramReadback = (m_HistogramReadbackBuffer.has_value() && m_HistogramReadbackBuffer->IsValid())
                                         ? m_HistogramReadbackBuffer->GetHandle()
                                         : RHI::BufferHandle{},
                // GRAPHICS-038B — current-frame HZB build target. Invalid on
                // non-operational devices or allocation failure, which keeps
                // the recipe from declaring `HZBBuildPass`.
                .HZBCurrent = hzbCurrent,
                // GRAPHICS-039C — renderer-owned clustered-light buffers.
                // Published to shaders through `GpuSceneTable` BDAs during
                // `PrepareFrame()` and imported here so the recipe can order
                // build/assignment before forward/deferred lighting consumes
                // the header/index pair.
                .ClusterGridAABBs =
                    (m_ClusterGridAABBBuffer.has_value() && m_ClusterGridAABBBuffer->IsValid())
                        ? m_ClusterGridAABBBuffer->GetHandle()
                        : RHI::BufferHandle{},
                .ClusterLightHeaders =
                    (m_ClusterLightHeaderBuffer.has_value() && m_ClusterLightHeaderBuffer->IsValid())
                        ? m_ClusterLightHeaderBuffer->GetHandle()
                        : RHI::BufferHandle{},
                .ClusterLightIndices =
                    (m_ClusterLightIndexBuffer.has_value() && m_ClusterLightIndexBuffer->IsValid())
                        ? m_ClusterLightIndexBuffer->GetHandle()
                        : RHI::BufferHandle{},
                .ClusterLightCounter =
                    (m_ClusterLightCounterBuffer.has_value() && m_ClusterLightCounterBuffer->IsValid())
                        ? m_ClusterLightCounterBuffer->GetHandle()
                        : RHI::BufferHandle{},
                .UvViewColor = uvView != nullptr && uvView->ShouldRecord()
                    ? uvView->GetTarget()
                    : RHI::TextureHandle{},
                .UvViewColorInitialized =
                    uvView != nullptr && uvView->TargetHasShaderReadContents(),
            };
            const FrameRecipeAAOptions aaOptions{
                .Mode = selectedAAMode,
                .ReconstructionHistoryPrevious = reconstructionHistoryPrevious,
                .ReconstructionHistoryCurrent = reconstructionHistoryCurrent,
            };
            // GRAPHICS-073 Slice B — derive the typed shadow sizing from the
            // current `ShadowSystem` params so transient fallbacks (no atlas
            // imported) still size the recipe-owned atlas per
            // `ShadowParams::AtlasResolution * CascadeCount`. When the atlas
            // is imported, `BuildDefaultFrameRecipe` ignores this sizing and
            // honors the imported handle's dimensions.
            FrameRecipeShadowSizing shadowSizing{};
            if (m_Subsystems.ShadowSystemRegistry())
            {
                const ShadowParams shadowParams = m_Subsystems.ShadowSystemRegistry()->GetParams();
                shadowSizing.AtlasResolution = shadowParams.AtlasResolution;
                shadowSizing.CascadeCount = shadowParams.CascadeCount;
            }
            // GRAPHICS-070 — derive default-recipe features once per frame so
            // the executor lambda below can route `"SurfacePass"` through the
            // forward or deferred surface body without re-deriving features
            // for every pass dispatch.
            // GRAPHICS-072 Slice A — apply the renderer-stored lighting-path
            // override after the per-world derivation so contract tests can
            // drive the deferred surface/composition branches without
            // re-deriving features at the call site. `DeriveDefaultFrameRecipeFeatures`
            // returns `Forward` by default; the override flips it to
            // `Deferred` / `Hybrid` when set via `SetLightingPath(...)`.
            FrameRecipeFeatures defaultRecipeFeatures = DeriveDefaultFrameRecipeFeatures(renderWorld);
            defaultRecipeFeatures.LightingPath = m_LightingPath;
            // GRAPHICS-075 Slice D.2a — flip `presentSource` to
            // `PostProcess.AATemp.Resolved` only when the postprocess
            // system reports a non-`None` AA selector *and* the selected
            // mode's pipeline(s) are actually available. Plumbing only on
            // the selector (`AntiAliasing != None`) would route present
            // to `AATemp.Resolved` even when the matching pipeline
            // failed to build — both AA pass bodies would short-circuit
            // (FXAA needs its pipeline; SMAA needs its three) and
            // present would consume the cleared / undefined resolved
            // attachment instead of the freshly-written `SceneColorLDR`.
            // For SMAA we require all three pipelines because the
            // resolve shader reads `AATemp.Weights`: if blend (or edge)
            // is missing the resolve still draws but reads cleared
            // inputs, so we treat AA as unavailable for present-routing
            // purposes and fall back to `SceneColorLDR`. The resolve
            // helper itself mirrors this gate so its
            // `RenderCommandPassStatus` faithfully reports
            // `SkippedUnavailable` when the mode's pipeline is missing.
            // `DeriveDefaultFrameRecipeFeatures` itself does not see
            // `PostProcessSettings` (it is renderer-internal state, not
            // a `RenderWorld` field), so plumb the flag here.
            defaultRecipeFeatures.EnableAntiAliasing =
                m_Subsystems.PostProcessSystemRegistry().has_value() &&
                SelectedAntiAliasingPipelinesAvailable();
            defaultRecipeFeatures.EnableHZBBuild =
                defaultRecipeFeatures.EnableDepthPrepass && hzbCurrent.IsValid();
            const bool clusterResourcesReady = ClusterLightResourcesReady();
            defaultRecipeFeatures.EnableClusterGridBuild =
                defaultRecipeFeatures.EnableDepthPrepass && clusterResourcesReady;
            defaultRecipeFeatures.EnableClusterLightAssignment =
                defaultRecipeFeatures.EnableClusterGridBuild && clusterResourcesReady;
            defaultRecipeFeatures.EnableUvView =
                uvView != nullptr && uvView->ShouldRecord();
            if (m_ActiveFrameRecipeOverride.has_value())
            {
                m_LastRenderGraphStats.FrameRecipeOverrideActive = true;
                const FrameRecipeOverrideProjection projection =
                    ProjectFrameRecipeOverride(defaultRecipeFeatures, *m_ActiveFrameRecipeOverride);
                m_LastRenderGraphStats.FrameRecipeOverrideApplied = projection.Applied;
                m_LastRenderGraphStats.FrameRecipeOverrideDisabledSlotCount =
                    projection.DisabledSlotCount;
                m_LastRenderGraphStats.FrameRecipeOverrideDiagnosticCount =
                    static_cast<std::uint32_t>(projection.Diagnostics.size());
                m_LastRenderGraphStats.FrameRecipeOverrideDiagnostics = projection.Diagnostics;
                if (projection.Diagnostics.empty())
                {
                    defaultRecipeFeatures = projection.Features;
                }
            }
            FrameRecipePassContributionRegistry frameRecipeContributions{};
            RegisterDefaultFrameRecipeOverlayContributions(frameRecipeContributions,
                                                           defaultRecipeFeatures,
                                                           aaOptions,
                                                           temporalOptions);
            if (m_DebugViewSystem.has_value())
            {
                const bool debugViewEnabled =
                    renderWorld.DebugOverlayEnabled ||
                    renderWorld.DebugPrimitives.HasTransientDebug;
                DebugViewSettings settings = m_DebugViewSystem->GetSettings();
                settings.Enabled = debugViewEnabled;
                m_DebugViewSystem->SetSettings(settings);

                const FrameRecipeContributionDescriptionResult debugViewRecipeDescription =
                    DescribeDefaultFrameRecipeWithContributions(defaultRecipeFeatures,
                                                                aaOptions,
                                                                temporalOptions,
                                                                frameRecipeContributions.Passes);
                if (!debugViewRecipeDescription.Succeeded)
                {
                    m_RenderGraphCompileCache.reset();
                    m_LastRenderGraphStats.Diagnostic =
                        "FrameRecipe contribution description failed.";
                    Core::Log::Error("[Graphics] FrameRecipe contribution description failed.");
                    m_Device->NoteRecipeGraphValidation(false);
                    return;
                }

                const DebugViewResolvedSelection resolved =
                    m_DebugViewSystem->ResolveSelection(debugViewRecipeDescription.Recipe);
                if (debugViewEnabled && resolved.UsedFallback)
                {
                    ++m_LastRenderGraphStats.DebugViewFallbackInvocationCount;
                }
                ApplyResolvedDebugViewRead(frameRecipeContributions.Passes, resolved);
            }
            RenderGraphCompileCacheKey compileCacheKey =
                BuildRenderGraphCompileCacheKey(defaultRecipeFeatures,
                                                imports,
                                                sizing,
                                                aaOptions,
                                                shadowSizing,
                                                temporalOptions,
                                                frameRecipeContributions.Passes);
            CompiledRenderGraph* compiled = nullptr;
            const FrameRecipeIntrospection* recipeIntrospection = nullptr;
            const bool cacheHit =
                m_RenderGraphCompileCache.has_value() &&
                RenderGraphCompileCacheKeyEqual(m_RenderGraphCompileCache->Key, compileCacheKey);
            if (cacheHit)
            {
                m_LastRenderGraphStats.Compile.CacheHitCount = 1u;
                m_LastRenderGraphStats.Compile.ReusedCachedGraph = true;
                compiled = &m_RenderGraphCompileCache->Compiled;
                recipeIntrospection = &m_RenderGraphCompileCache->Recipe;
            }
            else
            {
                m_LastRenderGraphStats.Compile.AttemptCount = 1u;
                m_LastRenderGraphStats.Compile.CacheMissCount = 1u;
                m_RenderGraph.Reset();
                const FrameRecipeBuildResult recipe =
                    BuildDefaultFrameRecipeWithContributions(m_RenderGraph,
                                                             defaultRecipeFeatures,
                                                             imports,
                                                             sizing,
                                                             aaOptions,
                                                             shadowSizing,
                                                             temporalOptions,
                                                             frameRecipeContributions.Passes);
                if (!recipe.Succeeded)
                {
                    m_RenderGraphCompileCache.reset();
                    m_LastRenderGraphStats.Diagnostic = recipe.Diagnostic;
                    Core::Log::Error("[Graphics] FrameRecipe build failed: diagnostic={}", recipe.Diagnostic);
                    // GRAPHICS-033E: a failed recipe build cannot satisfy gate 7;
                    // publish fail-closed so the operational gate sees the latest
                    // outcome before the next attempt.
                    m_Device->NoteRecipeGraphValidation(false);
                    return;
                }

                const auto compileBegin = std::chrono::steady_clock::now();
                auto compiledResult = m_RenderGraph.Compile();
                const auto compileEnd = std::chrono::steady_clock::now();
                m_LastRenderGraphStats.Compile.TimeMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(compileEnd - compileBegin).count());
                if (!compiledResult.has_value())
                {
                    m_RenderGraphCompileCache.reset();
                    const auto& findings = m_RenderGraph.GetLastCompileValidationResult().Findings;
                    m_LastRenderGraphStats.Diagnostic = findings.empty() ? std::string{} : findings.front().Message;
                    Core::Log::Error("[Graphics] RenderGraph Compile() failed: error={} diagnostic={}",
                                     static_cast<int>(compiledResult.error()),
                                     m_LastRenderGraphStats.Diagnostic);
                    // GRAPHICS-033E: a failed compile cannot satisfy gate 7. Publish
                    // fail-closed exactly once per compile attempt so the operational
                    // gate cannot oscillate stale-clean while the next attempt rebuilds.
                    m_Device->NoteRecipeGraphValidation(false);
                    return;
                }

                FrameRecipeContributionDescriptionResult recipeDescription =
                    DescribeDefaultFrameRecipeWithContributions(defaultRecipeFeatures,
                                                                aaOptions,
                                                                temporalOptions,
                                                                frameRecipeContributions.Passes);
                if (!recipeDescription.Succeeded)
                {
                    m_RenderGraphCompileCache.reset();
                    m_LastRenderGraphStats.Diagnostic = "FrameRecipe contribution description failed.";
                    Core::Log::Error("[Graphics] FrameRecipe contribution description failed.");
                    m_Device->NoteRecipeGraphValidation(false);
                    return;
                }

                m_RenderGraphCompileCache = RenderGraphCompileCacheEntry{
                    .Key = std::move(compileCacheKey),
                    .Compiled = std::move(*compiledResult),
                    .Recipe = std::move(recipeDescription.Recipe),
                };
                compiled = &m_RenderGraphCompileCache->Compiled;
                recipeIntrospection = &m_RenderGraphCompileCache->Recipe;
            }
            RebindCompiledGraphImports(*compiled, imports, aaOptions);
            // GRAPHICS-033E: run the recipe-aware validation against the
            // current compiled graph and publish the boolean to the device for
            // the frame. Cache hits reuse the compile product but still
            // validate the recipe view and per-frame transient resource
            // readiness before execution.
            const RenderGraphValidationResult recipeValidation =
                ValidateRecipeCompiledGraph(*recipeIntrospection, *compiled);
            m_InvalidateRenderGraphCompileCacheAfterFrame = false;
            const bool transientResourcesReady = AllocateFrameTransientResources(*compiled, frame.FrameIndex);
            const bool recipeValidationClean =
                recipeValidation.CountBySeverity(RenderGraphValidationSeverity::Error) == 0u && transientResourcesReady;
            if (!transientResourcesReady)
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph transient RHI resource allocation failed.";
                Core::Log::Error("[Graphics] RenderGraph transient RHI resource allocation failed");
            }
            m_Device->NoteRecipeGraphValidation(recipeValidationClean);

            m_LastRenderGraphStats.Compile.Succeeded = true;
            m_LastRenderGraphStats.Compile.PassCount = compiled->PassCount;
            m_LastRenderGraphStats.Compile.CulledPassCount = compiled->CulledPassCount;
            m_LastRenderGraphStats.Compile.ResourceCount = compiled->ResourceCount;
            m_LastRenderGraphStats.Compile.BarrierCount = static_cast<std::uint32_t>(compiled->BarrierPackets.size());
            m_LastRenderGraphStats.Compile.QueueHandoffEdgeCount = compiled->QueueHandoffEdgeCount;
            m_LastRenderGraphStats.Compile.CrossQueueTimelineEdgeCount = compiled->CrossQueueTimelineEdgeCount;
            m_LastRenderGraphStats.Compile.CrossQueueTimelineSignalCount =
                static_cast<std::uint32_t>(compiled->CrossQueueTimelineSignals.size());
            m_LastRenderGraphStats.Compile.CrossQueueTimelineWaitCount =
                static_cast<std::uint32_t>(compiled->CrossQueueTimelineWaits.size());
            m_LastRenderGraphStats.Compile.CrossQueueOwnershipTransferCount =
                compiled->CrossQueueOwnershipTransferCount;
            m_LastRenderGraphStats.Compile.TransientMemoryEstimateBytes = compiled->TransientMemoryEstimateBytes;
            m_LastRenderGraphStats.Compile.TransientNaiveMemoryEstimateBytes =
                compiled->TransientNaiveMemoryEstimateBytes;
            m_LastRenderGraphStats.Compile.TransientPlacedPeakMemoryEstimateBytes =
                compiled->TransientPlacedPeakMemoryEstimateBytes;
            if (m_RenderGraphDebugDumpEnabled)
            {
                m_LastRenderGraphStats.DebugDump = BuildRenderGraphDebugDump(*compiled);
                m_LastRenderGraphStats.Compile.DebugDumpGenerated = true;
            }
            m_LastRenderGraphStats.Execute.DeviceOperational = m_Device->IsOperational();

            const auto executeBegin = std::chrono::steady_clock::now();
            std::vector<std::string_view> passNameByIndex(compiled->PassDeclarations.size());
            for (std::size_t passIndex = 0; passIndex < passNameByIndex.size() && passIndex < compiled->PassNames.size(); ++passIndex)
            {
                passNameByIndex[passIndex] = compiled->PassNames[passIndex];
            }

            const RHI::CameraUBO camera = BuildCameraUbo(renderWorld, frame.FrameIndex);
            // GRAPHICS-070 — when the active recipe is the default recipe the
            // executor consults `usesDeferred` to choose between the forward
            // surface body (this task) and the deferred GBuffer body
            // (GRAPHICS-072 future scope). Mirrors the anonymous-namespace
            // `UsesDeferredResources()` predicate inside
            // `Graphics.FrameRecipe.cpp`: any non-forward lighting path uses
            // deferred resources.
            const bool defaultRecipeUsesDeferred =
                (defaultRecipeFeatures.LightingPath != FrameRecipeLightingPath::Forward);
            const QueueSubmitPlan queueSubmitPlan =
                BuildQueueSubmitPlan(*compiled, m_Device->GetQueueCapabilityProfile());
            std::vector<std::vector<RHI::QueueTimelineWaitDesc>> rhiQueueWaits(queueSubmitPlan.Batches.size());
            std::vector<std::vector<RHI::QueueTimelineSignalDesc>> rhiQueueSignals(queueSubmitPlan.Batches.size());
            std::vector<RHI::QueueSubmitBatchDesc> rhiSubmitBatches{};
            rhiSubmitBatches.reserve(queueSubmitPlan.Batches.size());
            for (std::size_t batchIndex = 0; batchIndex < queueSubmitPlan.Batches.size(); ++batchIndex)
            {
                const QueueSubmitBatch& batch = queueSubmitPlan.Batches[batchIndex];
                rhiQueueWaits[batchIndex].reserve(batch.Waits.size());
                for (const QueueSubmitTimelineWait& wait : batch.Waits)
                {
                    rhiQueueWaits[batchIndex].push_back(RHI::QueueTimelineWaitDesc{
                        .Queue = wait.Queue,
                        .SignalQueue = wait.SignalQueue,
                        .Value = wait.Value,
                    });
                }
                rhiQueueSignals[batchIndex].reserve(batch.Signals.size());
                for (const QueueSubmitTimelineSignal& signal : batch.Signals)
                {
                    rhiQueueSignals[batchIndex].push_back(RHI::QueueTimelineSignalDesc{
                        .Queue = signal.Queue,
                        .Value = signal.Value,
                    });
                }
                rhiSubmitBatches.push_back(RHI::QueueSubmitBatchDesc{
                    .Queue = batch.Queue,
                    .Waits = rhiQueueWaits[batchIndex],
                    .Signals = rhiQueueSignals[batchIndex],
                });
            }
            bool useQueueSubmitPlan = false;
            if (queueSubmitPlan.Batches.size() > 1u)
            {
                useQueueSubmitPlan = m_Device->BeginFrameQueueSubmitPlan(frame, RHI::FrameQueueSubmitPlanDesc{
                    .Batches = rhiSubmitBatches,
                });
            }
            const bool asyncComputeSubmitPlanAccepted =
                useQueueSubmitPlan &&
                std::any_of(queueSubmitPlan.Batches.begin(),
                            queueSubmitPlan.Batches.end(),
                            [](const QueueSubmitBatch& batch) {
                                return batch.Queue == RenderQueue::AsyncCompute;
                            });

            std::vector<RHI::QueueAffinity> singleQueueByPass(
                compiled->PassDeclarations.size(),
                RHI::QueueAffinity::Graphics);
            std::vector<RHI::QueueAffinity> submitQueueByPass =
                singleQueueByPass;
            if (useQueueSubmitPlan)
            {
                for (const QueueSubmitBatch& batch :
                     queueSubmitPlan.Batches)
                {
                    for (const std::uint32_t passIndex :
                         batch.PassIndices)
                    {
                        if (passIndex <
                            submitQueueByPass.size())
                        {
                            submitQueueByPass[passIndex] =
                                batch.Queue;
                        }
                    }
                }
            }
            const auto beginGpuProfileForQueues =
                [&](const std::vector<RHI::QueueAffinity>&
                        actualQueues)
                {
                    if (renderWorld.EnableGpuProfiling)
                    {
                        (void)BeginGpuProfile(
                            frame,
                            *compiled,
                            actualQueues);
                    }
                };

            const auto recordPassBody =
                [this, &passNameByIndex, &camera, &frame, &compiled,
                 defaultRecipeUsesDeferred, &renderWorld](RHI::ICommandContext& graphicsContext,
                                                           const std::uint32_t passIndex)
                {
                    if (passIndex >= passNameByIndex.size())
                    {
                        Core::Log::Warn("[Graphics] Routed pass-name resolution failed during execute: passIndex={}",
                                        passIndex);
                        return;
                    }

                    const std::string_view passName = passNameByIndex[passIndex];
                    if (passName.empty())
                    {
                        Core::Log::Warn("[Graphics] Routed pass-name resolution failed during execute: passIndex={}",
                                        passIndex);
                        return;
                    }
                    const FramePassId passId = passIndex < compiled->PassIds.size()
                        ? compiled->PassIds[passIndex]
                        : FramePassId{};
                    const ActiveRenderPassDesc activeRenderPass = BuildActiveRenderPassDesc(*compiled, passIndex);
                    const auto bindFrameSampledTextureByResource =
                        [&](const FrameResourceId resourceId,
                            const std::uint32_t descriptorSlot) -> bool
                        {
                            std::lock_guard<std::mutex> descriptorLock(m_FrameSampledDescriptorMutex);
                            return BindFrameSampledTextureByResourceId(
                                graphicsContext,
                                *compiled,
                                resourceId,
                                descriptorSlot);
                        };
                    const auto bindFrameSampledDeclaredTexture =
                        [&](const std::uint32_t textureIndex,
                            const std::uint32_t descriptorSlot) -> bool
                        {
                            std::lock_guard<std::mutex> descriptorLock(m_FrameSampledDescriptorMutex);
                            return BindFrameSampledDeclaredTexture(
                                graphicsContext,
                                *compiled,
                                textureIndex,
                                descriptorSlot);
                        };
                    if (passIndex < compiled->PassDeclarations.size())
                    {
                        bool sampledTextureBound = false;
                        if (passId == ToFramePassId(FrameRecipePassKind::SelectionOutline))
                        {
                            sampledTextureBound = bindFrameSampledTextureByResource(
                                ToFrameResourceId(FrameRecipeResourceKind::EntityId),
                                kFrameSampledDescriptorSlotSelectionOutline);
                        }
                        if (passId == ToFramePassId(FrameRecipePassKind::DebugView) &&
                            m_DebugViewSystem.has_value())
                        {
                            const DebugViewResolvedSelection selection = m_DebugViewSystem->GetResolvedSelection();
                            if (selection.Enabled)
                            {
                                sampledTextureBound = bindFrameSampledTextureByResource(
                                    selection.SelectedResourceId,
                                    kFrameSampledDescriptorSlotDebugView);
                            }
                        }
                        const CompiledPassDeclarations& declarations = compiled->PassDeclarations[passIndex];
                        // BUG-016: the ImGui overlay pass declares a framegraph
                        // read of the present source only so the compiler keeps
                        // the prior color content as a LOAD attachment; its
                        // fragment shader samples its own retained font-atlas /
                        // per-command user textures from dedicated bindless
                        // leases (real bindless texture leases start after the
                        // frame-sampled bridge slots 0..5), never the
                        // shared frame-sampled bridge slot 0. All passes share a
                        // single bindless descriptor set, so the *last* host-side
                        // descriptor write to a slot is what every recorded draw
                        // observes at submit. Binding slot 0 here to the ImGui
                        // pass's SceneColorLDR read would therefore overwrite the
                        // postprocess bridge descriptor that the
                        // earlier-recorded-but-later-executing tonemap
                        // (`PostProcessPass`) samples, so the tonemap would read
                        // its own output target instead of SceneColorHDR and
                        // collapse SceneColorLDR (the present source) to black.
                        // ImGui owns its descriptors, so skip the generic bridge
                        // bind for it.
                        const bool bindsFrameSampledBridge =
                            !sampledTextureBound &&
                            !declarations.ReadTextures.empty() &&
                            passId != ToFramePassId(FrameRecipePassKind::ImGui);
                        if (bindsFrameSampledBridge)
                        {
                            const std::uint32_t textureIndex = declarations.ReadTextures.front();
                            const std::uint32_t descriptorSlot =
                                FrameSampledDescriptorSlotForPass(passId);
                            (void)bindFrameSampledDeclaredTexture(textureIndex, descriptorSlot);
                        }
                    }
                    if (activeRenderPass.HasAttachments)
                    {
                        graphicsContext.BeginRenderPass(RHI::RenderPassDesc{
                            .ColorTargets = activeRenderPass.ColorAttachments,
                            .Depth = activeRenderPass.DepthAttachment,
                        });
                        const Core::Extent2D extent = m_Device != nullptr
                            ? m_Device->GetBackbufferExtent()
                            : Core::Extent2D{.Width = 1, .Height = 1};
                        const std::uint32_t width = extent.Width > 0 ? static_cast<std::uint32_t>(extent.Width) : 1u;
                        const std::uint32_t height = extent.Height > 0 ? static_cast<std::uint32_t>(extent.Height) : 1u;
                        graphicsContext.SetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
                        graphicsContext.SetScissor(0, 0, width, height);
                    }

                    // GRAPHICS-074 Slice D.2 — the picking executor branch
                    // needs to end the render pass mid-branch so it can
                    // record the texture-to-buffer copies (which must run
                    // outside any render pass). The route helper at the bottom
                    // must not double-end it, so this latch tracks whether
                    // the picking callback already closed the active pass.
                    bool renderPassEnded = false;
                    RenderCommandRouteContext routeContext{
                        .Camera = &camera,
                        .Frame = &frame,
                        .World = &renderWorld,
                        .Compiled = &*compiled,
                        .ActiveRenderPass = &activeRenderPass,
                        .RenderPassEnded = &renderPassEnded,
                        .DefaultRecipeUsesDeferred = defaultRecipeUsesDeferred,
                    };
                    const RenderCommandRoute route{
                        .PassId = passId,
                        .DebugName = passName,
                    };
                    if (!m_CommandRouter.Dispatch(route, graphicsContext, &routeContext))
                    {
                        const RenderCommandPassStatus status = MissingRenderCommandRouteStatus(
                            m_Device != nullptr && m_Device->IsOperational());
                        AccumulateCommandRecordStatus(passName, passId, status);
                    }
                    EndActiveRenderPassForRoute(graphicsContext, routeContext);
                };
            const auto recordPass =
                [this, &recordPassBody](
                    RHI::ICommandContext& actualContext,
                    const std::uint32_t passIndex)
                {
                    const bool scopeBegun =
                        BeginGpuProfileScope(
                            actualContext,
                            passIndex);
                    recordPassBody(actualContext, passIndex);
                    if (scopeBegun)
                    {
                        EndGpuProfileScope(
                            actualContext,
                            passIndex);
                    }
                };

            const RHI::QueueCapabilityProfile frameGraphQueueProfile =
                m_Device != nullptr ? m_Device->GetQueueCapabilityProfile()
                                    : RHI::QueueCapabilityProfile{};
            const auto submitBarriersForContext =
                [&compiled, frameGraphQueueProfile](RHI::ICommandContext& graphicsContext, const BarrierPacket& packet)
                {
                    SubmitBarrierPacket(graphicsContext, *compiled, packet, frameGraphQueueProfile);
                };
            std::vector<RHI::ParallelCommandContextRequest> parallelContextRequests{};
            std::vector<std::uint32_t> parallelContextIndexByPass(
                compiled->PassDeclarations.size(),
                std::numeric_limits<std::uint32_t>::max());
            const auto buildParallelContextRequests = [&]()
            {
                if (!parallelContextRequests.empty())
                {
                    return;
                }

                const RHI::QueueCapabilityProfile profile =
                    m_Device != nullptr ? m_Device->GetQueueCapabilityProfile()
                                        : RHI::QueueCapabilityProfile{};
                parallelContextRequests.reserve(compiled->TopologicalOrder.size());
                for (const std::uint32_t passIndex : compiled->TopologicalOrder)
                {
                    if (passIndex >= compiled->PassQueues.size() ||
                        passIndex >= compiled->TopologicalLayerByPass.size() ||
                        passIndex >= parallelContextIndexByPass.size())
                    {
                        continue;
                    }
                    const RHI::QueueAffinityResolution queue =
                        RHI::ResolveQueueAffinity(compiled->PassQueues[passIndex], profile);
                    const std::uint32_t contextIndex =
                        static_cast<std::uint32_t>(parallelContextRequests.size());
                    parallelContextIndexByPass[passIndex] = contextIndex;
                    parallelContextRequests.push_back(RHI::ParallelCommandContextRequest{
                        .Queue = queue.Resolved,
                        .FrameIndex = frame.FrameIndex,
                        .PassIndex = passIndex,
                        .TopologicalLayer = compiled->TopologicalLayerByPass[passIndex],
                        .ContextIndex = contextIndex,
                    });
                }
            };
            const auto beginParallelCommandContextPlan = [&]() -> bool
            {
                if (!m_Device->SupportsParallelCommandContexts())
                {
                    return false;
                }

                buildParallelContextRequests();
                return !parallelContextRequests.empty() &&
                       m_Device->BeginFrameParallelCommandContexts(
                           frame,
                           RHI::ParallelCommandContextPlanDesc{.Requests = parallelContextRequests});
            };
            const auto executeParallelRecordJoin =
                [&](RenderGraphExecutor::PassObserver onSubmit,
                    RenderGraphExecutor::BarrierObserver onBarriers,
                    ParallelRecordStats& parallelRecordStats) -> Core::Result
                {
                    return m_RenderGraphExecutor.ExecuteParallelRecordJoin(
                        *compiled,
                        [&](const std::uint32_t passIndex,
                            const std::uint32_t layerIndex) -> Core::Result
                        {
                            if (passIndex >= parallelContextIndexByPass.size())
                            {
                                return Core::Err(Core::ErrorCode::OutOfRange);
                            }
                            const std::uint32_t contextIndex = parallelContextIndexByPass[passIndex];
                            if (contextIndex >= parallelContextRequests.size())
                            {
                                return Core::Err(Core::ErrorCode::InvalidState);
                            }
                            const RHI::ParallelCommandContextRequest& request =
                                parallelContextRequests[contextIndex];
                            if (request.TopologicalLayer != layerIndex)
                            {
                                return Core::Err(Core::ErrorCode::InvalidState);
                            }

                            RHI::ICommandContext& passContext =
                                m_Device->GetParallelCommandContext(request);
                            passContext.Begin();
                            recordPass(passContext, passIndex);
                            passContext.End();
                            return Core::Ok();
                        },
                        std::move(onSubmit),
                        std::move(onBarriers),
                        &parallelRecordStats,
                        ParallelRecordOptions{
                            .UseScheduler = kRenderGraphParallelRecordWorkerFanOutEnabled,
                            .MinWorkerPassCount = 1u,
                        });
                };
            const auto publishParallelRecordStats =
                [&](const ParallelRecordStats& parallelRecordStats)
                {
                    m_LastRenderGraphStats.Execute.ParallelRecordedPassCount =
                        parallelRecordStats.ScheduledPassCount;
                    m_LastRenderGraphStats.Execute.ParallelRecordUsedScheduler =
                        parallelRecordStats.UsedScheduler;
                    m_LastRenderGraphStats.Execute.ParallelRecordWorkerTaskCount =
                        parallelRecordStats.WorkerTaskCount;
                    m_LastRenderGraphStats.Execute.ParallelRecordCallerRecordCount =
                        parallelRecordStats.CallerRecordCount;
                };

            const auto recordPostGraphReadbacks =
                [&](RHI::ICommandContext& graphicsContext, const bool graphExecuted)
                {
                    const bool transientDebugRecordedThisFrame =
                        CommandRecordPassRecorded(ToFramePassId(FrameRecipePassKind::TransientDebugSurface));
                    const bool visualizationOverlayRecordedThisFrame =
                        CommandRecordPassRecorded(ToFramePassId(FrameRecipePassKind::VisualizationOverlay));

                    // GRAPHICS-076E / GRAPHICS-077E / GRAPHICS-078E —
                    // opt-in pixel-readback hooks: copy after the executor's
                    // final Present transition and restore Present layout
                    // before the command buffer closes. Pass-scoped hooks are
                    // additionally gated on their pass recording this frame so
                    // their counters cannot be confused with the canonical
                    // surface-readback path.
                    if (graphExecuted &&
                        (m_DefaultRecipeReadbackBuffer.IsValid() ||
                         (m_TransientDebugReadbackBuffer.IsValid() && transientDebugRecordedThisFrame) ||
                         (m_VisualizationOverlayReadbackBuffer.IsValid() && visualizationOverlayRecordedThisFrame)) &&
                        m_Device != nullptr && m_Device->IsOperational())
                    {
                        const RHI::TextureHandle backbuffer = m_Device->GetBackbufferHandle(frame);
                        if (backbuffer.IsValid())
                        {
                            const auto copyBackbuffer = [&](const RHI::BufferHandle readbackBuffer)
                            {
                                graphicsContext.TextureBarrier(backbuffer,
                                                                RHI::TextureLayout::Present,
                                                                RHI::TextureLayout::TransferSrc);
                                graphicsContext.CopyTextureToBuffer(backbuffer,
                                                                    RHI::TextureLayout::TransferSrc,
                                                                    0u, 0u,
                                                                    readbackBuffer,
                                                                    0u,
                                                                    0u, 0u, 0u, 0u);
                                graphicsContext.TextureBarrier(backbuffer,
                                                                RHI::TextureLayout::TransferSrc,
                                                                RHI::TextureLayout::Present);
                            };

                            if (m_DefaultRecipeReadbackBuffer.IsValid())
                            {
                                copyBackbuffer(m_DefaultRecipeReadbackBuffer);
                                ++m_LastRenderGraphStats.DefaultRecipeBackbufferReadbackCopyCount;
                            }
                            if (m_TransientDebugReadbackBuffer.IsValid() && transientDebugRecordedThisFrame)
                            {
                                copyBackbuffer(m_TransientDebugReadbackBuffer);
                                ++m_LastRenderGraphStats.TransientDebugBackbufferReadbackCopyCount;
                            }
                            if (m_VisualizationOverlayReadbackBuffer.IsValid() && visualizationOverlayRecordedThisFrame)
                            {
                                copyBackbuffer(m_VisualizationOverlayReadbackBuffer);
                                ++m_LastRenderGraphStats.VisualizationOverlayBackbufferReadbackCopyCount;
                            }
                        }
                    }
                };

            const auto executeSingleQueue = [&]() -> Core::Result
            {
                beginGpuProfileForQueues(singleQueueByPass);
                RHI::ICommandContext& graphicsContext = m_Device->GetGraphicsContext(frame.FrameIndex);
                graphicsContext.Begin();
                BeginGpuProfileQueue(
                    graphicsContext,
                    RHI::QueueAffinity::Graphics);
                Core::Result result = m_RenderGraphExecutor.Execute(
                    *compiled,
                    {},
                    [&recordPass, &graphicsContext](const std::uint32_t passIndex)
                    {
                        recordPass(graphicsContext, passIndex);
                    },
                    [&submitBarriersForContext, &graphicsContext](const BarrierPacket& packet)
                    {
                        submitBarriersForContext(graphicsContext, packet);
                    });
                EndGpuProfileQueue(
                    graphicsContext,
                    RHI::QueueAffinity::Graphics);
                recordPostGraphReadbacks(graphicsContext, result.has_value());
                if (result.has_value())
                    InvokeRuntimeFrameCommandHooks(graphicsContext);
                graphicsContext.End();
                return result;
            };

            const auto executeParallelSingleQueueOrFallback = [&]() -> Core::Result
            {
                m_LastRenderGraphStats.Execute.ParallelRecordingRequested = true;
                if (!beginParallelCommandContextPlan())
                {
                    m_LastRenderGraphStats.Execute.SerialFallbackUsed = true;
                    return executeSingleQueue();
                }

                m_LastRenderGraphStats.Execute.ParallelRecordingAccepted = true;
                m_LastRenderGraphStats.Execute.ParallelCommandContextCount =
                    static_cast<std::uint32_t>(parallelContextRequests.size());

                beginGpuProfileForQueues(singleQueueByPass);
                RHI::ICommandContext& primaryContext = m_Device->GetGraphicsContext(frame.FrameIndex);
                primaryContext.Begin();
                BeginGpuProfileQueue(
                    primaryContext,
                    RHI::QueueAffinity::Graphics);
                ParallelRecordStats parallelRecordStats{};
                Core::Result result = executeParallelRecordJoin(
                    [&](const std::uint32_t passIndex)
                    {
                        if (passIndex >= parallelContextIndexByPass.size())
                        {
                            return;
                        }
                        const std::uint32_t contextIndex = parallelContextIndexByPass[passIndex];
                        if (contextIndex >= parallelContextRequests.size())
                        {
                            return;
                        }
                        m_Device->SubmitParallelCommandContext(
                            parallelContextRequests[contextIndex],
                            primaryContext);
                    },
                    [&submitBarriersForContext, &primaryContext](const BarrierPacket& packet)
                    {
                        submitBarriersForContext(primaryContext, packet);
                    },
                    parallelRecordStats);
                publishParallelRecordStats(parallelRecordStats);
                EndGpuProfileQueue(
                    primaryContext,
                    RHI::QueueAffinity::Graphics);
                recordPostGraphReadbacks(primaryContext, result.has_value());
                if (result.has_value())
                {
                    InvokeRuntimeFrameCommandHooks(primaryContext);
                }
                primaryContext.End();
                m_Device->EndFrameParallelCommandContexts(frame);
                return result;
            };

            const auto emitBarriersForPass =
                [&](RHI::ICommandContext& context,
                    const std::uint32_t passIndex,
                    const BarrierPacketStage stage) -> Core::Result
                {
                    const BarrierPacketRange range =
                        FindBarrierPacketRange(compiled->BarrierPackets, passIndex, stage);
                    for (std::size_t packetIndex = range.Begin; packetIndex < range.End; ++packetIndex)
                    {
                        const BarrierPacket& packet = compiled->BarrierPackets[packetIndex];
                        submitBarriersForContext(context, packet);
                    }
                    return Core::Ok();
                };

            const auto executeSubmitPlan = [&]() -> Core::Result
            {
                if (!AreBarrierPacketsSortedByPassAndStage(compiled->BarrierPackets))
                {
                    return Core::Err(Core::ErrorCode::InvalidState);
                }
                Core::Result barrierBounds = ValidateBarrierPacketBounds(*compiled);
                if (!barrierBounds.has_value())
                {
                    return barrierBounds;
                }

                beginGpuProfileForQueues(submitQueueByPass);
                std::array<bool, 2u> profileQueueOpened{};
                const auto profileQueueIndex =
                    [](const RHI::QueueAffinity queue)
                        -> std::optional<std::uint32_t>
                    {
                        if (queue ==
                            RHI::QueueAffinity::Graphics)
                        {
                            return 0u;
                        }
                        if (queue ==
                            RHI::QueueAffinity::AsyncCompute)
                        {
                            return 1u;
                        }
                        return std::nullopt;
                    };
                const auto isLastBatchForQueue =
                    [&](const std::size_t batchIndex,
                        const RHI::QueueAffinity queue)
                    {
                        return std::none_of(
                            queueSubmitPlan.Batches.begin() +
                                static_cast<std::ptrdiff_t>(
                                    batchIndex + 1u),
                            queueSubmitPlan.Batches.end(),
                            [queue](
                                const QueueSubmitBatch& candidate)
                            {
                                return candidate.Queue == queue;
                            });
                    };
                for (std::size_t batchIndex = 0; batchIndex < queueSubmitPlan.Batches.size(); ++batchIndex)
                {
                    const QueueSubmitBatch& batch = queueSubmitPlan.Batches[batchIndex];
                    RHI::ICommandContext& context =
                        m_Device->GetQueueSubmitContext(batch.Queue,
                                                        frame.FrameIndex,
                                                        static_cast<std::uint32_t>(batchIndex));
                    context.Begin();
                    const std::optional<std::uint32_t> queueIndex =
                        profileQueueIndex(batch.Queue);
                    if (queueIndex.has_value() &&
                        !profileQueueOpened[*queueIndex])
                    {
                        BeginGpuProfileQueue(
                            context,
                            batch.Queue);
                        profileQueueOpened[*queueIndex] = true;
                    }
                    for (const std::uint32_t passIndex : batch.PassIndices)
                    {
                        if (passIndex >= compiled->PassDeclarations.size())
                        {
                            context.End();
                            return Core::Err(Core::ErrorCode::OutOfRange);
                        }
                        const CompiledPassDeclarations& declarations =
                            compiled->PassDeclarations[passIndex];
                        if (declarations.PassIndex != passIndex)
                        {
                            context.End();
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }
                        Core::Result before =
                            emitBarriersForPass(context, passIndex, BarrierPacketStage::BeforePass);
                        if (!before.has_value())
                        {
                            context.End();
                            return before;
                        }
                        recordPass(context, passIndex);
                        Core::Result after =
                            emitBarriersForPass(context, passIndex, BarrierPacketStage::AfterPass);
                        if (!after.has_value())
                        {
                            context.End();
                            return after;
                        }
                    }
                    if (queueIndex.has_value() &&
                        isLastBatchForQueue(
                            batchIndex,
                            batch.Queue))
                    {
                        EndGpuProfileQueue(
                            context,
                            batch.Queue);
                    }
                    if (batchIndex + 1u == queueSubmitPlan.Batches.size())
                    {
                        Core::Result finalBarriers = emitBarriersForPass(
                            context,
                            static_cast<std::uint32_t>(compiled->PassDeclarations.size()),
                            BarrierPacketStage::BeforePass);
                        if (!finalBarriers.has_value())
                        {
                            context.End();
                            return finalBarriers;
                        }
                        recordPostGraphReadbacks(context, true);
                        InvokeRuntimeFrameCommandHooks(context);
                    }
                    context.End();
                }
                return Core::Ok();
            };

            const auto executeParallelSubmitPlanOrFallback = [&]() -> Core::Result
            {
                m_LastRenderGraphStats.Execute.ParallelRecordingRequested = true;
                if (!beginParallelCommandContextPlan())
                {
                    m_LastRenderGraphStats.Execute.SerialFallbackUsed = true;
                    return executeSubmitPlan();
                }

                m_LastRenderGraphStats.Execute.ParallelRecordingAccepted = true;
                m_LastRenderGraphStats.Execute.ParallelCommandContextCount =
                    static_cast<std::uint32_t>(parallelContextRequests.size());

                beginGpuProfileForQueues(submitQueueByPass);
                std::vector<bool> profilePrimaryPreBegun(
                    queueSubmitPlan.Batches.size(),
                    false);
                std::array<bool, 2u> profileQueueOpened{};
                const auto profileQueueIndex =
                    [](const RHI::QueueAffinity queue)
                        -> std::optional<std::uint32_t>
                    {
                        if (queue ==
                            RHI::QueueAffinity::Graphics)
                        {
                            return 0u;
                        }
                        if (queue ==
                            RHI::QueueAffinity::AsyncCompute)
                        {
                            return 1u;
                        }
                        return std::nullopt;
                    };
                const auto isLastBatchForQueue =
                    [&](const std::size_t batchIndex,
                        const RHI::QueueAffinity queue)
                    {
                        return std::none_of(
                            queueSubmitPlan.Batches.begin() +
                                static_cast<std::ptrdiff_t>(
                                    batchIndex + 1u),
                            queueSubmitPlan.Batches.end(),
                            [queue](
                                const QueueSubmitBatch& candidate)
                            {
                                return candidate.Queue == queue;
                            });
                    };
                if (m_ActiveGpuProfile.has_value())
                {
                    // Queue-submit and parallel-context vectors are both fully
                    // materialized here. Open each queue's first primary and
                    // record its reset/envelope before any worker can record a
                    // secondary timestamp.
                    for (std::size_t batchIndex = 0u;
                         batchIndex <
                         queueSubmitPlan.Batches.size();
                         ++batchIndex)
                    {
                        const QueueSubmitBatch& batch =
                            queueSubmitPlan.Batches[batchIndex];
                        const std::optional<std::uint32_t>
                            queueIndex =
                                profileQueueIndex(batch.Queue);
                        if (!queueIndex.has_value() ||
                            profileQueueOpened[*queueIndex])
                        {
                            continue;
                        }
                        RHI::ICommandContext& primary =
                            m_Device->GetQueueSubmitContext(
                                batch.Queue,
                                frame.FrameIndex,
                                static_cast<std::uint32_t>(
                                    batchIndex));
                        primary.Begin();
                        BeginGpuProfileQueue(
                            primary,
                            batch.Queue);
                        profilePrimaryPreBegun[batchIndex] =
                            true;
                        profileQueueOpened[*queueIndex] = true;
                    }
                }
                const auto closePreBegunProfilePrimaries =
                    [&]()
                    {
                        for (std::size_t batchIndex = 0u;
                             batchIndex <
                             profilePrimaryPreBegun.size();
                             ++batchIndex)
                        {
                            if (!profilePrimaryPreBegun[
                                    batchIndex])
                            {
                                continue;
                            }
                            const QueueSubmitBatch& batch =
                                queueSubmitPlan
                                    .Batches[batchIndex];
                            m_Device->GetQueueSubmitContext(
                                batch.Queue,
                                frame.FrameIndex,
                                static_cast<std::uint32_t>(
                                    batchIndex))
                                .End();
                            profilePrimaryPreBegun[
                                batchIndex] = false;
                        }
                    };
                ParallelRecordStats parallelRecordStats{};
                Core::Result recordResult = executeParallelRecordJoin(
                    RenderGraphExecutor::PassObserver{},
                    RenderGraphExecutor::BarrierObserver{},
                    parallelRecordStats);
                publishParallelRecordStats(parallelRecordStats);
                if (!recordResult.has_value())
                {
                    closePreBegunProfilePrimaries();
                    m_Device->EndFrameParallelCommandContexts(frame);
                    return recordResult;
                }

                if (!AreBarrierPacketsSortedByPassAndStage(compiled->BarrierPackets))
                {
                    closePreBegunProfilePrimaries();
                    m_Device->EndFrameParallelCommandContexts(frame);
                    return Core::Err(Core::ErrorCode::InvalidState);
                }
                Core::Result barrierBounds = ValidateBarrierPacketBounds(*compiled);
                if (!barrierBounds.has_value())
                {
                    closePreBegunProfilePrimaries();
                    m_Device->EndFrameParallelCommandContexts(frame);
                    return barrierBounds;
                }

                for (std::size_t batchIndex = 0; batchIndex < queueSubmitPlan.Batches.size(); ++batchIndex)
                {
                    const QueueSubmitBatch& batch = queueSubmitPlan.Batches[batchIndex];
                    RHI::ICommandContext& context =
                        m_Device->GetQueueSubmitContext(batch.Queue,
                                                        frame.FrameIndex,
                                                        static_cast<std::uint32_t>(batchIndex));
                    if (!profilePrimaryPreBegun[batchIndex])
                    {
                        context.Begin();
                    }
                    for (const std::uint32_t passIndex : batch.PassIndices)
                    {
                        if (passIndex >= compiled->PassDeclarations.size() ||
                            passIndex >= parallelContextIndexByPass.size())
                        {
                            context.End();
                            profilePrimaryPreBegun[batchIndex] = false;
                            closePreBegunProfilePrimaries();
                            m_Device->EndFrameParallelCommandContexts(frame);
                            return Core::Err(Core::ErrorCode::OutOfRange);
                        }
                        const CompiledPassDeclarations& declarations =
                            compiled->PassDeclarations[passIndex];
                        if (declarations.PassIndex != passIndex)
                        {
                            context.End();
                            profilePrimaryPreBegun[batchIndex] = false;
                            closePreBegunProfilePrimaries();
                            m_Device->EndFrameParallelCommandContexts(frame);
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }
                        Core::Result before =
                            emitBarriersForPass(context, passIndex, BarrierPacketStage::BeforePass);
                        if (!before.has_value())
                        {
                            context.End();
                            profilePrimaryPreBegun[batchIndex] = false;
                            closePreBegunProfilePrimaries();
                            m_Device->EndFrameParallelCommandContexts(frame);
                            return before;
                        }

                        const std::uint32_t contextIndex = parallelContextIndexByPass[passIndex];
                        if (contextIndex >= parallelContextRequests.size())
                        {
                            context.End();
                            profilePrimaryPreBegun[batchIndex] = false;
                            closePreBegunProfilePrimaries();
                            m_Device->EndFrameParallelCommandContexts(frame);
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }
                        m_Device->SubmitParallelCommandContext(
                            parallelContextRequests[contextIndex],
                            context);

                        Core::Result after =
                            emitBarriersForPass(context, passIndex, BarrierPacketStage::AfterPass);
                        if (!after.has_value())
                        {
                            context.End();
                            profilePrimaryPreBegun[batchIndex] = false;
                            closePreBegunProfilePrimaries();
                            m_Device->EndFrameParallelCommandContexts(frame);
                            return after;
                        }
                    }
                    if (isLastBatchForQueue(
                            batchIndex,
                            batch.Queue))
                    {
                        EndGpuProfileQueue(
                            context,
                            batch.Queue);
                    }
                    if (batchIndex + 1u == queueSubmitPlan.Batches.size())
                    {
                        Core::Result finalBarriers = emitBarriersForPass(
                            context,
                            static_cast<std::uint32_t>(compiled->PassDeclarations.size()),
                            BarrierPacketStage::BeforePass);
                        if (!finalBarriers.has_value())
                        {
                            context.End();
                            profilePrimaryPreBegun[batchIndex] = false;
                            closePreBegunProfilePrimaries();
                            m_Device->EndFrameParallelCommandContexts(frame);
                            return finalBarriers;
                        }
                        recordPostGraphReadbacks(context, true);
                        InvokeRuntimeFrameCommandHooks(context);
                    }
                    context.End();
                    profilePrimaryPreBegun[batchIndex] = false;
                }

                closePreBegunProfilePrimaries();
                m_Device->EndFrameParallelCommandContexts(frame);
                return Core::Ok();
            };

            const Core::Result executeResult =
                useQueueSubmitPlan
                    ? (m_ParallelRenderGraphRecordingEnabled
                           ? executeParallelSubmitPlanOrFallback()
                           : executeSubmitPlan())
                    : (m_ParallelRenderGraphRecordingEnabled
                           ? executeParallelSingleQueueOrFallback()
                           : executeSingleQueue());
            if (uvView != nullptr)
            {
                uvView->CompleteFrame(executeResult.has_value());
            }
            const auto executeEnd = std::chrono::steady_clock::now();
            m_LastRenderGraphStats.Execute.TimeMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(executeEnd - executeBegin).count());
            PublishCommandRecordStats();
            PublishActiveGpuProfileCommandStatuses(
                executeResult.has_value());
            m_LastRenderGraphStats.GpuProfile =
                m_CurrentGpuProfile;
            const auto resetCacheAfterFallback = [&]()
            {
                if (m_InvalidateRenderGraphCompileCacheAfterFrame)
                {
                    m_RenderGraphCompileCache.reset();
                    m_InvalidateRenderGraphCompileCacheAfterFrame = false;
                }
            };
            if (!executeResult.has_value())
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph execute failed.";
                Core::Log::Error("[Graphics] RenderGraph Execute() failed: error={}",
                                 static_cast<int>(executeResult.error()));
                FinalizeCurrentRendererContractIntegrationStats(m_LastRenderGraphStats);
                resetCacheAfterFallback();
                return;
            }
            if (asyncComputeSubmitPlanAccepted)
            {
                ++m_LastRenderGraphStats.AsyncComputeUtilizedFrames;
            }
            m_LastRenderGraphStats.Execute.Succeeded = true;
            FinalizeCurrentRendererContractIntegrationStats(m_LastRenderGraphStats);
            resetCacheAfterFallback();
            if (m_HZBSystem.has_value() && m_LastRenderGraphStats.HZBBuildRecordedFrames > 0u)
            {
                m_HZBSystem->AdvanceFrame();
            }
            if (m_ReconstructionHistorySystem.has_value() &&
                m_LastRenderGraphStats.ReconstructorAppliedFrames > 0u)
            {
                m_ReconstructionHistorySystem->AdvanceFrame();
            }

            // Phase 14.2 GPU order is intentionally fixed for concrete
            // backends:
            //   1) culling counter reset
            //   2) culling dispatch
            //   3) depth prepass (optional)
            //   4) gbuffer
            //   5) shadows
            //   6) deferred lighting
            //   7) forward lines
            //   8) forward points
            //   9) selection/outline
            //  10) postprocess/present
            //
            // Null backend records no commands.
        }

        std::uint64_t EndFrame(const RHI::FrameHandle& frame) override
        {
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.LifecycleDiagnostic = "EndFrame requires a live device.";
                Core::Log::Error("[Graphics] EndFrame failed: device missing");
                return 0;
            }

            m_Device->EndFrame(frame);
            const std::uint64_t completedFrameNumber =
                m_Device->GetGlobalFrameNumber();
            FinalizeGpuProfileAfterDeviceEnd(
                completedFrameNumber);
            return completedFrameNumber;
        }

        // ── Resource managers ─────────────────────────────────────────────

        [[nodiscard]] RHI::PipelineHandle GetDefaultDebugSurfacePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_DefaultDebugSurfacePipelineLease.has_value() ||
                !m_DefaultDebugSurfacePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_DefaultDebugSurfacePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetDefaultDebugSurfacePipelineDesc() const noexcept override
        {
            return BuildDefaultDebugSurfacePipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetForwardSurfacePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_ForwardSurfacePipelineLease.has_value() ||
                !m_ForwardSurfacePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_ForwardSurfacePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetForwardSurfacePipelineDesc() const noexcept override
        {
            return BuildForwardSurfacePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetForwardLinePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_ForwardLinePipelineLease.has_value() ||
                !m_ForwardLinePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_ForwardLinePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetForwardLinePipelineDesc() const noexcept override
        {
            return BuildForwardLinePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetForwardPointPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_ForwardPointPipelineLease.has_value() ||
                !m_ForwardPointPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_ForwardPointPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetForwardPointPipelineDesc() const noexcept override
        {
            return BuildForwardPointPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetShadowPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_ShadowPipelineLease.has_value() ||
                !m_ShadowPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_ShadowPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetShadowPipelineDesc() const noexcept override
        {
            return BuildShadowPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetDeferredGBufferPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_DeferredGBufferPipelineLease.has_value() ||
                !m_DeferredGBufferPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_DeferredGBufferPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetDeferredGBufferPipelineDesc() const noexcept override
        {
            return BuildDeferredGBufferPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetDeferredLightingPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_DeferredLightingPipelineLease.has_value() ||
                !m_DeferredLightingPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_DeferredLightingPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetDeferredLightingPipelineDesc() const noexcept override
        {
            return BuildDeferredLightingPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionEntityIdPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_SelectionEntityIdPipelineLease.has_value() ||
                !m_SelectionEntityIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionEntityIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionEntityIdPipelineDesc() const noexcept override
        {
            return BuildSelectionEntityIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionEntityIdOutlinePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() ||
                !m_SelectionEntityIdOutlinePipelineLease.has_value() ||
                !m_SelectionEntityIdOutlinePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(
                m_SelectionEntityIdOutlinePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionEntityIdOutlinePipelineDesc() const noexcept override
        {
            return BuildSelectionEntityIdOutlinePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionFaceIdPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_SelectionFaceIdPipelineLease.has_value() ||
                !m_SelectionFaceIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionFaceIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionFaceIdPipelineDesc() const noexcept override
        {
            return BuildSelectionFaceIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionEdgeIdPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_SelectionEdgeIdPipelineLease.has_value() ||
                !m_SelectionEdgeIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionEdgeIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionEdgeIdPipelineDesc() const noexcept override
        {
            return BuildSelectionEdgeIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionPointIdPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_SelectionPointIdPipelineLease.has_value() ||
                !m_SelectionPointIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionPointIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionPointIdPipelineDesc() const noexcept override
        {
            return BuildSelectionPointIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionOutlinePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_SelectionOutlinePipelineLease.has_value() ||
                !m_SelectionOutlinePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionOutlinePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionOutlinePipelineDesc() const noexcept override
        {
            return BuildSelectionOutlinePipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessToneMapPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessToneMapPipelineLease.has_value() ||
                !m_PostProcessToneMapPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessToneMapPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessToneMapPipelineDesc() const noexcept override
        {
            return BuildPostProcessToneMapPipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessBloomDownsamplePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessBloomDownsamplePipelineLease.has_value() ||
                !m_PostProcessBloomDownsamplePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessBloomDownsamplePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessBloomDownsamplePipelineDesc() const noexcept override
        {
            return BuildPostProcessBloomDownsamplePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessBloomUpsamplePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessBloomUpsamplePipelineLease.has_value() ||
                !m_PostProcessBloomUpsamplePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessBloomUpsamplePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessBloomUpsamplePipelineDesc() const noexcept override
        {
            return BuildPostProcessBloomUpsamplePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessFXAAPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessFXAAPipelineLease.has_value() ||
                !m_PostProcessFXAAPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessFXAAPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessFXAAPipelineDesc() const noexcept override
        {
            return BuildPostProcessFXAAPipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessSMAAEdgePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessSMAAEdgePipelineLease.has_value() ||
                !m_PostProcessSMAAEdgePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessSMAAEdgePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessSMAAEdgePipelineDesc() const noexcept override
        {
            return BuildPostProcessSMAAEdgePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessSMAABlendPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessSMAABlendPipelineLease.has_value() ||
                !m_PostProcessSMAABlendPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessSMAABlendPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessSMAABlendPipelineDesc() const noexcept override
        {
            return BuildPostProcessSMAABlendPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessSMAAResolvePipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessSMAAResolvePipelineLease.has_value() ||
                !m_PostProcessSMAAResolvePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessSMAAResolvePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessSMAAResolvePipelineDesc() const noexcept override
        {
            return BuildPostProcessSMAAResolvePipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessHistogramPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_PostProcessHistogramPipelineLease.has_value() ||
                !m_PostProcessHistogramPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessHistogramPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessHistogramPipelineDesc() const noexcept override
        {
            return BuildPostProcessHistogramPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetHZBBuildPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_HZBBuildPipelineLease.has_value() ||
                !m_HZBBuildPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_HZBBuildPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetHZBBuildPipelineDesc() const noexcept override
        {
            return BuildHZBBuildPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetClusterGridBuildPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_ClusterGridBuildPipelineLease.has_value() ||
                !m_ClusterGridBuildPipelineLease->IsValid())
            {
                return {};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_ClusterGridBuildPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetClusterGridBuildPipelineDesc() const noexcept override
        {
            return BuildClusterGridBuildPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetClusterLightAssignmentPipeline() const noexcept override
        {
            if (!m_Subsystems.PipelineManager().has_value() || !m_ClusterLightAssignmentPipelineLease.has_value() ||
                !m_ClusterLightAssignmentPipelineLease->IsValid())
            {
                return {};
            }
            return m_Subsystems.PipelineManager()->GetDeviceHandle(m_ClusterLightAssignmentPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetClusterLightAssignmentPipelineDesc() const noexcept override
        {
            return BuildClusterLightAssignmentPipelineDesc();
        }

        [[nodiscard]] RHI::BufferHandle GetPickingReadbackBuffer() const noexcept override
        {
            if (!m_PickingReadbackBuffer.has_value() || !m_PickingReadbackBuffer->IsValid())
            {
                return RHI::BufferHandle{};
            }
            return m_PickingReadbackBuffer->GetHandle();
        }

        [[nodiscard]] std::uint64_t GetPickingReadbackBufferSize() const noexcept override
        {
            return m_PickingReadbackBufferSize;
        }

        // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
        // `Histogram.Readback` buffer accessors. Same lazy-allocation
        // pattern as the picking accessors above: an invalid handle / zero
        // size means the operational publisher has not allocated the lease
        // yet (non-operational device, or pre-`Initialize()`).
        [[nodiscard]] RHI::BufferHandle GetHistogramReadbackBuffer() const noexcept override
        {
            if (!m_HistogramReadbackBuffer.has_value() || !m_HistogramReadbackBuffer->IsValid())
            {
                return RHI::BufferHandle{};
            }
            return m_HistogramReadbackBuffer->GetHandle();
        }

        [[nodiscard]] std::uint64_t GetHistogramReadbackBufferSize() const noexcept override
        {
            return m_HistogramReadbackBufferSize;
        }

        void SetLightingPath(FrameRecipeLightingPath path) noexcept override
        {
            m_LightingPath = path;
        }

        [[nodiscard]] FrameRecipeLightingPath GetLightingPath() const noexcept override
        {
            return m_LightingPath;
        }

        void SetActiveFrameRecipeOverride(
            std::optional<FrameRecipeOverride> recipeOverride) override
        {
            m_ActiveFrameRecipeOverride = std::move(recipeOverride);
        }

        void ClearActiveFrameRecipeOverride() noexcept override
        {
            m_ActiveFrameRecipeOverride.reset();
        }

        [[nodiscard]] const std::optional<FrameRecipeOverride>&
        GetActiveFrameRecipeOverride() const noexcept override
        {
            return m_ActiveFrameRecipeOverride;
        }

        void SetDefaultRecipeBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept override
        {
            m_DefaultRecipeReadbackBuffer = handle;
        }

        [[nodiscard]] RHI::BufferHandle GetDefaultRecipeBackbufferReadbackBuffer() const noexcept override
        {
            return m_DefaultRecipeReadbackBuffer;
        }

        void SetTransientDebugBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept override
        {
            m_TransientDebugReadbackBuffer = handle;
        }

        [[nodiscard]] RHI::BufferHandle GetTransientDebugBackbufferReadbackBuffer() const noexcept override
        {
            return m_TransientDebugReadbackBuffer;
        }

        void SetVisualizationOverlayBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept override
        {
            m_VisualizationOverlayReadbackBuffer = handle;
        }

        [[nodiscard]] RHI::BufferHandle GetVisualizationOverlayBackbufferReadbackBuffer() const noexcept override
        {
            return m_VisualizationOverlayReadbackBuffer;
        }

        // GRAPHICS-076 Slice B — public seam for the renderer-owned
        // `DebugViewSystem` request. The renderer drives the
        // `Enabled` field each frame from the world (see
        // `ExecuteFrame()`'s `SetSettings + ResolveSelection` block);
        // callers only own `RequestedResourceName`. Stored on the
        // system itself so the value survives across frames without
        // a renderer-local mirror, and so the system's existing
        // `ResolveSelection` path observes the new name on the next
        // frame. Becomes a no-op if `Initialize()` has not yet run.
        void SetDebugViewRequestedResourceName(std::string name) override
        {
            if (!m_DebugViewSystem.has_value())
            {
                return;
            }
            DebugViewSettings settings = m_DebugViewSystem->GetSettings();
            settings.RequestedResourceName = std::move(name);
            m_DebugViewSystem->SetSettings(settings);
        }

        [[nodiscard]] std::string GetDebugViewRequestedResourceName() const override
        {
            if (!m_DebugViewSystem.has_value())
            {
                return std::string{};
            }
            return m_DebugViewSystem->GetSettings().RequestedResourceName;
        }

        RHI::BufferManager&   GetBufferManager()   override { return *m_Subsystems.BufferManager();   }
        RHI::TextureManager&  GetTextureManager()  override { return *m_Subsystems.TextureManager();  }
        RHI::SamplerManager&  GetSamplerManager()  override { return *m_Subsystems.SamplerManager();  }
        RHI::PipelineManager& GetPipelineManager() override { return *m_Subsystems.PipelineManager(); }
        GpuWorld&             GetGpuWorld()        override { return *m_Subsystems.GpuWorldSystem();        }
        MaterialSystem&        GetMaterialSystem()  override { return *m_Subsystems.MaterialSystemRegistry();  }
        ColormapSystem&        GetColormapSystem()  override { return *m_Subsystems.ColormapSystemRegistry();  }
        VisualizationSyncSystem& GetVisualizationSyncSystem() override { return *m_Subsystems.VisualizationSyncSystemRegistry(); }
        CullingSystem&         GetCullingSystem()   override { return *m_Subsystems.CullingSystemRegistry();   }
        TransformSyncSystem&   GetTransformSyncSystem() override { return *m_Subsystems.TransformSyncSystemRegistry(); }
        LightSystem&           GetLightSystem()     override { return *m_Subsystems.LightSystemRegistry();     }
        SelectionSystem&       GetSelectionSystem() override { return *m_Subsystems.SelectionSystemRegistry(); }
        ForwardSystem&         GetForwardSystem()   override { return *m_Subsystems.ForwardSystemRegistry();   }
        DeferredSystem&        GetDeferredSystem()  override { return *m_Subsystems.DeferredSystemRegistry();  }
        PostProcessSystem&     GetPostProcessSystem() override { return *m_Subsystems.PostProcessSystemRegistry(); }
        ShadowSystem&          GetShadowSystem()    override { return *m_Subsystems.ShadowSystemRegistry();    }
        HZBSystem&             GetHZBSystem()       override { return *m_HZBSystem;       }
        const RenderGraphFrameStats& GetLastRenderGraphStats() const override { return m_LastRenderGraphStats; }

        void SetTransientAliasingEnabled(const bool enabled) noexcept override
        {
            if (m_RenderGraph.IsTransientAliasingEnabled() == enabled)
            {
                return;
            }
            m_RenderGraph.SetTransientAliasingEnabled(enabled);
            m_RenderGraphCompileCache.reset();
        }

        [[nodiscard]] bool IsTransientAliasingEnabled() const noexcept override
        {
            return m_RenderGraph.IsTransientAliasingEnabled();
        }

        void SetRenderGraphDebugDumpEnabled(const bool enabled) noexcept override
        {
            m_RenderGraphDebugDumpEnabled = enabled;
        }

        [[nodiscard]] bool GetRenderGraphDebugDumpEnabled() const noexcept override
        {
            return m_RenderGraphDebugDumpEnabled;
        }

        void SetParallelRenderGraphRecordingEnabled(const bool enabled) noexcept override
        {
            m_ParallelRenderGraphRecordingEnabled = enabled;
        }

        [[nodiscard]] bool IsParallelRenderGraphRecordingEnabled() const noexcept override
        {
            return m_ParallelRenderGraphRecordingEnabled;
        }

    private:
        struct RuntimeFrameCommandHookEntry
        {
            RuntimeFrameCommandHookHandle Handle{};
            RuntimeFrameCommandHook Hook{};
        };

        void InvokeRuntimeFrameCommandHooks(RHI::ICommandContext& context)
        {
            for (const RuntimeFrameCommandHookEntry& entry :
                 m_RuntimeFrameCommandHooks)
            {
                if (entry.Hook)
                    entry.Hook(context);
            }
        }

        // GRAPHICS-031A — canonical default-debug-surface PipelineDesc.
        //
        // VertexShaderPath / FragmentShaderPath point at the compiled SPIR-V
        // artifacts produced by `intrinsic_add_glsl_shaders()` under the
        // runtime shader output directory (`<bin>/shaders/<relative>.spv`).
        // The Vulkan backend's `ReadSpirvFile()` opens these paths verbatim,
        // so the renderer pre-resolves them via `Core::Filesystem::GetShaderPath`
        // (the same resolver used by the legacy `RenderOrchestrator`). When
        // the SPV files are absent (e.g. CI builds without
        // `INTRINSIC_BUILD_SANDBOX=ON`), `GetShaderPath` returns the raw
        // relative path so the resolved value remains deterministic. Initial
        // `Initialize()` and `RebuildOperationalResources()` therefore
        // republish a byte-identical descriptor against a stable filesystem
        // state.
        [[nodiscard]] static RHI::PipelineDesc BuildDefaultDebugSurfacePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = true;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Less;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.DefaultDebugSurface";
            return desc;
        }

        // GRAPHICS-070 — default-recipe forward surface pipeline descriptor.
        // Mirrors the depth-prepass-on contract from
        // `docs/architecture/rendering-three-pass.md`: surface samples
        // SceneDepth (`Equal` compare) and writes SceneColorHDR without
        // touching depth. Held byte-identical between the initial
        // `Initialize()` and any subsequent `RebuildOperationalResources()`
        // so the pipeline registry/dedupe can return a stable device handle.
        //
        // Shader pairing: `ForwardSurfacePass::Execute()` pushes
        // `RHI::GpuScenePushConstants` (SceneTableBDA / FrameIndex /
        // DrawBucket) and the pipeline layout's `PushConstantSize` matches
        // `sizeof(GpuScenePushConstants)`. The shaders must therefore observe
        // the GpuScene-aware push-constant block and the BDA-only descriptor
        // contract — pairing with the canonical GpuScene shader pair
        // (`forward/default_debug_surface.{vert,frag}`) satisfies both
        // contracts. The legacy `surface.vert/frag` pair predates the
        // GpuScene seam — it declares `mat4 Model` + `PtrPositions`-style
        // push constants plus `set = 0/2/3` descriptor sets — and would
        // either fail Vulkan pipeline-layout validation or read unrelated
        // push-constant bytes. A dedicated lit forward-surface shader is a
        // GRAPHICS-072 follow-up.
        [[nodiscard]] static RHI::PipelineDesc BuildForwardSurfacePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.ForwardSurface";
            return desc;
        }

        // GRAPHICS-071 — retained line renderables use the default recipe's
        // `LinePass` after the surface pass. Lines load `SceneDepth` and append
        // into `SceneColorHDR`; depth writes stay disabled so surface depth is
        // preserved for later point/selection/postprocess consumers.
        [[nodiscard]] static RHI::PipelineDesc BuildForwardLinePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/line.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/line.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::LessEqual;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.ForwardLine";
            return desc;
        }

        // GRAPHICS-071 / BUG-042 — retained point renderables use the
        // GpuScene BDA-backed `forward/point.vert` + `forward/point.frag`
        // shader pair. The cull shader emits six vertices per point so sphere
        // impostors can restore legacy billboard expansion and corrected depth.
        [[nodiscard]] static RHI::PipelineDesc BuildForwardPointPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/point.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/point.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = true;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::LessEqual;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.ForwardPoint";
            return desc;
        }

        // GRAPHICS-073 Slice A — default-recipe depth-only shadow pipeline.
        // Reuses `shaders/depth_prepass.vert.spv` so the existing GpuScene
        // push-constant block (`SceneTableBDA` / `FrameIndex` / `DrawBucket`)
        // matches `ShadowPass::Execute`. Depth-only: no fragment shader, no
        // color targets, `DepthWriteEnable = true`, `DepthFunc = LessOrEqual`,
        // single `D32_FLOAT` depth target matching the recipe's transient
        // `ShadowAtlas` declaration. A dedicated shadow-depth shader and the
        // `ShadowSystem`-owned atlas/sampler arrive with Slice B per the
        // `GRAPHICS-009Q` decision; the legacy `shaders/shadow_depth.vert`
        // pair pre-dates the GpuScene seam and is deliberately *not*
        // referenced here.
        [[nodiscard]] static RHI::PipelineDesc BuildShadowPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/depth_prepass.vert.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = true;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::LessEqual;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.Shadow";
            return desc;
        }

        // GRAPHICS-072 Slice A — default-recipe deferred GBuffer pipeline.
        // Pairs the GpuScene-aware
        // `forward/default_debug_surface.vert.spv` (shared with the forward
        // default-debug-surface pipeline) with a minimal three-RT GBuffer
        // fragment under `deferred/default_debug_gbuffer.frag.spv`. Both
        // declare a `layout(push_constant) ScenePC` block that matches
        // `RHI::GpuScenePushConstants` byte-for-byte, which is what
        // `DeferredGBufferPass::Execute` pushes via `cmd.PushConstants(...)`.
        // The legacy `assets/shaders/surface.vert` + `surface_gbuffer.frag`
        // pair declares the pre-GpuScene `mat4 Model + Ptr*` push block and
        // is deliberately *not* referenced here — feeding `GpuScenePushConstants`
        // bytes into that layout would silently misinterpret
        // `SceneTableBDA` as `mat4 Model` and corrupt every BDA dereference.
        // See `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the parallel forward / line / point /
        // shadow precedents and the explicit policy that prevents this
        // footgun from recurring. Three color targets match the frame
        // recipe's deferred attachment formats: `SceneNormal` (RGBA16F),
        // `Albedo` (RGBA8), `Material0` (RGBA16F). Depth uses the recipe's
        // `SceneDepth` D32_FLOAT. Depth-test on with `DepthOp::Equal`
        // mirrors the forward-surface pipeline because the depth prepass
        // already populated `SceneDepth`.
        [[nodiscard]] static RHI::PipelineDesc BuildDeferredGBufferPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/deferred/default_debug_gbuffer.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorBlend[2].Enable = false;
            desc.ColorTargetCount = 3u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT; // SceneNormal
            desc.ColorTargetFormats[1] = RHI::Format::RGBA8_UNORM;  // Albedo
            desc.ColorTargetFormats[2] = RHI::Format::RGBA16_FLOAT; // Material0
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.DeferredGBuffer";
            return desc;
        }

        // GRAPHICS-072 Slice B — default-recipe deferred lighting pipeline.
        // Pairs the fullscreen `post_fullscreen.vert.spv` (no vertex inputs,
        // no push constants — just emits a fullscreen triangle and a UV
        // varying) with the GpuScene-aware `deferred/lighting.frag.spv` whose
        // `layout(push_constant, scalar) PushConstants { uint64_t
        // SceneTableBDA; uint _pad0; uint _pad1; }` block matches
        // `DeferredLightingPushConstants` byte-for-byte — what
        // `DeferredLightingPass::Execute` pushes via
        // `cmd.PushConstants(&pc, sizeof(pc))`. The legacy
        // `assets/shaders/deferred_lighting.frag` declares a far larger
        // `Push { mat4 InvViewProj; vec4 ClearColor; ... }` block plus
        // multiple descriptor sets (4 G-buffer samplers + CameraUBO +
        // sampler2DShadow) and would silently truncate / misinterpret the
        // pushed bytes, so it is deliberately *not* referenced here — see
        // `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the policy. Single RGBA16F color
        // target (`SceneColorHDR`); no depth test/write (composition reads
        // depth via shader sampling in a future slice but the pipeline
        // itself runs without a depth attachment). The shadow-atlas
        // descriptor wiring at `set 1, binding 1` is Slice C scope.
        [[nodiscard]] static RHI::PipelineDesc BuildDeferredLightingPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/deferred/lighting.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT; // SceneColorHDR
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = 16u; // sizeof(DeferredLightingPushConstants)
            desc.DebugName = "Renderer.DeferredLighting";
            return desc;
        }

        // GRAPHICS-074 Slice A — default-recipe EntityId selection pipeline.
        // Pairs the GpuScene-aware `selection/entity_id.vert.spv` (reads
        // positions through `GpuScenePushConstants::SceneTableBDA` → instance
        // / dynamic / geometry buffer references and forwards the per-instance
        // stable entity ID as a flat `uint` varying) with the matching
        // `selection/entity_id.frag.spv` (writes two R32_UINT outputs: location
        // 0 = stable entity ID into the `EntityId` target, location 1 =
        // `EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0)` into the
        // `PrimitiveId` target per the GRAPHICS-012Q encoding contract). The
        // legacy `assets/shaders/pick_id.{vert,frag}` declares the pre-GpuScene
        // `mat4 Model + PtrPositions + PtrNormals + PtrAux + uint EntityID`
        // push-constant block and would silently truncate / misinterpret the
        // `RHI::GpuScenePushConstants` bytes that `EntityIdPass::Execute`
        // pushes via `cmd.PushConstants(&pc, sizeof(pc))`, so it is
        // deliberately *not* referenced here — see
        // `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the parallel forward / deferred /
        // shadow precedents. Two color targets match the frame recipe's
        // `PickingPass` attachment formats (`Graphics.FrameRecipe.cpp`,
        // `features.EnablePicking` branch): `EntityId` (R32_UINT) +
        // `PrimitiveId` (R32_UINT).
        //
        // Depth state: `BuildDefaultFrameRecipe` now orders `PickingPass`
        // *after* `DepthPrepass` and declares `Read(SceneDepth, DepthRead)`
        // on the picking pass (GRAPHICS-074 recipe-side follow-up). The
        // framegraph compiler therefore emits a render pass with a
        // `D32_FLOAT` depth attachment in read-only state, so the pipeline
        // mirrors the depth-equal / depth-write-off shape the forward and
        // deferred GBuffer pipelines use against the same depth buffer. The
        // depth-equal test guarantees only the nearest-surface fragment
        // wins each pixel — without it the recipe would last-fragment-win
        // and the Slice D readback drain would return wrong IDs for any
        // pixel covered by more than one draw. The matching recipe gating
        // (`EnablePicking && EnableDepthPrepass`) ensures this pipeline is
        // only requested when a populated `SceneDepth` is available.
        [[nodiscard]] static RHI::PipelineDesc BuildSelectionEntityIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/entity_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/entity_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionEntityId";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildSelectionEntityIdOutlinePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc = BuildSelectionEntityIdPipelineDesc();
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/entity_id_outline.frag.spv");
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[1] = RHI::Format::Undefined;
            desc.DebugName = "Renderer.SelectionEntityId.OutlineOnly";
            return desc;
        }

        // GRAPHICS-074 (Slice B) — Face / Edge / Point selection ID
        // pipeline descriptors. Each mirrors the EntityId descriptor's
        // render-pass-compatible shape (two R32_UINT color targets,
        // D32_FLOAT depth target, depth-equal / depth-test-on /
        // depth-write-off) so all four pipelines can be bound inside the
        // same recipe-declared `PickingPass` render pass. They differ
        // only in:
        //   - shader pair (`selection/{face,edge,point}_id.{vert,frag}`),
        //   - primitive topology (TriangleList / LineList / PointList),
        //   - cull mode (Back for faces, None for edges/points; mirrors
        //     `BuildForwardLinePipelineDesc` / `BuildForwardPointPipelineDesc`),
        //   - debug name.
        // The shader-side `EncodeSelectionId(domain, payload)` differs per
        // pipeline and lives in each fragment shader, not the descriptor.
        [[nodiscard]] static RHI::PipelineDesc BuildSelectionFaceIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/face_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/face_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionFaceId";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildSelectionEdgeIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/edge_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/edge_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionEdgeId";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildSelectionPointIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/point_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/point_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::PointList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionPointId";
            return desc;
        }

        // GRAPHICS-074 Slice C — default-recipe selection outline pipeline.
        // Pairs the fullscreen `post_fullscreen.vert.spv` (no vertex inputs,
        // no push constants — just emits a fullscreen triangle and a UV
        // varying) with `selection_outline.frag.spv` (samples the `EntityId`
        // R32_UINT target through `usampler2D uPickID` and writes the outline
        // RGBA overlay contribution). The recipe's `"SelectionOutlinePass"`
        // declares `Read(EntityId, ShaderRead) + Read(SceneDepth, DepthRead) +
        // Read(presentSource, ColorAttachmentRead) +
        // Write(presentSource, ColorAttachmentWrite)`, so the render pass loads
        // the current present source as the color target and alpha-blends the
        // shader's overlay into it. Depth state stays off — the
        // shader does not test or write depth — but the pipeline declares the
        // matching `DepthTargetFormat` so it remains render-pass-compatible
        // with the declared depth attachment.
        //
        // Push constants: `PushConstantSize = 144` matches
        // `SelectionOutlinePushConstants` defined in
        // `Passes/Pass.Selection.Outline.cpp`, which mirrors the
        // `selection_outline.frag` `layout(push_constant) uniform Push`
        // block byte-for-byte under Vulkan std430. The pass body pushes a
        // zero-initialised instance every frame so the shader never reads
        // stale push memory from a prior draw — without this, `OutlineWidth`
        // and `SelectedCount` could be arbitrary, producing nondeterministic
        // outlines and an unbounded fragment loop. Runtime-driven outline
        // state plumbing (selected/hovered IDs, colours, animation) is
        // deferred alongside the `Picking.Readback` drain (Slice D).
        // Portability caveat: 144 bytes exceeds the Vulkan-guaranteed
        // minimum `maxPushConstantsSize` of 128; reducing the block (e.g.
        // moving `SelectedIds[16]` into a UBO or bindless buffer) is the
        // tracked follow-up so the pipeline is portable across all
        // conformant devices. Current desktop Vulkan implementations expose
        // 256-byte push ranges, so this is non-blocking for the default
        // gate.
        //
        // The legacy shaders that previously sourced the outline overlay
        // (`forward/outline_overlay.frag` and friends) declared
        // incompatible push-constant blocks and descriptor sets and are
        // deliberately *not* referenced here — see
        // `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the policy.
        [[nodiscard]] static RHI::PipelineDesc BuildSelectionOutlinePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection_outline.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].ColorOp = RHI::BlendOp::Add;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].AlphaOp = RHI::BlendOp::Add;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = 144u; // sizeof(SelectionOutlinePushConstants)
            desc.DebugName = "Renderer.SelectionOutline";
            return desc;
        }

        // GRAPHICS-075 Slice A — default-recipe postprocess tonemap pipeline.
        // Pairs the fullscreen `post_fullscreen.vert.spv` (no vertex inputs,
        // no push constants; emits a fullscreen triangle and a UV varying)
        // with `post_tonemap.frag.spv` (samples the prior frame's HDR scene
        // color through `sampler2D uSceneColor` + bloom mix through
        // `sampler2D uBloomColor` and writes LDR back to the recipe's
        // `SceneColorLDR` target). The recipe's `"PostProcessPass"` declares
        // `Read(SceneColorHDR, ShaderRead) + Write(SceneColorLDR,
        // ColorAttachmentWrite)` (plus the bloom / histogram / AATemp
        // transient writes that the later slices' helpers consume), so the
        // render pass attaches `SceneColorLDR` (backbuffer format, per
        // `FrameRecipeSizing::BackbufferFormat`) with no depth attachment.
        //
        // Push constants: `PushConstantSize = sizeof(PostProcessToneMapPushConstants)`
        // (80 bytes) mirrors the shader's `layout(push_constant) Push { ... }`
        // block byte-for-byte under Vulkan std430 (4×4 bytes header + 4×4
        // bytes grading scalars + 3× `vec3 + float pad`). The pass body
        // builds the payload through `BuildPostProcessToneMapPushConstants(
        // m_Subsystems.PostProcessSystemRegistry().GetSettings())`, which derives `Exposure` /
        // `BloomIntensity` from settings and uses deterministic defaults
        // (`Operator = 0` ACES, `ColorGradingOn = 0`, neutral
        // `Saturation`/`Contrast`/`Lift`/`Gamma`/`Gain`) for the rest. The
        // canonical 20-byte `PostProcessPushConstants` block shared by the
        // other postprocess stages is intentionally *not* used here: under
        // std430 it aliases `HistogramBinCount` onto `ColorGradingOn` (so a
        // 256-bin default would enable grading) and `StageKind` onto
        // `Saturation` (`bit_cast<float>(2)` ≈ 0 → grayscale), with the
        // remaining 60 bytes of `Lift`/`Gamma`/`Gain` reading implementation
        // -defined memory — the standing "Shader push-constant compatibility
        // policy" hard gate.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessToneMapPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_tonemap.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessToneMapPushConstants));
            desc.DebugName = "Renderer.PostProcess.ToneMap";
            return desc;
        }

        // GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom
        // downsample pipeline. Pairs the fullscreen `post_fullscreen.vert.spv`
        // with `post_bloom_downsample.frag.spv` (samples the prior bloom
        // mip via `sampler2D uInput`, writes the 13-tap downsample result
        // into the next mip of `PostProcess.BloomScratch`). The target
        // format follows the recipe's `BloomScratch` declaration
        // (`RGBA16_FLOAT`) and there is no depth attachment.
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessBloomDownsamplePushConstants)` (16 bytes,
        // `vec2 InvSrcResolution + float Threshold + int IsFirstMip`)
        // mirrors the shader's `layout(push_constant) Push { ... }` block
        // byte-for-byte under std430. The canonical 20-byte
        // `PostProcessPushConstants` block shared by other postprocess
        // stages is intentionally *not* used here per the standing
        // "Shader push-constant compatibility policy": pushing it would
        // alias `Gamma` (2.2) onto `Threshold` and `BloomIntensity` onto
        // `IsFirstMip` (`bit_cast<int>(0.05f)` ≈ 1.04e9 → always-first-mip)
        // while reading past the shader's declared block.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessBloomDownsamplePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_bloom_downsample.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessBloomDownsamplePushConstants));
            desc.DebugName = "Renderer.PostProcess.Bloom.Downsample";
            return desc;
        }

        // GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom
        // upsample pipeline. Pairs `post_fullscreen.vert.spv` with
        // `post_bloom_upsample.frag.spv` (samples the coarser mip via
        // `sampler2D uCoarser` + the current downsample mip via
        // `sampler2D uCurrent`, writes the 9-tap tent-filter accumulation
        // result into the finer mip of `PostProcess.BloomScratch`). Same
        // target shape as the downsample pipeline (RGBA16F color,
        // no depth).
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessBloomUpsamplePushConstants)` (16 bytes,
        // `vec2 InvCoarserResolution + float FilterRadius + float _pad0`).
        // Same std430 alias hazard as the downsample pipeline; the canonical
        // 20-byte block stays out.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessBloomUpsamplePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_bloom_upsample.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessBloomUpsamplePushConstants));
            desc.DebugName = "Renderer.PostProcess.Bloom.Upsample";
            return desc;
        }

        // GRAPHICS-075 Slice C — default-recipe postprocess FXAA pipeline.
        // Pairs `post_fullscreen.vert.spv` with `post_fxaa.frag.spv`
        // (samples post-tonemap `SceneColorLDR` through one sampled-image
        // binding plus a linear-clamp sampler, writes the anti-aliased
        // result back into the recipe's LDR target). The target format
        // follows the recipe's `SceneColorLDR` declaration (the
        // backbuffer format `FrameRecipeSizing::BackbufferFormat`), so
        // the pipeline takes the same `colorFormat` parameter the
        // tonemap pipeline does. No depth attachment.
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessFXAAPushConstants)` (20 bytes, `vec2
        // InvResolution + float ContrastThreshold + float
        // RelativeThreshold + float SubpixelBlending`) mirrors the
        // shader's `layout(push_constant) Push { ... }` block byte-for-
        // byte under std430. The canonical 20-byte
        // `PostProcessPushConstants` block shared by other postprocess
        // stages is intentionally *not* used here per the standing
        // "Shader push-constant compatibility policy": pushing the
        // canonical block would alias `Exposure` (1.0) onto
        // `InvResolution.x`, `Gamma` (2.2) onto `InvResolution.y`,
        // `BloomIntensity` (0.05) onto `ContrastThreshold`, etc., and
        // produce visually-meaningless FXAA output even though the wire
        // size happens to match.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessFXAAPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fxaa.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessFXAAPushConstants));
            desc.DebugName = "Renderer.PostProcess.FXAA";
            return desc;
        }

        // GRAPHICS-075 Slice D.2a — three default-recipe postprocess SMAA
        // pipelines, each pairing `post_fullscreen.vert.spv` with the
        // matching SMAA fragment shader. The recipe's
        // `PostProcess.AATemp.{Edges,Weights,Resolved}` split allocates
        // three matched-format AA transients, so the edge pipeline is
        // *fixed* at `RG8_UNORM`, the blend pipeline is *fixed* at
        // `RGBA8_UNORM`, and the resolve pipeline keeps the
        // backbuffer-format `colorFormat` parameter (mirroring the FXAA
        // pipeline, which also writes to `AATemp.Resolved` under the
        // resolve graph pass). The edge / blend formats are no longer
        // parameterised because the recipe-level resource declarations
        // pin them — letting a caller pass a different `colorFormat`
        // would diverge from the recipe's AATemp.{Edges,Weights}
        // attachment formats and either fail Vulkan's render-pass-
        // compatibility rule or silently skip the bound stage. Push-
        // constant sizes still match each shader's std430 push block
        // byte-for-byte (16 bytes per stage); the canonical 20-byte
        // `PostProcessPushConstants` is intentionally not reused per the
        // "Shader push-constant compatibility policy" — see
        // `Pass.PostProcess.SMAA.cppm` for the aliasing rationale. The
        // retained `AreaTex` / `SearchTex` LUT textures sampled by the
        // blend pipeline are owned by `PostProcessSystem` and land in
        // Slice D.2b alongside the device-aware `Initialize(device)`
        // overload.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessSMAAEdgePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_smaa_edge.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            // Fixed at `RG8_UNORM` to match the recipe's
            // `PostProcess.AATemp.Edges` transient; the shader writes
            // `vec2 edges` so the unused .ba channels would waste
            // bandwidth on a wider target.
            desc.ColorTargetFormats[0] = RHI::Format::RG8_UNORM;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessSMAAEdgePushConstants));
            desc.DebugName = "Renderer.PostProcess.SMAA.Edge";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessSMAABlendPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_smaa_blend.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            // Fixed at `RGBA8_UNORM` to match the recipe's
            // `PostProcess.AATemp.Weights` transient (four-channel
            // blending weights per the SMAA reference). Happens to share
            // the byte shape of the default backbuffer format, but the
            // dependency is on the *recipe* declaration, not the
            // coincidence.
            desc.ColorTargetFormats[0] = RHI::Format::RGBA8_UNORM;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessSMAABlendPushConstants));
            desc.DebugName = "Renderer.PostProcess.SMAA.Blend";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessSMAAResolvePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_smaa_resolve.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            // Resolve writes the final anti-aliased LDR to
            // `PostProcess.AATemp.Resolved`, which the recipe allocates
            // with `FrameRecipeSizing::BackbufferFormat`. The FXAA
            // pipeline writes the same resolved attachment and takes the
            // same backbuffer-format `colorFormat` parameter, so both
            // pipelines stay render-pass-compatible with the recipe's
            // resolve graph pass.
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessSMAAResolvePushConstants));
            desc.DebugName = "Renderer.PostProcess.SMAA.Resolve";
            return desc;
        }

        // GRAPHICS-075 Slice E.1 — default-recipe postprocess histogram
        // compute pipeline. Standalone compute pipeline (no vertex /
        // fragment stages); the `ComputeShaderPath` field is what the
        // pipeline backend uses to interpret the descriptor as compute
        // per the `PipelineDesc` contract. The dispatch runs in its own
        // ordered graph pass `"PostProcessHistogramPass"` (declared by
        // the recipe with `Read(SceneColorHDR, ShaderRead)` +
        // `Write(PostProcess.Histogram, BufferUsage::ShaderWrite)`) so
        // the framegraph compiler emits the read-after-write barrier
        // and the dispatch executes outside any render-pass scope —
        // Vulkan rejects `vkCmdDispatch` inside an active render-pass
        // scope, which is why the histogram cannot share the
        // `"PostProcessPass"` umbrella's render-pass scope.
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessHistogramPushConstants)` (16 bytes,
        // `uint Width + uint Height + float MinLogLum + float
        // RangeLogLum`) mirrors the shader's
        // `layout(push_constant) PushConstants` block byte-for-byte
        // under std430. The canonical 20-byte `PostProcessPushConstants`
        // block shared by other postprocess stages is intentionally
        // *not* used here per the standing shader-push-constant
        // compatibility policy: under std430 it would alias `Exposure`
        // (1.0) onto `Width` (`bit_cast<uint>(1.0f)` = 0x3F800000 ≈
        // 1.07e9 pixels wide), `Gamma` (2.2) onto `Height`, and
        // `BloomIntensity` (0.05) onto `MinLogLum`, producing a
        // degenerate out-of-bounds dispatch shape and a meaningless
        // luminance histogram.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessHistogramPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.ComputeShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_histogram.comp.spv");
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessHistogramPushConstants));
            desc.DebugName = "Renderer.PostProcess.Histogram";
            return desc;
        }

        // GRAPHICS-038B — default-recipe HZB build compute pipeline. It records
        // outside a render-pass scope under `"HZBBuildPass"` and consumes the
        // 32-byte `HZBBuildPushConstants` block that selects the target mip and
        // fallback/single-pass mode. The shader asset contains the subgroup +
        // shared-memory reduction code; this slice pins the CPU/null command
        // shape while the storage-image binding and gpu;vulkan conservatism
        // proof remain `GRAPHICS-038E` scope.
        [[nodiscard]] static RHI::PipelineDesc BuildHZBBuildPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.ComputeShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/hzb_build.comp.spv");
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(HZBBuildPushConstants));
            desc.DebugName = "Renderer.HZB.Build";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildClusterGridBuildPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.ComputeShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/cluster_grid_build.comp.spv");
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(ClusterGridBuildPushConstants));
            desc.DebugName = "Renderer.ClusterGrid.Build";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildClusterLightAssignmentPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.ComputeShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/light_cluster_assign.comp.spv");
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize =
                static_cast<std::uint32_t>(sizeof(ClusterLightAssignmentPushConstants));
            desc.DebugName = "Renderer.ClusterLights.Assign";
            return desc;
        }

        // GRAPHICS-076 Slice A — canonical default-recipe present pipeline.
        // Pairs with the new `assets/shaders/present.{vert,frag}` shaders:
        // the vertex stage emits the fullscreen triangle (positions
        // [-1,-1], [3,-1], [-1,3]) and the fragment samples
        // `FrameRecipe.PresentSource` and writes the imported backbuffer
        // LDR target with alpha forced to opaque. Held byte-identical
        // between the initial `Initialize()` and any subsequent
        // `RebuildOperationalResources()` so the pipeline registry's
        // dedupe yields a stable device handle. `PushConstantSize = 0u`
        // because the canonical `PresentPass::Execute()` records only
        // `BindPipeline + Draw(3, 1, 0, 0)`; no per-frame push data is
        // required (the present source binding is descriptor-side, owned
        // by the backend's pipeline layout).
        [[nodiscard]] static RHI::PipelineDesc BuildPresentPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/present.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/present.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = 0u;
            desc.DebugName = "Renderer.Present";
            return desc;
        }

        // GRAPHICS-076 Slice B — canonical default-recipe `Pass.DebugView`
        // pipeline. Pairs with `assets/shaders/debug_view.{vert,frag}`:
        // the vertex stage emits the fullscreen-triangle (positions
        // [-1,-1], [3,-1], [-1,3]) and the fragment derives a
        // visualization path from the canonical
        // `DebugViewPushConstants::ResourceClass` field (per
        // GRAPHICS-013BQ §"Shader visualization modes") and writes the
        // recipe-owned `DebugViewRGBA` color attachment
        // (`Format::RGBA8_UNORM`, declared by `BuildDefaultFrameRecipe`).
        // Held byte-identical between the initial `Initialize()` and any
        // subsequent `RebuildOperationalResources()` so the pipeline
        // registry's dedupe yields a stable device handle.
        // `PushConstantSize = sizeof(DebugViewPushConstants)` (16 bytes)
        // matches the four-`uint32` packing from
        // `Graphics.DebugViewSystem.cppm`. Color target pinned to
        // `RGBA8_UNORM` regardless of the swapchain backbuffer format
        // because the `DebugViewRGBA` attachment is itself an
        // `RGBA8_UNORM` resource per the recipe.
        [[nodiscard]] static RHI::PipelineDesc BuildDebugViewPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/debug_view.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/debug_view.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA8_UNORM;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(DebugViewPushConstants));
            desc.DebugName = "Renderer.DebugView";
            return desc;
        }

        // GRAPHICS-079 Slice A — canonical default-recipe `Pass.ImGui`
        // pipeline. Match Dear ImGui's Vulkan backend straight-alpha color
        // blend (`SrcAlpha`, `OneMinusSrcAlpha`), no depth test/write,
        // scissor enabled (dynamic), color target pinned to the
        // `FrameRecipe.PresentSource` format (the swapchain backbuffer format,
        // since PresentSource aliases the present color resource), and a
        // dynamic viewport. The push-constant block is the
        // `ImGuiOverlayPushConstants` (16 bytes) the `ImGuiOverlaySystem`
        // builds; the real `ImDrawVert` vertex-buffer binding + per-draw-list
        // scissored draws + bindless user textures are owned by Slice C. Paired
        // with `assets/shaders/imgui.{vert,frag}`. Held byte-identical between
        // the initial `Initialize()` and any subsequent
        // `RebuildOperationalResources()` so the pipeline registry's dedupe
        // yields a stable device handle.
        [[nodiscard]] static RHI::PipelineDesc BuildImGuiPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/imgui.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/imgui.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            // Dear ImGui emits straight-alpha vertex/texture colors; match
            // `imgui_impl_vulkan` so transparent font-atlas pixels do not
            // fill the whole glyph quad.
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].ColorOp = RHI::BlendOp::Add;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].AlphaOp = RHI::BlendOp::Add;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(ImGuiOverlayPushConstants));
            desc.DebugName = "Renderer.ImGui";
            return desc;
        }

        // GRAPHICS-077 Slice B — transient-debug triangle pipelines. Two
        // variants per lane (depth-tested + always-on-top) so packets
        // with `DepthTested = true` rasterize against the prepass depth
        // and packets with `DepthTested = false` overlay on top
        // regardless of occlusion. Both variants share the same shader
        // pair (`assets/shaders/transient_debug_triangle.{vert,frag}`),
        // a BDA-fetch vertex layout (positions + packed RGBA8 color
        // pulled from the helper's host-visible vertex buffer), and the
        // 16-byte `TransientDebugTrianglePushConstants` push block (BDA +
        // per-draw `FirstVertex`). Color target pinned to `RGBA16_FLOAT`
        // because the pass writes the `SceneColorHDR` resource declared
        // by `BuildDefaultFrameRecipe(...)`. `DepthTargetFormat` is
        // `D32_FLOAT` for both variants (matching the prepass depth);
        // the always-on-top variant disables `DepthTestEnable` so it
        // ignores occlusion while still consuming the same render-pass
        // attachment layout. `ColorBlend[0].Enable = false` matches the
        // GRAPHICS-077 task non-goal of "no new blend modes" — opaque
        // overlay is the canonical CPUContracted form; alpha-blended
        // overlays are reserved for a follow-up task.
        [[nodiscard]] static RHI::PipelineDesc BuildTransientDebugTrianglePipelineDesc(
            const bool depthTested) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/transient_debug_triangle.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/transient_debug_triangle.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = depthTested;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize =
                static_cast<std::uint32_t>(sizeof(TransientDebugTrianglePushConstants));
            desc.DebugName = depthTested
                ? "Renderer.TransientDebug.Triangle.DepthTested"
                : "Renderer.TransientDebug.Triangle.AlwaysOnTop";
            return desc;
        }

        // GRAPHICS-077 Slice C — transient-debug line + point pipelines.
        // Mirror the triangle helper's invariant set (color target =
        // `RGBA16_FLOAT` because the pass writes `SceneColorHDR`;
        // depth target = `D32_FLOAT` matching the prepass depth; depth
        // write disabled because the overlay must not occlude later
        // composition; `ColorBlend[0].Enable = false` because opaque
        // overlay is the canonical CPUContracted form). Topology
        // selects `LineList` / `PointList` per lane. Width / radius
        // expansion is deferred — the CPUContracted form pins the
        // bind/push/draw shape only; Slice D verifies the pixel-level
        // rasterization through an opt-in `gpu;vulkan` smoke. The
        // shared 16-byte push block carries the helper's vertex buffer
        // BDA + the per-draw `FirstVertex` so each lane's BDA-fetch
        // vertex shader resolves the right packet's vertices.
        [[nodiscard]] static RHI::PipelineDesc BuildTransientDebugLinePipelineDesc(
            const bool depthTested) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/transient_debug_line.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/transient_debug_line.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = depthTested;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize =
                static_cast<std::uint32_t>(sizeof(TransientDebugLinePushConstants));
            desc.DebugName = depthTested
                ? "Renderer.TransientDebug.Line.DepthTested"
                : "Renderer.TransientDebug.Line.AlwaysOnTop";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildTransientDebugPointPipelineDesc(
            const bool depthTested) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/transient_debug_point.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/transient_debug_point.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::PointList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = depthTested;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize =
                static_cast<std::uint32_t>(sizeof(TransientDebugPointPushConstants));
            desc.DebugName = depthTested
                ? "Renderer.TransientDebug.Point.DepthTested"
                : "Renderer.TransientDebug.Point.AlwaysOnTop";
            return desc;
        }

        // GRAPHICS-078 Slice B — visualization-overlay vector-field
        // pipelines. Two variants per kind (depth-tested + always-on-
        // top) so packets with `DepthTested = true` rasterize against
        // the prepass depth and packets with `DepthTested = false`
        // overlay on top regardless of occlusion. Both variants share
        // the same shader pair
        // (`assets/shaders/visualization_vector_field.{vert,frag}`),
        // a BDA-fetch vertex layout (positions + packed RGBA8 color
        // pulled from the helper's host-visible vertex buffer), and
        // the 16-byte `VisualizationVectorFieldPushConstants` push
        // block (BDA + per-draw `FirstVertex`). Color target pinned to
        // `RGBA16_FLOAT` because the pass writes the `SceneColorHDR`
        // resource declared by `BuildDefaultFrameRecipe(...)`.
        // `DepthTargetFormat` is `D32_FLOAT` matching the prepass
        // depth; the always-on-top variant disables `DepthTestEnable`
        // so it ignores occlusion while still consuming the same
        // render-pass attachment layout. `ColorBlend[0].Enable = false`
        // matches the GRAPHICS-078 task non-goal of "no third pipeline
        // variant per kind" — opaque overlay is the canonical
        // CPUContracted form; alpha-blended glyphs are reserved for a
        // follow-up task. Topology is `LineList` because each glyph is
        // expanded into a single anchor→tip line segment by the
        // helper (two vertices per glyph).
        [[nodiscard]] static RHI::PipelineDesc BuildVisualizationVectorFieldPipelineDesc(
            const bool depthTested) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/visualization_vector_field.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/visualization_vector_field.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = depthTested;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize =
                static_cast<std::uint32_t>(sizeof(VisualizationVectorFieldPushConstants));
            desc.DebugName = depthTested
                ? "Renderer.VisualizationOverlay.VectorField.DepthTested"
                : "Renderer.VisualizationOverlay.VectorField.AlwaysOnTop";
            return desc;
        }

        // GRAPHICS-078 Slice C — visualization-overlay isoline pipelines.
        // Mirrors the vector-field desc exactly except for the shader
        // pair (`visualization_isoline.{vert,frag}`) and the debug
        // name. `LineList` topology because each iso value is expanded
        // by the helper into a deterministic placeholder line segment
        // (two vertices per iso). Actual scalar-field-derived contour
        // polylines remain future source-BDA work while preserving the
        // topology + push-constant contract.
        // Push-constant block is the dedicated
        // `VisualizationIsolinePushConstants` shape (BDA +
        // `FirstVertex`) so per-kind evolution (e.g. per-iso line width
        // expansion) can land without disturbing the vector-field lane.
        [[nodiscard]] static RHI::PipelineDesc BuildVisualizationIsolinePipelineDesc(
            const bool depthTested) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/visualization_isoline.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/visualization_isoline.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = depthTested;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize =
                static_cast<std::uint32_t>(sizeof(VisualizationIsolinePushConstants));
            desc.DebugName = depthTested
                ? "Renderer.VisualizationOverlay.Isoline.DepthTested"
                : "Renderer.VisualizationOverlay.Isoline.AlwaysOnTop";
            return desc;
        }

        void ResetClusterLightResources()
        {
            m_ClusterGridAABBBuffer.reset();
            m_ClusterLightHeaderBuffer.reset();
            m_ClusterLightIndexBuffer.reset();
            m_ClusterLightCounterBuffer.reset();
            m_ClusterGridDesc = {};
            m_ClusterGridProjection = {};
            if (m_Subsystems.GpuWorldSystem())
            {
                m_Subsystems.GpuWorldSystem()->ClearClusterLightTable();
            }
        }

        [[nodiscard]] bool EnsureClusterLightResources(const RenderWorld& renderWorld)
        {
            if (m_Device == nullptr || !m_Device->IsOperational() || !m_Subsystems.BufferManager() || !m_Subsystems.GpuWorldSystem())
            {
                if (m_Subsystems.GpuWorldSystem())
                {
                    m_Subsystems.GpuWorldSystem()->ClearClusterLightTable();
                }
                return false;
            }

            const std::uint32_t width = renderWorld.Viewport.Width > 0
                ? static_cast<std::uint32_t>(renderWorld.Viewport.Width)
                : 1u;
            const std::uint32_t height = renderWorld.Viewport.Height > 0
                ? static_cast<std::uint32_t>(renderWorld.Viewport.Height)
                : 1u;
            const ClusterGridDesc desc = ComputeClusterGridDesc(width, height);
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const ClusterGridProjection projection =
                BuildClusterGridProjectionFromVerticalFov(
                    kClusterDefaultVerticalFovRadians,
                    aspect,
                    kClusterDefaultNearZ,
                    kClusterDefaultFarZ);
            if (!desc.IsValid() || !projection.IsValid())
            {
                ResetClusterLightResources();
                return false;
            }

            const bool needsAllocation =
                m_ClusterGridDesc != desc ||
                !m_ClusterGridAABBBuffer.has_value() ||
                !m_ClusterGridAABBBuffer->IsValid() ||
                !m_ClusterLightHeaderBuffer.has_value() ||
                !m_ClusterLightHeaderBuffer->IsValid() ||
                !m_ClusterLightIndexBuffer.has_value() ||
                !m_ClusterLightIndexBuffer->IsValid() ||
                !m_ClusterLightCounterBuffer.has_value() ||
                !m_ClusterLightCounterBuffer->IsValid();
            if (needsAllocation)
            {
                m_ClusterGridAABBBuffer.reset();
                m_ClusterLightHeaderBuffer.reset();
                m_ClusterLightIndexBuffer.reset();
                m_ClusterLightCounterBuffer.reset();
                m_ClusterGridDesc = {};
                m_ClusterGridProjection = {};

                auto gridOr = m_Subsystems.BufferManager()->Create(BuildClusterGridAABBBufferDesc(desc));
                auto headersOr = m_Subsystems.BufferManager()->Create(BuildClusterLightHeaderBufferDesc(desc));
                auto indicesOr = m_Subsystems.BufferManager()->Create(
                    BuildClusterLightIndexBufferDesc(desc, kMaxClusterLightsPerCell));
                auto counterOr = m_Subsystems.BufferManager()->Create(BuildClusterLightCounterBufferDesc());
                if (!gridOr.has_value() || !headersOr.has_value() ||
                    !indicesOr.has_value() || !counterOr.has_value())
                {
                    ResetClusterLightResources();
                    Core::Log::Warn(
                        "[Graphics] Clustered-light buffers unavailable; cluster passes will be omitted from the default recipe.");
                    return false;
                }

                m_ClusterGridAABBBuffer.emplace(std::move(*gridOr));
                m_ClusterLightHeaderBuffer.emplace(std::move(*headersOr));
                m_ClusterLightIndexBuffer.emplace(std::move(*indicesOr));
                m_ClusterLightCounterBuffer.emplace(std::move(*counterOr));
                m_ClusterGridDesc = desc;
                m_ClusterGridProjection = projection;
            }
            else
            {
                m_ClusterGridProjection = projection;
            }

            m_Subsystems.GpuWorldSystem()->SetClusterLightTable(GpuWorld::ClusterLightTableDesc{
                .HeaderBuffer = m_ClusterLightHeaderBuffer->GetHandle(),
                .IndexBuffer = m_ClusterLightIndexBuffer->GetHandle(),
                .TilePx = desc.ClusterTilePx,
                .TilesX = desc.TilesX,
                .TilesY = desc.TilesY,
                .SlicesZ = desc.SlicesZ,
                .CellCount = desc.CellCount,
                .MaxLightsPerCell = kMaxClusterLightsPerCell,
                .NearZ = projection.NearZ,
                .FarZ = projection.FarZ,
                .ProjectionScaleX = projection.ProjectionScaleX,
                .ProjectionScaleY = projection.ProjectionScaleY,
            });
            return true;
        }

        [[nodiscard]] bool ClusterLightResourcesReady() const noexcept
        {
            return m_ClusterGridDesc.IsValid() &&
                   m_ClusterGridProjection.IsValid() &&
                   m_ClusterGridAABBBuffer.has_value() &&
                   m_ClusterGridAABBBuffer->IsValid() &&
                   m_ClusterLightHeaderBuffer.has_value() &&
                   m_ClusterLightHeaderBuffer->IsValid() &&
                   m_ClusterLightIndexBuffer.has_value() &&
                   m_ClusterLightIndexBuffer->IsValid() &&
                   m_ClusterLightCounterBuffer.has_value() &&
                   m_ClusterLightCounterBuffer->IsValid();
        }

        [[nodiscard]] bool InitializeOperationalPassResources(RHI::IDevice& device)
        {
            if (!device.IsOperational() || !m_Subsystems.CullingSystemRegistry() || !m_Subsystems.BufferManager() || !m_Subsystems.PipelineManager())
            {
                m_CullingOutputAvailable = false;
                return false;
            }

            m_Subsystems.CullingSystemRegistry()->Shutdown();
            m_CullingOutputAvailable = m_Subsystems.CullingSystemRegistry()->Initialize(
                device,
                *m_Subsystems.BufferManager(),
                *m_Subsystems.PipelineManager(),
                Core::Filesystem::GetShaderPath("shaders/instance_cull.comp.spv"));

            m_DepthPrepassPipelineLease.reset();
            RHI::PipelineDesc depthPrepassDesc{};
            depthPrepassDesc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/depth_prepass.vert.spv");
            depthPrepassDesc.Rasterizer.Winding = RHI::FrontFace::Clockwise;
            depthPrepassDesc.ColorTargetCount = 0u;
            depthPrepassDesc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            depthPrepassDesc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            depthPrepassDesc.DebugName = "Renderer.DepthPrepass";
            auto depthPipeline = m_Subsystems.PipelineManager()->Create(depthPrepassDesc);
            if (depthPipeline.has_value())
            {
                m_DepthPrepassPipelineLease.emplace(std::move(*depthPipeline));
                m_DepthPrepassPass.SetPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(m_DepthPrepassPipelineLease->GetHandle()));
            }
            else
            {
                Core::Log::Warn("[Graphics] DepthPrepass pipeline unavailable; pass commands will be skipped: error={}",
                                static_cast<int>(depthPipeline.error()));
            }

            // GRAPHICS-031A: canonical missing-material fallback pipeline.
            // Republished byte-identical from BuildDefaultDebugSurfacePipelineDesc()
            // so the descriptor matches across initial init and rebuilds.
            m_DefaultDebugSurfacePipelineLease.reset();
            const RHI::PipelineDesc defaultDebugSurfaceDesc = BuildDefaultDebugSurfacePipelineDesc(m_BackbufferFormat);
            auto defaultDebugSurfacePipeline = m_Subsystems.PipelineManager()->Create(defaultDebugSurfaceDesc);
            if (defaultDebugSurfacePipeline.has_value())
            {
                m_DefaultDebugSurfacePipelineLease.emplace(std::move(*defaultDebugSurfacePipeline));
            }
            else
            {
                Core::Log::Warn("[Graphics] DefaultDebugSurface pipeline unavailable; fallback recording will be skipped: error={}",
                                static_cast<int>(defaultDebugSurfacePipeline.error()));
            }

            // GRAPHICS-070 — forward surface pipeline. Drop the lease before
            // republishing so a non-deduped registry would not leak a dangling
            // entry; `SetPipeline(PipelineHandle{})` zeros the cached handle
            // so a failed `Create()` leaves the pass in the fail-closed state
            // that `RecordForwardSurfacePass` interprets as `SkippedUnavailable`.
            m_ForwardSurfacePipelineLease.reset();
            if (m_ForwardSurfacePass)
            {
                m_ForwardSurfacePass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc forwardSurfaceDesc = BuildForwardSurfacePipelineDesc();
            auto forwardSurfacePipeline = m_Subsystems.PipelineManager()->Create(forwardSurfaceDesc);
            if (forwardSurfacePipeline.has_value())
            {
                m_ForwardSurfacePipelineLease.emplace(std::move(*forwardSurfacePipeline));
                if (m_ForwardSurfacePass)
                {
                    m_ForwardSurfacePass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_ForwardSurfacePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ForwardSurface pipeline unavailable; default-recipe surface recording will be skipped: error={}",
                                static_cast<int>(forwardSurfacePipeline.error()));
            }

            // GRAPHICS-071 — retained line and point forward pipelines. These
            // use the same reset/republish pattern as GRAPHICS-070 so failed
            // creates leave the pass in `SkippedUnavailable` rather than
            // retaining stale device handles across rebuilds.
            m_ForwardLinePipelineLease.reset();
            if (m_ForwardLinePass)
            {
                m_ForwardLinePass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc forwardLineDesc = BuildForwardLinePipelineDesc();
            auto forwardLinePipeline = m_Subsystems.PipelineManager()->Create(forwardLineDesc);
            if (forwardLinePipeline.has_value())
            {
                m_ForwardLinePipelineLease.emplace(std::move(*forwardLinePipeline));
                if (m_ForwardLinePass)
                {
                    m_ForwardLinePass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_ForwardLinePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ForwardLine pipeline unavailable; default-recipe line recording will be skipped: error={}",
                                static_cast<int>(forwardLinePipeline.error()));
            }

            m_ForwardPointPipelineLease.reset();
            if (m_ForwardPointPass)
            {
                m_ForwardPointPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc forwardPointDesc = BuildForwardPointPipelineDesc();
            auto forwardPointPipeline = m_Subsystems.PipelineManager()->Create(forwardPointDesc);
            if (forwardPointPipeline.has_value())
            {
                m_ForwardPointPipelineLease.emplace(std::move(*forwardPointPipeline));
                if (m_ForwardPointPass)
                {
                    m_ForwardPointPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_ForwardPointPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ForwardPoint pipeline unavailable; default-recipe point recording will be skipped: error={}",
                                static_cast<int>(forwardPointPipeline.error()));
            }

            // GRAPHICS-073 Slice A — depth-only shadow pipeline. Same
            // reset/republish pattern as the forward pipelines so a failed
            // `Create()` leaves the pass in `SkippedUnavailable` rather than
            // retaining a stale device handle across rebuilds.
            m_ShadowPipelineLease.reset();
            if (m_ShadowPass)
            {
                m_ShadowPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc shadowDesc = BuildShadowPipelineDesc();
            auto shadowPipeline = m_Subsystems.PipelineManager()->Create(shadowDesc);
            if (shadowPipeline.has_value())
            {
                m_ShadowPipelineLease.emplace(std::move(*shadowPipeline));
                if (m_ShadowPass)
                {
                    m_ShadowPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_ShadowPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] Shadow pipeline unavailable; default-recipe shadow recording will be skipped: error={}",
                                static_cast<int>(shadowPipeline.error()));
            }

            // GRAPHICS-072 Slice A — deferred GBuffer pipeline. Same
            // reset/republish pattern as the forward and shadow pipelines so a
            // failed `Create()` leaves the pass in `SkippedUnavailable` rather
            // than retaining a stale device handle across rebuilds.
            m_DeferredGBufferPipelineLease.reset();
            if (m_DeferredGBufferPass)
            {
                m_DeferredGBufferPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc deferredGBufferDesc = BuildDeferredGBufferPipelineDesc();
            auto deferredGBufferPipeline = m_Subsystems.PipelineManager()->Create(deferredGBufferDesc);
            if (deferredGBufferPipeline.has_value())
            {
                m_DeferredGBufferPipelineLease.emplace(std::move(*deferredGBufferPipeline));
                if (m_DeferredGBufferPass)
                {
                    m_DeferredGBufferPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_DeferredGBufferPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] DeferredGBuffer pipeline unavailable; default-recipe deferred surface recording will be skipped: error={}",
                                static_cast<int>(deferredGBufferPipeline.error()));
            }

            // GRAPHICS-072 Slice B — deferred lighting pipeline. Same
            // reset/republish pattern as the forward, shadow, and GBuffer
            // pipelines so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` rather than retaining a stale device handle
            // across rebuilds.
            m_DeferredLightingPipelineLease.reset();
            if (m_DeferredLightingPass)
            {
                m_DeferredLightingPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc deferredLightingDesc = BuildDeferredLightingPipelineDesc();
            auto deferredLightingPipeline = m_Subsystems.PipelineManager()->Create(deferredLightingDesc);
            if (deferredLightingPipeline.has_value())
            {
                m_DeferredLightingPipelineLease.emplace(std::move(*deferredLightingPipeline));
                if (m_DeferredLightingPass)
                {
                    m_DeferredLightingPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_DeferredLightingPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] DeferredLighting pipeline unavailable; default-recipe deferred composition recording will be skipped: error={}",
                                static_cast<int>(deferredLightingPipeline.error()));
            }

            // GRAPHICS-074 Slice D.1 — renderer-owned host-visible
            // `Picking.Readback` buffer. Allocated lazily so the buffer
            // survives `RebuildOperationalResources()` byte-identical when
            // `device.GetFramesInFlight()` is unchanged (same pattern
            // `ShadowSystem` follows for its depth atlas); the
            // `m_Subsystems.BufferManager()` itself is torn down in `Shutdown()` along
            // with the lease, so a fresh `Initialize(device)` after
            // `Shutdown()` will allocate a new buffer against the new
            // manager. Sized for `kPickingReadbackSlotStride *
            // frames-in-flight` bytes: one 4-byte `EntityId` word, one 4-byte
            // `EncodedSelectionId` word (`GRAPHICS-012Q`), one 4-byte
            // `SceneDepth` R32 float sample (BUG-026), and 4 pad bytes per
            // in-flight frame slot. If the expected size differs from the
            // current allocation (e.g. the device reports a different
            // frames-in-flight after a swapchain rebuild), the lease is
            // dropped and re-created so the per-slot addressing never
            // overruns the buffer. The handle is imported into the recipe and
            // the copies are recorded in `RecordPickingCommandRoute`; the
            // drain runs on `BeginFrame()`.
            const std::uint32_t pickingFramesInFlight = std::max(1u, device.GetFramesInFlight());
            const std::uint64_t pickingReadbackBytes =
                kPickingReadbackSlotStride *
                static_cast<std::uint64_t>(pickingFramesInFlight);
            const bool pickingReadbackNeedsAllocation =
                !m_PickingReadbackBuffer.has_value() ||
                !m_PickingReadbackBuffer->IsValid() ||
                m_PickingReadbackBufferSize != pickingReadbackBytes;
            if (pickingReadbackNeedsAllocation)
            {
                m_PickingReadbackBuffer.reset();
                m_PickingReadbackBufferSize = 0u;
                auto pickingReadbackOr = m_Subsystems.BufferManager()->Create({
                    .SizeBytes   = pickingReadbackBytes,
                    .Usage       = RHI::BufferUsage::TransferDst,
                    .HostVisible = true,
                    .DebugName   = "Renderer.PickingReadback",
                });
                if (pickingReadbackOr.has_value())
                {
                    m_PickingReadbackBuffer.emplace(std::move(*pickingReadbackOr));
                    m_PickingReadbackBufferSize = pickingReadbackBytes;
                }
                else
                {
                    Core::Log::Warn("[Graphics] Picking.Readback buffer unavailable; default-recipe picking readback will be skipped: error={}",
                                    static_cast<int>(pickingReadbackOr.error()));
                }
            }

            // GRAPHICS-074 Slice D.3 — keep the per-slot picking metadata
            // arrays sized to match the current `frames-in-flight` slot
            // count. Three cases:
            //   1. Slot count shrank (FIF demoted, e.g. triple → double
            //      buffered). Slots at indices `>= newSlotCount` are about
            //      to be truncated, so any pending readback in that tail is
            //      *resolved* by publishing `PublishNoHit()` before the
            //      array shrinks — otherwise the SelectionSystem would
            //      keep its `PendingPick` visible to the runtime/editor
            //      forever (the new slot indexing addresses a strictly
            //      smaller range, so the dropped slots can never be
            //      drained naturally).
            //   2. Slot count unchanged or grew with previously pending
            //      slots still within the new bound. Those still-pending
            //      slots are flagged `Invalidated=true` so the upcoming
            //      `BeginFrame()` drain publishes `PublishNoHit` rather
            //      than a stale pre-rebuild hit (the buffer itself is
            //      preserved across same-FIF rebuilds, so the underlying
            //      bytes would still decode to a hit — that's exactly the
            //      case the test `PublishesNoHitForInvalidatedRequest`
            //      exercises).
            //   3. Slot count grew (FIF promoted). New trailing entries
            //      start in a clean non-pending state.
            const std::size_t newSlotCount = static_cast<std::size_t>(pickingFramesInFlight);
            if (m_PickingSlotPending.size() > newSlotCount)
            {
                for (std::size_t slot = newSlotCount; slot < m_PickingSlotPending.size(); ++slot)
                {
                    if (m_PickingSlotPending[slot] && m_Subsystems.SelectionSystemRegistry())
                    {
                        // RUNTIME-089 — resolve the truncated pending pick as a
                        // NoHit carrying its correlation Sequence so the runtime
                        // releases the exact in-flight request, not the oldest.
                        m_Subsystems.SelectionSystemRegistry()->PublishPickResult(PickReadbackResult{
                            .Hit      = false,
                            .Sequence = m_PickingSlotSequence[slot],
                        });
                    }
                }
                m_PickingSlotPending.resize(newSlotCount);
                m_PickingSlotIssuedFrame.resize(newSlotCount);
                m_PickingSlotRequest.resize(newSlotCount);
                m_PickingSlotInvalidated.resize(newSlotCount);
                m_PickingSlotSequence.resize(newSlotCount);
                m_PickingSlotDepthCopied.resize(newSlotCount);
            }
            for (std::size_t slot = 0; slot < m_PickingSlotPending.size(); ++slot)
            {
                if (m_PickingSlotPending[slot])
                {
                    m_PickingSlotInvalidated[slot] = true;
                }
            }
            if (m_PickingSlotPending.size() < newSlotCount)
            {
                m_PickingSlotPending.resize(newSlotCount, false);
                m_PickingSlotIssuedFrame.resize(newSlotCount, 0u);
                m_PickingSlotRequest.resize(newSlotCount, PickPixelRequest{});
                m_PickingSlotInvalidated.resize(newSlotCount, false);
                m_PickingSlotSequence.resize(newSlotCount, 0u);
                m_PickingSlotDepthCopied.resize(newSlotCount, false);
            }

            // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
            // `Histogram.Readback` buffer. Allocated lazily so the buffer
            // survives `RebuildOperationalResources()` byte-identical when
            // `device.GetFramesInFlight()` is unchanged (same pattern picking
            // follows above). Sized for `kHistogramReadbackSlotBytes *
            // frames-in-flight` bytes (256 uint32 bins per slot, one slot
            // per in-flight frame). If the expected size differs from the
            // current allocation (e.g. the device reports a different
            // frames-in-flight after a swapchain rebuild), the lease is
            // dropped and re-created so the executor's `slot *
            // kHistogramReadbackSlotBytes` per-frame addressing never
            // overruns the buffer.
            const std::uint64_t histogramReadbackBytes =
                kHistogramReadbackSlotBytes *
                static_cast<std::uint64_t>(pickingFramesInFlight);
            const bool histogramReadbackNeedsAllocation =
                !m_HistogramReadbackBuffer.has_value() ||
                !m_HistogramReadbackBuffer->IsValid() ||
                m_HistogramReadbackBufferSize != histogramReadbackBytes;
            if (histogramReadbackNeedsAllocation)
            {
                m_HistogramReadbackBuffer.reset();
                m_HistogramReadbackBufferSize = 0u;
                auto histogramReadbackOr = m_Subsystems.BufferManager()->Create({
                    .SizeBytes   = histogramReadbackBytes,
                    .Usage       = RHI::BufferUsage::TransferDst,
                    .HostVisible = true,
                    .DebugName   = "Renderer.HistogramReadback",
                });
                if (histogramReadbackOr.has_value())
                {
                    m_HistogramReadbackBuffer.emplace(std::move(*histogramReadbackOr));
                    m_HistogramReadbackBufferSize = histogramReadbackBytes;
                }
                else
                {
                    Core::Log::Warn("[Graphics] Histogram.Readback buffer unavailable; default-recipe histogram readback will be skipped: error={}",
                                    static_cast<int>(histogramReadbackOr.error()));
                }
            }

            // GRAPHICS-075 Slice E.2 — keep the per-slot histogram metadata
            // arrays sized to match the current `frames-in-flight` slot
            // count. Mirrors the picking-slot resize policy above: shrinking
            // the FIF discards trailing pending readbacks (they would never
            // be drained naturally since the new slot indexing addresses a
            // strictly smaller range), and any still-pending slots are
            // flagged `Invalidated=true` so the upcoming `BeginFrame()`
            // drain skips the publish for slots whose pre-rebuild copy is
            // no longer trustworthy.
            if (m_HistogramSlotPending.size() > newSlotCount)
            {
                m_HistogramSlotPending.resize(newSlotCount);
                m_HistogramSlotIssuedFrame.resize(newSlotCount);
                m_HistogramSlotInvalidated.resize(newSlotCount);
            }
            for (std::size_t slot = 0; slot < m_HistogramSlotPending.size(); ++slot)
            {
                if (m_HistogramSlotPending[slot])
                {
                    m_HistogramSlotInvalidated[slot] = true;
                }
            }
            if (m_HistogramSlotPending.size() < newSlotCount)
            {
                m_HistogramSlotPending.resize(newSlotCount, false);
                m_HistogramSlotIssuedFrame.resize(newSlotCount, 0u);
                m_HistogramSlotInvalidated.resize(newSlotCount, false);
            }

            // GRAPHICS-074 Slice A — EntityId selection pipeline. Same
            // reset/republish pattern as the forward, shadow, and deferred
            // pipelines so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` rather than retaining a stale device handle
            // across rebuilds. Slices B/C add the Face/Edge/Point + outline
            // pipelines; Slice D allocates the `Picking.Readback` buffer.
            m_SelectionEntityIdPipelineLease.reset();
            if (m_SelectionEntityIdPass)
            {
                m_SelectionEntityIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionEntityIdDesc = BuildSelectionEntityIdPipelineDesc();
            auto selectionEntityIdPipeline = m_Subsystems.PipelineManager()->Create(selectionEntityIdDesc);
            if (selectionEntityIdPipeline.has_value())
            {
                m_SelectionEntityIdPipelineLease.emplace(std::move(*selectionEntityIdPipeline));
                if (m_SelectionEntityIdPass)
                {
                    m_SelectionEntityIdPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionEntityIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionEntityId pipeline unavailable; default-recipe picking recording will be skipped: error={}",
                                static_cast<int>(selectionEntityIdPipeline.error()));
            }

            // GRAPHICS-074 Slice B — Face / Edge / Point selection ID
            // pipelines. Same reset/republish pattern as the EntityId
            // pipeline above so a failed `Create()` leaves the matching
            // pass in `SkippedUnavailable` rather than retaining a stale
            // device handle across rebuilds.
            m_SelectionFaceIdPipelineLease.reset();
            if (m_SelectionFaceIdPass)
            {
                m_SelectionFaceIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionFaceIdDesc = BuildSelectionFaceIdPipelineDesc();
            auto selectionFaceIdPipeline = m_Subsystems.PipelineManager()->Create(selectionFaceIdDesc);
            if (selectionFaceIdPipeline.has_value())
            {
                m_SelectionFaceIdPipelineLease.emplace(std::move(*selectionFaceIdPipeline));
                if (m_SelectionFaceIdPass)
                {
                    m_SelectionFaceIdPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionFaceIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionFaceId pipeline unavailable; default-recipe face picking recording will be skipped: error={}",
                                static_cast<int>(selectionFaceIdPipeline.error()));
            }

            m_SelectionEdgeIdPipelineLease.reset();
            if (m_SelectionEdgeIdPass)
            {
                m_SelectionEdgeIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionEdgeIdDesc = BuildSelectionEdgeIdPipelineDesc();
            auto selectionEdgeIdPipeline = m_Subsystems.PipelineManager()->Create(selectionEdgeIdDesc);
            if (selectionEdgeIdPipeline.has_value())
            {
                m_SelectionEdgeIdPipelineLease.emplace(std::move(*selectionEdgeIdPipeline));
                if (m_SelectionEdgeIdPass)
                {
                    m_SelectionEdgeIdPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionEdgeIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionEdgeId pipeline unavailable; default-recipe edge picking recording will be skipped: error={}",
                                static_cast<int>(selectionEdgeIdPipeline.error()));
            }

            m_SelectionPointIdPipelineLease.reset();
            if (m_SelectionPointIdPass)
            {
                m_SelectionPointIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionPointIdDesc = BuildSelectionPointIdPipelineDesc();
            auto selectionPointIdPipeline = m_Subsystems.PipelineManager()->Create(selectionPointIdDesc);
            if (selectionPointIdPipeline.has_value())
            {
                m_SelectionPointIdPipelineLease.emplace(std::move(*selectionPointIdPipeline));
                if (m_SelectionPointIdPass)
                {
                    m_SelectionPointIdPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionPointIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionPointId pipeline unavailable; default-recipe point picking recording will be skipped: error={}",
                                static_cast<int>(selectionPointIdPipeline.error()));
            }

            // GRAPHICS-074 Slice C — selection outline pipeline. Same reset/
            // republish pattern as the four selection-ID pipelines above so a
            // failed `Create()` leaves the outline pass in `SkippedUnavailable`
            // rather than retaining a stale device handle across rebuilds.
            m_SelectionOutlinePipelineLease.reset();
            if (m_SelectionOutlinePass)
            {
                m_SelectionOutlinePass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionOutlineDesc =
                BuildSelectionOutlinePipelineDesc(m_BackbufferFormat);
            auto selectionOutlinePipeline = m_Subsystems.PipelineManager()->Create(selectionOutlineDesc);
            if (selectionOutlinePipeline.has_value())
            {
                m_SelectionOutlinePipelineLease.emplace(std::move(*selectionOutlinePipeline));
                if (m_SelectionOutlinePass)
                {
                    m_SelectionOutlinePass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_SelectionOutlinePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionOutline pipeline unavailable; default-recipe selection outline recording will be skipped: error={}",
                                static_cast<int>(selectionOutlinePipeline.error()));
            }

            // GRAPHICS-075 Slice A — postprocess tonemap pipeline. Same
            // reset/republish pattern as the selection-outline pipeline
            // above so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` rather than retaining a stale device
            // handle across rebuilds. Bloom/Histogram/FXAA/SMAA pipelines
            // arrive with Slices B–E behind the same umbrella executor
            // branch.
            m_PostProcessToneMapPipelineLease.reset();
            if (m_PostProcessToneMapPass)
            {
                m_PostProcessToneMapPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessToneMapDesc =
                BuildPostProcessToneMapPipelineDesc(m_BackbufferFormat);
            auto postProcessToneMapPipeline = m_Subsystems.PipelineManager()->Create(postProcessToneMapDesc);
            if (postProcessToneMapPipeline.has_value())
            {
                m_PostProcessToneMapPipelineLease.emplace(std::move(*postProcessToneMapPipeline));
                if (m_PostProcessToneMapPass)
                {
                    m_PostProcessToneMapPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessToneMapPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.ToneMap pipeline unavailable; default-recipe tonemap recording will be skipped: error={}",
                                static_cast<int>(postProcessToneMapPipeline.error()));
            }

            // GRAPHICS-075 Slice B.1 — postprocess bloom downsample +
            // upsample pipelines. Same reset/republish pattern as the
            // tonemap pipeline above so a failed `Create()` leaves the
            // bloom pass in `SkippedUnavailable` (per the independent
            // early-skips in `PostProcessBloomPass::Execute`) rather than
            // retaining a stale device handle across rebuilds. Slice B.2
            // keeps both pipelines and adds per-mip iteration on top.
            m_PostProcessBloomDownsamplePipelineLease.reset();
            m_PostProcessBloomUpsamplePipelineLease.reset();
            if (m_PostProcessBloomPass)
            {
                m_PostProcessBloomPass->SetDownsamplePipeline(RHI::PipelineHandle{});
                m_PostProcessBloomPass->SetUpsamplePipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessBloomDownsampleDesc =
                BuildPostProcessBloomDownsamplePipelineDesc();
            auto postProcessBloomDownsamplePipeline = m_Subsystems.PipelineManager()->Create(postProcessBloomDownsampleDesc);
            if (postProcessBloomDownsamplePipeline.has_value())
            {
                m_PostProcessBloomDownsamplePipelineLease.emplace(std::move(*postProcessBloomDownsamplePipeline));
                if (m_PostProcessBloomPass)
                {
                    m_PostProcessBloomPass->SetDownsamplePipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessBloomDownsamplePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.Bloom.Downsample pipeline unavailable; default-recipe bloom downsample recording will be skipped: error={}",
                                static_cast<int>(postProcessBloomDownsamplePipeline.error()));
            }

            const RHI::PipelineDesc postProcessBloomUpsampleDesc =
                BuildPostProcessBloomUpsamplePipelineDesc();
            auto postProcessBloomUpsamplePipeline = m_Subsystems.PipelineManager()->Create(postProcessBloomUpsampleDesc);
            if (postProcessBloomUpsamplePipeline.has_value())
            {
                m_PostProcessBloomUpsamplePipelineLease.emplace(std::move(*postProcessBloomUpsamplePipeline));
                if (m_PostProcessBloomPass)
                {
                    m_PostProcessBloomPass->SetUpsamplePipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessBloomUpsamplePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.Bloom.Upsample pipeline unavailable; default-recipe bloom upsample recording will be skipped: error={}",
                                static_cast<int>(postProcessBloomUpsamplePipeline.error()));
            }

            // GRAPHICS-075 Slice C — postprocess FXAA pipeline. Same
            // reset/republish pattern as the tonemap + bloom pipelines
            // above so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` (per the early-skip inside
            // `PostProcessFXAAPass::Execute` and the umbrella helper's
            // `IsValid()` gate) rather than retaining a stale device
            // handle across rebuilds. The pipeline targets the recipe's
            // LDR backbuffer format so it stays render-pass-compatible
            // with the tonemap leg's output target.
            m_PostProcessFXAAPipelineLease.reset();
            if (m_PostProcessFXAAPass)
            {
                m_PostProcessFXAAPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessFXAADesc =
                BuildPostProcessFXAAPipelineDesc(m_BackbufferFormat);
            auto postProcessFXAAPipeline = m_Subsystems.PipelineManager()->Create(postProcessFXAADesc);
            if (postProcessFXAAPipeline.has_value())
            {
                m_PostProcessFXAAPipelineLease.emplace(std::move(*postProcessFXAAPipeline));
                if (m_PostProcessFXAAPass)
                {
                    m_PostProcessFXAAPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessFXAAPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.FXAA pipeline unavailable; default-recipe FXAA recording will be skipped: error={}",
                                static_cast<int>(postProcessFXAAPipeline.error()));
            }

            // GRAPHICS-075 Slice D.1 — postprocess SMAA pipelines (edge,
            // blend, resolve). Same reset/republish pattern as the tonemap
            // + bloom + FXAA pipelines above so a failed `Create()` on any
            // stage leaves that stage's bind/push/draw silenced inside
            // `PostProcessSMAAPass::Execute` rather than retaining a stale
            // device handle across rebuilds. The pass-side `IsValid()`
            // gate inside `PostProcessSMAAPass::Execute` independently
            // short-circuits each stage's bind/push/draw on its own lease
            // validity, mirroring the bloom helper's per-stage early-skip
            // — a partial outage (e.g. only the resolve shader compiles)
            // still records the surviving stages. The umbrella helper
            // requires *at least one* valid SMAA lease to proceed so a
            // complete-shader-outage downgrades the helper to
            // `SkippedUnavailable` rather than reporting a Recorded
            // no-op against missing pipelines.
            m_PostProcessSMAAEdgePipelineLease.reset();
            m_PostProcessSMAABlendPipelineLease.reset();
            m_PostProcessSMAAResolvePipelineLease.reset();
            if (m_PostProcessSMAAPass)
            {
                m_PostProcessSMAAPass->SetEdgePipeline(RHI::PipelineHandle{});
                m_PostProcessSMAAPass->SetBlendPipeline(RHI::PipelineHandle{});
                m_PostProcessSMAAPass->SetResolvePipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessSMAAEdgeDesc =
                BuildPostProcessSMAAEdgePipelineDesc();
            auto postProcessSMAAEdgePipeline = m_Subsystems.PipelineManager()->Create(postProcessSMAAEdgeDesc);
            if (postProcessSMAAEdgePipeline.has_value())
            {
                m_PostProcessSMAAEdgePipelineLease.emplace(std::move(*postProcessSMAAEdgePipeline));
                if (m_PostProcessSMAAPass)
                {
                    m_PostProcessSMAAPass->SetEdgePipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessSMAAEdgePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.SMAA.Edge pipeline unavailable; default-recipe SMAA edge recording will be skipped: error={}",
                                static_cast<int>(postProcessSMAAEdgePipeline.error()));
            }
            const RHI::PipelineDesc postProcessSMAABlendDesc =
                BuildPostProcessSMAABlendPipelineDesc();
            auto postProcessSMAABlendPipeline = m_Subsystems.PipelineManager()->Create(postProcessSMAABlendDesc);
            if (postProcessSMAABlendPipeline.has_value())
            {
                m_PostProcessSMAABlendPipelineLease.emplace(std::move(*postProcessSMAABlendPipeline));
                if (m_PostProcessSMAAPass)
                {
                    m_PostProcessSMAAPass->SetBlendPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessSMAABlendPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.SMAA.Blend pipeline unavailable; default-recipe SMAA blend recording will be skipped: error={}",
                                static_cast<int>(postProcessSMAABlendPipeline.error()));
            }
            const RHI::PipelineDesc postProcessSMAAResolveDesc =
                BuildPostProcessSMAAResolvePipelineDesc(m_BackbufferFormat);
            auto postProcessSMAAResolvePipeline = m_Subsystems.PipelineManager()->Create(postProcessSMAAResolveDesc);
            if (postProcessSMAAResolvePipeline.has_value())
            {
                m_PostProcessSMAAResolvePipelineLease.emplace(std::move(*postProcessSMAAResolvePipeline));
                if (m_PostProcessSMAAPass)
                {
                    m_PostProcessSMAAPass->SetResolvePipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessSMAAResolvePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.SMAA.Resolve pipeline unavailable; default-recipe SMAA resolve recording will be skipped: error={}",
                                static_cast<int>(postProcessSMAAResolvePipeline.error()));
            }

            // GRAPHICS-075 Slice E.1 — postprocess histogram compute
            // pipeline. Same reset/republish pattern as the tonemap +
            // bloom + FXAA + SMAA leases above so a failed `Create()`
            // leaves the histogram helper in `SkippedUnavailable`
            // rather than retaining a stale device handle across
            // rebuilds.
            m_PostProcessHistogramPipelineLease.reset();
            if (m_PostProcessHistogramPass)
            {
                m_PostProcessHistogramPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessHistogramDesc =
                BuildPostProcessHistogramPipelineDesc();
            auto postProcessHistogramPipeline = m_Subsystems.PipelineManager()->Create(postProcessHistogramDesc);
            if (postProcessHistogramPipeline.has_value())
            {
                m_PostProcessHistogramPipelineLease.emplace(std::move(*postProcessHistogramPipeline));
                if (m_PostProcessHistogramPass)
                {
                    m_PostProcessHistogramPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_PostProcessHistogramPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.Histogram pipeline unavailable; default-recipe histogram recording will be skipped: error={}",
                                static_cast<int>(postProcessHistogramPipeline.error()));
            }

            // GRAPHICS-076 Slice A — canonical default-recipe present
            // pipeline. Created after postprocess so the test fixtures that
            // target `FailPipelineCreateCall` against specific upstream
            // pipelines (culling=1, depth=2, defaultDebugSurface=3,
            // forward/shadow/deferred at 4-9, selection at 10-14,
            // postprocess at 15-22) keep their documented call indices
            // explicit. The present slot is call #23. Same reset/republish
            // + fail-closed pattern as
            // the other leases above so a failed `Create()` leaves
            // `m_PresentPass` in the fail-closed state that
            // `RecordPresentPass` interprets as `SkippedUnavailable`.
            m_PresentPipelineLease.reset();
            m_PresentPass.SetPipeline(RHI::PipelineHandle{});
            const RHI::PipelineDesc presentDesc =
                BuildPresentPipelineDesc(m_BackbufferFormat);
            auto presentPipeline = m_Subsystems.PipelineManager()->Create(presentDesc);
            if (presentPipeline.has_value())
            {
                m_PresentPipelineLease.emplace(std::move(*presentPipeline));
                m_PresentPass.SetPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(m_PresentPipelineLease->GetHandle()));
            }
            else
            {
                Core::Log::Warn("[Graphics] Present pipeline unavailable; default-recipe present recording will be skipped: error={}",
                                static_cast<int>(presentPipeline.error()));
            }

            // GRAPHICS-076 Slice B — canonical default-recipe `Pass.DebugView`
            // pipeline. Created after present so the test fixtures that
            // target `FailPipelineCreateCall` against specific upstream
            // pipelines (culling=1, depth=2, defaultDebugSurface=3,
            // forward/shadow/deferred at 4-9, selection at 10-14,
            // postprocess at 15-22, present=23) keep their documented call
            // indices explicit. The DebugView slot is call #24. Same
            // reset/republish + fail-closed pattern as
            // the other leases above so a failed `Create()` leaves
            // `m_DebugViewPass` in the fail-closed state that
            // `RecordDebugViewPass` interprets as `SkippedUnavailable`.
            m_DebugViewPipelineLease.reset();
            if (m_DebugViewPass)
            {
                m_DebugViewPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc debugViewDesc = BuildDebugViewPipelineDesc();
            auto debugViewPipeline = m_Subsystems.PipelineManager()->Create(debugViewDesc);
            if (debugViewPipeline.has_value())
            {
                m_DebugViewPipelineLease.emplace(std::move(*debugViewPipeline));
                if (m_DebugViewPass)
                {
                    m_DebugViewPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_DebugViewPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] DebugView pipeline unavailable; default-recipe debug-view recording will be skipped: error={}",
                                static_cast<int>(debugViewPipeline.error()));
            }

            // GRAPHICS-077 Slices B + C — transient-debug pipelines.
            // Created LAST so contract-test fixtures targeting
            // `FailPipelineCreateCall` against specific upstream
            // pipelines keep their documented call indices unchanged
            // (culling=1, depth=2, defaultDebugSurface=3,
            // forward/shadow/deferred=4-9, selection=10-14,
            // postprocess=15-22, present=23, debugView=24). Slice B
            // introduced the triangle DepthTested slot at call #25 and the
            // triangle AlwaysOnTop slot at call #26. Slice C appends the line
            // DepthTested slot at call #27, the line AlwaysOnTop slot at call
            // #28, the point DepthTested slot at call #29, and the point
            // AlwaysOnTop slot at call #30 — keeping
            // the earlier call indices stable so the Slice B contract
            // tests that pin `FailPipelineCreateCall = 25` still
            // exercise the triangle DepthTested gate. Same
            // reset/republish + fail-closed pattern as the other
            // leases above so a failed `Create()` leaves the pass in
            // the fail-closed state that
            // `RecordTransientDebugSurfacePass` interprets as
            // `SkippedUnavailable` for that lane
            // (`MissingPipelineSkipCount` then increments on the
            // operational-no-pipeline path).
            m_TransientDebugTrianglePipelineLeaseDepthTested.reset();
            m_TransientDebugTrianglePipelineLeaseAlwaysOnTop.reset();
            m_TransientDebugLinePipelineLeaseDepthTested.reset();
            m_TransientDebugLinePipelineLeaseAlwaysOnTop.reset();
            m_TransientDebugPointPipelineLeaseDepthTested.reset();
            m_TransientDebugPointPipelineLeaseAlwaysOnTop.reset();
            // GRAPHICS-078 Slices B + C — visualization-overlay pipelines.
            // Created after the GRAPHICS-077 point-lane pipelines so call
            // indices stay stable: vector-field DepthTested at call #31
            // (immediately after point AlwaysOnTop at #30), vector-field
            // AlwaysOnTop at #32, isoline DepthTested at #33, isoline
            // AlwaysOnTop at #34.
            m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested.reset();
            m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop.reset();
            m_VisualizationOverlayIsolinePipelineLeaseDepthTested.reset();
            m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop.reset();
            m_TransientDebugSurfacePass.SetTriangleDepthTestedPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetTriangleAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetLineDepthTestedPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetLineAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetPointDepthTestedPipeline(RHI::PipelineHandle{});
            m_TransientDebugSurfacePass.SetPointAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetVectorFieldDepthTestedPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetVectorFieldAlwaysOnTopPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetIsolineDepthTestedPipeline(RHI::PipelineHandle{});
            m_VisualizationOverlayPass.SetIsolineAlwaysOnTopPipeline(RHI::PipelineHandle{});

            const RHI::PipelineDesc triangleDepthTestedDesc =
                BuildTransientDebugTrianglePipelineDesc(true);
            auto triangleDepthTestedPipeline = m_Subsystems.PipelineManager()->Create(triangleDepthTestedDesc);
            if (triangleDepthTestedPipeline.has_value())
            {
                m_TransientDebugTrianglePipelineLeaseDepthTested.emplace(
                    std::move(*triangleDepthTestedPipeline));
                m_TransientDebugSurfacePass.SetTriangleDepthTestedPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_TransientDebugTrianglePipelineLeaseDepthTested->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] TransientDebug.Triangle.DepthTested pipeline unavailable; "
                    "default-recipe transient-debug recording will be skipped: error={}",
                    static_cast<int>(triangleDepthTestedPipeline.error()));
            }

            const RHI::PipelineDesc triangleAlwaysOnTopDesc =
                BuildTransientDebugTrianglePipelineDesc(false);
            auto triangleAlwaysOnTopPipeline = m_Subsystems.PipelineManager()->Create(triangleAlwaysOnTopDesc);
            if (triangleAlwaysOnTopPipeline.has_value())
            {
                m_TransientDebugTrianglePipelineLeaseAlwaysOnTop.emplace(
                    std::move(*triangleAlwaysOnTopPipeline));
                m_TransientDebugSurfacePass.SetTriangleAlwaysOnTopPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_TransientDebugTrianglePipelineLeaseAlwaysOnTop->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] TransientDebug.Triangle.AlwaysOnTop pipeline unavailable; "
                    "default-recipe transient-debug recording will be skipped: error={}",
                    static_cast<int>(triangleAlwaysOnTopPipeline.error()));
            }

            const RHI::PipelineDesc lineDepthTestedDesc =
                BuildTransientDebugLinePipelineDesc(true);
            auto lineDepthTestedPipeline = m_Subsystems.PipelineManager()->Create(lineDepthTestedDesc);
            if (lineDepthTestedPipeline.has_value())
            {
                m_TransientDebugLinePipelineLeaseDepthTested.emplace(
                    std::move(*lineDepthTestedPipeline));
                m_TransientDebugSurfacePass.SetLineDepthTestedPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_TransientDebugLinePipelineLeaseDepthTested->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] TransientDebug.Line.DepthTested pipeline unavailable; "
                    "default-recipe transient-debug recording will be skipped: error={}",
                    static_cast<int>(lineDepthTestedPipeline.error()));
            }

            const RHI::PipelineDesc lineAlwaysOnTopDesc =
                BuildTransientDebugLinePipelineDesc(false);
            auto lineAlwaysOnTopPipeline = m_Subsystems.PipelineManager()->Create(lineAlwaysOnTopDesc);
            if (lineAlwaysOnTopPipeline.has_value())
            {
                m_TransientDebugLinePipelineLeaseAlwaysOnTop.emplace(
                    std::move(*lineAlwaysOnTopPipeline));
                m_TransientDebugSurfacePass.SetLineAlwaysOnTopPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_TransientDebugLinePipelineLeaseAlwaysOnTop->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] TransientDebug.Line.AlwaysOnTop pipeline unavailable; "
                    "default-recipe transient-debug recording will be skipped: error={}",
                    static_cast<int>(lineAlwaysOnTopPipeline.error()));
            }

            const RHI::PipelineDesc pointDepthTestedDesc =
                BuildTransientDebugPointPipelineDesc(true);
            auto pointDepthTestedPipeline = m_Subsystems.PipelineManager()->Create(pointDepthTestedDesc);
            if (pointDepthTestedPipeline.has_value())
            {
                m_TransientDebugPointPipelineLeaseDepthTested.emplace(
                    std::move(*pointDepthTestedPipeline));
                m_TransientDebugSurfacePass.SetPointDepthTestedPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_TransientDebugPointPipelineLeaseDepthTested->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] TransientDebug.Point.DepthTested pipeline unavailable; "
                    "default-recipe transient-debug recording will be skipped: error={}",
                    static_cast<int>(pointDepthTestedPipeline.error()));
            }

            const RHI::PipelineDesc pointAlwaysOnTopDesc =
                BuildTransientDebugPointPipelineDesc(false);
            auto pointAlwaysOnTopPipeline = m_Subsystems.PipelineManager()->Create(pointAlwaysOnTopDesc);
            if (pointAlwaysOnTopPipeline.has_value())
            {
                m_TransientDebugPointPipelineLeaseAlwaysOnTop.emplace(
                    std::move(*pointAlwaysOnTopPipeline));
                m_TransientDebugSurfacePass.SetPointAlwaysOnTopPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_TransientDebugPointPipelineLeaseAlwaysOnTop->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] TransientDebug.Point.AlwaysOnTop pipeline unavailable; "
                    "default-recipe transient-debug recording will be skipped: error={}",
                    static_cast<int>(pointAlwaysOnTopPipeline.error()));
            }

            // GRAPHICS-078 Slice B — visualization-overlay vector-
            // field pipelines (call indices #31 + #32). Same
            // reset/republish + fail-closed pattern as the transient-
            // debug pipelines above so a failed `Create()` leaves
            // `m_VisualizationOverlayPass` in the fail-closed state
            // that `RecordVisualizationOverlayPass` interprets as
            // `SkippedUnavailable` for the vector-field lane (with
            // `MissingPipelineSkipCount += 1` on the operational-no-
            // pipeline path).
            const RHI::PipelineDesc vectorFieldDepthTestedDesc =
                BuildVisualizationVectorFieldPipelineDesc(true);
            auto vectorFieldDepthTestedPipeline =
                m_Subsystems.PipelineManager()->Create(vectorFieldDepthTestedDesc);
            if (vectorFieldDepthTestedPipeline.has_value())
            {
                m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested.emplace(
                    std::move(*vectorFieldDepthTestedPipeline));
                m_VisualizationOverlayPass.SetVectorFieldDepthTestedPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] VisualizationOverlay.VectorField.DepthTested pipeline unavailable; "
                    "default-recipe visualization-overlay recording will be skipped: error={}",
                    static_cast<int>(vectorFieldDepthTestedPipeline.error()));
            }

            const RHI::PipelineDesc vectorFieldAlwaysOnTopDesc =
                BuildVisualizationVectorFieldPipelineDesc(false);
            auto vectorFieldAlwaysOnTopPipeline =
                m_Subsystems.PipelineManager()->Create(vectorFieldAlwaysOnTopDesc);
            if (vectorFieldAlwaysOnTopPipeline.has_value())
            {
                m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop.emplace(
                    std::move(*vectorFieldAlwaysOnTopPipeline));
                m_VisualizationOverlayPass.SetVectorFieldAlwaysOnTopPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] VisualizationOverlay.VectorField.AlwaysOnTop pipeline unavailable; "
                    "default-recipe visualization-overlay recording will be skipped: error={}",
                    static_cast<int>(vectorFieldAlwaysOnTopPipeline.error()));
            }

            // GRAPHICS-078 Slice C — visualization-overlay isoline
            // pipelines (call indices #33 + #34). Same reset/republish
            // + fail-closed pattern as the vector-field pipelines above
            // so a failed `Create()` leaves the isoline lane's accessors
            // at `RHI::PipelineHandle{}`; `RecordVisualizationOverlayPass`
            // gates the lane independently from the vector-field lane,
            // increments `MissingPipelineSkipCount` on the operational-
            // no-pipeline path, and keeps the sibling vector-field lane
            // recording when its own pipelines are healthy.
            const RHI::PipelineDesc isolineDepthTestedDesc =
                BuildVisualizationIsolinePipelineDesc(true);
            auto isolineDepthTestedPipeline =
                m_Subsystems.PipelineManager()->Create(isolineDepthTestedDesc);
            if (isolineDepthTestedPipeline.has_value())
            {
                m_VisualizationOverlayIsolinePipelineLeaseDepthTested.emplace(
                    std::move(*isolineDepthTestedPipeline));
                m_VisualizationOverlayPass.SetIsolineDepthTestedPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_VisualizationOverlayIsolinePipelineLeaseDepthTested->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] VisualizationOverlay.Isoline.DepthTested pipeline unavailable; "
                    "default-recipe visualization-overlay recording will be skipped: error={}",
                    static_cast<int>(isolineDepthTestedPipeline.error()));
            }

            const RHI::PipelineDesc isolineAlwaysOnTopDesc =
                BuildVisualizationIsolinePipelineDesc(false);
            auto isolineAlwaysOnTopPipeline =
                m_Subsystems.PipelineManager()->Create(isolineAlwaysOnTopDesc);
            if (isolineAlwaysOnTopPipeline.has_value())
            {
                m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop.emplace(
                    std::move(*isolineAlwaysOnTopPipeline));
                m_VisualizationOverlayPass.SetIsolineAlwaysOnTopPipeline(
                    m_Subsystems.PipelineManager()->GetDeviceHandle(
                        m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop->GetHandle()));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] VisualizationOverlay.Isoline.AlwaysOnTop pipeline unavailable; "
                    "default-recipe visualization-overlay recording will be skipped: error={}",
                    static_cast<int>(isolineAlwaysOnTopPipeline.error()));
            }

            // GRAPHICS-038B — HZB build compute pipeline. Appended after the
            // GRAPHICS-077/078 visualization pipeline lanes so all existing
            // `FailPipelineCreateCall` indices through #34 remain stable. Same
            // reset/republish + fail-closed pattern as the other default-recipe
            // compute pipelines: a failed create leaves `"HZBBuildPass"` in
            // `SkippedUnavailable` rather than retaining a stale device handle.
            m_HZBBuildPipelineLease.reset();
            const RHI::PipelineDesc hzbBuildDesc = BuildHZBBuildPipelineDesc();
            auto hzbBuildPipeline = m_Subsystems.PipelineManager()->Create(hzbBuildDesc);
            if (hzbBuildPipeline.has_value())
            {
                m_HZBBuildPipelineLease.emplace(std::move(*hzbBuildPipeline));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] HZB.Build pipeline unavailable; default-recipe HZB build recording will be skipped: error={}",
                    static_cast<int>(hzbBuildPipeline.error()));
            }

            // GRAPHICS-039C — clustered-light compute pipelines. Appended
            // after HZB.Build so existing `FailPipelineCreateCall` indices
            // through the HZB slot remain stable. Missing leases leave the
            // corresponding cluster graph passes in `SkippedUnavailable`.
            m_ClusterGridBuildPipelineLease.reset();
            const RHI::PipelineDesc clusterGridBuildDesc =
                BuildClusterGridBuildPipelineDesc();
            auto clusterGridBuildPipeline = m_Subsystems.PipelineManager()->Create(clusterGridBuildDesc);
            if (clusterGridBuildPipeline.has_value())
            {
                m_ClusterGridBuildPipelineLease.emplace(std::move(*clusterGridBuildPipeline));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] ClusterGrid.Build pipeline unavailable; default-recipe cluster-grid recording will be skipped: error={}",
                    static_cast<int>(clusterGridBuildPipeline.error()));
            }

            m_ClusterLightAssignmentPipelineLease.reset();
            const RHI::PipelineDesc clusterLightAssignmentDesc =
                BuildClusterLightAssignmentPipelineDesc();
            auto clusterLightAssignmentPipeline =
                m_Subsystems.PipelineManager()->Create(clusterLightAssignmentDesc);
            if (clusterLightAssignmentPipeline.has_value())
            {
                m_ClusterLightAssignmentPipelineLease.emplace(
                    std::move(*clusterLightAssignmentPipeline));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] ClusterLights.Assign pipeline unavailable; default-recipe light-assignment recording will be skipped: error={}",
                    static_cast<int>(clusterLightAssignmentPipeline.error()));
            }

            // GRAPHICS-079 Slice A — canonical default-recipe `Pass.ImGui`
            // pipeline. Created after HZB and the clustered-light compute
            // pipelines. Same reset/republish + fail-closed pattern as the
            // present/debug-view leases so a failed `Create()` leaves
            // `m_ImGuiPass` (when attached) in the fail-closed state that
            // `RecordImGuiPass` interprets as `SkippedUnavailable`.
            // The pass is only attached once the runtime hands in the overlay
            // system via `SetImGuiOverlaySystem`, so the lease may exist before
            // the pass does.
            m_ImGuiPipelineLease.reset();
            m_ImGuiRgba8PipelineLease.reset();
            if (m_ImGuiPass)
            {
                m_ImGuiPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc imguiDesc = BuildImGuiPipelineDesc(m_BackbufferFormat);
            auto imguiPipeline = m_Subsystems.PipelineManager()->Create(imguiDesc);
            if (imguiPipeline.has_value())
            {
                m_ImGuiPipelineLease.emplace(std::move(*imguiPipeline));
                if (m_ImGuiPass)
                {
                    m_ImGuiPass->SetPipeline(
                        m_Subsystems.PipelineManager()->GetDeviceHandle(m_ImGuiPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ImGui pipeline unavailable; default-recipe ImGui recording will be skipped: error={}",
                                static_cast<int>(imguiPipeline.error()));
            }
            if (m_BackbufferFormat != RHI::Format::RGBA8_UNORM)
            {
                const RHI::PipelineDesc imguiRgba8Desc =
                    BuildImGuiPipelineDesc(RHI::Format::RGBA8_UNORM);
                auto imguiRgba8Pipeline = m_Subsystems.PipelineManager()->Create(imguiRgba8Desc);
                if (imguiRgba8Pipeline.has_value())
                {
                    m_ImGuiRgba8PipelineLease.emplace(std::move(*imguiRgba8Pipeline));
                }
                else
                {
                    Core::Log::Warn(
                        "[Graphics] RGBA8 ImGui pipeline unavailable; ImGui over DebugViewRGBA will be skipped: error={}",
                        static_cast<int>(imguiRgba8Pipeline.error()));
                }
            }

            // GRAPHICS-113 — one-target EntityId pipeline for outline-only
            // selected/hovered frames. Appended after all pre-existing
            // default-recipe pipeline slots so historical
            // `FailPipelineCreateCall` indices remain stable.
            m_SelectionEntityIdOutlinePipelineLease.reset();
            const RHI::PipelineDesc selectionEntityIdOutlineDesc =
                BuildSelectionEntityIdOutlinePipelineDesc();
            auto selectionEntityIdOutlinePipeline =
                m_Subsystems.PipelineManager()->Create(selectionEntityIdOutlineDesc);
            if (selectionEntityIdOutlinePipeline.has_value())
            {
                m_SelectionEntityIdOutlinePipelineLease.emplace(
                    std::move(*selectionEntityIdOutlinePipeline));
            }
            else
            {
                Core::Log::Warn(
                    "[Graphics] SelectionEntityId outline-only pipeline unavailable; "
                    "outline-only selection ID recording will be skipped: error={}",
                    static_cast<int>(selectionEntityIdOutlinePipeline.error()));
            }

            return m_CullingOutputAvailable && m_DepthPrepassPipelineLease.has_value() &&
                m_DepthPrepassPipelineLease->IsValid();
        }

        struct ActiveRenderPassDesc
        {
            std::vector<RHI::ColorAttachment> ColorAttachments{};
            RHI::DepthAttachment DepthAttachment{};
            RHI::Format FirstColorFormat = RHI::Format::Undefined;
            bool HasAttachments = false;
        };

        [[nodiscard]] static ActiveRenderPassDesc BuildActiveRenderPassDesc(
            const CompiledRenderGraph& compiled,
            const std::uint32_t passIndex)
        {
            ActiveRenderPassDesc out{};
            for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
            {
                if (attachment.PassIndex != passIndex || attachment.ResourceIndex >= compiled.TextureHandles.size())
                {
                    continue;
                }

                const RHI::TextureHandle texture = compiled.TextureHandles[attachment.ResourceIndex];
                if (!texture.IsValid())
                {
                    continue;
                }

                if (attachment.IsDepthAttachment)
                {
                    out.DepthAttachment = RHI::DepthAttachment{
                        .Target = texture,
                        .Load = attachment.Load,
                        .Store = attachment.Store,
                        .ClearDepth = 1.0f,
                    };
                    out.HasAttachments = true;
                    continue;
                }

                if (out.ColorAttachments.size() <= attachment.AttachmentIndex)
                {
                    out.ColorAttachments.resize(attachment.AttachmentIndex + 1u);
                }
                out.ColorAttachments[attachment.AttachmentIndex] = RHI::ColorAttachment{
                    .Target = texture,
                    .Load = attachment.Load,
                    .Store = attachment.Store,
                    // BUG-016: honor the recipe-declared clear color carried
                    // through compilation so the default-recipe scene clears to
                    // its configured blue background instead of black.
                    .ClearR = attachment.ClearR,
                    .ClearG = attachment.ClearG,
                    .ClearB = attachment.ClearB,
                    .ClearA = attachment.ClearA,
                };
                if (out.FirstColorFormat == RHI::Format::Undefined)
                {
                    out.FirstColorFormat = attachment.Format;
                }
                out.HasAttachments = true;
            }
            return out;
        }

        struct RenderCommandRouteContext
        {
            const RHI::CameraUBO* Camera = nullptr;
            const RHI::FrameHandle* Frame = nullptr;
            const RenderWorld* World = nullptr;
            const CompiledRenderGraph* Compiled = nullptr;
            const ActiveRenderPassDesc* ActiveRenderPass = nullptr;
            bool* RenderPassEnded = nullptr;
            bool DefaultRecipeUsesDeferred = false;
        };

        [[nodiscard]] static RenderCommandRouteContext& RouteContextFrom(void* context) noexcept;
        static void EndActiveRenderPassForRoute(RHI::ICommandContext& cmd,
                                                RenderCommandRouteContext& context);
        void RegisterCommandRoutes();
        void RecordPickingCommandRoute(const RenderCommandRoute& route,
                                       RHI::ICommandContext& cmd,
                                       RenderCommandRouteContext& context);
        void RecordPostProcessCommandRoute(const RenderCommandRoute& route,
                                           RHI::ICommandContext& cmd,
                                           RenderCommandRouteContext& context);
        void RecordPostProcessHistogramCommandRoute(const RenderCommandRoute& route,
                                                    RHI::ICommandContext& cmd,
                                                    RenderCommandRouteContext& context);

        [[nodiscard]] static bool CompatibleTextureDesc(const RHI::TextureDesc& lhs,
                                                        const RHI::TextureDesc& rhs) noexcept
        {
            return lhs.Width == rhs.Width && lhs.Height == rhs.Height &&
                lhs.DepthOrArrayLayers == rhs.DepthOrArrayLayers && lhs.MipLevels == rhs.MipLevels &&
                lhs.Fmt == rhs.Fmt && lhs.Dimension == rhs.Dimension && lhs.Usage == rhs.Usage &&
                lhs.InitialLayout == rhs.InitialLayout && lhs.SampleCount == rhs.SampleCount;
        }

        [[nodiscard]] static bool CompatibleBufferDesc(const RHI::BufferDesc& lhs,
                                                       const RHI::BufferDesc& rhs) noexcept
        {
            return lhs.SizeBytes == rhs.SizeBytes && lhs.Usage == rhs.Usage && lhs.HostVisible == rhs.HostVisible;
        }

        struct TransientMemoryBlockCache
        {
            RHI::MemoryBlockHandle Handle{};
            std::uint64_t SizeBytes = 0u;
            std::uint64_t AlignmentBytes = 1u;
            std::uint32_t MemoryTypeBits = 0u;
        };

        enum class PlacedTransientAllocationStatus
        {
            Succeeded,
            Unsupported,
            Failed,
        };

        [[nodiscard]] static bool HasValidTransientMemoryBlock(
            const std::vector<TransientMemoryBlockCache>& blocks) noexcept
        {
            return std::ranges::any_of(blocks, [](const TransientMemoryBlockCache& block) {
                return block.Handle.IsValid();
            });
        }

        [[nodiscard]] static bool CompatibleTransientMemoryBlocks(
            const std::vector<TransientMemoryBlockCache>& blocks,
            const RendererTransientPlacementPlan& plan) noexcept
        {
            if (plan.PeakBytes == 0u)
            {
                return !HasValidTransientMemoryBlock(blocks);
            }
            if (blocks.empty() || !blocks.front().Handle.IsValid())
            {
                return true;
            }
            return blocks.size() == 1u &&
                   blocks.front().SizeBytes == plan.PeakBytes &&
                   blocks.front().AlignmentBytes == plan.BlockAlignmentBytes &&
                   blocks.front().MemoryTypeBits == plan.MemoryTypeBits;
        }

        [[nodiscard]] static bool CompatiblePlacedResource(
            const RHI::PlacedResourceInfo& actual,
            const RHI::MemoryBlockHandle block,
            const TransientResourcePlacement& placement) noexcept
        {
            return actual.IsPlaced &&
                   actual.Block == block &&
                   actual.OffsetBytes == placement.OffsetBytes &&
                   actual.SizeBytes == placement.SizeBytes &&
                   actual.AlignmentBytes == placement.AlignmentBytes;
        }

        void ResizeFrameTransientSlot(const CompiledRenderGraph& compiled,
                                      const std::uint32_t slot)
        {
            m_FrameTransientTextures[slot].resize(compiled.TextureHandles.size());
            m_FrameTransientTextureDescs[slot].resize(compiled.TextureHandles.size());
            m_FrameTransientBuffers[slot].resize(compiled.BufferHandles.size());
            m_FrameTransientBufferDescs[slot].resize(compiled.BufferHandles.size());
        }

        void ReleaseFrameTransientResources(const std::uint32_t slot)
        {
            if (m_Device == nullptr)
            {
                return;
            }
            if (slot < m_FrameTransientTextures.size())
            {
                for (const RHI::TextureHandle handle : m_FrameTransientTextures[slot])
                {
                    if (handle.IsValid())
                    {
                        m_Device->DestroyTexture(handle);
                    }
                }
                m_FrameTransientTextures[slot].clear();
            }
            if (slot < m_FrameTransientBuffers.size())
            {
                for (const RHI::BufferHandle handle : m_FrameTransientBuffers[slot])
                {
                    if (handle.IsValid())
                    {
                        m_Device->DestroyBuffer(handle);
                    }
                }
                m_FrameTransientBuffers[slot].clear();
            }
            if (slot < m_FrameTransientTextureDescs.size())
            {
                m_FrameTransientTextureDescs[slot].clear();
            }
            if (slot < m_FrameTransientBufferDescs.size())
            {
                m_FrameTransientBufferDescs[slot].clear();
            }
            if (slot < m_FrameTransientTextureMemoryBlocks.size())
            {
                for (const TransientMemoryBlockCache& block : m_FrameTransientTextureMemoryBlocks[slot])
                {
                    if (block.Handle.IsValid())
                    {
                        m_Device->DestroyMemoryBlock(block.Handle);
                    }
                }
                m_FrameTransientTextureMemoryBlocks[slot].clear();
            }
            if (slot < m_FrameTransientBufferMemoryBlocks.size())
            {
                for (const TransientMemoryBlockCache& block : m_FrameTransientBufferMemoryBlocks[slot])
                {
                    if (block.Handle.IsValid())
                    {
                        m_Device->DestroyMemoryBlock(block.Handle);
                    }
                }
                m_FrameTransientBufferMemoryBlocks[slot].clear();
            }
        }

        void ReleaseAllFrameTransientResources()
        {
            for (std::uint32_t slot = 0; slot < m_FrameTransientTextures.size(); ++slot)
            {
                ReleaseFrameTransientResources(slot);
            }
            m_FrameTransientTextures.clear();
            m_FrameTransientBuffers.clear();
            m_FrameTransientTextureDescs.clear();
            m_FrameTransientBufferDescs.clear();
            m_FrameTransientTextureMemoryBlocks.clear();
            m_FrameTransientBufferMemoryBlocks.clear();
        }

        [[nodiscard]] bool SlotHasPlacedTransientMemoryBlocks(const std::uint32_t slot) const noexcept
        {
            return (slot < m_FrameTransientTextureMemoryBlocks.size() &&
                    HasValidTransientMemoryBlock(m_FrameTransientTextureMemoryBlocks[slot])) ||
                   (slot < m_FrameTransientBufferMemoryBlocks.size() &&
                    HasValidTransientMemoryBlock(m_FrameTransientBufferMemoryBlocks[slot]));
        }

        [[nodiscard]] std::optional<RendererTransientPlacementPlan> BuildTexturePlacementPlanForDevice(
            const CompiledRenderGraph& compiled,
            const bool aliasingEnabled) const
        {
            if (m_Device == nullptr)
            {
                return std::nullopt;
            }

            std::vector<RendererTransientPlacementItem> items{};
            items.reserve(compiled.TextureTransientPlacements.size());
            for (const TransientResourcePlacement& placement : compiled.TextureTransientPlacements)
            {
                const TextureResourceDesc* desc = m_RenderGraph.GetTextureDescByIndex(placement.ResourceIndex);
                if (desc == nullptr)
                {
                    return std::nullopt;
                }
                const RHI::ResourceMemoryRequirements requirements =
                    m_Device->GetTextureMemoryRequirements(desc->Desc);
                if (!requirements.IsValid() || requirements.DedicatedAllocationRequired)
                {
                    return std::nullopt;
                }
                items.push_back(RendererTransientPlacementItem{
                    .ResourceIndex = placement.ResourceIndex,
                    .FirstUsePass = placement.FirstUsePass,
                    .LastUsePass = placement.LastUsePass,
                    .Requirements = requirements,
                });
            }

            RendererTransientPlacementPlan plan =
                BuildRendererTransientPlacementPlan(std::move(items), aliasingEnabled);
            if (!plan.IsValid)
            {
                return std::nullopt;
            }
            return plan;
        }

        [[nodiscard]] std::optional<RendererTransientPlacementPlan> BuildBufferPlacementPlanForDevice(
            const CompiledRenderGraph& compiled,
            const bool aliasingEnabled) const
        {
            if (m_Device == nullptr)
            {
                return std::nullopt;
            }

            std::vector<RendererTransientPlacementItem> items{};
            items.reserve(compiled.BufferTransientPlacements.size());
            for (const TransientResourcePlacement& placement : compiled.BufferTransientPlacements)
            {
                const BufferResourceDesc* desc = m_RenderGraph.GetBufferDescByIndex(placement.ResourceIndex);
                if (desc == nullptr)
                {
                    return std::nullopt;
                }
                const RHI::ResourceMemoryRequirements requirements =
                    m_Device->GetBufferMemoryRequirements(desc->Desc);
                if (!requirements.IsValid() || requirements.DedicatedAllocationRequired)
                {
                    return std::nullopt;
                }
                items.push_back(RendererTransientPlacementItem{
                    .ResourceIndex = placement.ResourceIndex,
                    .FirstUsePass = placement.FirstUsePass,
                    .LastUsePass = placement.LastUsePass,
                    .Requirements = requirements,
                });
            }

            RendererTransientPlacementPlan plan =
                BuildRendererTransientPlacementPlan(std::move(items), aliasingEnabled);
            if (!plan.IsValid)
            {
                return std::nullopt;
            }
            return plan;
        }

        [[nodiscard]] bool EnsureFrameTransientMemoryBlock(
            std::vector<TransientMemoryBlockCache>& blocks,
            const RendererTransientPlacementPlan& plan,
            const char* debugName)
        {
            if (m_Device == nullptr || plan.PeakBytes == 0u)
            {
                return plan.PeakBytes == 0u;
            }

            blocks.resize(1u);
            TransientMemoryBlockCache& block = blocks.front();
            if (block.Handle.IsValid())
            {
                return true;
            }

            const RHI::MemoryBlockHandle handle = m_Device->CreateMemoryBlock({
                .SizeBytes = plan.PeakBytes,
                .AlignmentBytes = plan.BlockAlignmentBytes,
                .MemoryTypeBits = plan.MemoryTypeBits,
                .DebugName = debugName,
            });
            if (!handle.IsValid())
            {
                return false;
            }

            block = TransientMemoryBlockCache{
                .Handle = handle,
                .SizeBytes = plan.PeakBytes,
                .AlignmentBytes = plan.BlockAlignmentBytes,
                .MemoryTypeBits = plan.MemoryTypeBits,
            };
            return true;
        }

        void ReplaceCompiledAliasReuseBarriers(
            CompiledRenderGraph& compiled,
            const RendererTransientPlacementPlan& texturePlan,
            const RendererTransientPlacementPlan& bufferPlan)
        {
            for (BarrierPacket& packet : compiled.BarrierPackets)
            {
                packet.TextureAliasReuseBarriers.clear();
                packet.BufferAliasReuseBarriers.clear();
            }

            auto resolvePassIndexFromExecutionRank =
                [&compiled](const std::uint32_t executionRank) noexcept
            {
                return executionRank < compiled.TopologicalOrder.size()
                    ? compiled.TopologicalOrder[executionRank]
                    : executionRank;
            };

            for (const RendererTransientAliasReuseHazard& hazard : texturePlan.AliasReuseHazards)
            {
                BarrierPacket& packet = FindOrCreateRendererBarrierPacket(
                    compiled.BarrierPackets,
                    resolvePassIndexFromExecutionRank(hazard.PassIndex),
                    BarrierPacketStage::BeforePass);
                packet.TextureAliasReuseBarriers.push_back(TextureAliasReuseBarrierPacket{
                    .PreviousTextureIndex = hazard.PreviousResourceIndex,
                    .TextureIndex = hazard.ResourceIndex,
                    .BlockIndex = hazard.BlockIndex,
                    .OffsetBytes = hazard.OffsetBytes,
                    .SizeBytes = hazard.SizeBytes,
                });
            }

            for (const RendererTransientAliasReuseHazard& hazard : bufferPlan.AliasReuseHazards)
            {
                BarrierPacket& packet = FindOrCreateRendererBarrierPacket(
                    compiled.BarrierPackets,
                    resolvePassIndexFromExecutionRank(hazard.PassIndex),
                    BarrierPacketStage::BeforePass);
                packet.BufferAliasReuseBarriers.push_back(BufferAliasReuseBarrierPacket{
                    .PreviousBufferIndex = hazard.PreviousResourceIndex,
                    .BufferIndex = hazard.ResourceIndex,
                    .BlockIndex = hazard.BlockIndex,
                    .OffsetBytes = hazard.OffsetBytes,
                    .SizeBytes = hazard.SizeBytes,
                });
            }

            std::erase_if(compiled.BarrierPackets, [](const BarrierPacket& packet) {
                return packet.TextureBarriers.empty() &&
                       packet.BufferBarriers.empty() &&
                       packet.TextureAliasReuseBarriers.empty() &&
                       packet.BufferAliasReuseBarriers.empty();
            });
            SortRendererBarrierPackets(compiled.BarrierPackets);
        }

        void ClearCompiledAliasReuseBarriers(CompiledRenderGraph& compiled)
        {
            RendererTransientPlacementPlan emptyTexturePlan{};
            RendererTransientPlacementPlan emptyBufferPlan{};
            ReplaceCompiledAliasReuseBarriers(compiled, emptyTexturePlan, emptyBufferPlan);
        }

        [[nodiscard]] bool AllocatePlacedFrameTransientTextures(
            CompiledRenderGraph& compiled,
            const std::uint32_t slot,
            const RendererTransientPlacementPlan& plan)
        {
            if (plan.PeakBytes == 0u)
            {
                return true;
            }

            const RHI::MemoryBlockHandle block =
                m_FrameTransientTextureMemoryBlocks[slot].front().Handle;
            if (!block.IsValid())
            {
                return false;
            }

            for (const TransientResourcePlacement& placement : plan.Placements)
            {
                const std::uint32_t index = placement.ResourceIndex;
                if (index >= m_FrameTransientTextures[slot].size())
                {
                    return false;
                }

                const TextureResourceDesc* desc = m_RenderGraph.GetTextureDescByIndex(index);
                if (desc == nullptr)
                {
                    return false;
                }

                if (m_FrameTransientTextures[slot][index].IsValid() &&
                    CompatibleTextureDesc(m_FrameTransientTextureDescs[slot][index], desc->Desc) &&
                    CompatiblePlacedResource(
                        m_Device->GetTextureMemoryPlacement(m_FrameTransientTextures[slot][index]),
                        block,
                        placement))
                {
                    compiled.TextureHandles[index] = m_FrameTransientTextures[slot][index];
                    continue;
                }

                if (m_FrameTransientTextures[slot][index].IsValid())
                {
                    m_Device->DestroyTexture(m_FrameTransientTextures[slot][index]);
                    m_FrameTransientTextures[slot][index] = RHI::TextureHandle{};
                }

                const RHI::TextureHandle handle = m_Device->CreatePlacedTexture({
                    .Desc = desc->Desc,
                    .Placement = {.Block = block, .OffsetBytes = placement.OffsetBytes},
                });
                if (!handle.IsValid())
                {
                    return false;
                }

                if (!CompatiblePlacedResource(
                        m_Device->GetTextureMemoryPlacement(handle),
                        block,
                        placement))
                {
                    m_Device->DestroyTexture(handle);
                    return false;
                }

                compiled.TextureHandles[index] = handle;
                m_FrameTransientTextures[slot][index] = handle;
                m_FrameTransientTextureDescs[slot][index] = desc->Desc;
            }

            return true;
        }

        [[nodiscard]] bool AllocatePlacedFrameTransientBuffers(
            CompiledRenderGraph& compiled,
            const std::uint32_t slot,
            const RendererTransientPlacementPlan& plan)
        {
            if (plan.PeakBytes == 0u)
            {
                return true;
            }

            const RHI::MemoryBlockHandle block =
                m_FrameTransientBufferMemoryBlocks[slot].front().Handle;
            if (!block.IsValid())
            {
                return false;
            }

            for (const TransientResourcePlacement& placement : plan.Placements)
            {
                const std::uint32_t index = placement.ResourceIndex;
                if (index >= m_FrameTransientBuffers[slot].size())
                {
                    return false;
                }

                const BufferResourceDesc* desc = m_RenderGraph.GetBufferDescByIndex(index);
                if (desc == nullptr)
                {
                    return false;
                }

                if (m_FrameTransientBuffers[slot][index].IsValid() &&
                    CompatibleBufferDesc(m_FrameTransientBufferDescs[slot][index], desc->Desc) &&
                    CompatiblePlacedResource(
                        m_Device->GetBufferMemoryPlacement(m_FrameTransientBuffers[slot][index]),
                        block,
                        placement))
                {
                    compiled.BufferHandles[index] = m_FrameTransientBuffers[slot][index];
                    continue;
                }

                if (m_FrameTransientBuffers[slot][index].IsValid())
                {
                    m_Device->DestroyBuffer(m_FrameTransientBuffers[slot][index]);
                    m_FrameTransientBuffers[slot][index] = RHI::BufferHandle{};
                }

                const RHI::BufferHandle handle = m_Device->CreatePlacedBuffer({
                    .Desc = desc->Desc,
                    .Placement = {.Block = block, .OffsetBytes = placement.OffsetBytes},
                });
                if (!handle.IsValid())
                {
                    return false;
                }

                if (!CompatiblePlacedResource(
                        m_Device->GetBufferMemoryPlacement(handle),
                        block,
                        placement))
                {
                    m_Device->DestroyBuffer(handle);
                    return false;
                }

                compiled.BufferHandles[index] = handle;
                m_FrameTransientBuffers[slot][index] = handle;
                m_FrameTransientBufferDescs[slot][index] = desc->Desc;
            }

            return true;
        }

        [[nodiscard]] PlacedTransientAllocationStatus AllocateFramePlacedTransientResources(
            CompiledRenderGraph& compiled,
            const std::uint32_t slot)
        {
            const bool aliasingEnabled =
                compiled.TransientPlacedPeakMemoryEstimateBytes <
                compiled.TransientNaiveMemoryEstimateBytes;
            auto texturePlan = BuildTexturePlacementPlanForDevice(compiled, aliasingEnabled);
            auto bufferPlan = BuildBufferPlacementPlanForDevice(compiled, aliasingEnabled);
            if (!texturePlan.has_value() || !bufferPlan.has_value())
            {
                return PlacedTransientAllocationStatus::Unsupported;
            }

            if (!CompatibleTransientMemoryBlocks(m_FrameTransientTextureMemoryBlocks[slot], *texturePlan) ||
                !CompatibleTransientMemoryBlocks(m_FrameTransientBufferMemoryBlocks[slot], *bufferPlan))
            {
                ReleaseFrameTransientResources(slot);
                ResizeFrameTransientSlot(compiled, slot);
            }

            if (!EnsureFrameTransientMemoryBlock(
                    m_FrameTransientTextureMemoryBlocks[slot],
                    *texturePlan,
                    "Renderer.TransientTextures.PlacedBlock") ||
                !EnsureFrameTransientMemoryBlock(
                    m_FrameTransientBufferMemoryBlocks[slot],
                    *bufferPlan,
                    "Renderer.TransientBuffers.PlacedBlock"))
            {
                return PlacedTransientAllocationStatus::Failed;
            }

            compiled.TextureTransientPlacements = texturePlan->Placements;
            compiled.BufferTransientPlacements = bufferPlan->Placements;
            compiled.TransientNaiveMemoryEstimateBytes =
                texturePlan->NaiveBytes + bufferPlan->NaiveBytes;
            compiled.TransientPlacedPeakMemoryEstimateBytes =
                texturePlan->PeakBytes + bufferPlan->PeakBytes;
            compiled.TransientMemoryEstimateBytes =
                compiled.TransientPlacedPeakMemoryEstimateBytes;
            ReplaceCompiledAliasReuseBarriers(compiled, *texturePlan, *bufferPlan);

            if (!AllocatePlacedFrameTransientTextures(compiled, slot, *texturePlan) ||
                !AllocatePlacedFrameTransientBuffers(compiled, slot, *bufferPlan))
            {
                return PlacedTransientAllocationStatus::Failed;
            }

            return PlacedTransientAllocationStatus::Succeeded;
        }

        [[nodiscard]] bool AllocateFrameTransientResources(CompiledRenderGraph& compiled,
                                                           const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr)
            {
                return false;
            }

            const std::uint32_t framesInFlight = std::max(1u, m_Device->GetFramesInFlight());
            if (m_FrameTransientTextures.size() != framesInFlight ||
                m_FrameTransientBuffers.size() != framesInFlight ||
                m_FrameTransientTextureMemoryBlocks.size() != framesInFlight ||
                m_FrameTransientBufferMemoryBlocks.size() != framesInFlight)
            {
                ReleaseAllFrameTransientResources();
                m_FrameTransientTextures.resize(framesInFlight);
                m_FrameTransientBuffers.resize(framesInFlight);
                m_FrameTransientTextureDescs.resize(framesInFlight);
                m_FrameTransientBufferDescs.resize(framesInFlight);
                m_FrameTransientTextureMemoryBlocks.resize(framesInFlight);
                m_FrameTransientBufferMemoryBlocks.resize(framesInFlight);
            }

            const std::uint32_t slot = frameIndex % framesInFlight;
            ResizeFrameTransientSlot(compiled, slot);

            const bool hasAliasingSavings =
                compiled.TransientPlacedPeakMemoryEstimateBytes <
                compiled.TransientNaiveMemoryEstimateBytes;
            if (hasAliasingSavings)
            {
                const PlacedTransientAllocationStatus placedAllocationStatus =
                    AllocateFramePlacedTransientResources(compiled, slot);
                if (placedAllocationStatus == PlacedTransientAllocationStatus::Succeeded)
                {
                    return true;
                }

                ReleaseFrameTransientResources(slot);
                ResizeFrameTransientSlot(compiled, slot);
                compiled.TransientPlacedPeakMemoryEstimateBytes =
                    compiled.TransientNaiveMemoryEstimateBytes;
                compiled.TransientMemoryEstimateBytes =
                    compiled.TransientNaiveMemoryEstimateBytes;
                ClearCompiledAliasReuseBarriers(compiled);
                Core::Log::Warn(
                    "[Graphics] Placed transient allocation unavailable; using non-aliased transient resource allocation for this frame");
                m_InvalidateRenderGraphCompileCacheAfterFrame =
                    placedAllocationStatus == PlacedTransientAllocationStatus::Failed;
            }
            else if (SlotHasPlacedTransientMemoryBlocks(slot))
            {
                ReleaseFrameTransientResources(slot);
                ResizeFrameTransientSlot(compiled, slot);
            }

            for (std::uint32_t index = 0; index < compiled.TextureHandles.size(); ++index)
            {
                if (index >= compiled.TextureImported.size() || index >= compiled.TextureLifetimes.size() ||
                    compiled.TextureImported[index] || !compiled.TextureLifetimes[index].HasUse)
                {
                    continue;
                }

                const TextureResourceDesc* desc = m_RenderGraph.GetTextureDescByIndex(index);
                if (desc == nullptr)
                {
                    return false;
                }
                if (m_FrameTransientTextures[slot][index].IsValid() &&
                    CompatibleTextureDesc(m_FrameTransientTextureDescs[slot][index], desc->Desc))
                {
                    compiled.TextureHandles[index] = m_FrameTransientTextures[slot][index];
                    continue;
                }
                if (m_FrameTransientTextures[slot][index].IsValid())
                {
                    m_Device->DestroyTexture(m_FrameTransientTextures[slot][index]);
                    m_FrameTransientTextures[slot][index] = RHI::TextureHandle{};
                }
                const RHI::TextureHandle handle = m_Device->CreateTexture(desc->Desc);
                if (!handle.IsValid())
                {
                    return false;
                }
                compiled.TextureHandles[index] = handle;
                m_FrameTransientTextures[slot][index] = handle;
                m_FrameTransientTextureDescs[slot][index] = desc->Desc;
            }

            for (std::uint32_t index = 0; index < compiled.BufferHandles.size(); ++index)
            {
                if (index >= compiled.BufferImported.size() || index >= compiled.BufferLifetimes.size() ||
                    compiled.BufferImported[index] || !compiled.BufferLifetimes[index].HasUse)
                {
                    continue;
                }

                const BufferResourceDesc* desc = m_RenderGraph.GetBufferDescByIndex(index);
                if (desc == nullptr)
                {
                    return false;
                }
                if (m_FrameTransientBuffers[slot][index].IsValid() &&
                    CompatibleBufferDesc(m_FrameTransientBufferDescs[slot][index], desc->Desc))
                {
                    compiled.BufferHandles[index] = m_FrameTransientBuffers[slot][index];
                    continue;
                }
                if (m_FrameTransientBuffers[slot][index].IsValid())
                {
                    m_Device->DestroyBuffer(m_FrameTransientBuffers[slot][index]);
                    m_FrameTransientBuffers[slot][index] = RHI::BufferHandle{};
                }
                const RHI::BufferHandle handle = m_Device->CreateBuffer(desc->Desc);
                if (!handle.IsValid())
                {
                    return false;
                }
                compiled.BufferHandles[index] = handle;
                m_FrameTransientBuffers[slot][index] = handle;
                m_FrameTransientBufferDescs[slot][index] = desc->Desc;
            }

            return true;
        }

        // GRAPHICS-074 Slice D.3 — drain any picking-readback slot whose
        // issuing frame has completed since its copy was recorded. Called at
        // the top of `BeginFrame()` so the `SelectionSystem` observes the
        // pick result before the runtime extracts the next frame's
        // `RenderWorld` (the runtime/editor decides what to do with the
        // resolved hit during this frame's extraction phase). The drain is
        // gated on (a) a live operational device, (b) a valid
        // renderer-owned `Picking.Readback` lease, and (c) at least one
        // slot whose `IssuedFrame` predates the current `GlobalFrameNumber`
        // (slots issued this very frame have not yet had the chance to
        // run their copy + complete, so they stay pending). The routing
        // matches the Slice D.3 task contract:
        //   - `Invalidated` (set by `RebuildOperationalResources()` for
        //     in-flight slots, simulating a device-lost recovery whose
        //     pre-rebuild copy is no longer trustworthy) → `PublishNoHit()`.
        //   - `EntityId == 0` (background pixel after the depth prepass
        //     decided no surface won the depth-equal test for that pixel)
        //     → `PublishNoHit()`.
        //   - Otherwise → `PublishPickResult({EncodedId, StableEntityId,
        //     Hit=true})`.
        // A read failure (no host-mapped contents, MockDevice without
        // seeded bytes, etc.) leaves the local decode buffer zeroed and
        // therefore falls through to the `EntityId == 0` NoHit branch —
        // safe by construction.
        void DrainCompletedPickingSlots()
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return;
            }
            if (!m_PickingReadbackBuffer.has_value() || !m_PickingReadbackBuffer->IsValid())
            {
                return;
            }
            if (m_PickingSlotPending.empty() || !m_Subsystems.SelectionSystemRegistry())
            {
                return;
            }
            const std::uint64_t completedFrameNumber = m_Device->GetGlobalFrameNumber();
            const RHI::BufferHandle bufferHandle = m_PickingReadbackBuffer->GetHandle();
            for (std::size_t slot = 0; slot < m_PickingSlotPending.size(); ++slot)
            {
                if (!m_PickingSlotPending[slot])
                {
                    continue;
                }
                // Slot's copy was issued in `IssuedFrame`. The post-EndFrame
                // counter advances exactly once per `EndFrame(...)`, so
                // `IssuedFrame < GlobalFrameNumber` means the issuing frame
                // has at minimum been submitted. See the comment at
                // `BeginFrame()` for the async-backend caveat.
                if (m_PickingSlotIssuedFrame[slot] >= completedFrameNumber)
                {
                    continue;
                }
                std::uint32_t entityId    = 0u;
                std::uint32_t encodedBits = 0u;
                float         depth       = 1.0f;
                bool          hasDepth    = false;
                const std::uint64_t slotOffset =
                    static_cast<std::uint64_t>(slot) * kPickingReadbackSlotStride;
                if (slotOffset + kPickingReadbackSlotStride <= m_PickingReadbackBufferSize)
                {
                    m_Device->ReadBuffer(bufferHandle, &entityId,    sizeof(entityId),
                                         slotOffset + kPickingReadbackEntityIdOffset);
                    m_Device->ReadBuffer(bufferHandle, &encodedBits, sizeof(encodedBits),
                                         slotOffset + kPickingReadbackEncodedIdOffset);
                    m_Device->ReadBuffer(bufferHandle, &depth,       sizeof(depth),
                                         slotOffset + kPickingReadbackDepthOffset);
                    hasDepth = m_PickingSlotDepthCopied[slot] && std::isfinite(depth);
                }
                const EncodedSelectionId encoded{.Value = encodedBits};
                // RUNTIME-089 — replay the slot's correlation Sequence on the
                // published readback (hit or no-hit) so the runtime resolves the
                // exact in-flight request even when several slots complete in
                // this one drain and publish out of issue order.
                const std::uint64_t slotSequence = m_PickingSlotSequence[slot];
                if (m_PickingSlotInvalidated[slot] || entityId == 0u)
                {
                    m_Subsystems.SelectionSystemRegistry()->PublishPickResult(PickReadbackResult{
                        .Hit      = false,
                        .Sequence = slotSequence,
                        .PixelX   = m_PickingSlotRequest[slot].X,
                        .PixelY   = m_PickingSlotRequest[slot].Y,
                    });
                }
                else
                {
                    m_Subsystems.SelectionSystemRegistry()->PublishPickResult(PickReadbackResult{
                        .EncodedId      = encoded,
                        .StableEntityId = entityId,
                        .Hit            = true,
                        .Sequence       = slotSequence,
                        // BUG-026 — depth sample + pick pixel so the runtime can
                        // unproject the world-space cursor position.
                        .HasDepth       = hasDepth,
                        .Depth          = depth,
                        .PixelX         = m_PickingSlotRequest[slot].X,
                        .PixelY         = m_PickingSlotRequest[slot].Y,
                    });
                }
                m_PickingSlotPending[slot] = false;
                m_PickingSlotInvalidated[slot] = false;
                m_PickingSlotIssuedFrame[slot] = 0u;
                m_PickingSlotRequest[slot] = PickPixelRequest{};
                m_PickingSlotSequence[slot] = 0u;
                m_PickingSlotDepthCopied[slot] = false;
            }
        }

        // GRAPHICS-075 Slice E.2 — drain any histogram-readback slot whose
        // issuing frame has completed since its copy was recorded. Called
        // at the top of `BeginFrame()` so the `PostProcessSystem` observes
        // the exposure-history update before the runtime extracts the next
        // frame's `RenderWorld` (the tonemap leg of the upcoming frame
        // benefits from the freshly published adaptation state). The drain
        // is gated on (a) a live operational device, (b) a valid
        // renderer-owned `Histogram.Readback` lease, and (c) at least one
        // slot whose `IssuedFrame` predates the current `GlobalFrameNumber`
        // (slots issued this very frame have not yet completed their copy
        // and stay pending). Slots flagged `Invalidated` (e.g. by a
        // `RebuildOperationalResources()` device-lost recovery) are
        // released without publishing — the publish handshake intentionally
        // never sees stale pre-rebuild bytes.
        void DrainCompletedHistogramSlots()
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return;
            }
            if (!m_HistogramReadbackBuffer.has_value() || !m_HistogramReadbackBuffer->IsValid())
            {
                return;
            }
            if (m_HistogramSlotPending.empty() || !m_Subsystems.PostProcessSystemRegistry().has_value())
            {
                return;
            }
            const std::uint64_t completedFrameNumber = m_Device->GetGlobalFrameNumber();
            const RHI::BufferHandle bufferHandle = m_HistogramReadbackBuffer->GetHandle();
            for (std::size_t slot = 0; slot < m_HistogramSlotPending.size(); ++slot)
            {
                if (!m_HistogramSlotPending[slot])
                {
                    continue;
                }
                if (m_HistogramSlotIssuedFrame[slot] >= completedFrameNumber)
                {
                    continue;
                }

                if (m_HistogramSlotInvalidated[slot])
                {
                    // Release without publishing — pre-rebuild bytes are
                    // not trustworthy, and `PublishHistogramReadback` would
                    // anchor the retained adaptation history to them.
                    m_HistogramSlotPending[slot] = false;
                    m_HistogramSlotInvalidated[slot] = false;
                    m_HistogramSlotIssuedFrame[slot] = 0u;
                    continue;
                }

                std::array<std::uint32_t, 256> bins{};
                const std::uint64_t slotOffset =
                    static_cast<std::uint64_t>(slot) * kHistogramReadbackSlotBytes;
                if (slotOffset + kHistogramReadbackSlotBytes <= m_HistogramReadbackBufferSize)
                {
                    m_Device->ReadBuffer(bufferHandle,
                                         bins.data(),
                                         kHistogramReadbackSlotBytes,
                                         slotOffset);
                }
                m_Subsystems.PostProcessSystemRegistry()->PublishHistogramReadback(
                    std::span<const std::uint32_t>{bins.data(), bins.size()},
                    m_HistogramSlotIssuedFrame[slot],
                    m_Device);

                m_HistogramSlotPending[slot] = false;
                m_HistogramSlotInvalidated[slot] = false;
                m_HistogramSlotIssuedFrame[slot] = 0u;
            }
        }

        [[nodiscard]] static std::uint64_t SaturatingProfileAge(
            const std::uint64_t currentFrame,
            const std::uint64_t sampledFrame) noexcept
        {
            return currentFrame >= sampledFrame
                       ? currentFrame - sampledFrame
                       : 0u;
        }

        static void ClearGpuPassTelemetry()
        {
            Core::Telemetry::TelemetrySystem::Get().SetPassGpuTimings({});
        }

        [[nodiscard]] static RenderGraphGpuProfileStatus
        ProfileStatusForError(const RHI::ProfilerError error) noexcept
        {
            switch (error)
            {
            case RHI::ProfilerError::NotReady:
                return RenderGraphGpuProfileStatus::NotReady;
            case RHI::ProfilerError::DeviceLost:
                return RenderGraphGpuProfileStatus::DeviceLost;
            case RHI::ProfilerError::Unsupported:
                return RenderGraphGpuProfileStatus::Unsupported;
            case RHI::ProfilerError::Exhausted:
            case RHI::ProfilerError::Overflow:
                return RenderGraphGpuProfileStatus::Exhausted;
            case RHI::ProfilerError::InvalidState:
            case RHI::ProfilerError::InvalidArgument:
                return RenderGraphGpuProfileStatus::InvalidLifecycle;
            }
            return RenderGraphGpuProfileStatus::InvalidLifecycle;
        }

        [[nodiscard]] static RenderGraphGpuProfileStatus
        ProfileStatusForBackend(
            const RHI::ProfilerBackendStatus status) noexcept
        {
            switch (status)
            {
            case RHI::ProfilerBackendStatus::Ready:
                return RenderGraphGpuProfileStatus::Recording;
            case RHI::ProfilerBackendStatus::ContractOnly:
            case RHI::ProfilerBackendStatus::InitializationFailed:
                return RenderGraphGpuProfileStatus::Unavailable;
            case RHI::ProfilerBackendStatus::Unsupported:
                return RenderGraphGpuProfileStatus::Unsupported;
            case RHI::ProfilerBackendStatus::DeviceLost:
                return RenderGraphGpuProfileStatus::DeviceLost;
            }
            return RenderGraphGpuProfileStatus::Unavailable;
        }

        [[nodiscard]] static std::optional<std::size_t>
        GpuProfileQueueIndex(
            const RHI::QueueAffinity queue) noexcept
        {
            if (queue == RHI::QueueAffinity::Graphics)
            {
                return 0u;
            }
            if (queue == RHI::QueueAffinity::AsyncCompute)
            {
                return 1u;
            }
            return std::nullopt;
        }

        void PublishStaleGpuProfileStatus(
            const RenderGraphGpuProfileStatus status,
            std::string diagnostic)
        {
            RenderGraphGpuProfileStats snapshot{};
            if (m_LastGoodGpuProfile.has_value())
            {
                snapshot = *m_LastGoodGpuProfile;
                snapshot.Fresh = false;
                snapshot.Stale = true;
                const std::uint64_t currentFrame =
                    m_Device != nullptr
                        ? m_Device->GetGlobalFrameNumber()
                        : 0u;
                snapshot.SampleAgeFrames = SaturatingProfileAge(
                    currentFrame,
                    snapshot.ResolvedSubmittedFrameNumber);
            }
            snapshot.Status = status;
            snapshot.Diagnostic = std::move(diagnostic);
            m_CurrentGpuProfile = std::move(snapshot);
            ClearGpuPassTelemetry();
        }

        void UpdateCurrentGpuProfileStatus(
            const RenderGraphGpuProfileStatus status,
            std::string diagnostic)
        {
            m_CurrentGpuProfile.Status = status;
            m_CurrentGpuProfile.Diagnostic = std::move(diagnostic);
        }

        void LatchGpuProfileFailure(
            const RHI::ProfilerError error) noexcept
        {
            const std::uint8_t encoded =
                static_cast<std::uint8_t>(error) + 1u;
            std::uint8_t expected = 0u;
            (void)m_GpuProfileFailure.compare_exchange_strong(
                expected,
                encoded,
                std::memory_order_release,
                std::memory_order_relaxed);
        }

        [[nodiscard]] std::optional<RHI::ProfilerError>
        GpuProfileFailure() const noexcept
        {
            const std::uint8_t encoded =
                m_GpuProfileFailure.load(std::memory_order_acquire);
            if (encoded == 0u)
            {
                return std::nullopt;
            }
            return static_cast<RHI::ProfilerError>(encoded - 1u);
        }

        void ResolveCompletedGpuProfile(const std::uint32_t frameSlot)
        {
            ClearGpuPassTelemetry();
            const std::uint64_t currentFrame =
                m_Device != nullptr
                    ? m_Device->GetGlobalFrameNumber()
                    : 0u;
            if (frameSlot >= m_SubmittedGpuProfiles.size() ||
                !m_SubmittedGpuProfiles[frameSlot].has_value())
            {
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::NotReady,
                    "No completed native GPU profile resolved for this frame.");
                return;
            }

            RHI::IProfiler* profiler =
                m_Device != nullptr ? m_Device->GetProfiler() : nullptr;
            if (profiler == nullptr)
            {
                m_SubmittedGpuProfiles[frameSlot].reset();
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::Unavailable,
                    "The active render device does not expose a profiler.");
                return;
            }

            SubmittedGpuProfileFrame& submitted =
                *m_SubmittedGpuProfiles[frameSlot];
            const auto resolved = profiler->Resolve(submitted.Frame);
            if (!resolved)
            {
                const RHI::ProfilerError error = resolved.error();
                if (error != RHI::ProfilerError::NotReady)
                {
                    m_SubmittedGpuProfiles[frameSlot].reset();
                }
                PublishStaleGpuProfileStatus(
                    ProfileStatusForError(error),
                    "GPU profile resolution failed: " +
                        std::string{RHI::ProfilerErrorName(error)} + ".");
                return;
            }
            if (resolved->Source != RHI::GpuTimestampSource::NativeGpu)
            {
                m_SubmittedGpuProfiles[frameSlot].reset();
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::Unavailable,
                    "Resolved profiler data is not native GPU evidence.");
                return;
            }
            if (resolved->Frame != submitted.Frame)
            {
                m_SubmittedGpuProfiles[frameSlot].reset();
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::InvalidLifecycle,
                    "Resolved GPU profile did not match the exact "
                    "submitted frame key.");
                return;
            }

            RenderGraphGpuProfileStats snapshot{
                .Status = RenderGraphGpuProfileStatus::Resolved,
                .Source = RHI::GpuTimestampSource::NativeGpu,
                .Diagnostic = "Native GPU timestamps resolved.",
                .Fresh = true,
                .Stale = false,
                .HasResolvedFrame = true,
                .ResolvedSubmittedFrameNumber =
                    resolved->Frame.FrameNumber,
                .ResolvedFrameSlot = resolved->Frame.FrameSlot,
                .SampleAgeFrames = SaturatingProfileAge(
                    currentFrame,
                    resolved->Frame.FrameNumber),
            };
            snapshot.QueueEnvelopes.reserve(
                resolved->QueueEnvelopes.size());
            for (const RHI::GpuTimestampQueueEnvelope& queue :
                 resolved->QueueEnvelopes)
            {
                snapshot.QueueEnvelopes.push_back(
                    RenderGraphGpuProfileQueueStats{
                        .Queue = queue.Queue,
                        .Source = queue.Source,
                        .DurationNs = queue.DurationNs,
                    });
            }

            std::vector<bool> matched(submitted.Passes.size(), false);
            snapshot.Passes.reserve(submitted.Passes.size());
            std::vector<Core::Telemetry::PassTimingEntry> telemetry{};
            telemetry.reserve(submitted.Passes.size());
            for (const RHI::GpuTimestampScope& scope : resolved->Scopes)
            {
                const auto metadataIt = std::find_if(
                    submitted.Passes.begin(),
                    submitted.Passes.end(),
                    [&scope](const GpuProfilePassMetadata& metadata)
                    {
                        return metadata.Ordinal == scope.Ordinal;
                    });
                if (metadataIt == submitted.Passes.end() ||
                    metadataIt->Name != scope.Name ||
                    metadataIt->Queue != scope.Queue)
                {
                    m_SubmittedGpuProfiles[frameSlot].reset();
                    PublishStaleGpuProfileStatus(
                        RenderGraphGpuProfileStatus::InvalidLifecycle,
                        "Resolved GPU profile metadata did not match the "
                        "submitted compiled-pass plan.");
                    return;
                }
                const std::size_t metadataIndex =
                    static_cast<std::size_t>(
                        std::distance(
                            submitted.Passes.begin(),
                            metadataIt));
                if (matched[metadataIndex])
                {
                    m_SubmittedGpuProfiles[frameSlot].reset();
                    PublishStaleGpuProfileStatus(
                        RenderGraphGpuProfileStatus::InvalidLifecycle,
                        "Resolved GPU profile repeated a compiled-pass "
                        "ordinal.");
                    return;
                }
                matched[metadataIndex] = true;

                const bool recorded =
                    metadataIt->CommandStatus ==
                        RenderCommandPassStatus::Recorded;
                const bool nativeDuration =
                    recorded &&
                    scope.Source ==
                        RHI::GpuTimestampSource::NativeGpu &&
                    scope.DurationNs.has_value();
                snapshot.Passes.push_back(
                    RenderGraphGpuProfilePassStats{
                        .Name = metadataIt->Name,
                        .Id = metadataIt->Id,
                        .Queue = metadataIt->Queue,
                        .CommandStatus = metadataIt->CommandStatus,
                        .Source =
                            nativeDuration
                                ? RHI::GpuTimestampSource::NativeGpu
                                : RHI::GpuTimestampSource::Unavailable,
                        .DurationNs =
                            nativeDuration
                                ? scope.DurationNs
                                : std::optional<std::uint64_t>{},
                    });
                if (nativeDuration)
                {
                    telemetry.push_back(
                        Core::Telemetry::PassTimingEntry{
                            .Name = metadataIt->Name,
                            .GpuTimeNs = *scope.DurationNs,
                            .CpuTimeNs = 0u,
                        });
                }
            }

            if (std::any_of(
                    matched.begin(),
                    matched.end(),
                    [](const bool value) { return !value; }))
            {
                m_SubmittedGpuProfiles[frameSlot].reset();
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::InvalidLifecycle,
                    "Resolved GPU profile omitted a planned compiled pass.");
                return;
            }

            m_SubmittedGpuProfiles[frameSlot].reset();
            m_CurrentGpuProfile = snapshot;
            m_LastGoodGpuProfile = std::move(snapshot);
            Core::Telemetry::TelemetrySystem::Get().SetPassGpuTimings(
                std::move(telemetry));
        }

        [[nodiscard]] bool BeginGpuProfile(
            const RHI::FrameHandle& frame,
            const CompiledRenderGraph& compiled,
            const std::span<const RHI::QueueAffinity> actualQueues)
        {
            DiscardActiveGpuProfile();
            RHI::IProfiler* profiler =
                m_Device != nullptr ? m_Device->GetProfiler() : nullptr;
            if (profiler == nullptr)
            {
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::Unavailable,
                    "The active render device does not expose a profiler.");
                return false;
            }

            const RHI::ProfilerStatusSnapshot backend =
                profiler->GetStatus();
            if (backend.Status != RHI::ProfilerBackendStatus::Ready &&
                backend.Status !=
                    RHI::ProfilerBackendStatus::ContractOnly)
            {
                PublishStaleGpuProfileStatus(
                    ProfileStatusForBackend(backend.Status),
                    backend.Diagnostic);
                return false;
            }

            std::vector<RHI::ProfilerScopeDesc> descriptors{};
            descriptors.reserve(compiled.TopologicalOrder.size());
            std::vector<GpuProfilePassMetadata> metadata{};
            metadata.reserve(compiled.TopologicalOrder.size());
            std::vector<std::uint32_t> metadataIndexByPass(
                compiled.PassDeclarations.size(),
                std::numeric_limits<std::uint32_t>::max());
            for (const std::uint32_t passIndex :
                 compiled.TopologicalOrder)
            {
                if (passIndex >= compiled.PassDeclarations.size() ||
                    passIndex >= compiled.PassNames.size() ||
                    passIndex >= compiled.PassIds.size() ||
                    passIndex >= actualQueues.size() ||
                    compiled.PassNames[passIndex].empty())
                {
                    PublishStaleGpuProfileStatus(
                        RenderGraphGpuProfileStatus::InvalidLifecycle,
                        "Compiled-pass profiling metadata is incomplete.");
                    return false;
                }
                const std::uint32_t metadataIndex =
                    static_cast<std::uint32_t>(metadata.size());
                metadataIndexByPass[passIndex] = metadataIndex;
                descriptors.push_back(RHI::ProfilerScopeDesc{
                    .Ordinal = passIndex,
                    .Name = compiled.PassNames[passIndex],
                    .Queue = actualQueues[passIndex],
                });
                metadata.push_back(GpuProfilePassMetadata{
                    .Ordinal = passIndex,
                    .Name = compiled.PassNames[passIndex],
                    .Id = compiled.PassIds[passIndex],
                    .Queue = actualQueues[passIndex],
                });
            }

            const RHI::ProfilerFrameKey key{
                .FrameNumber = m_Device->GetGlobalFrameNumber(),
                .FrameSlot = frame.FrameIndex,
            };
            auto plan = profiler->BeginFrame(key, descriptors);
            if (!plan)
            {
                PublishStaleGpuProfileStatus(
                    ProfileStatusForError(plan.error()),
                    "GPU profile planning failed: " +
                        std::string{
                            RHI::ProfilerErrorName(plan.error())} +
                        ".");
                return false;
            }
            if (plan->Frame != key ||
                plan->ScopeTokens.size() != metadata.size())
            {
                (void)profiler->EndFrame(
                    key,
                    RHI::ProfilerFrameDisposition::Discarded);
                PublishStaleGpuProfileStatus(
                    RenderGraphGpuProfileStatus::InvalidLifecycle,
                    "GPU profiler returned a mismatched immutable plan.");
                return false;
            }
            for (std::size_t index = 0u;
                 index < metadata.size();
                 ++index)
            {
                metadata[index].Token = plan->ScopeTokens[index];
            }

            m_ActiveGpuProfile = ActiveGpuProfileFrame{
                .Profiler = profiler,
                .Frame = key,
                .Passes = std::move(metadata),
                .MetadataIndexByPass =
                    std::move(metadataIndexByPass),
            };
            if (backend.Source ==
                RHI::GpuTimestampSource::ContractOnly)
            {
                ClearGpuPassTelemetry();
            }
            UpdateCurrentGpuProfileStatus(
                RenderGraphGpuProfileStatus::Recording,
                backend.Diagnostic);
            return true;
        }

        void DiscardActiveGpuProfile() noexcept
        {
            if (m_ActiveGpuProfile.has_value() &&
                m_ActiveGpuProfile->Profiler != nullptr)
            {
                (void)m_ActiveGpuProfile->Profiler->EndFrame(
                    m_ActiveGpuProfile->Frame,
                    RHI::ProfilerFrameDisposition::Discarded);
            }
            m_ActiveGpuProfile.reset();
            m_GpuProfileFailure.store(
                0u,
                std::memory_order_release);
        }

        void BeginGpuProfileQueue(
            RHI::ICommandContext& context,
            const RHI::QueueAffinity queue)
        {
            if (!m_ActiveGpuProfile.has_value())
            {
                return;
            }
            const std::optional<std::size_t> queueIndex =
                GpuProfileQueueIndex(queue);
            if (!queueIndex.has_value() ||
                m_ActiveGpuProfile->OpenQueues[*queueIndex])
            {
                LatchGpuProfileFailure(
                    RHI::ProfilerError::InvalidState);
                return;
            }
            const auto result =
                m_ActiveGpuProfile->Profiler->BeginQueue(
                    context,
                    queue);
            if (!result)
            {
                LatchGpuProfileFailure(result.error());
                return;
            }
            m_ActiveGpuProfile->OpenQueues[*queueIndex] = true;
        }

        void EndGpuProfileQueue(
            RHI::ICommandContext& context,
            const RHI::QueueAffinity queue)
        {
            if (!m_ActiveGpuProfile.has_value())
            {
                return;
            }
            const std::optional<std::size_t> queueIndex =
                GpuProfileQueueIndex(queue);
            if (!queueIndex.has_value() ||
                !m_ActiveGpuProfile->OpenQueues[*queueIndex])
            {
                if (!GpuProfileFailure().has_value())
                {
                    LatchGpuProfileFailure(
                        RHI::ProfilerError::InvalidState);
                }
                return;
            }
            m_ActiveGpuProfile->OpenQueues[*queueIndex] = false;
            const auto result =
                m_ActiveGpuProfile->Profiler->EndQueue(
                    context,
                    queue);
            if (!result)
            {
                LatchGpuProfileFailure(result.error());
            }
        }

        [[nodiscard]] bool BeginGpuProfileScope(
            RHI::ICommandContext& context,
            const std::uint32_t passIndex)
        {
            if (!m_ActiveGpuProfile.has_value())
            {
                return false;
            }
            if (passIndex >=
                m_ActiveGpuProfile->MetadataIndexByPass.size())
            {
                LatchGpuProfileFailure(
                    RHI::ProfilerError::InvalidArgument);
                return false;
            }
            const std::uint32_t metadataIndex =
                m_ActiveGpuProfile
                    ->MetadataIndexByPass[passIndex];
            if (metadataIndex >=
                m_ActiveGpuProfile->Passes.size())
            {
                LatchGpuProfileFailure(
                    RHI::ProfilerError::InvalidState);
                return false;
            }
            const auto result =
                m_ActiveGpuProfile->Profiler->BeginScope(
                    context,
                    m_ActiveGpuProfile
                        ->Passes[metadataIndex].Token);
            if (!result)
            {
                LatchGpuProfileFailure(result.error());
                return false;
            }
            return true;
        }

        void EndGpuProfileScope(
            RHI::ICommandContext& context,
            const std::uint32_t passIndex)
        {
            if (!m_ActiveGpuProfile.has_value() ||
                passIndex >=
                    m_ActiveGpuProfile
                        ->MetadataIndexByPass.size())
            {
                LatchGpuProfileFailure(
                    RHI::ProfilerError::InvalidState);
                return;
            }
            const std::uint32_t metadataIndex =
                m_ActiveGpuProfile
                    ->MetadataIndexByPass[passIndex];
            if (metadataIndex >=
                m_ActiveGpuProfile->Passes.size())
            {
                LatchGpuProfileFailure(
                    RHI::ProfilerError::InvalidState);
                return;
            }
            const auto result =
                m_ActiveGpuProfile->Profiler->EndScope(
                    context,
                    m_ActiveGpuProfile
                        ->Passes[metadataIndex].Token);
            if (!result)
            {
                LatchGpuProfileFailure(result.error());
            }
        }

        void PublishActiveGpuProfileCommandStatuses(
            const bool executeSucceeded)
        {
            if (!m_ActiveGpuProfile.has_value())
            {
                return;
            }
            const RenderGraphCommandRecordStats commandStats =
                SnapshotCommandRecordStats();
            for (GpuProfilePassMetadata& metadata :
                 m_ActiveGpuProfile->Passes)
            {
                bool sawNonOperational = false;
                bool sawRecorded = false;
                for (const RenderGraphCommandPassStats& pass :
                     commandStats.Passes)
                {
                    const bool matches =
                        metadata.Id.IsValid()
                            ? pass.Id == metadata.Id &&
                                  pass.Name == metadata.Name
                            : pass.Name == metadata.Name;
                    if (!matches)
                    {
                        continue;
                    }
                    sawRecorded =
                        sawRecorded ||
                        pass.Status ==
                            RenderCommandPassStatus::Recorded;
                    sawNonOperational =
                        sawNonOperational ||
                        pass.Status ==
                            RenderCommandPassStatus::
                                SkippedNonOperational;
                }
                metadata.CommandStatus =
                    sawRecorded
                        ? RenderCommandPassStatus::Recorded
                        : sawNonOperational
                            ? RenderCommandPassStatus::
                                  SkippedNonOperational
                            : RenderCommandPassStatus::
                                  SkippedUnavailable;
            }
            m_ActiveGpuProfile->ExecuteSucceeded =
                executeSucceeded;
        }

        void FinalizeGpuProfileAfterDeviceEnd(
            const std::uint64_t completedFrameNumber)
        {
            if (!m_ActiveGpuProfile.has_value())
            {
                m_LastRenderGraphStats.GpuProfile =
                    m_CurrentGpuProfile;
                return;
            }

            ActiveGpuProfileFrame& active =
                *m_ActiveGpuProfile;
            const std::optional<RHI::ProfilerError> failure =
                GpuProfileFailure();
            const bool globalFrameAdvanced =
                completedFrameNumber > active.Frame.FrameNumber;
            const bool submit =
                active.ExecuteSucceeded &&
                !failure.has_value() &&
                globalFrameAdvanced;
            auto endResult = active.Profiler->EndFrame(
                active.Frame,
                submit
                    ? RHI::ProfilerFrameDisposition::Submitted
                    : RHI::ProfilerFrameDisposition::Discarded);
            if (submit && endResult)
            {
                if (active.Frame.FrameSlot >=
                    m_SubmittedGpuProfiles.size())
                {
                    m_SubmittedGpuProfiles.resize(
                        active.Frame.FrameSlot + 1u);
                }
                m_SubmittedGpuProfiles[
                    active.Frame.FrameSlot] =
                    SubmittedGpuProfileFrame{
                        .Frame = active.Frame,
                        .Passes = std::move(active.Passes),
                    };
                UpdateCurrentGpuProfileStatus(
                    RenderGraphGpuProfileStatus::Submitted,
                    "GPU profile candidate submitted; resolution waits for "
                    "the reused slot's fence proof.");
            }
            else
            {
                if (submit && !endResult)
                {
                    (void)active.Profiler->EndFrame(
                        active.Frame,
                        RHI::ProfilerFrameDisposition::Discarded);
                }
                const RHI::ProfilerError error =
                    failure.value_or(
                        endResult
                            ? RHI::ProfilerError::InvalidState
                            : endResult.error());
                PublishStaleGpuProfileStatus(
                    failure.has_value() || !endResult
                        ? ProfileStatusForError(error)
                        : RenderGraphGpuProfileStatus::
                              InvalidLifecycle,
                    globalFrameAdvanced
                        ? "GPU profile candidate was discarded."
                        : "GPU profile candidate was discarded because "
                          "device submission did not advance the global "
                          "frame.");
            }
            m_ActiveGpuProfile.reset();
            m_GpuProfileFailure.store(
                0u,
                std::memory_order_release);
            m_LastRenderGraphStats.GpuProfile =
                m_CurrentGpuProfile;
        }

        void ResetFrameState()
        {
            m_ActiveRuntimeSnapshotReadSlot = 0u;
            if (m_Subsystems.MaterialSystemRegistry())
            {
                m_Subsystems.MaterialSystemRegistry()->ResetPerFrameSubstitutionCounters();
            }
            m_HasExtractedRenderWorld = false;
            m_HasPreparedFrame = false;
            m_LastRenderPrepResult = {};
            m_LastRenderGraphStats = {};
            ResetCommandRecordStats();
        }

        [[nodiscard]] RHI::CameraUBO BuildCameraUbo(const RenderWorld& world,
                                                    const std::uint32_t frameIndex) const
        {
            RHI::CameraUBO camera{};
            camera.View = world.Camera.View;
            camera.Proj = world.Camera.Projection;
            camera.ViewProj = world.Camera.ViewProjection;
            camera.CameraPosition = glm::vec4{world.Camera.Position, 0.f};
            camera.CameraDirection = glm::vec4{world.Camera.Forward, 0.f};
            camera.ViewportWidth = world.Viewport.Width > 0 ? static_cast<float>(world.Viewport.Width) : 0.f;
            camera.ViewportHeight = world.Viewport.Height > 0 ? static_cast<float>(world.Viewport.Height) : 0.f;
            camera.NearPlane = world.Camera.NearPlane;
            camera.FarPlane = world.Camera.FarPlane;
            camera.FrameIndex = frameIndex;
            camera.CullingFlags = world.Camera.ExplicitCameraTransition
                ? RHI::CameraCulling_ExplicitTransition
                : RHI::CameraCulling_None;

            if (world.Camera.Valid)
            {
                if (IsInvertibleFiniteMatrix(world.Camera.View))
                {
                    camera.InvView = glm::inverse(world.Camera.View);
                }
                if (IsInvertibleFiniteMatrix(world.Camera.Projection))
                {
                    camera.InvProj = glm::inverse(world.Camera.Projection);
                }
            }

            if (m_Subsystems.LightSystemRegistry())
            {
                m_Subsystems.LightSystemRegistry()->ApplyTo(camera);
            }
            if (m_Subsystems.ShadowSystemRegistry())
            {
                m_Subsystems.ShadowSystemRegistry()->ApplyTo(camera);
            }
            return camera;
        }

        void ResetCommandRecordStats()
        {
            std::lock_guard<std::mutex> lock(m_CommandRecordStatsMutex);
            m_CommandRecordStats = {};
        }

        [[nodiscard]] RenderGraphCommandRecordStats SnapshotCommandRecordStats()
        {
            std::lock_guard<std::mutex> lock(m_CommandRecordStatsMutex);
            return m_CommandRecordStats;
        }

        void PublishCommandRecordStats()
        {
            m_LastRenderGraphStats.CommandRecords = SnapshotCommandRecordStats();
        }

        [[nodiscard]] bool CommandRecordPassRecorded(const FramePassId passId)
        {
            std::lock_guard<std::mutex> lock(m_CommandRecordStatsMutex);
            return std::any_of(m_CommandRecordStats.Passes.begin(),
                               m_CommandRecordStats.Passes.end(),
                               [passId](const RenderGraphCommandPassStats& pass)
                               {
                                   return pass.Id == passId &&
                                          pass.Status == RenderCommandPassStatus::Recorded;
                               });
        }

        void AccumulateCommandRecordStatus(const std::string_view passName,
                                           const FramePassId passId,
                                           const RenderCommandPassStatus status)
        {
            std::lock_guard<std::mutex> lock(m_CommandRecordStatsMutex);
            m_CommandRecordStats.Passes.push_back(RenderGraphCommandPassStats{
                .Name = std::string{passName},
                .Id = passId,
                .Status = status,
            });

            switch (status)
            {
            case RenderCommandPassStatus::Recorded:
                ++m_CommandRecordStats.Recorded;
                break;
            case RenderCommandPassStatus::SkippedNonOperational:
                ++m_CommandRecordStats.Skipped;
                ++m_CommandRecordStats.SkippedNonOperational;
                break;
            case RenderCommandPassStatus::SkippedUnavailable:
                ++m_CommandRecordStats.Skipped;
                ++m_CommandRecordStats.SkippedUnavailable;
                break;
            }
        }

        void NotePickingReadbackCopyIssued(const std::uint32_t frameIndex,
                                           const std::uint32_t slot,
                                           const std::uint32_t pickX,
                                           const std::uint32_t pickY,
                                           const std::uint64_t sequence,
                                           const bool depthCopied)
        {
            std::lock_guard<std::mutex> lock(m_ReadbackIssueMutex);
            ++m_LastRenderGraphStats.PickingReadbackCopyCount;

            if (slot < m_PickingSlotPending.size())
            {
                m_PickingSlotPending[slot] = true;
                m_PickingSlotIssuedFrame[slot] = frameIndex;
                m_PickingSlotRequest[slot] = PickPixelRequest{
                    .X = pickX,
                    .Y = pickY,
                    .Pending = true,
                };
                m_PickingSlotInvalidated[slot] = false;
                m_PickingSlotSequence[slot] = sequence;
                m_PickingSlotDepthCopied[slot] = depthCopied;
            }
        }

        void NoteHistogramReadbackCopyIssued(const std::uint32_t frameIndex,
                                             const std::uint32_t slot)
        {
            std::lock_guard<std::mutex> lock(m_ReadbackIssueMutex);
            ++m_LastRenderGraphStats.HistogramReadbackCopyCount;

            if (slot < m_HistogramSlotPending.size())
            {
                m_HistogramSlotPending[slot] = true;
                m_HistogramSlotIssuedFrame[slot] = frameIndex;
                m_HistogramSlotInvalidated[slot] = false;
            }
        }

        [[nodiscard]] RenderCommandPassStatus RecordReconstructionPass(const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_ReconstructionHistorySystem.has_value() ||
                !m_ReconstructionHistorySystem->IsAllocated())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            constexpr Core::Extent2D kReferenceExtent{.Width = 1u, .Height = 1u};
            const std::array<glm::vec4, 1u> currentColor{
                glm::vec4{0.25f, 0.5f, 0.75f, 1.0f},
            };
            const std::array<float, 1u> depth{1.0f};
            const std::array<glm::vec2, 1u> motion{glm::vec2{0.0f}};
            const std::array<glm::vec4, 1u> historyColor{
                glm::vec4{0.25f, 0.5f, 0.75f, 1.0f},
            };
            std::array<glm::vec4, 1u> output{};

            const float exposure = m_Subsystems.PostProcessSystemRegistry().has_value()
                ? m_Subsystems.PostProcessSystemRegistry()->GetSettings().Exposure
                : 1.0f;
            const ReconstructionResult result = m_ReferenceTAAReconstructor.Apply(
                ReconstructionColorView{.Pixels = currentColor, .Extent = kReferenceExtent},
                ReconstructionDepthView{.Pixels = depth, .Extent = kReferenceExtent},
                ReconstructionMotionVectorView{.Pixels = motion, .Extent = kReferenceExtent},
                ReconstructionColorView{.Pixels = historyColor, .Extent = kReferenceExtent},
                ReconstructionOutputView{.Pixels = output, .Extent = kReferenceExtent},
                ReconstructionHints{
                    .Sharpness = 0.5f,
                    .Exposure = exposure,
                    .JitterOffset = glm::vec2{
                        m_LastRenderGraphStats.JitterOffsetX,
                        m_LastRenderGraphStats.JitterOffsetY,
                    },
                    .FrameIndex = frameIndex,
                    .InputExtent = kReferenceExtent,
                    .OutputExtent = kReferenceExtent,
                    .Reset = false,
                });
            if (!result.Applied)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            ++m_LastRenderGraphStats.ReconstructorAppliedFrames;
            m_LastRenderGraphStats.HistoryDisocclusionPercent = result.DisocclusionPercent * 100.0f;
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordCullingPass(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            std::optional<GpuWorld>& gpuWorld =
                m_Subsystems.GpuWorldSystem();
            if (!gpuWorld.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }
            // The culling pass has no render attachments and is the explicit
            // predecessor of every managed-buffer consumer, including UvView.
            // Record upload visibility before any availability soft-skip so a
            // missing culling pipeline cannot strand freshly uploaded geometry.
            gpuWorld->SubmitPendingUploadBarriers(cmd);
            if (!m_CullingOutputAvailable)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_Subsystems.CullingSystemRegistry()->ResetCounters(cmd);
            m_Subsystems.CullingSystemRegistry()->DispatchCull(
                cmd, camera, *gpuWorld);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-070 — default-recipe forward surface command recording.
        // Called from the executor's `"SurfacePass"` branch when the active
        // recipe is the forward variant (`!usesDeferred`). Routes through the
        // same `RenderCommandPassStatus` taxonomy as the depth prepass: a
        // non-operational device returns `SkippedNonOperational`; a missing
        // pipeline lease or culling output / SurfaceOpaque bucket returns
        // `SkippedUnavailable`; otherwise the existing `ForwardSurfacePass`
        // body records the `Bind/Bind/Push/DrawIndexedIndirectCount` shape and
        // we return `Recorded`. The deferred-mode surface body is owned by
        // GRAPHICS-072 and falls through to the catch-all soft-skip.
        [[nodiscard]] RenderCommandPassStatus RecordForwardSurfacePass(RHI::ICommandContext& cmd,
                                                                       const RHI::CameraUBO& camera,
                                                                       const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ForwardSurfacePass.has_value() ||
                !m_ForwardSurfacePipelineLease.has_value() ||
                !m_ForwardSurfacePipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ForwardSurfacePass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordForwardLinePass(RHI::ICommandContext& cmd,
                                                                    const RHI::CameraUBO& camera,
                                                                    const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ForwardLinePass.has_value() ||
                !m_ForwardLinePipelineLease.has_value() ||
                !m_ForwardLinePipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ForwardLinePass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordForwardPointPass(RHI::ICommandContext& cmd,
                                                                     const RHI::CameraUBO& camera,
                                                                     const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ForwardPointPass.has_value() ||
                !m_ForwardPointPipelineLease.has_value() ||
                !m_ForwardPointPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ForwardPointPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-073 Slice A — default-recipe `"ShadowPass"` route. The
        // recipe only declares the pass when `EnableShadows` is on, so the
        // executor reaches this helper only for shadow-enabled frames. The
        // `ShadowSystem::IsEnabled()` gate inside `ShadowPass::Execute` keeps
        // the bind/draw shape silent if cascade/atlas params end up disabled
        // after the recipe build.
        [[nodiscard]] RenderCommandPassStatus RecordShadowPass(RHI::ICommandContext& cmd,
                                                                const RHI::CameraUBO& camera,
                                                                const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ShadowPass.has_value() ||
                !m_ShadowPipelineLease.has_value() ||
                !m_ShadowPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ShadowPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-074 Slice A — default-recipe `"PickingPass"` route. The
        // recipe declares the pass for actual picking and for selection
        // outline ID generation. Mirrors `RecordForwardSurfacePass` /
        // `RecordShadowPass`: a non-operational device →
        // `SkippedNonOperational`; a missing culling output, pass, lease,
        // `GpuWorld`, or culling system → `SkippedUnavailable`; otherwise
        // `EntityIdPass::Execute` records the
        // `Bind/Bind/Push/DrawIndexedIndirectCount` shape against the
        // `SurfaceOpaque` cull bucket and we return `Recorded`. The
        // primitive-id sub-passes and readback copy are only needed for an
        // actual pending pick request.
        [[nodiscard]] RenderCommandPassStatus RecordSelectionEntityIdPass(RHI::ICommandContext& cmd,
                                                                           const RHI::CameraUBO& camera,
                                                                           const std::uint32_t frameIndex,
                                                                           const bool primitivePickingActive)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            const auto& pipelineLease = primitivePickingActive
                ? m_SelectionEntityIdPipelineLease
                : m_SelectionEntityIdOutlinePipelineLease;
            if (!m_CullingOutputAvailable || !m_SelectionEntityIdPass.has_value() ||
                !pipelineLease.has_value() ||
                !pipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionEntityIdPass->SetPipeline(
                m_Subsystems.PipelineManager()->GetDeviceHandle(pipelineLease->GetHandle()));
            m_SelectionEntityIdPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-074 Slice B — default-recipe Face / Edge / Point
        // sub-pass routes inside the `"PickingPass"` executor branch.
        // Each helper mirrors `RecordSelectionEntityIdPass` exactly: a
        // non-operational device → `SkippedNonOperational`; missing
        // culling output, pass, lease, GpuWorld, or CullingSystem →
        // `SkippedUnavailable`; otherwise the bucket-bound
        // `FaceIdPass` / `EdgeIdPass` / `PointIdPass` `Execute(...)`
        // records the matching `Bind/Bind/Push/DrawIndexedIndirectCount`
        // (or non-indexed `DrawIndirectCount` for points) shape and
        // returns `Recorded`. The four sub-passes share the recipe's
        // `PickingPass` render pass, so all bound pipelines are
        // render-pass-compatible (two R32_UINT color targets + D32_FLOAT
        // depth, depth-equal / depth-write-off). The Face/Edge/Point
        // pipelines write the matching `EncodeSelectionId(domain,
        // gl_PrimitiveID)` value into `PrimitiveId` while still emitting
        // the per-instance stable entity ID into `EntityId`, so the
        // last-pass-wins-per-pixel behavior after depth-equal yields the
        // most refined domain code that survives the prepass depth test.
        // The `Picking.Readback` drain + `PublishPickResult` /
        // `PublishNoHit` wiring remain Slice D scope.
        [[nodiscard]] RenderCommandPassStatus RecordSelectionFaceIdPass(RHI::ICommandContext& cmd,
                                                                         const RHI::CameraUBO& camera,
                                                                         const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionFaceIdPass.has_value() ||
                !m_SelectionFaceIdPipelineLease.has_value() ||
                !m_SelectionFaceIdPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionFaceIdPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordSelectionEdgeIdPass(RHI::ICommandContext& cmd,
                                                                         const RHI::CameraUBO& camera,
                                                                         const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionEdgeIdPass.has_value() ||
                !m_SelectionEdgeIdPipelineLease.has_value() ||
                !m_SelectionEdgeIdPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionEdgeIdPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordSelectionPointIdPass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera,
                                                                          const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionPointIdPass.has_value() ||
                !m_SelectionPointIdPipelineLease.has_value() ||
                !m_SelectionPointIdPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionPointIdPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-074 Slice C/D.4 — default-recipe `"SelectionOutlinePass"`
        // route. The recipe only declares the pass when
        // `features.EnableSelectionOutline` is true, so this helper is
        // reached only when at least one selectable entity is present this
        // frame. Mirrors the selection-ID helpers above with the
        // fullscreen-pass shape (no culling/GpuWorld prerequisites since
        // the pass body is `BindPipeline + PushConstants + Draw(3,1,0,0)`):
        // a non-operational device → `SkippedNonOperational`; missing pass /
        // lease → `SkippedUnavailable`; otherwise
        // `SelectionOutlinePass::Execute` records the fullscreen draw and
        // we return `Recorded`. Slice D.4 sources the push payload from
        // `renderWorld.Selection` so the shader actually sees the seeded
        // hovered/selected ids and outline style instead of the Slice C
        // all-zero placeholder.
        [[nodiscard]] RenderCommandPassStatus RecordSelectionOutlinePass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera,
                                                                          const std::uint32_t frameIndex,
                                                                          const SelectionSnapshot& selection)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_SelectionOutlinePass.has_value() ||
                !m_SelectionOutlinePipelineLease.has_value() ||
                !m_SelectionOutlinePipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const SelectionOutlinePushConstants pushConstants =
                BuildSelectionOutlinePushConstants(selection);
            m_SelectionOutlinePass->Execute(cmd, camera, frameIndex, pushConstants);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice A — default-recipe `"PostProcessPass"` umbrella
        // executor route, ToneMap leg. Mirrors the selection-outline helper
        // above (fullscreen-pass shape: no `GpuWorld` / `CullingSystem`
        // prerequisites since `PostProcessToneMapPass::Execute` records
        // `BindPipeline + PushConstants + Draw(3,1,0,0)`): a non-operational
        // device → `SkippedNonOperational`; a missing pass / lease /
        // `PostProcessSystem` → `SkippedUnavailable`; otherwise the tonemap
        // pass records the fullscreen draw and we return `Recorded`. The
        // `Pass::Execute` body additionally early-returns when
        // `IsStageEnabled(ToneMap)` is false (i.e. when
        // `PostProcessSettings::Enabled` was flipped off), so a disabled
        // chain becomes a structurally-recorded no-op rather than altering
        // the executor's per-pass status taxonomy. The Slices B–E
        // Histogram / Bloom / FXAA / SMAA helpers fan out from the same
        // umbrella branch (mirroring the GRAPHICS-074 `"PickingPass"`
        // sub-pass pattern).
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessToneMapPass(RHI::ICommandContext& cmd,
                                                                            const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_Subsystems.PostProcessSystemRegistry().has_value() ||
                !m_PostProcessToneMapPass.has_value() ||
                !m_PostProcessToneMapPipelineLease.has_value() ||
                !m_PostProcessToneMapPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessToneMapPass->Execute(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice B.1 — default-recipe `"PostProcessPass"`
        // umbrella executor route, Bloom leg. Mirrors the tonemap helper
        // above: a non-operational device → `SkippedNonOperational`; a
        // missing pass / system → `SkippedUnavailable`; both bloom leases
        // missing or invalid → `SkippedUnavailable`; otherwise the bloom
        // pass records its placeholder downsample + upsample bind/push/
        // draw for whichever stage's lease succeeded and we return
        // `Recorded`. Crucially the helper requires only ONE valid bloom
        // pipeline lease to proceed — the per-stage early-skips inside
        // `PostProcessBloomPass::Execute` independently gate the
        // downsample and upsample bind/push/draw on their own lease
        // validity, so a partial pipeline outage (e.g. only the upsample
        // shader compiles) still records the surviving stage rather than
        // collapsing the whole bloom leg into a SkippedUnavailable. The
        // `Execute` body additionally early-returns when
        // `IsStageEnabled(Bloom)` is false (i.e. when
        // `PostProcessSettings::EnableBloom` is off or the chain is
        // globally disabled), so a disabled bloom chain becomes a
        // structurally-recorded no-op rather than altering the executor's
        // per-pass status taxonomy. Slice B.2 keeps this helper and adds
        // per-mip iteration with the matching inline barriers.
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessBloomPass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            const bool hasDownsamplePipeline =
                m_PostProcessBloomDownsamplePipelineLease.has_value() &&
                m_PostProcessBloomDownsamplePipelineLease->IsValid();
            const bool hasUpsamplePipeline =
                m_PostProcessBloomUpsamplePipelineLease.has_value() &&
                m_PostProcessBloomUpsamplePipelineLease->IsValid();
            if (!m_Subsystems.PostProcessSystemRegistry().has_value() ||
                !m_PostProcessBloomPass.has_value() ||
                (!hasDownsamplePipeline && !hasUpsamplePipeline))
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessBloomPass->Execute(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice E.1 — default-recipe
        // `"PostProcessHistogramPass"` executor route. The histogram is
        // a compute dispatch (`vkCmdDispatch(ceil(W/16), ceil(H/16),
        // 1)`) and Vulkan rejects dispatches inside an active render-
        // pass scope, so it runs in its own ordered graph pass before
        // `"PostProcessPass"` (declared by the recipe with
        // `Read(SceneColorHDR, ShaderRead)` + `Write(PostProcess.Histogram,
        // BufferUsage::ShaderWrite)`). The helper follows the same
        // status taxonomy as the tonemap / bloom helpers: a non-
        // operational device returns `SkippedNonOperational`; a missing
        // system / pass / pipeline lease returns `SkippedUnavailable`;
        // otherwise the helper invokes `Execute(...)` and returns
        // `Recorded`. The pass body independently gates on
        // `IsStageEnabled(Histogram)` so when histogram is gated off
        // the body emits no bind/push/dispatch but the helper still
        // records `Recorded` ("structurally-recorded no-op" taxonomy,
        // same as bloom-disabled and the Slice C/D.1 FXAA/SMAA
        // helpers). Slice E.2 adds the renderer-owned host-visible
        // `Histogram.Readback` buffer + `BeginFrame()`-side drain +
        // `PostProcessSystem::PublishHistogramReadback(...)` that
        // consumes the exposure-adaptation history buffer.
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessHistogramPass(RHI::ICommandContext& cmd,
                                                                              const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_Subsystems.PostProcessSystemRegistry().has_value() ||
                !m_PostProcessHistogramPass.has_value() ||
                !m_PostProcessHistogramPipelineLease.has_value() ||
                !m_PostProcessHistogramPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessHistogramPass->Execute(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice D.2a — per-stage AA helpers. The AA umbrella
        // splits into three ordered graph passes so edge / blend /
        // resolve pipelines can target format-incompatible color
        // attachments. FXAA records under the resolve pass only; SMAA
        // records under all three. Each helper follows the same status
        // taxonomy as the tonemap / bloom helpers: a non-operational
        // device returns `SkippedNonOperational`; a missing system /
        // pass / pipeline lease returns `SkippedUnavailable`; otherwise
        // the helper invokes the matching per-stage Execute and returns
        // `Recorded`. The pass body independently gates on
        // `IsStageEnabled(...)` so when AA is gated off the body emits
        // no bind/push/draw but the helper still records `Recorded`
        // ("structurally-recorded no-op" taxonomy, same as bloom-
        // disabled and the Slice D.1 SMAA/FXAA helpers).
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessAAEdgePass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_Subsystems.PostProcessSystemRegistry().has_value() ||
                !m_PostProcessSMAAPass.has_value() ||
                !m_PostProcessSMAAEdgePipelineLease.has_value() ||
                !m_PostProcessSMAAEdgePipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessSMAAPass->ExecuteEdge(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordPostProcessAABlendPass(RHI::ICommandContext& cmd,
                                                                           const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_Subsystems.PostProcessSystemRegistry().has_value() ||
                !m_PostProcessSMAAPass.has_value() ||
                !m_PostProcessSMAABlendPipelineLease.has_value() ||
                !m_PostProcessSMAABlendPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessSMAAPass->ExecuteBlend(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] FrameRecipeAAMode SelectedFrameRecipeAAMode() const noexcept
        {
            if (!m_Subsystems.PostProcessSystemRegistry().has_value())
            {
                return FrameRecipeAAMode::NoAA;
            }
            switch (m_Subsystems.PostProcessSystemRegistry()->GetSettings().AntiAliasing)
            {
            case PostProcessAntiAliasing::None:
                return FrameRecipeAAMode::NoAA;
            case PostProcessAntiAliasing::FXAA:
                return FrameRecipeAAMode::FXAA;
            case PostProcessAntiAliasing::SMAA:
                return FrameRecipeAAMode::SMAA;
            case PostProcessAntiAliasing::TAA:
                return FrameRecipeAAMode::TAA;
            case PostProcessAntiAliasing::ExternalReconstructor:
                return FrameRecipeAAMode::ExternalReconstructor;
            }
            return FrameRecipeAAMode::NoAA;
        }

        // Reports whether the currently-selected spatial AA mode's pipeline(s)
        // are all available. For `None` this is trivially false (AA is
        // off). For `FXAA` it requires the FXAA pipeline lease. For
        // `SMAA` it requires all three SMAA pipeline leases — the
        // resolve shader reads `AATemp.Weights` and the blend shader
        // reads `AATemp.Edges`, so if either upstream pipeline is
        // missing the resolved attachment is sourced from cleared
        // inputs and the AA leg cannot produce a usable image. The
        // recipe-build site uses this to gate
        // `FrameRecipeFeatures::EnableAntiAliasing`, and
        // `RecordPostProcessAAResolvePass` mirrors the same gate so a
        // user-selected AA mode without its matching pipeline returns
        // `SkippedUnavailable` instead of falsely reporting `Recorded`
        // against a no-op draw.
        [[nodiscard]] bool SelectedAntiAliasingPipelinesAvailable() const noexcept
        {
            if (!m_Subsystems.PostProcessSystemRegistry().has_value())
            {
                return false;
            }
            const PostProcessAntiAliasing aa = m_Subsystems.PostProcessSystemRegistry()->GetSettings().AntiAliasing;
            switch (aa)
            {
            case PostProcessAntiAliasing::None:
            case PostProcessAntiAliasing::TAA:
            case PostProcessAntiAliasing::ExternalReconstructor:
                return false;
            case PostProcessAntiAliasing::FXAA:
                return m_PostProcessFXAAPipelineLease.has_value() &&
                       m_PostProcessFXAAPipelineLease->IsValid();
            case PostProcessAntiAliasing::SMAA:
                return m_PostProcessSMAAEdgePipelineLease.has_value() &&
                       m_PostProcessSMAAEdgePipelineLease->IsValid() &&
                       m_PostProcessSMAABlendPipelineLease.has_value() &&
                       m_PostProcessSMAABlendPipelineLease->IsValid() &&
                       m_PostProcessSMAAResolvePipelineLease.has_value() &&
                       m_PostProcessSMAAResolvePipelineLease->IsValid();
            }
            return false;
        }

        // Resolve runs whichever stage matches
        // `PostProcessSettings::AntiAliasing`. When AA is `None` the
        // body is a structurally-recorded no-op (neither sub-stage
        // emits draws, and `presentSource` stays on `SceneColorLDR` via
        // `FrameRecipeFeatures::EnableAntiAliasing = false`). When AA
        // is `FXAA` or `SMAA` the matching pipeline must be available
        // — otherwise we return `SkippedUnavailable` and the
        // recipe-build site has already kept `presentSource` on
        // `SceneColorLDR`, so present still sees a usable image.
        // Falling back to "either pipeline is good enough" here would
        // hide the mismatch: with AA = FXAA + only the SMAA-resolve
        // lease (or vice versa) both pass bodies' `IsStageEnabled`
        // gate would short-circuit, neither stage would draw, the
        // helper would report `Recorded`, and the recipe could already
        // route present to the unwritten resolved attachment.
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessAAResolvePass(RHI::ICommandContext& cmd,
                                                                             const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_Subsystems.PostProcessSystemRegistry().has_value() ||
                !m_PostProcessFXAAPass.has_value() ||
                !m_PostProcessSMAAPass.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const PostProcessAntiAliasing aa = m_Subsystems.PostProcessSystemRegistry()->GetSettings().AntiAliasing;
            const bool hasFxaaPipeline =
                m_PostProcessFXAAPipelineLease.has_value() &&
                m_PostProcessFXAAPipelineLease->IsValid();
            const bool hasSmaaResolvePipeline =
                m_PostProcessSMAAResolvePipelineLease.has_value() &&
                m_PostProcessSMAAResolvePipelineLease->IsValid();

            switch (aa)
            {
            case PostProcessAntiAliasing::None:
            case PostProcessAntiAliasing::TAA:
            case PostProcessAntiAliasing::ExternalReconstructor:
                // Structurally-recorded no-op: both bodies' selector
                // gate short-circuits regardless of which pipelines
                // exist; `presentSource` stays on `SceneColorLDR`.
                break;
            case PostProcessAntiAliasing::FXAA:
                if (!hasFxaaPipeline)
                {
                    return RenderCommandPassStatus::SkippedUnavailable;
                }
                m_PostProcessFXAAPass->Execute(cmd, camera);
                break;
            case PostProcessAntiAliasing::SMAA:
                if (!hasSmaaResolvePipeline)
                {
                    return RenderCommandPassStatus::SkippedUnavailable;
                }
                m_PostProcessSMAAPass->ExecuteResolve(cmd, camera);
                break;
            }
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-072 Slice A — default-recipe deferred-mode `"SurfacePass"`
        // route. Reached from the executor lambda when
        // `defaultRecipeUsesDeferred` is true and the active pass is
        // `"SurfacePass"`. Mirrors `RecordForwardSurfacePass` exactly: a
        // non-operational device → `SkippedNonOperational`; a missing
        // culling output, pass, lease, GpuWorld, or culling system →
        // `SkippedUnavailable`; otherwise `DeferredGBufferPass::Execute`
        // records the `Bind/Bind/Push/DrawIndexedIndirectCount` shape and
        // we return `Recorded`. The deferred-lighting composition body is
        // owned by GRAPHICS-072 Slice B and currently falls through to the
        // catch-all soft-skip.
        [[nodiscard]] RenderCommandPassStatus RecordDeferredGBufferPass(RHI::ICommandContext& cmd,
                                                                         const RHI::CameraUBO& camera,
                                                                         const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_DeferredGBufferPass.has_value() ||
                !m_DeferredGBufferPipelineLease.has_value() ||
                !m_DeferredGBufferPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value() || !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DeferredGBufferPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-072 Slice B — default-recipe `"CompositionPass"` route.
        // Reached from the executor lambda when `BuildDefaultFrameRecipe`
        // declared the deferred lighting pass (i.e. `usesDeferred`). The
        // recipe wires `CompositionPass` to read `SceneNormal`/`Albedo`/
        // `Material0` produced by the deferred-mode `SurfacePass`, so this
        // helper inherits the GBuffer pass's prerequisites in addition to
        // its own: a non-operational device → `SkippedNonOperational`;
        // missing culling output, GBuffer pass/lease, lighting pass/lease,
        // GpuWorld, or CullingSystem → `SkippedUnavailable` (so a failed
        // GBuffer record never lets lighting consume cleared/unwritten
        // attachments); otherwise `DeferredLightingPass::Execute` records
        // the `Bind/Push/Draw(3,1,0,0)` fullscreen shape and we return
        // `Recorded`. The shadow-atlas descriptor binding at `set 1,
        // binding 1` is Slice C scope.
        [[nodiscard]] RenderCommandPassStatus RecordDeferredLightingPass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera,
                                                                          const std::uint32_t frameIndex)
        {
            (void)frameIndex;
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            // GBuffer prerequisites: if `RecordDeferredGBufferPass` would
            // have returned `SkippedUnavailable`, the lighting pass must
            // mirror that taxonomy rather than recording against
            // uninitialized GBuffer attachments.
            if (!m_CullingOutputAvailable || !m_DeferredGBufferPass.has_value() ||
                !m_DeferredGBufferPipelineLease.has_value() ||
                !m_DeferredGBufferPipelineLease->IsValid() ||
                !m_Subsystems.CullingSystemRegistry().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }
            if (!m_DeferredLightingPass.has_value() ||
                !m_DeferredLightingPipelineLease.has_value() ||
                !m_DeferredLightingPipelineLease->IsValid() ||
                !m_Subsystems.GpuWorldSystem().has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DeferredLightingPass->Execute(cmd, camera, *m_Subsystems.GpuWorldSystem());
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordDepthPrepass(RHI::ICommandContext& cmd,
                                                                 const RHI::CameraUBO& camera,
                                                                 const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            // DepthPrepass consumes the indirect draw output written by the
            // culling pass, so the cached culling output must exist before it
            // records commands.
            if (!m_CullingOutputAvailable || !m_DepthPrepassPipelineLease.has_value() ||
                !m_DepthPrepassPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DepthPrepassPass.Execute(cmd, camera, *m_Subsystems.GpuWorldSystem(), *m_Subsystems.CullingSystemRegistry(), frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-038B — default-recipe `"HZBBuildPass"` executor route.
        // The recipe declares this compute pass only after a valid retained
        // `HZB.Current` import is available. This slice records the deterministic
        // per-mip fallback command shape; the optional single-pass/SPD backend
        // path is covered by the pure planner and reserved for a later
        // capability-gated backend implementation.
        [[nodiscard]] RenderCommandPassStatus RecordHZBBuildPass(RHI::ICommandContext& cmd)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_HZBSystem.has_value() ||
                !m_HZBSystem->IsAllocated() ||
                !m_CullingOutputAvailable ||
                !m_DepthPrepassPipelineLease.has_value() ||
                !m_DepthPrepassPipelineLease->IsValid() ||
                !m_HZBBuildPipelineLease.has_value() ||
                !m_HZBBuildPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const HZBBuildDispatchPlan plan = ComputeHZBBuildDispatchPlan(
                m_HZBSystem->GetAllocatedDesc(),
                HZBBuildCapabilities{.SupportsSinglePassMipChain = false});
            if (!RecordHZBBuild(cmd, GetHZBBuildPipeline(), m_HZBSystem->CurrentHZB(), plan))
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            ++m_LastRenderGraphStats.HZBBuildRecordedFrames;
            m_LastRenderGraphStats.HZBBuildDispatchCount +=
                static_cast<std::uint32_t>(plan.Dispatches.size());
            m_LastRenderGraphStats.HZBBuildMipCount += plan.Desc.MipLevels;
            if (plan.Mode == HZBBuildMode::SinglePassMipChain)
            {
                ++m_LastRenderGraphStats.HZBBuildSinglePassFrames;
            }
            else
            {
                ++m_LastRenderGraphStats.HZBBuildFallbackFrames;
            }
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordClusterGridBuildPass(RHI::ICommandContext& cmd)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!ClusterLightResourcesReady() ||
                !m_ClusterGridBuildPipelineLease.has_value() ||
                !m_ClusterGridBuildPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const ClusterGridBuildDispatchPlan plan =
                ComputeClusterGridBuildDispatchPlan(m_ClusterGridDesc);
            const RHI::BufferHandle aabbHandle = m_ClusterGridAABBBuffer->GetHandle();
            const std::uint64_t aabbAddress = m_Device->GetBufferDeviceAddress(aabbHandle);
            if (!RecordClusterGridBuild(cmd,
                                        GetClusterGridBuildPipeline(),
                                        aabbHandle,
                                        aabbAddress,
                                        plan,
                                        m_ClusterGridProjection))
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            ++m_LastRenderGraphStats.ClusterGridBuildRecordedFrames;
            ++m_LastRenderGraphStats.ClusterGridBuildDispatchCount;
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordClusterLightAssignmentPass(RHI::ICommandContext& cmd)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_Subsystems.GpuWorldSystem() ||
                !ClusterLightResourcesReady() ||
                !m_ClusterGridBuildPipelineLease.has_value() ||
                !m_ClusterGridBuildPipelineLease->IsValid() ||
                !m_ClusterLightAssignmentPipelineLease.has_value() ||
                !m_ClusterLightAssignmentPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const ClusterLightAssignmentDispatchPlan plan =
                ComputeClusterLightAssignmentDispatchPlan(
                    m_ClusterGridDesc,
                    m_Subsystems.GpuWorldSystem() ? m_Subsystems.GpuWorldSystem()->GetLightCount() : 0u,
                    kMaxClusterLightsPerCell);
            const RHI::BufferHandle aabbHandle = m_ClusterGridAABBBuffer->GetHandle();
            const RHI::BufferHandle lightsHandle = m_Subsystems.GpuWorldSystem()->GetLightBuffer();
            const RHI::BufferHandle headerHandle = m_ClusterLightHeaderBuffer->GetHandle();
            const RHI::BufferHandle indexHandle = m_ClusterLightIndexBuffer->GetHandle();
            const RHI::BufferHandle counterHandle = m_ClusterLightCounterBuffer->GetHandle();
            if (!RecordClusterLightAssignment(cmd,
                                              GetClusterLightAssignmentPipeline(),
                                              aabbHandle,
                                              m_Device->GetBufferDeviceAddress(aabbHandle),
                                              lightsHandle,
                                              m_Device->GetBufferDeviceAddress(lightsHandle),
                                              headerHandle,
                                              m_Device->GetBufferDeviceAddress(headerHandle),
                                              indexHandle,
                                              m_Device->GetBufferDeviceAddress(indexHandle),
                                              counterHandle,
                                              m_Device->GetBufferDeviceAddress(counterHandle),
                                              plan))
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            ++m_LastRenderGraphStats.ClusterLightAssignmentRecordedFrames;
            ++m_LastRenderGraphStats.ClusterLightAssignmentDispatchCount;
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-076 Slice A — canonical default-recipe present executor
        // helper. The `PresentPass::Execute()` body records the
        // `BindPipeline + Draw(3, 1, 0, 0)` shape unconditionally when its
        // pipeline handle is valid, so the helper only needs the
        // device-operational / pipeline-lease prerequisite checks the rest
        // of the default recipe's pass helpers already use; no per-pass
        // counter is added because the executor's `RenderCommandPassStatus`
        // taxonomy already distinguishes `Recorded` from
        // `SkippedNonOperational` / `SkippedUnavailable`.
        [[nodiscard]] RenderCommandPassStatus RecordPresentPass(RHI::ICommandContext& cmd)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PresentPipelineLease.has_value() ||
                !m_PresentPipelineLease->IsValid() ||
                !m_PresentPass.GetPipeline().IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PresentPass.Execute(cmd);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-079 Slice D.1 — canonical default-recipe `Pass.ImGui`
        // executor helper. Mirrors `RecordPresentPass` for the operational
        // gate and `RecordDebugViewPass` for the missing-resource taxonomy.
        // The `ImGuiPass` consumer only exists once the runtime hands in the
        // engine-owned `ImGuiOverlaySystem` (`SetImGuiOverlaySystem`), and the
        // pass body short-circuits internally on `!HasOverlayWork()`.
        //
        // The overlay draw is a `BindPipeline + DrawIndexed` sequence and must
        // only run inside a render pass. Slice D.1 promotes the default recipe
        // to read and write `FrameRecipe.PresentSource`, so
        // `BuildActiveRenderPassDesc(...).HasAttachments` is the live safety
        // signal: if it is false, the helper reports `SkippedUnavailable`
        // rather than recording invalid Vulkan command-buffer usage. With an
        // attached overlay payload and valid upload/pipeline resources, the
        // route records and publishes `ImGuiOverlayDiagnostics::DrawCalls`.
        [[nodiscard]] RHI::PipelineHandle ResolveImGuiPipelineForFormat(
            const RHI::Format colorFormat) const noexcept
        {
            if (!m_Subsystems.PipelineManager().has_value())
            {
                return {};
            }

            const auto deviceHandleFor =
                [this](const std::optional<RHI::PipelineManager::PipelineLease>& lease)
                    -> RHI::PipelineHandle
                {
                    if (!lease.has_value() || !lease->IsValid())
                    {
                        return {};
                    }
                    return m_Subsystems.PipelineManager()->GetDeviceHandle(lease->GetHandle());
                };

            if (colorFormat == RHI::Format::RGBA8_UNORM &&
                m_BackbufferFormat != RHI::Format::RGBA8_UNORM)
            {
                return deviceHandleFor(m_ImGuiRgba8PipelineLease);
            }
            return deviceHandleFor(m_ImGuiPipelineLease);
        }

        [[nodiscard]] RenderCommandPassStatus RecordImGuiPass(RHI::ICommandContext& cmd,
                                                              const RHI::Format activeColorFormat)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            const RHI::PipelineHandle imguiPipeline =
                ResolveImGuiPipelineForFormat(activeColorFormat);
            std::lock_guard<std::mutex> uploadLock(m_DynamicUploadMutex);
            if (activeColorFormat == RHI::Format::Undefined ||
                !m_ImGuiPass.has_value() ||
                m_ImGuiOverlaySystem == nullptr ||
                !imguiPipeline.IsValid() ||
                !m_ImGuiOverlaySystem->HasOverlayWork() ||
                !m_ImGuiUploadHelper)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }
            m_ImGuiPass->SetPipeline(imguiPipeline);

            m_ImGuiOverlaySystem->UploadPendingFontAtlas();
            m_Device->GetBindlessHeap().FlushPending();
            const ImGuiOverlayFrame* frame = m_ImGuiOverlaySystem->GetCurrentFrame();
            if (frame == nullptr)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const ImGuiUploadResult upload = m_ImGuiUploadHelper->UploadFrame(*frame);
            if (!upload.Uploaded)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ImGuiPass->Execute(cmd, upload);
            return m_ImGuiOverlaySystem->GetDiagnostics().DrawCalls > 0u
                ? RenderCommandPassStatus::Recorded
                : RenderCommandPassStatus::SkippedUnavailable;
        }

        // GRAPHICS-076 Slice B — canonical default-recipe debug-view
        // executor helper. Mirrors `RecordPresentPass` for the
        // operational/lease checks, and additionally gates on
        // `m_DebugViewSystem`'s resolved-selection enablement so a
        // disabled or unresolvable selection reports `SkippedUnavailable`
        // rather than silently no-op'ing inside `DebugViewPass::Execute`.
        // `DebugViewPass::Execute(cmd, camera)` itself short-circuits
        // when `!IsInitialized() || !pipeline.IsValid() ||
        // !selection.Enabled`, so this helper's gates are the renderer-
        // side observable surface for the same conditions; without
        // them the executor would record `Recorded` even though the
        // pass body emitted zero commands. The recipe-side gate
        // (`features.EnableDebugView`) already prevents this branch
        // from being reached when the world has no debug overlay /
        // transient debug, so the helper only sees frames where the
        // recipe enabled the pass.
        [[nodiscard]] RenderCommandPassStatus RecordDebugViewPass(RHI::ICommandContext& cmd,
                                                                   const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_DebugViewPipelineLease.has_value() ||
                !m_DebugViewPipelineLease->IsValid() ||
                !m_DebugViewPass.has_value() ||
                !m_DebugViewPass->GetPipeline().IsValid() ||
                !m_DebugViewSystem.has_value() ||
                !m_DebugViewSystem->IsInitialized())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }
            // Resolved-selection gate. `ExecuteFrame` already drives
            // `SetSettings(...) + ResolveSelection(recipeIntrospection)`
            // before the executor lambda runs, so by the time this helper
            // executes the resolved selection reflects the current frame's
            // world state and recipe declarations. If the resolved
            // selection is disabled (debug overlay off, requested resource
            // missing AND no usable fallback, etc.), the pass body is a
            // no-op and we surface that as `SkippedUnavailable` so the
            // executor taxonomy stays truthful.
            const DebugViewResolvedSelection resolved = m_DebugViewSystem->GetResolvedSelection();
            if (!resolved.Enabled)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DebugViewPass->Execute(cmd, camera);
            ++m_LastRenderGraphStats.DebugViewPassExecutions;
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-077 Slices B + C — operational executor helper for
        // the `TransientDebugSurfacePass`. Mirrors `RecordPresentPass`
        // for the operational gate and `RecordDebugViewPass` for the
        // missing-pipeline taxonomy. Each of the three lanes (triangle,
        // line, point) is gated independently on its own pipeline pair
        // (depth-tested + always-on-top); a lane with packets but a
        // missing or invalid pipeline pair increments
        // `MissingPipelineSkipCount` and is skipped, while other lanes
        // that have both packets and valid pipelines still record. The
        // pass status is `Recorded` when at least one lane recorded
        // its draws, and `SkippedUnavailable` when every lane that had
        // packets failed its pipeline gate (the recipe-side
        // `EnableTransientDebugSurface` derivation guarantees at least
        // one lane has packets when this branch is reached, so this is
        // an exhaustive cover of "feature on but everything failed").
        // `MissingPipelineSkipCount` continues to distinguish "feature
        // on, pipeline missing" from "feature off" (the latter does
        // not reach this branch at all).
        [[nodiscard]] RenderCommandPassStatus RecordTransientDebugSurfacePass(
            RHI::ICommandContext& cmd,
            const RenderWorld&    world)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            std::lock_guard<std::mutex> uploadLock(m_DynamicUploadMutex);

            // The helper is constructed alongside `m_Subsystems.BufferManager()` in
            // `Initialize(...)`; on a successful operational gate it
            // is always present. Defensive nullopt gate left in place
            // so a future refactor that defers helper construction
            // (e.g. behind a Vulkan-specific impl) cannot silently
            // regress to a no-op. When the helper is missing every
            // submitted lane counts as "pipeline missing" because the
            // pass cannot upload or record anything.
            if (!m_TransientDebugUploadHelper)
            {
                ++m_LastRenderGraphStats.TransientDebugUpload.MissingPipelineSkipCount;
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const auto trianglePipelinesValid =
                m_TransientDebugTrianglePipelineLeaseDepthTested.has_value() &&
                m_TransientDebugTrianglePipelineLeaseDepthTested->IsValid() &&
                m_TransientDebugTrianglePipelineLeaseAlwaysOnTop.has_value() &&
                m_TransientDebugTrianglePipelineLeaseAlwaysOnTop->IsValid() &&
                m_TransientDebugSurfacePass.GetTriangleDepthTestedPipeline().IsValid() &&
                m_TransientDebugSurfacePass.GetTriangleAlwaysOnTopPipeline().IsValid();
            const auto linePipelinesValid =
                m_TransientDebugLinePipelineLeaseDepthTested.has_value() &&
                m_TransientDebugLinePipelineLeaseDepthTested->IsValid() &&
                m_TransientDebugLinePipelineLeaseAlwaysOnTop.has_value() &&
                m_TransientDebugLinePipelineLeaseAlwaysOnTop->IsValid() &&
                m_TransientDebugSurfacePass.GetLineDepthTestedPipeline().IsValid() &&
                m_TransientDebugSurfacePass.GetLineAlwaysOnTopPipeline().IsValid();
            const auto pointPipelinesValid =
                m_TransientDebugPointPipelineLeaseDepthTested.has_value() &&
                m_TransientDebugPointPipelineLeaseDepthTested->IsValid() &&
                m_TransientDebugPointPipelineLeaseAlwaysOnTop.has_value() &&
                m_TransientDebugPointPipelineLeaseAlwaysOnTop->IsValid() &&
                m_TransientDebugSurfacePass.GetPointDepthTestedPipeline().IsValid() &&
                m_TransientDebugSurfacePass.GetPointAlwaysOnTopPipeline().IsValid();

            const auto hasTriangles = !world.DebugPrimitives.Triangles.empty();
            const auto hasLines = !world.DebugPrimitives.Lines.empty();
            const auto hasPoints = !world.DebugPrimitives.Points.empty();

            // `recordedAnyLane` flips true ONLY when `Execute*` actually
            // emitted draw calls for the lane — i.e. when the upload
            // helper successfully packed the lane's packets into a
            // host-visible vertex buffer (`uploadResult.Uploaded`).
            // An upload that fails (overflow past the per-lane vertex
            // cap or a `BufferManager::Create` failure) leaves the
            // lane silent: `Execute*` short-circuits, no draws land,
            // and `UploadOverflowCount` ticks. The lane's pipeline
            // gate is independent — upload failures do NOT increment
            // `MissingPipelineSkipCount` (the pipelines are healthy;
            // the transient buffer is not). When all submitted lanes
            // either skip their pipeline gate or fail upload, the
            // pass returns `SkippedUnavailable` so the
            // "feature on but nothing recorded" path is observable
            // through the status taxonomy rather than masked as
            // `Recorded`.
            bool recordedAnyLane = false;

            if (hasTriangles)
            {
                if (trianglePipelinesValid)
                {
                    const TransientDebugTriangleUploadResult uploadResult =
                        m_TransientDebugUploadHelper->UploadTriangles(world.DebugPrimitives.Triangles);
                    m_TransientDebugSurfacePass.ExecuteTriangles(
                        cmd,
                        world.DebugPrimitives.Triangles,
                        uploadResult,
                        m_LastRenderGraphStats.TransientDebugUpload);
                    if (uploadResult.Uploaded)
                    {
                        recordedAnyLane = true;
                    }
                }
                else
                {
                    ++m_LastRenderGraphStats.TransientDebugUpload.MissingPipelineSkipCount;
                }
            }

            if (hasLines)
            {
                if (linePipelinesValid)
                {
                    const TransientDebugLineUploadResult uploadResult =
                        m_TransientDebugUploadHelper->UploadLines(world.DebugPrimitives.Lines);
                    m_TransientDebugSurfacePass.ExecuteLines(
                        cmd,
                        world.DebugPrimitives.Lines,
                        uploadResult,
                        m_LastRenderGraphStats.TransientDebugUpload);
                    if (uploadResult.Uploaded)
                    {
                        recordedAnyLane = true;
                    }
                }
                else
                {
                    ++m_LastRenderGraphStats.TransientDebugUpload.MissingPipelineSkipCount;
                }
            }

            if (hasPoints)
            {
                if (pointPipelinesValid)
                {
                    const TransientDebugPointUploadResult uploadResult =
                        m_TransientDebugUploadHelper->UploadPoints(world.DebugPrimitives.Points);
                    m_TransientDebugSurfacePass.ExecutePoints(
                        cmd,
                        world.DebugPrimitives.Points,
                        uploadResult,
                        m_LastRenderGraphStats.TransientDebugUpload);
                    if (uploadResult.Uploaded)
                    {
                        recordedAnyLane = true;
                    }
                }
                else
                {
                    ++m_LastRenderGraphStats.TransientDebugUpload.MissingPipelineSkipCount;
                }
            }

            return recordedAnyLane
                ? RenderCommandPassStatus::Recorded
                : RenderCommandPassStatus::SkippedUnavailable;
        }

        // GRAPHICS-078 Slices B + C — operational executor helper for
        // the canonical default-recipe `VisualizationOverlayPass`. The
        // recipe declares this pass only when at least one
        // visualization-overlay packet (vector field or isoline)
        // exists for the frame (`features.EnableVisualizationOverlay`
        // derived from per-kind span emptiness in
        // `DeriveDefaultFrameRecipeFeatures`), so this helper is
        // reached only when the world has submitted overlay payload.
        // Mirrors `RecordTransientDebugSurfacePass(...)`: each lane
        // (vector-field, isoline) is gated independently on its own
        // pipeline pair (depth-tested + always-on-top); a lane with
        // packets but a missing or invalid pipeline pair increments
        // `MissingPipelineSkipCount` and is skipped, while other
        // lanes that have both packets and valid pipelines still
        // record. The pass status is `Recorded` when at least one
        // lane recorded its draws, and `SkippedUnavailable` when
        // every lane that had packets failed its pipeline gate (the
        // recipe-side `EnableVisualizationOverlay` derivation
        // guarantees at least one lane has packets when this branch
        // is reached, so this is an exhaustive cover of "feature on
        // but everything failed"). `MissingPipelineSkipCount`
        // continues to distinguish "feature on, pipeline missing"
        // from "feature off" (the latter does not reach this branch
        // at all).
        [[nodiscard]] RenderCommandPassStatus RecordVisualizationOverlayPass(
            RHI::ICommandContext& cmd,
            const RenderWorld&    world)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            std::lock_guard<std::mutex> uploadLock(m_DynamicUploadMutex);

            // The helper is constructed alongside `m_Subsystems.BufferManager()` in
            // `Initialize(...)`; on a successful operational gate it
            // is always present. Defensive nullopt gate left in place
            // so a future refactor that defers helper construction
            // (e.g. behind a Vulkan-specific impl) cannot silently
            // regress to a no-op. When the helper is missing every
            // submitted lane counts as "pipeline missing" because the
            // pass cannot upload or record anything.
            if (!m_VisualizationOverlayUploadHelper)
            {
                ++m_LastRenderGraphStats.VisualizationOverlayUpload.MissingPipelineSkipCount;
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const auto vectorFieldPipelinesValid =
                m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested.has_value() &&
                m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested->IsValid() &&
                m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop.has_value() &&
                m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop->IsValid() &&
                m_VisualizationOverlayPass.GetVectorFieldDepthTestedPipeline().IsValid() &&
                m_VisualizationOverlayPass.GetVectorFieldAlwaysOnTopPipeline().IsValid();

            const auto isolinePipelinesValid =
                m_VisualizationOverlayIsolinePipelineLeaseDepthTested.has_value() &&
                m_VisualizationOverlayIsolinePipelineLeaseDepthTested->IsValid() &&
                m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop.has_value() &&
                m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop->IsValid() &&
                m_VisualizationOverlayPass.GetIsolineDepthTestedPipeline().IsValid() &&
                m_VisualizationOverlayPass.GetIsolineAlwaysOnTopPipeline().IsValid();

            const auto hasVectorFields = !world.Visualization.VectorFields.empty();
            const auto hasIsolines = !world.Visualization.Isolines.empty();

            // `recordedAnyLane` flips true ONLY when `Execute*`
            // actually emitted draw calls for the lane — i.e. when
            // the upload helper successfully packed the lane's
            // packets into a host-visible vertex buffer
            // (`uploadResult.Uploaded`). An upload that fails
            // (overflow past the per-lane vertex cap or a
            // `BufferManager::Create` failure) leaves the lane
            // silent: `Execute*` short-circuits, no draws land, and
            // `UploadOverflowCount` ticks. The lane's pipeline gate
            // is independent — upload failures do NOT increment
            // `MissingPipelineSkipCount` (the pipelines are healthy;
            // the transient buffer is not). When all submitted lanes
            // either skip their pipeline gate or fail upload, the
            // pass returns `SkippedUnavailable` so the "feature on
            // but nothing recorded" path is observable through the
            // status taxonomy rather than masked as `Recorded`.
            bool recordedAnyLane = false;

            if (hasVectorFields)
            {
                if (vectorFieldPipelinesValid)
                {
                    const VisualizationVectorFieldUploadResult uploadResult =
                        m_VisualizationOverlayUploadHelper->UploadVectorFields(
                            world.Visualization.VectorFields);
                    m_VisualizationOverlayPass.ExecuteVectorFields(
                        cmd,
                        world.Visualization.VectorFields,
                        uploadResult,
                        m_LastRenderGraphStats.VisualizationOverlayUpload);
                    if (uploadResult.Uploaded)
                    {
                        recordedAnyLane = true;
                    }
                }
                else
                {
                    ++m_LastRenderGraphStats.VisualizationOverlayUpload.MissingPipelineSkipCount;
                }
            }

            // GRAPHICS-078 Slice C — isoline lane wiring. Gated
            // independently from the vector-field lane (mirroring the
            // GRAPHICS-077 Slice C per-lane independence): a lane with
            // packets but missing pipelines increments
            // `MissingPipelineSkipCount` and is skipped, while sibling
            // lanes with valid pipelines still record.
            if (hasIsolines)
            {
                if (isolinePipelinesValid)
                {
                    const VisualizationIsolineUploadResult uploadResult =
                        m_VisualizationOverlayUploadHelper->UploadIsolines(
                            world.Visualization.Isolines);
                    m_VisualizationOverlayPass.ExecuteIsolines(
                        cmd,
                        world.Visualization.Isolines,
                        uploadResult,
                        m_LastRenderGraphStats.VisualizationOverlayUpload);
                    if (uploadResult.Uploaded)
                    {
                        recordedAnyLane = true;
                    }
                }
                else
                {
                    ++m_LastRenderGraphStats.VisualizationOverlayUpload.MissingPipelineSkipCount;
                }
            }

            return recordedAnyLane
                ? RenderCommandPassStatus::Recorded
                : RenderCommandPassStatus::SkippedUnavailable;
        }

        RenderSubsystemRegistry             m_Subsystems;
        RHI::Format                          m_BackbufferFormat{RHI::Format::RGBA8_UNORM};
        std::optional<HZBSystem>             m_HZBSystem;
        std::optional<ReconstructionHistorySystem> m_ReconstructionHistorySystem;
        ReferenceTAAReconstructor            m_ReferenceTAAReconstructor;
        RHI::IDevice*                        m_Device{nullptr};
        RenderGraph                          m_RenderGraph;
        std::optional<RenderGraphCompileCacheEntry> m_RenderGraphCompileCache{};
        bool                                 m_InvalidateRenderGraphCompileCacheAfterFrame{false};
        bool                                 m_RenderGraphDebugDumpEnabled{false};
        bool                                 m_ParallelRenderGraphRecordingEnabled{false};
        RenderGraphExecutor                  m_RenderGraphExecutor;
        RenderCommandRouter                  m_CommandRouter;
        std::vector<std::vector<RHI::TextureHandle>> m_FrameTransientTextures{};
        std::vector<std::vector<RHI::BufferHandle>>  m_FrameTransientBuffers{};
        std::vector<std::vector<RHI::TextureDesc>>   m_FrameTransientTextureDescs{};
        std::vector<std::vector<RHI::BufferDesc>>    m_FrameTransientBufferDescs{};
        std::vector<std::vector<TransientMemoryBlockCache>> m_FrameTransientTextureMemoryBlocks{};
        std::vector<std::vector<TransientMemoryBlockCache>> m_FrameTransientBufferMemoryBlocks{};
        RenderPrepPipeline                  m_RenderPrepPipeline;
        RenderPrepPipelineResult            m_LastRenderPrepResult{};
        DepthPrepassPass                     m_DepthPrepassPass;
        // GRAPHICS-076 Slice A — canonical default-recipe present pass.
        // Default-constructed (no system dependency); the publisher in
        // `InitializeOperationalPassResources(device)` calls
        // `SetPipeline(...)` once the present pipeline lease is created.
        // `Execute(cmd)` records the `BindPipeline + Draw(3, 1, 0, 0)`
        // shape unconditionally when its cached pipeline handle is valid,
        // matching the contract enforced by the new `PresentPassContract`
        // tests. Lives for the renderer's full lifetime, with its pipeline
        // handle zeroed in `Shutdown()` before `m_Subsystems.PipelineManager()` is reset.
        PresentPass                          m_PresentPass;
        // GRAPHICS-076 Slice B — canonical default-recipe `DebugViewSystem`
        // + `DebugViewPass`. The system owns resource inspection /
        // selection / diagnostics; the pass holds a reference to the
        // system and records `BindPipeline + PushConstants(16) +
        // Draw(3, 1, 0, 0)` when both `IsInitialized() && pipeline.IsValid()
        // && resolvedSelection.Enabled` (per
        // `DebugViewPass::Execute(cmd, camera)`). Both are held as
        // `std::optional` because `DebugViewPass` requires an explicit
        // `DebugViewSystem&` constructor argument; the system is
        // emplaced + `Initialize()`d in `Initialize(device)`, the pass
        // is emplaced immediately after holding `*m_DebugViewSystem`,
        // and both are reset in `Shutdown()` before
        // `m_Subsystems.PipelineManager()` / `m_DebugViewPipelineLease` are torn
        // down. The system is driven from `ExecuteFrame()`:
        // `SetSettings({.Enabled = world.DebugOverlayEnabled ||
        // world.DebugPrimitives.HasTransientDebug, ...})` then
        // `ResolveSelection(recipeIntrospection)` per frame, mirroring
        // the recipe's `features.EnableDebugView` gate so the
        // CPU/null contract observes the same operational seam as the
        // recipe-side enablement.
        std::optional<DebugViewSystem>       m_DebugViewSystem;
        std::optional<DebugViewPass>         m_DebugViewPass;
        // GRAPHICS-079 Slice A — canonical default-recipe `Pass.ImGui`
        // consumer. The overlay system is engine-owned (runtime composition)
        // and borrowed through the `m_ImGuiOverlaySystem` pointer set by
        // `SetImGuiOverlaySystem`; `m_ImGuiPass` is `std::optional` because
        // `ImGuiPass` requires an explicit `ImGuiOverlaySystem&` constructor
        // argument and is non-movable, so it is emplaced only once the runtime
        // hands the overlay in (which may be before or after `Initialize()`).
        // The pass is reset in `Shutdown()` before `m_Subsystems.PipelineManager()` /
        // `m_ImGuiPipelineLease` are torn down. Until the runtime attaches an
        // overlay system the route reports `SkippedUnavailable`.
        ImGuiOverlaySystem*                  m_ImGuiOverlaySystem{nullptr};
        std::optional<ImGuiPass>             m_ImGuiPass;
        std::vector<RuntimeFrameCommandHookEntry> m_RuntimeFrameCommandHooks{};
        std::uint64_t m_NextRuntimeFrameCommandHookHandle{1u};
        // GRAPHICS-077 Slice A — scaffold-only `TransientDebugSurfacePass`.
        // Default-constructible (no system dependency), held as a plain
        // member so it lives for the renderer's full lifetime. Slice A
        // never publishes a pipeline through `SetPipeline(...)`, so
        // `GetPipeline().IsValid()` stays false and
        // `RecordTransientDebugSurfacePass` always returns
        // `SkippedUnavailable` on the operational path. Slice B/C
        // introduce the per-lane pipeline leases (`m_TransientDebug*PipelineLease*`)
        // and the renderer-side backend-local upload helper that
        // `Execute(...)` consumes.
        TransientDebugSurfacePass            m_TransientDebugSurfacePass;
        // GRAPHICS-078 Slice A — scaffold-only `VisualizationOverlayPass`.
        // Default-constructible (no system dependency), held as a plain
        // member so it lives for the renderer's full lifetime. Slice A
        // never publishes a pipeline through any `Set*Pipeline(...)`,
        // so every `Get*Pipeline().IsValid()` stays false and
        // `RecordVisualizationOverlayPass` always returns
        // `SkippedUnavailable` on the operational path with
        // `MissingPipelineSkipCount += 1`. Slice B/C introduce the
        // per-kind pipeline leases and the backend-local upload helper
        // that future `Execute*(...)` bodies consume.
        VisualizationOverlayPass             m_VisualizationOverlayPass;
        // GRAPHICS-070 — default-recipe forward surface pass. Owned as an
        // `optional` so the explicit `ForwardSystem&` constructor invariant is
        // preserved: emplaced in `Initialize()` immediately after the
        // `m_Subsystems.ForwardSystemRegistry()` slot is constructed, and reset in `Shutdown()`
        // before the `ForwardSystem` slot is torn down.
        std::optional<ForwardSurfacePass>    m_ForwardSurfacePass;
        std::optional<ForwardLinePass>       m_ForwardLinePass;
        std::optional<ForwardPointPass>      m_ForwardPointPass;
        // GRAPHICS-073 Slice A — default-recipe shadow pass. Same lifetime
        // contract as the forward pass optionals: emplaced after
        // `m_Subsystems.ShadowSystemRegistry()` in `Initialize()` and reset before the system in
        // `Shutdown()`.
        std::optional<ShadowPass>            m_ShadowPass;
        // GRAPHICS-072 Slice A — default-recipe deferred GBuffer pass. Same
        // lifetime contract: emplaced after `m_Subsystems.DeferredSystemRegistry()` is initialised
        // and before the operational publisher runs; reset before
        // `m_Subsystems.DeferredSystemRegistry()` in `Shutdown()`.
        std::optional<DeferredGBufferPass>   m_DeferredGBufferPass;
        // GRAPHICS-072 Slice B — default-recipe deferred lighting pass. Same
        // lifetime contract as the GBuffer pass: emplaced alongside
        // `m_DeferredGBufferPass` after `m_Subsystems.DeferredSystemRegistry()` is initialised,
        // reset before `m_Subsystems.DeferredSystemRegistry()` in `Shutdown()`.
        std::optional<DeferredLightingPass>  m_DeferredLightingPass;
        // GRAPHICS-074 Slice A — default-recipe EntityId selection pass.
        // Same lifetime contract as the forward / shadow / deferred passes:
        // emplaced after `m_Subsystems.SelectionSystemRegistry()` is initialised and before the
        // operational publisher runs; reset before `m_Subsystems.SelectionSystemRegistry()` in
        // `Shutdown()`.
        std::optional<EntityIdPass>          m_SelectionEntityIdPass;
        // GRAPHICS-074 Slice B — default-recipe Face / Edge / Point
        // selection ID passes. Same lifetime contract as the EntityId pass
        // above: each is emplaced after `m_Subsystems.SelectionSystemRegistry()` is initialised
        // and before the operational publisher runs; reset before
        // `m_Subsystems.SelectionSystemRegistry()` in `Shutdown()`.
        std::optional<FaceIdPass>            m_SelectionFaceIdPass;
        std::optional<EdgeIdPass>            m_SelectionEdgeIdPass;
        std::optional<PointIdPass>           m_SelectionPointIdPass;
        // GRAPHICS-074 Slice C — default-recipe selection outline pass.
        // Same lifetime contract as the selection-ID passes above: emplaced
        // after `m_Subsystems.SelectionSystemRegistry()` is initialised and before the operational
        // publisher runs; reset before `m_Subsystems.SelectionSystemRegistry()` in `Shutdown()`.
        std::optional<SelectionOutlinePass>  m_SelectionOutlinePass;
        // GRAPHICS-075 Slice A — default-recipe postprocess tonemap pass.
        // Same lifetime contract as the selection / forward / deferred /
        // shadow passes above: emplaced after `m_Subsystems.PostProcessSystemRegistry()` is
        // initialised and before the operational publisher runs; reset
        // before `m_Subsystems.PostProcessSystemRegistry()` in `Shutdown()`. Slices B–E add the
        // sibling Histogram / Bloom / FXAA / SMAA pass instances behind
        // the same `"PostProcessPass"` umbrella executor branch.
        std::optional<PostProcessToneMapPass> m_PostProcessToneMapPass;
        // GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom pass.
        // Same lifetime contract as the tonemap pass above.
        std::optional<PostProcessBloomPass>   m_PostProcessBloomPass;
        // GRAPHICS-075 Slice C — default-recipe postprocess FXAA pass.
        // Same lifetime contract as the tonemap + bloom passes above.
        std::optional<PostProcessFXAAPass>    m_PostProcessFXAAPass;
        // GRAPHICS-075 Slice D.2a — default-recipe postprocess SMAA pass.
        // Holds the three SMAA pipelines (edge / blend / resolve) and
        // fans out across three ordered graph passes
        // (`"PostProcessAA{Edge,Blend,Resolve}Pass"`); FXAA records on
        // the resolve pass only. Mutually exclusive with FXAA per
        // `PostProcessSettings::AntiAliasing` (each per-stage Execute
        // gates on `IsStageEnabled(SMAA)`). Same lifetime contract as
        // the tonemap + bloom + FXAA passes above: emplaced after
        // `m_Subsystems.PostProcessSystemRegistry()` is initialised and before the
        // operational publisher runs; reset before `m_Subsystems.PostProcessSystemRegistry()`
        // in `Shutdown()`.
        std::optional<PostProcessSMAAPass>    m_PostProcessSMAAPass;
        // GRAPHICS-075 Slice E.1 — default-recipe postprocess histogram
        // compute pass. The histogram is a compute dispatch and so cannot
        // share the `"PostProcessPass"` umbrella's render-pass scope
        // (Vulkan forbids `vkCmdDispatch` inside an active render-pass
        // scope); it therefore fans out under its own ordered graph pass
        // `"PostProcessHistogramPass"` declared by the recipe. Same
        // lifetime contract as the tonemap + bloom + FXAA + SMAA passes
        // above: emplaced after `m_Subsystems.PostProcessSystemRegistry()` is initialised and
        // before the operational publisher runs; reset before
        // `m_Subsystems.PostProcessSystemRegistry()` in `Shutdown()`.
        std::optional<PostProcessHistogramPass> m_PostProcessHistogramPass;
        std::optional<RHI::PipelineManager::PipelineLease> m_DepthPrepassPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DefaultDebugSurfacePipelineLease;
        // GRAPHICS-076 Slice A — canonical default-recipe present pipeline
        // lease. A failed `Create()` leaves `m_PresentPass` in the
        // fail-closed state that `RecordPresentPass` interprets as
        // `SkippedUnavailable`.
        std::optional<RHI::PipelineManager::PipelineLease> m_PresentPipelineLease;
        // GRAPHICS-076 Slice B — canonical default-recipe `Pass.DebugView`
        // pipeline lease. Same reset/republish pattern as the present
        // lease above so a failed `Create()` leaves `m_DebugViewPass` in
        // the fail-closed state that `RecordDebugViewPass` interprets as
        // `SkippedUnavailable`. Created after present inside
        // `InitializeOperationalPassResources()` (call #24, immediately
        // after present at #23) so the existing
        // `FailPipelineCreateCall` indices (1-23) used by other lifecycle
        // tests remain stable.
        std::optional<RHI::PipelineManager::PipelineLease> m_DebugViewPipelineLease;
        // GRAPHICS-079 Slice A — canonical default-recipe `Pass.ImGui`
        // pipeline lease. Created LAST inside
        // `InitializeOperationalPassResources()` (after the HZB build compute
        // pipeline, which itself appends after the visualization-overlay
        // isoline pipelines) so existing `FailPipelineCreateCall` indices used
        // by other lifecycle tests remain stable. Same reset/republish +
        // fail-closed pattern as the present/debug-view leases so a failed
        // `Create()` leaves `m_ImGuiPass` (when attached) in the fail-closed
        // state that `RecordImGuiPass` interprets as `SkippedUnavailable`.
        // The default-recipe present source can be either swapchain-format
        // (`SceneColorLDR` / AA resolved) or `RGBA8_UNORM` (`DebugViewRGBA`),
        // so the executor selects the matching variant from the active
        // render-pass attachment format before recording.
        std::optional<RHI::PipelineManager::PipelineLease> m_ImGuiPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ImGuiRgba8PipelineLease;
        // GRAPHICS-077 Slice B — transient-debug triangle pipelines.
        // Two variants per lane (depth-tested + always-on-top); same
        // reset/republish pattern as the debug-view lease above so a
        // failed `Create()` leaves `m_TransientDebugSurfacePass` in
        // the fail-closed state that `RecordTransientDebugSurfacePass`
        // interprets as `SkippedUnavailable` (with
        // `MissingPipelineSkipCount += 1` per operational-no-pipeline
        // frame). Created after debug-view inside
        // `InitializeOperationalPassResources()` — depth-tested at
        // call #25 (immediately after debug-view at #24) and
        // always-on-top at call #26 — so the existing
        // `FailPipelineCreateCall` indices (1-24) used by other
        // lifecycle tests remain stable.
        std::optional<RHI::PipelineManager::PipelineLease> m_TransientDebugTrianglePipelineLeaseDepthTested;
        std::optional<RHI::PipelineManager::PipelineLease> m_TransientDebugTrianglePipelineLeaseAlwaysOnTop;
        // GRAPHICS-077 Slice C — transient-debug line + point pipelines.
        // Same reset/republish + fail-closed pattern as the triangle
        // leases above. Created at call indices #27 (line DepthTested),
        // #28 (line AlwaysOnTop), #29 (point DepthTested), and #30
        // (point AlwaysOnTop) inside `InitializeOperationalPassResources()`
        // so the Slice B contract tests that pin
        // `FailPipelineCreateCall = 25` still exercise the triangle
        // DepthTested gate without disturbance.
        std::optional<RHI::PipelineManager::PipelineLease> m_TransientDebugLinePipelineLeaseDepthTested;
        std::optional<RHI::PipelineManager::PipelineLease> m_TransientDebugLinePipelineLeaseAlwaysOnTop;
        std::optional<RHI::PipelineManager::PipelineLease> m_TransientDebugPointPipelineLeaseDepthTested;
        std::optional<RHI::PipelineManager::PipelineLease> m_TransientDebugPointPipelineLeaseAlwaysOnTop;
        // GRAPHICS-078 Slice B — visualization-overlay vector-field
        // pipelines. Two variants per kind (depth-tested + always-
        // on-top); same reset/republish pattern as the transient-
        // debug leases above so a failed `Create()` leaves
        // `m_VisualizationOverlayPass` in the fail-closed state that
        // `RecordVisualizationOverlayPass` interprets as
        // `SkippedUnavailable` (with `MissingPipelineSkipCount += 1`
        // per operational-no-pipeline frame). Created after the
        // GRAPHICS-077 point-lane pipelines inside
        // `InitializeOperationalPassResources()` — depth-tested at
        // call #31 (immediately after point AlwaysOnTop at #30) and
        // always-on-top at call #32 — so the existing
        // `FailPipelineCreateCall` indices (1-30) used by other
        // lifecycle tests remain stable. The Slice C isoline lane
        // will append at call indices #33 + #34.
        std::optional<RHI::PipelineManager::PipelineLease> m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested;
        std::optional<RHI::PipelineManager::PipelineLease> m_VisualizationOverlayVectorFieldPipelineLeaseAlwaysOnTop;
        // GRAPHICS-078 Slice C — visualization-overlay isoline
        // pipelines. Two variants per kind (depth-tested + always-on-
        // top); same reset/republish + fail-closed pattern as the
        // vector-field leases above. Created after the vector-field
        // pipelines inside `InitializeOperationalPassResources()` —
        // depth-tested at call #33 (immediately after vector-field
        // AlwaysOnTop at #32) and always-on-top at call #34 — so the
        // existing `FailPipelineCreateCall` indices (1-32) used by
        // other lifecycle tests remain stable. New Slice C tests
        // target `FailPipelineCreateCall = 33` for the per-lane
        // missing-pipeline taxonomy.
        std::optional<RHI::PipelineManager::PipelineLease> m_VisualizationOverlayIsolinePipelineLeaseDepthTested;
        std::optional<RHI::PipelineManager::PipelineLease> m_VisualizationOverlayIsolinePipelineLeaseAlwaysOnTop;
        // GRAPHICS-038B — HZB build compute pipeline lease. Created after
        // visualization-overlay call #34 and before ImGui so all previously
        // documented pipeline-create indices stay stable; a failed create makes
        // `"HZBBuildPass"` report `SkippedUnavailable`.
        std::optional<RHI::PipelineManager::PipelineLease> m_HZBBuildPipelineLease;
        // GRAPHICS-039C — clustered-light compute pipeline leases. Created
        // after HZB so the existing pipeline-create indices through HZB stay
        // stable; missing leases make the cluster recipe passes report
        // `SkippedUnavailable` while the scene-table publication remains
        // fail-closed.
        std::optional<RHI::PipelineManager::PipelineLease> m_ClusterGridBuildPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ClusterLightAssignmentPipelineLease;
        // GRAPHICS-077 Slice B — backend-local transient-debug upload
        // helper. Held as `unique_ptr<ITransientDebugUploadHelper>` so
        // Slice D can swap in a Vulkan-tuned concrete implementation
        // without disturbing the renderer-side wiring. Constructed in
        // `Initialize(device)` against the live `BufferManager`; reset
        // in `Shutdown()` before the `BufferManager` so the helper's
        // internal lease destructor observes a live manager.
        std::unique_ptr<ITransientDebugUploadHelper> m_TransientDebugUploadHelper;
        // GRAPHICS-078 Slice B — backend-local visualization-overlay
        // upload helper. Same lifetime contract as the transient-
        // debug helper above: held as
        // `unique_ptr<IVisualizationOverlayUploadHelper>` so future
        // source-BDA expansion can replace placeholder endpoint
        // generation without disturbing the renderer-side wiring.
        // Constructed in `Initialize(device)` against the live `BufferManager`;
        // reset in `Shutdown()` before the `BufferManager` so the
        // helper's internal lease destructor observes a live
        // manager.
        std::unique_ptr<IVisualizationOverlayUploadHelper> m_VisualizationOverlayUploadHelper;
        std::unique_ptr<VisualizationPropertyBufferResidency> m_VisualizationPropertyBufferResidency;
        // GRAPHICS-079 Slice C — renderer-owned ImGui transient upload helper.
        // Held as an interface pointer for parity with the transient-debug and
        // visualization-overlay helpers and reset before BufferManager teardown.
        std::unique_ptr<IImGuiUploadHelper> m_ImGuiUploadHelper;
        // GRAPHICS-119 Slice C.5: pass recording can eventually run on worker
        // threads, so renderer-owned dynamic upload helpers share one guard for
        // per-frame reset plus Upload/Execute sections that touch BufferManager
        // leases, helper-owned slots, bindless flushes, and upload diagnostics.
        std::mutex m_DynamicUploadMutex;
        // GRAPHICS-119 Slice C.5b: postprocess pass helpers cache per-frame
        // handles/settings (`SetBloomScratch`, histogram viewport/buffer) and
        // share PostProcessSystem-backed pass objects. Serialize their setter
        // + Execute sections while worker fan-out records pass callbacks.
        std::mutex m_PostProcessPassMutex;
        // GRAPHICS-119 Slice C.6: frame-sampled bridge descriptors update
        // shared backend descriptor-set state before pass route dispatch.
        std::mutex m_FrameSampledDescriptorMutex;
        std::optional<RHI::PipelineManager::PipelineLease> m_ForwardSurfacePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ForwardLinePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ForwardPointPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ShadowPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DeferredGBufferPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DeferredLightingPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionEntityIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionEntityIdOutlinePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionFaceIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionEdgeIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionPointIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionOutlinePipelineLease;
        // GRAPHICS-075 Slice A — postprocess tonemap pipeline lease. Same
        // reset/republish pattern as the selection-outline lease above so a
        // failed `Create()` leaves `m_PostProcessToneMapPass` in
        // `SkippedUnavailable` rather than retaining a stale device handle
        // across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessToneMapPipelineLease;
        // GRAPHICS-075 Slice B.1 — postprocess bloom downsample + upsample
        // pipeline leases. Same reset/republish pattern as the tonemap
        // lease above so a failed `Create()` leaves the bloom helper in
        // `SkippedUnavailable` rather than retaining a stale device
        // handle across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessBloomDownsamplePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessBloomUpsamplePipelineLease;
        // GRAPHICS-075 Slice C — postprocess FXAA pipeline lease. Same
        // reset/republish pattern as the tonemap + bloom leases above so
        // a failed `Create()` leaves the FXAA helper in
        // `SkippedUnavailable` rather than retaining a stale device
        // handle across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessFXAAPipelineLease;
        // GRAPHICS-075 Slice D.1 — postprocess SMAA pipeline leases (edge,
        // blend, resolve). Same reset/republish pattern as the tonemap +
        // bloom + FXAA leases above so a failed `Create()` on any stage
        // leaves that stage's bind/push/draw silenced inside
        // `PostProcessSMAAPass::Execute` while the umbrella helper still
        // returns `Recorded` per the structurally-recorded-no-op taxonomy.
        // Retained `AreaTex` / `SearchTex` LUT textures sampled by the
        // blend pipeline land in Slice D.2 alongside the recipe-side
        // `PostProcess.AATemp.{Edges,Weights}` split.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessSMAAEdgePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessSMAABlendPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessSMAAResolvePipelineLease;
        // GRAPHICS-075 Slice E.1 — postprocess histogram compute pipeline
        // lease. Same reset/republish pattern as the tonemap + bloom +
        // FXAA + SMAA leases above so a failed `Create()` leaves the
        // histogram helper in `SkippedUnavailable` rather than retaining
        // a stale device handle across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessHistogramPipelineLease;
        // GRAPHICS-074 Slice D.1 — renderer-owned host-visible `Picking.Readback`
        // buffer. Allocated by `InitializeOperationalPassResources()` when
        // the device first becomes operational and re-used across
        // `RebuildOperationalResources()` calls as long as the expected
        // `kPickingReadbackSlotStride * device.GetFramesInFlight()` size
        // matches the current `m_PickingReadbackBufferSize` (same pattern
        // `ShadowSystem` follows for its depth atlas). When the device
        // reports a different frames-in-flight after a swapchain rebuild
        // the lease is dropped and re-created so the per-frame copy
        // addressing never overruns the allocation. The lease is reset in
        // `Shutdown()` before `m_Subsystems.BufferManager()` so the destruction order
        // matches the rest of the lease-owning members above. Slice D.1
        // only exposes the buffer through `GetPickingReadbackBuffer()` /
        // `GetPickingReadbackBufferSize()`; Slice D.2 imports it into the
        // recipe and records the `CopyTextureToBuffer` calls, Slice D.3
        // drains it on `BeginFrame()`.
        std::optional<RHI::BufferManager::BufferLease> m_PickingReadbackBuffer;
        std::uint64_t                                  m_PickingReadbackBufferSize{0u};
        // GRAPHICS-074 Slice D.3 — per-slot picking-readback metadata. Sized
        // to match the buffer's `frames-in-flight` slot count whenever
        // `InitializeOperationalPassResources()` (re-)allocates the buffer,
        // so every slot in `m_PickingReadbackBuffer` has matching bookkeeping
        // entries. The metadata is populated in `ExecuteFrame` immediately
        // after the D.2 `CopyTextureToBuffer` pair records (`Pending` flips
        // to true, `IssuedFrame` captures `frame.FrameIndex`, `Request`
        // captures the world-space pick coordinates, `Invalidated` resets to
        // false), and drained at the start of the *next* `BeginFrame(...)`
        // call before `m_Device->BeginFrame(...)` acquires the next frame.
        // The drain decodes the 8 bytes the executor copied — one 4-byte
        // `EntityId` word + one 4-byte `EncodedSelectionId` word per
        // `GRAPHICS-012Q` — and routes to `SelectionSystem::PublishPickResult`
        // (`EntityId != 0` && !`Invalidated`) or `PublishNoHit()`
        // (`EntityId == 0`, `Invalidated`, or read failure). `Invalidated`
        // is set to true by `RebuildOperationalResources()` for every
        // pending slot at the time of the rebuild so a device-lost recovery
        // path publishes NoHit rather than a stale pre-rebuild hit; the
        // buffer itself is preserved across same-FIF rebuilds (Slice D.1
        // invariant), so the drained bytes are well-defined even when
        // invalidation forces a NoHit publish.
        std::vector<bool>                              m_PickingSlotPending;
        std::vector<std::uint64_t>                     m_PickingSlotIssuedFrame;
        std::vector<PickPixelRequest>                  m_PickingSlotRequest;
        std::vector<bool>                              m_PickingSlotInvalidated;
        // RUNTIME-089 — per-slot runtime correlation Sequence, recorded at issue
        // and replayed on the published PickReadbackResult so the runtime
        // resolves the exact in-flight pick regardless of slot publish order.
        std::vector<std::uint64_t>                     m_PickingSlotSequence;
        // BUG-026 — true when the slot's copy pass also recorded the SceneDepth
        // pixel copy (the depth target can be unavailable in degenerate
        // recipes); gates `PickReadbackResult::HasDepth` on the drain.
        std::vector<bool>                              m_PickingSlotDepthCopied;
        // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
        // `Histogram.Readback` buffer + per-slot drain metadata. Sized for
        // `kHistogramReadbackSlotBytes * frames-in-flight` bytes (256 uint32
        // bins per slot, one slot per in-flight frame). Allocated by
        // `InitializeOperationalPassResources()` on first operational init,
        // re-allocated on `RebuildOperationalResources()` only when the
        // device's `GetFramesInFlight()` changes (same pattern picking
        // follows). Drained from the top of `BeginFrame()` once the issuing
        // frame's `GlobalFrameNumber` has advanced; the drain decodes the
        // 256 uint32 bins and forwards them to
        // `PostProcessSystem::PublishHistogramReadback(...)`. `Invalidated`
        // is set by `RebuildOperationalResources()` for in-flight slots so
        // the drain skips the publish for slots whose pre-rebuild copy is
        // no longer trustworthy (same device-lost-recovery contract as
        // picking).
        std::optional<RHI::BufferManager::BufferLease> m_HistogramReadbackBuffer;
        std::uint64_t                                  m_HistogramReadbackBufferSize{0u};
        std::vector<bool>                              m_HistogramSlotPending;
        std::vector<std::uint64_t>                     m_HistogramSlotIssuedFrame;
        std::vector<bool>                              m_HistogramSlotInvalidated;
        // GRAPHICS-039C — retained clustered-light buffers sized to the
        // current viewport. `PrepareFrame()` publishes the header/index BDAs
        // and grid metadata through `GpuSceneTable` before `GpuWorld::SyncFrame()`.
        std::optional<RHI::BufferManager::BufferLease> m_ClusterGridAABBBuffer;
        std::optional<RHI::BufferManager::BufferLease> m_ClusterLightHeaderBuffer;
        std::optional<RHI::BufferManager::BufferLease> m_ClusterLightIndexBuffer;
        std::optional<RHI::BufferManager::BufferLease> m_ClusterLightCounterBuffer;
        ClusterGridDesc                                m_ClusterGridDesc{};
        ClusterGridProjection                          m_ClusterGridProjection{};
        RHI::FrameHandle                               m_CurrentFrame{};
        std::array<RuntimeSnapshotStorage, kRuntimeSnapshotStorageSlots> m_RuntimeSnapshotSlots{};
        std::uint32_t                        m_ActiveRuntimeSnapshotReadSlot{0u};
        bool                                 m_CullingOutputAvailable{false};
        bool                                 m_HasExtractedRenderWorld{false};
        bool                                 m_HasPreparedFrame{false};
        // GRAPHICS-072 Slice A — renderer-stored lighting-path override
        // applied after `DeriveDefaultFrameRecipeFeatures(world)`. Default is
        // `Forward` so the legacy contract tests stay green; contract tests
        // can flip this to `Deferred` via `SetLightingPath(...)` to drive the
        // `"SurfacePass"` deferred executor branch added in this slice.
        FrameRecipeLightingPath              m_LightingPath{FrameRecipeLightingPath::Forward};
        std::optional<FrameRecipeOverride>   m_ActiveFrameRecipeOverride{};
        // GRAPHICS-076E — opt-in default-recipe readback target. Invalid
        // handle = disabled (default).
        RHI::BufferHandle                    m_DefaultRecipeReadbackBuffer{};
        // GRAPHICS-077E — opt-in transient-debug readback target. Invalid
        // handle = disabled (default); additionally gated on
        // `TransientDebugSurfacePass` recording this frame.
        RHI::BufferHandle                    m_TransientDebugReadbackBuffer{};
        // GRAPHICS-078E — opt-in visualization-overlay readback target.
        // Invalid handle = disabled (default); additionally gated on
        // `VisualizationOverlayPass` recording this frame.
        RHI::BufferHandle                    m_VisualizationOverlayReadbackBuffer{};
        RenderGraphFrameStats                m_LastRenderGraphStats;
        std::optional<ActiveGpuProfileFrame> m_ActiveGpuProfile{};
        std::vector<std::optional<SubmittedGpuProfileFrame>>
            m_SubmittedGpuProfiles{};
        RenderGraphGpuProfileStats m_CurrentGpuProfile{};
        std::optional<RenderGraphGpuProfileStats>
            m_LastGoodGpuProfile{};
        // Only this failure latch is worker-writable. Candidate, per-slot,
        // status, and last-good state stay render-thread-owned.
        std::atomic<std::uint8_t> m_GpuProfileFailure{0u};
        // GRAPHICS-119 Slice C.3: pass callbacks can run from worker threads
        // once fan-out is enabled, so command-record diagnostics accumulate
        // behind this guard and publish to m_LastRenderGraphStats after join.
        std::mutex                           m_CommandRecordStatsMutex;
        RenderGraphCommandRecordStats        m_CommandRecordStats;
        // GRAPHICS-119 Slice C.4: pass callbacks route readback issue
        // counters and per-slot metadata through helpers guarded here. The
        // BeginFrame drains and rebuild resize policy remain render-thread
        // lifecycle work.
        std::mutex                           m_ReadbackIssueMutex;
    };

    NullRenderer::RenderCommandRouteContext& NullRenderer::RouteContextFrom(void* context) noexcept
    {
        return *static_cast<RenderCommandRouteContext*>(context);
    }

    void NullRenderer::EndActiveRenderPassForRoute(RHI::ICommandContext& cmd,
                                                   RenderCommandRouteContext& context)
    {
        if (context.RenderPassEnded != nullptr &&
            context.ActiveRenderPass != nullptr &&
            !*context.RenderPassEnded &&
            context.ActiveRenderPass->HasAttachments)
        {
            cmd.EndRenderPass();
            *context.RenderPassEnded = true;
        }
    }

    void NullRenderer::RecordPickingCommandRoute(const RenderCommandRoute& route,
                                                 RHI::ICommandContext& cmd,
                                                 RenderCommandRouteContext& context)
    {
        const RHI::CameraUBO& camera = *context.Camera;
        const RHI::FrameHandle& frame = *context.Frame;
        const RenderWorld& renderWorld = *context.World;
        const CompiledRenderGraph& compiled = *context.Compiled;
        const bool primitivePickingActive = renderWorld.PickRequest.Pending;

        const RenderCommandPassStatus entityStatus =
            RecordSelectionEntityIdPass(cmd, camera, frame.FrameIndex, primitivePickingActive);
        AccumulateCommandRecordStatus(route.DebugName, route.PassId, entityStatus);
        if (!primitivePickingActive && entityStatus == RenderCommandPassStatus::Recorded)
        {
            ++m_LastRenderGraphStats.SelectionOutlineEntityIdPassCount;
        }
        if (primitivePickingActive)
        {
            const RenderCommandPassStatus faceStatus =
                RecordSelectionFaceIdPass(cmd, camera, frame.FrameIndex);
            AccumulateCommandRecordStatus(route.DebugName, route.PassId, faceStatus);
            const RenderCommandPassStatus edgeStatus =
                RecordSelectionEdgeIdPass(cmd, camera, frame.FrameIndex);
            AccumulateCommandRecordStatus(route.DebugName, route.PassId, edgeStatus);
            const RenderCommandPassStatus pointStatus =
                RecordSelectionPointIdPass(cmd, camera, frame.FrameIndex);
            AccumulateCommandRecordStatus(route.DebugName, route.PassId, pointStatus);
            if (faceStatus == RenderCommandPassStatus::Recorded &&
                edgeStatus == RenderCommandPassStatus::Recorded &&
                pointStatus == RenderCommandPassStatus::Recorded)
            {
                ++m_LastRenderGraphStats.SelectionPrimitiveIdPassCount;
            }
        }

        if (m_Device != nullptr && m_Device->IsOperational() &&
            m_PickingReadbackBuffer.has_value() &&
            m_PickingReadbackBuffer->IsValid() &&
            primitivePickingActive)
        {
            const RHI::BufferHandle pickingBuffer =
                m_PickingReadbackBuffer->GetHandle();
            RHI::TextureHandle entityIdHandle{};
            RHI::TextureHandle primitiveIdHandle{};
            RHI::TextureHandle sceneDepthHandle{};
            for (std::size_t i = 0; i < compiled.TextureNames.size(); ++i)
            {
                if (i >= compiled.TextureHandles.size())
                {
                    break;
                }
                if (compiled.TextureNames[i] == std::string_view{"EntityId"})
                {
                    entityIdHandle = compiled.TextureHandles[i];
                }
                else if (compiled.TextureNames[i] == std::string_view{"PrimitiveId"})
                {
                    primitiveIdHandle = compiled.TextureHandles[i];
                }
                else if (compiled.TextureNames[i] == std::string_view{"SceneDepth"})
                {
                    sceneDepthHandle = compiled.TextureHandles[i];
                }
            }
            if (entityIdHandle.IsValid() && primitiveIdHandle.IsValid())
            {
                EndActiveRenderPassForRoute(cmd, context);
                const std::uint32_t framesInFlight =
                    std::max(1u, m_Device->GetFramesInFlight());
                const std::uint32_t slot =
                    frame.FrameIndex % framesInFlight;
                const std::uint64_t slotOffset =
                    static_cast<std::uint64_t>(slot) * kPickingReadbackSlotStride;
                const std::uint32_t pickX = renderWorld.PickRequest.X;
                const std::uint32_t pickY = renderWorld.PickRequest.Y;

                cmd.TextureBarrier(entityIdHandle,
                                   RHI::TextureLayout::ColorAttachment,
                                   RHI::TextureLayout::TransferSrc);
                cmd.TextureBarrier(primitiveIdHandle,
                                   RHI::TextureLayout::ColorAttachment,
                                   RHI::TextureLayout::TransferSrc);
                cmd.CopyTextureToBuffer(entityIdHandle,
                                        RHI::TextureLayout::TransferSrc,
                                        0u, 0u,
                                        pickingBuffer,
                                        slotOffset + kPickingReadbackEntityIdOffset,
                                        pickX, pickY,
                                        1u, 1u);
                cmd.CopyTextureToBuffer(primitiveIdHandle,
                                        RHI::TextureLayout::TransferSrc,
                                        0u, 0u,
                                        pickingBuffer,
                                        slotOffset + kPickingReadbackEncodedIdOffset,
                                        pickX, pickY,
                                        1u, 1u);
                cmd.TextureBarrier(entityIdHandle,
                                   RHI::TextureLayout::TransferSrc,
                                   RHI::TextureLayout::ColorAttachment);
                cmd.TextureBarrier(primitiveIdHandle,
                                   RHI::TextureLayout::TransferSrc,
                                   RHI::TextureLayout::ColorAttachment);
                // BUG-026 — sample the pick pixel's scene depth alongside the
                // IDs so the runtime can reconstruct the world-space cursor
                // position. The PickingPass reads SceneDepth as DepthRead, so
                // the round-trip restores DepthReadOnly for the downstream
                // surface pass barriers derived from the declared usages.
                const bool depthCopied = sceneDepthHandle.IsValid();
                if (depthCopied)
                {
                    cmd.TextureBarrier(sceneDepthHandle,
                                       RHI::TextureLayout::DepthReadOnly,
                                       RHI::TextureLayout::TransferSrc);
                    cmd.CopyTextureToBuffer(sceneDepthHandle,
                                            RHI::TextureLayout::TransferSrc,
                                            0u, 0u,
                                            pickingBuffer,
                                            slotOffset + kPickingReadbackDepthOffset,
                                            pickX, pickY,
                                            1u, 1u);
                    cmd.TextureBarrier(sceneDepthHandle,
                                       RHI::TextureLayout::TransferSrc,
                                       RHI::TextureLayout::DepthReadOnly);
                }
                NotePickingReadbackCopyIssued(frame.FrameIndex,
                                              slot,
                                              pickX,
                                              pickY,
                                              renderWorld.PickRequest.Sequence,
                                              depthCopied);
            }
        }
    }

    void NullRenderer::RecordPostProcessCommandRoute(const RenderCommandRoute& route,
                                                     RHI::ICommandContext& cmd,
                                                     RenderCommandRouteContext& context)
    {
        const RHI::CameraUBO& camera = *context.Camera;
        const CompiledRenderGraph& compiled = *context.Compiled;
        RenderCommandPassStatus bloomStatus = RenderCommandPassStatus::SkippedUnavailable;
        RenderCommandPassStatus toneMapStatus = RenderCommandPassStatus::SkippedUnavailable;

        {
            std::lock_guard<std::mutex> postProcessLock(m_PostProcessPassMutex);
            if (m_PostProcessBloomPass.has_value())
            {
                RHI::TextureHandle bloomScratchHandle{};
                for (std::size_t i = 0; i < compiled.TextureNames.size(); ++i)
                {
                    if (i >= compiled.TextureHandles.size())
                    {
                        break;
                    }
                    if (compiled.TextureNames[i] == std::string_view{"PostProcess.BloomScratch"})
                    {
                        bloomScratchHandle = compiled.TextureHandles[i];
                        break;
                    }
                }
                const Core::Extent2D bloomExtent = m_Device != nullptr
                    ? m_Device->GetBackbufferExtent()
                    : Core::Extent2D{.Width = 1, .Height = 1};
                const std::uint32_t bloomWidth = bloomExtent.Width > 0
                    ? static_cast<std::uint32_t>(bloomExtent.Width)
                    : 1u;
                const std::uint32_t bloomHeight = bloomExtent.Height > 0
                    ? static_cast<std::uint32_t>(bloomExtent.Height)
                    : 1u;
                const std::uint32_t bloomMipLevels =
                    ComputeBloomMipChainLevels(bloomWidth, bloomHeight);
                m_PostProcessBloomPass->SetBloomScratch(bloomScratchHandle, bloomMipLevels);
            }
            bloomStatus = RecordPostProcessBloomPass(cmd, camera);
            toneMapStatus = RecordPostProcessToneMapPass(cmd, camera);
        }
        AccumulateCommandRecordStatus(route.DebugName, route.PassId, bloomStatus);
        AccumulateCommandRecordStatus(route.DebugName, route.PassId, toneMapStatus);
    }

    void NullRenderer::RecordPostProcessHistogramCommandRoute(const RenderCommandRoute& route,
                                                              RHI::ICommandContext& cmd,
                                                              RenderCommandRouteContext& context)
    {
        const RHI::CameraUBO& camera = *context.Camera;
        const RHI::FrameHandle& frame = *context.Frame;
        const CompiledRenderGraph& compiled = *context.Compiled;

        RHI::BufferHandle histogramHandle{};
        for (std::size_t i = 0; i < compiled.BufferNames.size(); ++i)
        {
            if (i >= compiled.BufferHandles.size())
            {
                break;
            }
            if (compiled.BufferNames[i] == std::string_view{"PostProcess.Histogram"})
            {
                histogramHandle = compiled.BufferHandles[i];
                break;
            }
        }
        RenderCommandPassStatus status = RenderCommandPassStatus::SkippedUnavailable;
        {
            std::lock_guard<std::mutex> postProcessLock(m_PostProcessPassMutex);
            if (m_PostProcessHistogramPass.has_value())
            {
                const Core::Extent2D histogramExtent = m_Device != nullptr
                    ? m_Device->GetBackbufferExtent()
                    : Core::Extent2D{.Width = 1, .Height = 1};
                const std::uint32_t histogramWidth = histogramExtent.Width > 0
                    ? static_cast<std::uint32_t>(histogramExtent.Width)
                    : 1u;
                const std::uint32_t histogramHeight = histogramExtent.Height > 0
                    ? static_cast<std::uint32_t>(histogramExtent.Height)
                    : 1u;
                m_PostProcessHistogramPass->SetViewport(histogramWidth, histogramHeight);
                m_PostProcessHistogramPass->SetHistogramBuffer(histogramHandle);
            }
            status = RecordPostProcessHistogramPass(cmd, camera);
        }
        AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);

        const bool histogramStageLive =
            m_Subsystems.PostProcessSystemRegistry().has_value() &&
            m_Subsystems.PostProcessSystemRegistry()->IsStageEnabled(PostProcessStageKind::Histogram);
        if (status == RenderCommandPassStatus::Recorded &&
            histogramStageLive &&
            m_HistogramReadbackBuffer.has_value() &&
            m_HistogramReadbackBuffer->IsValid() &&
            histogramHandle.IsValid())
        {
            const RHI::BufferHandle readbackBuffer =
                m_HistogramReadbackBuffer->GetHandle();
            const std::uint32_t framesInFlight =
                std::max(1u, m_Device->GetFramesInFlight());
            const std::uint32_t slot =
                frame.FrameIndex % framesInFlight;
            const std::uint64_t slotOffset =
                static_cast<std::uint64_t>(slot) *
                kHistogramReadbackSlotBytes;

            cmd.BufferBarrier(histogramHandle,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::TransferRead);
            cmd.CopyBuffer(histogramHandle,
                           readbackBuffer,
                           /*srcOffset=*/0u,
                           /*dstOffset=*/slotOffset,
                           /*sizeBytes=*/kHistogramReadbackSlotBytes);
            cmd.BufferBarrier(histogramHandle,
                              RHI::MemoryAccess::TransferRead,
                              RHI::MemoryAccess::ShaderWrite);
            NoteHistogramReadbackCopyIssued(frame.FrameIndex, slot);
        }
    }

    void NullRenderer::RegisterCommandRoutes()
    {
        m_CommandRouter.Clear();
        const auto id = [](const FrameRecipePassKind kind) noexcept {
            return ToFramePassId(kind);
        };

        m_CommandRouter.Register(id(FrameRecipePassKind::Culling),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status = RecordCullingPass(cmd, *context.Camera);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::DepthPrepass),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordDepthPrepass(cmd, *context.Camera, context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::HZBBuild),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void*) {
                const RenderCommandPassStatus status = RecordHZBBuildPass(cmd);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::ClusterGridBuild),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void*) {
                const RenderCommandPassStatus status = RecordClusterGridBuildPass(cmd);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::LightClusterAssignment),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void*) {
                const RenderCommandPassStatus status = RecordClusterLightAssignmentPass(cmd);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Surface),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status = context.DefaultRecipeUsesDeferred
                    ? RecordDeferredGBufferPass(cmd, *context.Camera, context.Frame->FrameIndex)
                    : RecordForwardSurfacePass(cmd, *context.Camera, context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Composition),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordDeferredLightingPass(cmd, *context.Camera, context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Line),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordForwardLinePass(cmd, *context.Camera, context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Point),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordForwardPointPass(cmd, *context.Camera, context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Shadow),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordShadowPass(cmd, *context.Camera, context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::SelectionOutline),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordSelectionOutlinePass(cmd,
                                               *context.Camera,
                                               context.Frame->FrameIndex,
                                               context.World->Selection);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Picking),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RecordPickingCommandRoute(route, cmd, RouteContextFrom(contextPointer));
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Reconstruction),
            [this](const RenderCommandRoute& route, RHI::ICommandContext&, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordReconstructionPass(context.Frame->FrameIndex);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::PostProcess),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RecordPostProcessCommandRoute(route, cmd, RouteContextFrom(contextPointer));
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::PostProcessHistogram),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RecordPostProcessHistogramCommandRoute(route, cmd, RouteContextFrom(contextPointer));
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::PostProcessAAEdge),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                RenderCommandPassStatus status = RenderCommandPassStatus::SkippedUnavailable;
                {
                    std::lock_guard<std::mutex> postProcessLock(m_PostProcessPassMutex);
                    status = RecordPostProcessAAEdgePass(cmd, *context.Camera);
                }
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::PostProcessAABlend),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                RenderCommandPassStatus status = RenderCommandPassStatus::SkippedUnavailable;
                {
                    std::lock_guard<std::mutex> postProcessLock(m_PostProcessPassMutex);
                    status = RecordPostProcessAABlendPass(cmd, *context.Camera);
                }
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::PostProcessAAResolve),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                RenderCommandPassStatus status = RenderCommandPassStatus::SkippedUnavailable;
                {
                    std::lock_guard<std::mutex> postProcessLock(m_PostProcessPassMutex);
                    status = RecordPostProcessAAResolvePass(cmd, *context.Camera);
                }
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::TransientDebugSurface),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordTransientDebugSurfacePass(cmd, *context.World);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::VisualizationOverlay),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordVisualizationOverlayPass(cmd, *context.World);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::UvView),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void*) {
                if (!m_Subsystems.UvViewSystem())
                {
                    AccumulateCommandRecordStatus(
                        route.DebugName,
                        route.PassId,
                        RenderCommandPassStatus::SkippedUnavailable);
                    return;
                }
                UvView& uvView = *m_Subsystems.UvViewSystem();
                const std::uint64_t before =
                    uvView.GetOutput().RecordedPassCount;
                uvView.Record(cmd);
                const RenderCommandPassStatus status =
                    uvView.GetOutput().RecordedPassCount > before
                        ? RenderCommandPassStatus::Recorded
                        : RenderCommandPassStatus::SkippedUnavailable;
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::ImGui),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordImGuiPass(cmd, context.ActiveRenderPass->FirstColorFormat);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::Present),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void*) {
                const RenderCommandPassStatus status = RecordPresentPass(cmd);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });

        m_CommandRouter.Register(id(FrameRecipePassKind::DebugView),
            [this](const RenderCommandRoute& route, RHI::ICommandContext& cmd, void* contextPointer) {
                RenderCommandRouteContext& context = RouteContextFrom(contextPointer);
                const RenderCommandPassStatus status =
                    RecordDebugViewPass(cmd, *context.Camera);
                AccumulateCommandRecordStatus(route.DebugName, route.PassId, status);
            });
    }

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
