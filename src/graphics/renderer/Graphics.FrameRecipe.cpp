module;

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Graphics.FrameRecipe;

import Extrinsic.Graphics.Pass.PostProcess.Bloom;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] constexpr bool UsesDeferredResources(const FrameRecipeFeatures& features)
        {
            return features.LightingPath == FrameRecipeLightingPath::Deferred ||
                   features.LightingPath == FrameRecipeLightingPath::Hybrid;
        }

        [[nodiscard]] constexpr bool IsSpatialAAMode(const FrameRecipeAAMode mode) noexcept
        {
            return mode == FrameRecipeAAMode::FXAA || mode == FrameRecipeAAMode::SMAA;
        }

        [[nodiscard]] constexpr bool IsReconstructionAAMode(const FrameRecipeAAMode mode) noexcept
        {
            return mode == FrameRecipeAAMode::TAA ||
                   mode == FrameRecipeAAMode::ExternalReconstructor;
        }

        [[nodiscard]] constexpr std::uint32_t ClampExtent(const std::uint32_t value)
        {
            return value == 0u ? 1u : value;
        }

        void AddPass(FrameRecipeIntrospection& out,
                     const FrameRecipePassKind kind,
                     const std::string_view name,
                     const bool enabled,
                     const bool finalizesBackbuffer = false,
                     std::initializer_list<std::string_view> reads = {},
                     std::initializer_list<std::string_view> writes = {})
        {
            out.Passes.push_back(FrameRecipePassDeclaration{
                .Kind = kind,
                .Id = ToFramePassId(kind),
                .Name = name,
                .Enabled = enabled,
                .FinalizesBackbuffer = finalizesBackbuffer,
                .Reads = std::vector<std::string_view>(reads),
                .Writes = std::vector<std::string_view>(writes),
            });
        }

        void AddResource(FrameRecipeIntrospection& out,
                         const FrameRecipeResourceKind kind,
                         const std::string_view name,
                         const bool enabled,
                         const bool imported = false,
                         const bool backbuffer = false,
                         const bool optional = false,
                         const bool importedWriteAllowed = false)
        {
            out.Resources.push_back(FrameRecipeResourceDeclaration{
                .Kind = kind,
                .Id = ToFrameResourceId(kind),
                .Name = name,
                .Enabled = enabled,
                .Imported = imported,
                .Backbuffer = backbuffer,
                .Optional = optional,
                .ImportedWriteAllowed = importedWriteAllowed,
            });
        }

        void AddPassWithVectors(FrameRecipeIntrospection& out,
                                const FrameRecipePassKind kind,
                                const std::string_view name,
                                const bool enabled,
                                const bool finalizesBackbuffer,
                                std::vector<std::string_view> reads,
                                std::vector<std::string_view> writes)
        {
            out.Passes.push_back(FrameRecipePassDeclaration{
                .Kind = kind,
                .Id = ToFramePassId(kind),
                .Name = name,
                .Enabled = enabled,
                .FinalizesBackbuffer = finalizesBackbuffer,
                .Reads = std::move(reads),
                .Writes = std::move(writes),
            });
        }

        [[nodiscard]] RHI::TextureDesc ColorTargetDesc(const std::uint32_t width,
                                                       const std::uint32_t height,
                                                       const RHI::Format format,
                                                       const char* name)
        {
            return RHI::TextureDesc{
                .Width = width,
                .Height = height,
                .Fmt = format,
                .Usage = RHI::TextureUsage::ColorTarget | RHI::TextureUsage::Sampled,
                .DebugName = name,
            };
        }

        [[nodiscard]] RHI::TextureDesc DepthTargetDesc(const std::uint32_t width,
                                                       const std::uint32_t height,
                                                       const RHI::Format format,
                                                       const char* name)
        {
            return RHI::TextureDesc{
                .Width = width,
                .Height = height,
                .Fmt = format,
                .Usage = RHI::TextureUsage::DepthTarget | RHI::TextureUsage::Sampled,
                .DebugName = name,
            };
        }

        [[nodiscard]] RHI::TextureDesc StorageTextureDesc(const std::uint32_t width,
                                                          const std::uint32_t height,
                                                          const RHI::Format format,
                                                          const char* name)
        {
            return RHI::TextureDesc{
                .Width = width,
                .Height = height,
                .Fmt = format,
                .Usage = RHI::TextureUsage::Storage | RHI::TextureUsage::Sampled,
                .DebugName = name,
            };
        }

        [[nodiscard]] constexpr RHI::TextureHandle RenderPassAttachmentToken() noexcept
        {
            return RHI::TextureHandle{0u, 1u};
        }

        constexpr RHI::ColorAttachment kMinimalRenderPassColorAttachments[] = {
            RHI::ColorAttachment{
                .Target = RenderPassAttachmentToken(),
                .Load = RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::Store,
                .ClearR = 0.0f,
                .ClearG = 0.0f,
                .ClearB = 0.0f,
                .ClearA = 1.0f,
            },
        };

        constexpr RHI::ColorAttachment kDefaultClearColorAttachments[] = {
            RHI::ColorAttachment{
                .Target = RenderPassAttachmentToken(),
                .Load = RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::Store,
                // BUG-015: clear the default-recipe scene color to a visible
                // blue so an operational promoted-Vulkan frame is plainly
                // distinguishable from an undefined/black backbuffer.
                .ClearR = 0.10f,
                .ClearG = 0.20f,
                .ClearB = 0.45f,
                .ClearA = 1.0f,
            },
        };

        constexpr RHI::ColorAttachment kDefaultLoadColorAttachments[] = {
            RHI::ColorAttachment{
                .Target = RenderPassAttachmentToken(),
                .Load = RHI::LoadOp::Load,
                .Store = RHI::StoreOp::Store,
            },
        };

        constexpr RHI::ColorAttachment kDefaultClearTwoColorAttachments[] = {
            // BUG-015: first attachment is the forward scene color — clear it to
            // the same visible blue as the no-motion path; the second attachment
            // is motion vectors and stays cleared to zero.
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store,
                                 .ClearR = 0.10f, .ClearG = 0.20f, .ClearB = 0.45f, .ClearA = 1.0f},
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
        };

        constexpr RHI::ColorAttachment kDefaultClearThreeColorAttachments[] = {
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
        };

        constexpr RHI::ColorAttachment kDefaultClearFourColorAttachments[] = {
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
            RHI::ColorAttachment{.Target = RenderPassAttachmentToken(), .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
        };

        // GRAPHICS-077 Slice A — LOAD-store color attachment template for
        // the `TransientDebugSurfacePass` render-pass desc. The lit
        // composition must survive into the postprocess chain, so the
        // load op is `Load` (not `Clear`). Single color target binds
        // against `SceneColorHDR`; the executor resolves the
        // `RenderPassAttachmentToken` to the compiled `SceneColorHDR`
        // handle through `CompiledRenderPassAttachment::ResourceIndex`.
        constexpr RHI::ColorAttachment kTransientDebugSurfaceRenderPassColorAttachments[] = {
            RHI::ColorAttachment{
                .Target = RenderPassAttachmentToken(),
                .Load = RHI::LoadOp::Load,
                .Store = RHI::StoreOp::Store,
            },
        };

        // GRAPHICS-078 Slice A — LOAD-store color attachment template for
        // the `VisualizationOverlayPass` render-pass desc, mirroring the
        // GRAPHICS-077 transient-debug shape. The pass runs immediately
        // after `TransientDebugSurfacePass` so the lit + transient-debug
        // composition must survive into the postprocess chain (load op
        // `Load`, not `Clear`). Single color target binds against
        // `SceneColorHDR`; the executor resolves the
        // `RenderPassAttachmentToken` to the compiled `SceneColorHDR`
        // handle through `CompiledRenderPassAttachment::ResourceIndex`.
        constexpr RHI::ColorAttachment kVisualizationOverlayRenderPassColorAttachments[] = {
            RHI::ColorAttachment{
                .Target = RenderPassAttachmentToken(),
                .Load = RHI::LoadOp::Load,
                .Store = RHI::StoreOp::Store,
            },
        };

    }

    std::string_view FrameRecipePassKindName(const FrameRecipePassKind kind) noexcept
    {
        switch (kind)
        {
        case FrameRecipePassKind::Culling: return "CullingPass";
        case FrameRecipePassKind::Picking: return "PickingPass";
        case FrameRecipePassKind::DepthPrepass: return "DepthPrepass";
        case FrameRecipePassKind::Shadow: return "ShadowPass";
        case FrameRecipePassKind::Surface: return "SurfacePass";
        case FrameRecipePassKind::Composition: return "CompositionPass";
        case FrameRecipePassKind::Line: return "LinePass";
        case FrameRecipePassKind::Point: return "PointPass";
        case FrameRecipePassKind::PostProcess: return "PostProcessPass";
        case FrameRecipePassKind::PostProcessHistogram: return "PostProcessHistogramPass";
        case FrameRecipePassKind::PostProcessAAEdge: return "PostProcessAAEdgePass";
        case FrameRecipePassKind::PostProcessAABlend: return "PostProcessAABlendPass";
        case FrameRecipePassKind::PostProcessAAResolve: return "PostProcessAAResolvePass";
        case FrameRecipePassKind::SelectionOutline: return "SelectionOutlinePass";
        case FrameRecipePassKind::DebugView: return "DebugViewPass";
        case FrameRecipePassKind::ImGui: return "ImGuiPass";
        case FrameRecipePassKind::Present: return "Present";
        case FrameRecipePassKind::TransientDebugSurface: return "TransientDebugSurfacePass";
        case FrameRecipePassKind::VisualizationOverlay: return "VisualizationOverlayPass";
        case FrameRecipePassKind::HZBBuild: return "HZBBuildPass";
        case FrameRecipePassKind::ClusterGridBuild: return "ClusterGridBuildPass";
        case FrameRecipePassKind::LightClusterAssignment: return "LightClusterAssignmentPass";
        case FrameRecipePassKind::Reconstruction: return "ReconstructionPass";
        }
        return "UnknownPass";
    }

    std::string_view FrameRecipeResourceKindName(const FrameRecipeResourceKind kind) noexcept
    {
        switch (kind)
        {
        case FrameRecipeResourceKind::Backbuffer: return "Backbuffer";
        case FrameRecipeResourceKind::SceneDepth: return "SceneDepth";
        case FrameRecipeResourceKind::EntityId: return "EntityId";
        case FrameRecipeResourceKind::PrimitiveId: return "PrimitiveId";
        case FrameRecipeResourceKind::SceneNormal: return "SceneNormal";
        case FrameRecipeResourceKind::Albedo: return "Albedo";
        case FrameRecipeResourceKind::Material0: return "Material0";
        case FrameRecipeResourceKind::SceneColorHDR: return "SceneColorHDR";
        case FrameRecipeResourceKind::ShadowAtlas: return "ShadowAtlas";
        case FrameRecipeResourceKind::SceneColorLDR: return "SceneColorLDR";
        case FrameRecipeResourceKind::SelectionOutline: return "SelectionOutline";
        case FrameRecipeResourceKind::DebugViewRGBA: return "DebugViewRGBA";
        case FrameRecipeResourceKind::SceneTable: return "GpuWorld.SceneTable";
        case FrameRecipeResourceKind::InstanceStatic: return "GpuWorld.InstanceStatic";
        case FrameRecipeResourceKind::InstanceDynamic: return "GpuWorld.InstanceDynamic";
        case FrameRecipeResourceKind::EntityConfig: return "GpuWorld.EntityConfig";
        case FrameRecipeResourceKind::GeometryRecords: return "GpuWorld.GeometryRecords";
        case FrameRecipeResourceKind::Bounds: return "GpuWorld.Bounds";
        case FrameRecipeResourceKind::Lights: return "GpuWorld.Lights";
        case FrameRecipeResourceKind::MaterialBuffer: return "Material.Buffer";
        case FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs: return "Cull.SurfaceOpaque.IndexedArgs";
        case FrameRecipeResourceKind::SurfaceOpaqueCount: return "Cull.SurfaceOpaque.Count";
        case FrameRecipeResourceKind::LinesIndexedArgs: return "Cull.Lines.IndexedArgs";
        case FrameRecipeResourceKind::LinesCount: return "Cull.Lines.Count";
        case FrameRecipeResourceKind::PointsNonIndexedArgs: return "Cull.Points.NonIndexedArgs";
        case FrameRecipeResourceKind::PointsCount: return "Cull.Points.Count";
        case FrameRecipeResourceKind::PickingReadback: return "Picking.Readback";
        case FrameRecipeResourceKind::PostProcessBloomScratch: return "PostProcess.BloomScratch";
        case FrameRecipeResourceKind::PostProcessHistogram: return "PostProcess.Histogram";
        case FrameRecipeResourceKind::PostProcessAATempEdges: return "PostProcess.AATemp.Edges";
        case FrameRecipeResourceKind::PostProcessAATempWeights: return "PostProcess.AATemp.Weights";
        case FrameRecipeResourceKind::PostProcessAATempResolved: return "PostProcess.AATemp.Resolved";
        case FrameRecipeResourceKind::HistogramReadback: return "Histogram.Readback";
        case FrameRecipeResourceKind::HZBCurrent: return "HZB.Current";
        case FrameRecipeResourceKind::ClusterGridAABBs: return "ClusterGrid.AABBs";
        case FrameRecipeResourceKind::ClusterLightHeaders: return "ClusterLights.Headers";
        case FrameRecipeResourceKind::ClusterLightIndices: return "ClusterLights.Indices";
        case FrameRecipeResourceKind::ClusterLightCounter: return "ClusterLights.Counter";
        case FrameRecipeResourceKind::MotionVectors: return "MotionVectors";
        case FrameRecipeResourceKind::ReconstructionHistoryPrevious: return "Reconstruction.HistoryPrevious";
        case FrameRecipeResourceKind::ReconstructionHistoryCurrent: return "Reconstruction.HistoryCurrent";
        case FrameRecipeResourceKind::ReconstructionResolvedHDR: return "Reconstruction.ResolvedHDR";
        }
        return "UnknownResource";
    }

    std::string_view FrameRecipePassIdName(const FramePassId id) noexcept
    {
        if (!id.IsValid())
        {
            return "InvalidPass";
        }
        return FrameRecipePassKindName(static_cast<FrameRecipePassKind>(id.Value - 1u));
    }

    std::string_view FrameRecipeResourceIdName(const FrameResourceId id) noexcept
    {
        if (!id.IsValid())
        {
            return "InvalidResource";
        }
        return FrameRecipeResourceKindName(static_cast<FrameRecipeResourceKind>(id.Value - 1u));
    }

    [[nodiscard]] FrameRecipeFeatures DeriveDefaultFrameRecipeFeatures(const RenderWorld& world)
    {
        FrameRecipeFeatures features{};
        // GRAPHICS-070 — until the deferred GBuffer + lighting wiring lands
        // (GRAPHICS-072), the default recipe operationally selects the forward
        // surface path. The deferred-mode resources/passes remain declared and
        // testable via explicit `FrameRecipeFeatures{}.LightingPath = Deferred`
        // at the recipe-introspection level (see
        // `tests/contract/graphics/Test.FrameRecipeContract.cpp`); GRAPHICS-072
        // is the slice that gates the runtime default back to a coexistence
        // policy once both surface bodies are wired.
        features.LightingPath = FrameRecipeLightingPath::Forward;
        features.EnablePicking = world.HasPendingPick || world.PickRequest.Pending;
        features.EnableShadows = world.Shadows.Enabled && world.Shadows.CascadeCount > 0u;
        features.EnableSelectionOutline = world.Selection.HasHovered || !world.Selection.SelectedStableIds.empty();
        features.EnableDebugView = world.DebugOverlayEnabled || world.DebugPrimitives.HasTransientDebug;
        features.EnablePostProcess = true;
        features.EnableImGui = true;
        // GRAPHICS-077 Slice A — enable the `TransientDebugSurfacePass`
        // only when at least one transient debug primitive packet exists
        // for the frame. The recipe omits the pass entirely when all three
        // lanes are empty so `CommandRecords` stays clean on frames where
        // the world has no transient debug payload. The world-side gate
        // checks each lane span directly (rather than
        // `HasTransientDebug`) because the latter is also set by
        // `DebugOverlayEnabled` upstream, and the new pass must remain
        // span-driven independent of the overlay toggle.
        features.EnableTransientDebugSurface =
            !world.DebugPrimitives.Lines.empty() ||
            !world.DebugPrimitives.Points.empty() ||
            !world.DebugPrimitives.Triangles.empty();
        // GRAPHICS-078 Slice A — enable the `VisualizationOverlayPass`
        // only when at least one visualization-overlay packet (vector
        // field or isoline) exists for the frame. The recipe omits the
        // pass entirely when both kinds are empty so `CommandRecords`
        // stays clean on frames where the world has no overlay payload.
        // The gate is span-driven (mirroring GRAPHICS-077) so the new
        // pass remains independent of any UI toggle. Htex /
        // fragment-bake atlases stay out of scope per the task
        // non-goals (they remain consumed by other paths, not produced
        // by this pass).
        features.EnableVisualizationOverlay =
            !world.Visualization.VectorFields.empty() ||
            !world.Visualization.Isolines.empty();
        return features;
    }

    [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features)
    {
        return DescribeDefaultFrameRecipe(features, FrameRecipeAAOptions{}, FrameRecipeTemporalOptions{});
    }

    [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features,
                                                                      const FrameRecipeTemporalOptions temporalOptions)
    {
        return DescribeDefaultFrameRecipe(features, FrameRecipeAAOptions{}, temporalOptions);
    }

    [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features,
                                                                      const FrameRecipeAAOptions& aaOptions,
                                                                      const FrameRecipeTemporalOptions temporalOptions)
    {
        const bool usesDeferred = UsesDeferredResources(features);
        // GRAPHICS-074 recipe-side follow-up — picking-active is the
        // recipe-wide condition that gates `PickingPass`, its `PrimitiveId`
        // and `Picking.Readback` outputs, and the `EntityId` half of its
        // output (jointly with `EnableSelectionOutline`, which is the only
        // other consumer of `EntityId`). Centralising the conjunction keeps
        // the pass declaration and the resource declarations from drifting
        // and avoids allocating the full-resolution R32_UINT `PrimitiveId`
        // target / the `Picking.Readback` buffer when picking is dropped.
        const bool pickingActive = features.EnablePicking && features.EnableDepthPrepass;
        const bool hzbBuildActive = features.EnableHZBBuild && features.EnableDepthPrepass;
        const bool clusterGridBuildActive = features.EnableClusterGridBuild && features.EnableDepthPrepass;
        const bool clusterLightAssignmentActive =
            features.EnableClusterLightAssignment && clusterGridBuildActive;
        const bool reconstructionActive =
            IsReconstructionAAMode(aaOptions.Mode) && !temporalOptions.NoJitterNoHistory;
        const bool smaaActive = aaOptions.Mode == FrameRecipeAAMode::SMAA;
        const bool spatialAAActive = IsSpatialAAMode(aaOptions.Mode);
        const bool motionVectorsActive =
            (temporalOptions.EnableMotionVectors || reconstructionActive) &&
            !temporalOptions.NoJitterNoHistory;
        const std::string_view postProcessInput =
            reconstructionActive ? std::string_view{"Reconstruction.ResolvedHDR"}
                                 : std::string_view{"SceneColorHDR"};
        FrameRecipeIntrospection out{};

        AddPass(out, FrameRecipePassKind::Culling, "CullingPass", true, false,
                {"GpuWorld.SceneTable", "GpuWorld.InstanceStatic", "GpuWorld.InstanceDynamic", "GpuWorld.EntityConfig", "GpuWorld.GeometryRecords", "GpuWorld.Bounds", "Material.Buffer", "GpuWorld.Lights"},
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "Cull.Lines.IndexedArgs", "Cull.Lines.Count", "Cull.Points.NonIndexedArgs", "Cull.Points.Count"});
        AddPass(out, FrameRecipePassKind::DepthPrepass, "DepthPrepass", features.EnableDepthPrepass, false,
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"}, {"SceneDepth"});
        AddPass(out, FrameRecipePassKind::HZBBuild, "HZBBuildPass", hzbBuildActive, false,
                {"SceneDepth"}, {"HZB.Current"});
        AddPass(out, FrameRecipePassKind::ClusterGridBuild, "ClusterGridBuildPass",
                clusterGridBuildActive, false, {}, {"ClusterGrid.AABBs"});
        AddPass(out, FrameRecipePassKind::LightClusterAssignment, "LightClusterAssignmentPass",
                clusterLightAssignmentActive, false,
                {"ClusterGrid.AABBs", "GpuWorld.Lights"},
                {"ClusterLights.Headers", "ClusterLights.Indices", "ClusterLights.Counter"});
        // GRAPHICS-074 recipe-side follow-up — picking now runs *after*
        // `DepthPrepass` and reads `SceneDepth` so the picking pipeline can
        // depth-equal-test against the nearest-surface depth instead of
        // last-fragment-winning into the `EntityId`/`PrimitiveId` targets.
        // The pass is therefore gated on `pickingActive` (`EnablePicking &&
        // EnableDepthPrepass`); with `EnableDepthPrepass=false` the recipe
        // would not produce a valid `SceneDepth` and the depth-equal pipeline
        // would be render-pass-incompatible.
        AddPass(out, FrameRecipePassKind::Picking, "PickingPass",
                pickingActive, false,
                {"SceneDepth", "Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "Cull.Lines.IndexedArgs", "Cull.Lines.Count", "Cull.Points.NonIndexedArgs", "Cull.Points.Count"},
                {"EntityId", "PrimitiveId", "Picking.Readback"});
        AddPass(out, FrameRecipePassKind::Shadow, "ShadowPass", features.EnableShadows, false,
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"}, {"ShadowAtlas"});
        if (usesDeferred)
        {
            // GRAPHICS-072 Slice C — the deferred SurfacePass is the GBuffer
            // pass; it no longer samples `ShadowAtlas`. Shadow sampling moved
            // to the deferred CompositionPass with `TextureUsage::ShaderRead`.
            std::vector<std::string_view> surfaceWrites =
                features.EnableDepthPrepass
                    ? std::vector<std::string_view>{"SceneNormal", "Albedo", "Material0"}
                    : std::vector<std::string_view>{"SceneNormal", "Albedo", "Material0", "SceneDepth"};
            if (motionVectorsActive)
            {
                surfaceWrites.push_back("MotionVectors");
            }
            AddPassWithVectors(out,
                               FrameRecipePassKind::Surface,
                               "SurfacePass",
                               true,
                               false,
                               {"GpuWorld.SceneTable", "GpuWorld.InstanceStatic", "GpuWorld.InstanceDynamic", "GpuWorld.GeometryRecords", "Material.Buffer", "Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "SceneDepth"},
                               std::move(surfaceWrites));
        }
        else
        {
            std::vector<std::string_view> surfaceReads{
                "GpuWorld.SceneTable",
                "GpuWorld.InstanceStatic",
                "GpuWorld.InstanceDynamic",
                "GpuWorld.GeometryRecords",
                "Material.Buffer",
                "Cull.SurfaceOpaque.IndexedArgs",
                "Cull.SurfaceOpaque.Count",
                "SceneDepth",
                "ShadowAtlas",
            };
            if (clusterLightAssignmentActive)
            {
                surfaceReads.push_back("ClusterLights.Headers");
                surfaceReads.push_back("ClusterLights.Indices");
            }
            std::vector<std::string_view> surfaceWrites{"SceneColorHDR", "SceneDepth"};
            if (motionVectorsActive)
            {
                surfaceWrites.push_back("MotionVectors");
            }
            AddPassWithVectors(out,
                               FrameRecipePassKind::Surface,
                               "SurfacePass",
                               true,
                               false,
                               std::move(surfaceReads),
                               std::move(surfaceWrites));
        }
        std::vector<std::string_view> compositionReads{
            "SceneNormal",
            "Albedo",
            "Material0",
            "SceneDepth",
            "GpuWorld.Lights",
            "ShadowAtlas",
        };
        if (clusterLightAssignmentActive && usesDeferred)
        {
            compositionReads.push_back("ClusterLights.Headers");
            compositionReads.push_back("ClusterLights.Indices");
        }
        AddPassWithVectors(out,
                           FrameRecipePassKind::Composition,
                           "CompositionPass",
                           usesDeferred,
                           false,
                           std::move(compositionReads),
                           {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::Line, "LinePass", true, false,
                {"SceneDepth", "Cull.Lines.IndexedArgs", "Cull.Lines.Count"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::Point, "PointPass", true, false,
                {"SceneDepth", "Cull.Points.NonIndexedArgs", "Cull.Points.Count"}, {"SceneColorHDR"});
        // GRAPHICS-077 Slice A — transient-debug surface overlay. Placed
        // between the lit-composition family (`SurfacePass` /
        // `CompositionPass` / `LinePass` / `PointPass`) and the
        // postprocess chain (`PostProcessHistogramPass` is first under
        // `EnablePostProcess`) so transient primitives reach postprocess
        // inputs deterministically when present. The pass reads
        // `SceneDepth` (depth-test variant per packet `DepthTested`
        // field, lands in Slice B/C), reads + writes `SceneColorHDR`
        // (LOAD-store color attachment so the lit color survives), and
        // is omitted entirely when no transient debug primitives exist
        // for the frame. Slice A is scaffold-only — the executor branch
        // reports `SkippedUnavailable` because no pipelines exist yet.
        AddPass(out, FrameRecipePassKind::TransientDebugSurface, "TransientDebugSurfacePass",
                features.EnableTransientDebugSurface, false,
                {"SceneColorHDR", "SceneDepth"}, {"SceneColorHDR"});
        // GRAPHICS-078 Slice A — visualization-overlay pass. Placed
        // immediately after `TransientDebugSurfacePass` (same "post-lit,
        // pre-postprocess" band) and before the postprocess chain so
        // vector-field glyphs / isoline polylines reach postprocess
        // inputs deterministically when present. The pass reads
        // `SceneDepth` (depth-test variant per packet `DepthTested`
        // field, lands in Slice B/C), reads + writes `SceneColorHDR`
        // (LOAD-store color attachment so the lit + transient-debug
        // composition survives), and is omitted entirely when no
        // visualization overlay packets exist for the frame. Slice A
        // is scaffold-only — the executor branch reports
        // `SkippedUnavailable` because no pipelines exist yet.
        AddPass(out, FrameRecipePassKind::VisualizationOverlay, "VisualizationOverlayPass",
                features.EnableVisualizationOverlay, false,
                {"SceneColorHDR", "SceneDepth"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::Reconstruction, "ReconstructionPass",
                reconstructionActive, false,
                {"SceneColorHDR", "SceneDepth", "MotionVectors", "Reconstruction.HistoryPrevious"},
                {"Reconstruction.HistoryCurrent", "Reconstruction.ResolvedHDR"});
        // GRAPHICS-040C — postprocess consumes reconstructed HDR when the AA
        // selector chooses a temporal mode, otherwise it consumes the lit HDR
        // target directly. Spatial AA remains post-tonemap and declares only
        // the passes needed by the selected mode: FXAA resolve-only; SMAA
        // edge/blend/resolve.
        AddPass(out, FrameRecipePassKind::PostProcessHistogram, "PostProcessHistogramPass", features.EnablePostProcess, false,
                {postProcessInput}, {"PostProcess.Histogram", "Histogram.Readback"});
        AddPass(out, FrameRecipePassKind::PostProcess, "PostProcessPass", features.EnablePostProcess, false,
                {postProcessInput}, {"PostProcess.BloomScratch", "SceneColorLDR"});
        AddPass(out, FrameRecipePassKind::PostProcessAAEdge, "PostProcessAAEdgePass",
                features.EnablePostProcess && smaaActive, false,
                {"SceneColorLDR"}, {"PostProcess.AATemp.Edges"});
        AddPass(out, FrameRecipePassKind::PostProcessAABlend, "PostProcessAABlendPass",
                features.EnablePostProcess && smaaActive, false,
                {"PostProcess.AATemp.Edges"}, {"PostProcess.AATemp.Weights"});
        const std::vector<std::string_view> aaResolveReads =
            smaaActive ? std::vector<std::string_view>{"SceneColorLDR", "PostProcess.AATemp.Weights"}
                       : std::vector<std::string_view>{"SceneColorLDR"};
        AddPassWithVectors(out, FrameRecipePassKind::PostProcessAAResolve,
                           "PostProcessAAResolvePass",
                           features.EnablePostProcess && spatialAAActive,
                           false,
                           aaResolveReads,
                           {"PostProcess.AATemp.Resolved"});
        AddPass(out, FrameRecipePassKind::SelectionOutline, "SelectionOutlinePass", features.EnableSelectionOutline, false,
                {"FrameRecipe.PresentSource", "EntityId", "SceneDepth"}, {"SelectionOutline"});
        AddPass(out, FrameRecipePassKind::DebugView, "DebugViewPass", features.EnableDebugView, false,
                {"FrameRecipe.PresentSource"}, {"DebugViewRGBA"});
        AddPass(out, FrameRecipePassKind::ImGui, "ImGuiPass", features.EnableImGui, false,
                {"FrameRecipe.PresentSource"}, {"FrameRecipe.PresentSource"});
        AddPass(out, FrameRecipePassKind::Present, "Present", true, true, {"FrameRecipe.PresentSource"}, {"Backbuffer"});

        AddResource(out, FrameRecipeResourceKind::Backbuffer, "Backbuffer", true, true, true);
        AddResource(out, FrameRecipeResourceKind::SceneDepth, "SceneDepth", true);
        AddResource(out, FrameRecipeResourceKind::HZBCurrent, "HZB.Current", hzbBuildActive, true, false, true, true);
        AddResource(out, FrameRecipeResourceKind::ClusterGridAABBs, "ClusterGrid.AABBs",
                    clusterGridBuildActive, true, false, true, true);
        AddResource(out, FrameRecipeResourceKind::ClusterLightHeaders, "ClusterLights.Headers",
                    clusterLightAssignmentActive, true, false, true, true);
        AddResource(out, FrameRecipeResourceKind::ClusterLightIndices, "ClusterLights.Indices",
                    clusterLightAssignmentActive, true, false, true, true);
        AddResource(out, FrameRecipeResourceKind::ClusterLightCounter, "ClusterLights.Counter",
                    clusterLightAssignmentActive, true, false, true, true);
        // GRAPHICS-074 recipe-side follow-up — `EntityId` is consumed by
        // PickingPass (active iff `pickingActive`) and SelectionOutlinePass
        // (active iff `EnableSelectionOutline`); `PrimitiveId` and
        // `Picking.Readback` are consumed only by PickingPass. Gating these
        // on the same conjunction the pass uses avoids allocating dead
        // full-resolution R32_UINT targets / the host-visible readback
        // buffer when picking is dropped because the depth prepass is off.
        AddResource(out, FrameRecipeResourceKind::EntityId, "EntityId", pickingActive || features.EnableSelectionOutline, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PrimitiveId, "PrimitiveId", pickingActive, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneNormal, "SceneNormal", usesDeferred, false, false, true);
        AddResource(out, FrameRecipeResourceKind::Albedo, "Albedo", usesDeferred, false, false, true);
        AddResource(out, FrameRecipeResourceKind::Material0, "Material0", usesDeferred, false, false, true);
        AddResource(out, FrameRecipeResourceKind::MotionVectors, "MotionVectors", motionVectorsActive, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneColorHDR, "SceneColorHDR", true);
        AddResource(out, FrameRecipeResourceKind::ReconstructionHistoryPrevious,
                    "Reconstruction.HistoryPrevious", reconstructionActive, true, false, true);
        AddResource(out, FrameRecipeResourceKind::ReconstructionHistoryCurrent,
                    "Reconstruction.HistoryCurrent", reconstructionActive, true, false, true, true);
        AddResource(out, FrameRecipeResourceKind::ReconstructionResolvedHDR,
                    "Reconstruction.ResolvedHDR", reconstructionActive, false, false, true);
        // GRAPHICS-073 Slice B — declare ShadowAtlas as imported-write-allowed.
        // `BuildDefaultFrameRecipe` chooses between an imported handle (when
        // `imports.ShadowAtlas.IsValid()`) and a transient depth target at
        // build time; the validator only attaches an authorized writer set
        // when the *compiled* graph marks the resource imported, so the
        // declaration stays valid for both paths.
        AddResource(out, FrameRecipeResourceKind::ShadowAtlas, "ShadowAtlas", features.EnableShadows, true, false, true, true);
        AddResource(out, FrameRecipeResourceKind::SceneColorLDR, "SceneColorLDR", features.EnablePostProcess, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PostProcessBloomScratch, "PostProcess.BloomScratch", features.EnablePostProcess, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PostProcessHistogram, "PostProcess.Histogram", features.EnablePostProcess, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PostProcessAATempEdges, "PostProcess.AATemp.Edges",
                    features.EnablePostProcess && smaaActive, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PostProcessAATempWeights, "PostProcess.AATemp.Weights",
                    features.EnablePostProcess && smaaActive, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PostProcessAATempResolved, "PostProcess.AATemp.Resolved",
                    features.EnablePostProcess && spatialAAActive, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SelectionOutline, "SelectionOutline", features.EnableSelectionOutline, false, false, true);
        AddResource(out, FrameRecipeResourceKind::DebugViewRGBA, "DebugViewRGBA", features.EnableDebugView, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneTable, "GpuWorld.SceneTable", true, true);
        AddResource(out, FrameRecipeResourceKind::InstanceStatic, "GpuWorld.InstanceStatic", true, true);
        AddResource(out, FrameRecipeResourceKind::InstanceDynamic, "GpuWorld.InstanceDynamic", true, true);
        AddResource(out, FrameRecipeResourceKind::EntityConfig, "GpuWorld.EntityConfig", true, true);
        AddResource(out, FrameRecipeResourceKind::GeometryRecords, "GpuWorld.GeometryRecords", true, true);
        AddResource(out, FrameRecipeResourceKind::Bounds, "GpuWorld.Bounds", true, true);
        AddResource(out, FrameRecipeResourceKind::Lights, "GpuWorld.Lights", true, true);
        AddResource(out, FrameRecipeResourceKind::MaterialBuffer, "Material.Buffer", true, true);
        AddResource(out, FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs, "Cull.SurfaceOpaque.IndexedArgs", true, true, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SurfaceOpaqueCount, "Cull.SurfaceOpaque.Count", true, true, false, false, true);
        AddResource(out, FrameRecipeResourceKind::LinesIndexedArgs, "Cull.Lines.IndexedArgs", true, true, false, false, true);
        AddResource(out, FrameRecipeResourceKind::LinesCount, "Cull.Lines.Count", true, true, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PointsNonIndexedArgs, "Cull.Points.NonIndexedArgs", true, true, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PointsCount, "Cull.Points.Count", true, true, false, false, true);
        // GRAPHICS-074 Slice D.2 — the Picking.Readback buffer is now the
        // renderer-owned host-visible buffer imported through
        // `FrameRecipeImports::PickingReadback` rather than a transient
        // `graph.CreateBuffer(...)`. Declare it as imported (so the
        // recipe-aware validator authorises `PickingPass` to write to it via
        // `ImportedWriteAllowed=true` + the matching "Picking.Readback"
        // entry in the pass's `Writes` list) while keeping `optional=true`
        // since the resource still only exists when `pickingActive` is true.
        AddResource(out, FrameRecipeResourceKind::PickingReadback, "Picking.Readback", pickingActive, true, false, true, true);
        // GRAPHICS-075 Slice E.2 — the histogram readback buffer is the
        // renderer-owned host-visible buffer imported through
        // `FrameRecipeImports::HistogramReadback`. Declared imported +
        // imported-write-allowed (Slice E.2 records
        // `CopyBuffer(PostProcess.Histogram → Histogram.Readback)` after the
        // dispatch). Gated on `EnablePostProcess` to match the other
        // postprocess transients above.
        AddResource(out, FrameRecipeResourceKind::HistogramReadback, "Histogram.Readback", features.EnablePostProcess, true, false, true, true);
        return out;
    }

    std::optional<std::uint32_t> FindFrameRecipePassIndexById(
        const FrameRecipeIntrospection& recipe,
        const FramePassId id)
    {
        if (!id.IsValid())
        {
            return std::nullopt;
        }

        for (std::uint32_t passIndex = 0; passIndex < recipe.Passes.size(); ++passIndex)
        {
            if (recipe.Passes[passIndex].Id == id)
            {
                return passIndex;
            }
        }
        return std::nullopt;
    }

    std::optional<std::uint32_t> FindFrameRecipeResourceIndexById(
        const FrameRecipeIntrospection& recipe,
        const FrameResourceId id)
    {
        if (!id.IsValid())
        {
            return std::nullopt;
        }

        for (std::uint32_t resourceIndex = 0; resourceIndex < recipe.Resources.size(); ++resourceIndex)
        {
            if (recipe.Resources[resourceIndex].Id == id)
            {
                return resourceIndex;
            }
        }
        return std::nullopt;
    }

    namespace
    {
        [[nodiscard]] std::optional<std::uint32_t> FindCompiledNameIndex(
            const std::vector<std::string>& names,
            const std::string_view name)
        {
            for (std::uint32_t index = 0; index < names.size(); ++index)
            {
                if (names[index] == name)
                {
                    return index;
                }
            }
            return std::nullopt;
        }
    }

    std::optional<std::uint32_t> FindCompiledPassIndexForRecipeId(
        const FrameRecipeIntrospection& recipe,
        const CompiledRenderGraph& compiled,
        const FramePassId id)
    {
        if (!FindFrameRecipePassIndexById(recipe, id).has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t index = 0u; index < compiled.PassIds.size(); ++index)
        {
            if (compiled.PassIds[index] == id)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<std::uint32_t> FindCompiledTextureIndexForRecipeId(
        const FrameRecipeIntrospection& recipe,
        const CompiledRenderGraph& compiled,
        const FrameResourceId id)
    {
        const std::optional<std::uint32_t> recipeIndex = FindFrameRecipeResourceIndexById(recipe, id);
        if (!recipeIndex.has_value())
        {
            return std::nullopt;
        }
        return FindCompiledNameIndex(compiled.TextureNames, recipe.Resources[*recipeIndex].Name);
    }

    std::optional<std::uint32_t> FindCompiledBufferIndexForRecipeId(
        const FrameRecipeIntrospection& recipe,
        const CompiledRenderGraph& compiled,
        const FrameResourceId id)
    {
        const std::optional<std::uint32_t> recipeIndex = FindFrameRecipeResourceIndexById(recipe, id);
        if (!recipeIndex.has_value())
        {
            return std::nullopt;
        }
        return FindCompiledNameIndex(compiled.BufferNames, recipe.Resources[*recipeIndex].Name);
    }

    [[nodiscard]] RenderGraphValidationResult ValidateRecipeCompiledGraph(
        const FrameRecipeIntrospection& recipe,
        const CompiledRenderGraph& compiled)
    {
        auto containsName = [](const std::vector<std::string_view>& names, const std::string_view name) {
            for (const std::string_view candidate : names)
            {
                if (candidate == name)
                {
                    return true;
                }
            }
            return false;
        };

        auto authorizedWritersFor = [&recipe, &containsName](const FrameRecipeResourceDeclaration& resource) {
            std::vector<std::string> writerNames{};
            for (const FrameRecipePassDeclaration& pass : recipe.Passes)
            {
                if (!pass.Enabled)
                {
                    continue;
                }

                if ((resource.Backbuffer && pass.FinalizesBackbuffer) ||
                    (resource.ImportedWriteAllowed && containsName(pass.Writes, resource.Name)))
                {
                    writerNames.emplace_back(pass.Name);
                }
            }
            return writerNames;
        };

        std::vector<ImportedResourceAuthorization> authorizations{};
        auto addAuthorizationsFor = [&authorizations](const std::string_view resourceName,
                                                       const ImportedResourceWritePolicy policy,
                                                       const std::vector<std::string>& writerNames,
                                                       const bool isTexture,
                                                       const std::vector<std::string>& compiledNames,
                                                       const std::vector<bool>& importedFlags) {
            for (std::uint32_t index = 0; index < compiledNames.size(); ++index)
            {
                if (compiledNames[index] != resourceName || index >= importedFlags.size() || !importedFlags[index])
                {
                    continue;
                }

                authorizations.push_back(ImportedResourceAuthorization{
                    .ResourceIndex = index,
                    .IsTexture = isTexture,
                    .Policy = policy,
                    .AuthorizedWriterPassNames = writerNames,
                });
            }
        };

        for (const FrameRecipeResourceDeclaration& resource : recipe.Resources)
        {
            if (!resource.Enabled || !resource.Imported)
            {
                continue;
            }

            const ImportedResourceWritePolicy policy = (resource.Backbuffer || resource.ImportedWriteAllowed)
                                                           ? ImportedResourceWritePolicy::AllowFinalizerOnly
                                                           : ImportedResourceWritePolicy::Disallow;
            const std::vector<std::string> writerNames = policy == ImportedResourceWritePolicy::Disallow
                                                             ? std::vector<std::string>{}
                                                             : authorizedWritersFor(resource);

            addAuthorizationsFor(resource.Name, policy, writerNames, true, compiled.TextureNames, compiled.TextureImported);
            addAuthorizationsFor(resource.Name, policy, writerNames, false, compiled.BufferNames, compiled.BufferImported);
        }

        return ValidateCompiledGraph(compiled, authorizations);
    }

    [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeShadowSizing& shadowSizing)
    {
        return BuildDefaultFrameRecipe(graph,
                                       features,
                                       imports,
                                       sizing,
                                       FrameRecipeAAOptions{},
                                       shadowSizing,
                                       FrameRecipeTemporalOptions{});
    }

    [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeShadowSizing& shadowSizing,
                                                                        const FrameRecipeTemporalOptions temporalOptions)
    {
        return BuildDefaultFrameRecipe(graph,
                                       features,
                                       imports,
                                       sizing,
                                       FrameRecipeAAOptions{},
                                       shadowSizing,
                                       temporalOptions);
    }

    [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing,
                                                                        const FrameRecipeAAOptions& aaOptions,
                                                                        const FrameRecipeShadowSizing& shadowSizing,
                                                                        const FrameRecipeTemporalOptions temporalOptions)
    {
        if (!imports.Backbuffer.IsValid())
        {
            return FrameRecipeBuildResult{
                .Succeeded = false,
                .Diagnostic = "FrameRecipe requires a valid imported Backbuffer handle.",
            };
        }

        const bool usesDeferred = UsesDeferredResources(features);
        // GRAPHICS-074 recipe-side follow-up — same `pickingActive`
        // conjunction `DescribeDefaultFrameRecipe` uses; keep the
        // declaration and the build wired identically so the `EntityId` /
        // `PrimitiveId` / `Picking.Readback` resources are not allocated
        // when `PickingPass` is dropped.
        const bool pickingActive = features.EnablePicking && features.EnableDepthPrepass;
        const bool hzbBuildActive = features.EnableHZBBuild && features.EnableDepthPrepass && imports.HZBCurrent.IsValid();
        const bool clusterGridBuildActive = features.EnableClusterGridBuild && features.EnableDepthPrepass &&
                                            imports.ClusterGridAABBs.IsValid();
        const bool clusterLightAssignmentActive =
            features.EnableClusterLightAssignment && clusterGridBuildActive &&
            imports.ClusterLightHeaders.IsValid() && imports.ClusterLightIndices.IsValid() &&
            imports.ClusterLightCounter.IsValid();
        const bool reconstructionActive =
            IsReconstructionAAMode(aaOptions.Mode) && !temporalOptions.NoJitterNoHistory;
        const bool smaaActive = aaOptions.Mode == FrameRecipeAAMode::SMAA;
        const bool spatialAAActive = IsSpatialAAMode(aaOptions.Mode);
        const bool motionVectorsActive =
            (temporalOptions.EnableMotionVectors || reconstructionActive) &&
            !temporalOptions.NoJitterNoHistory;
        if (reconstructionActive &&
            (!aaOptions.ReconstructionHistoryPrevious.IsValid() ||
             !aaOptions.ReconstructionHistoryCurrent.IsValid()))
        {
            return FrameRecipeBuildResult{
                .Succeeded = false,
                .MissingPrerequisiteCount = 2u,
                .Diagnostic = "FrameRecipe temporal AA requires valid Reconstruction history imports.",
            };
        }
        const auto width = ClampExtent(sizing.Width);
        const auto height = ClampExtent(sizing.Height);
        const auto inputWidth = ClampExtent(aaOptions.InputWidth == 0u ? sizing.Width : aaOptions.InputWidth);
        const auto inputHeight = ClampExtent(aaOptions.InputHeight == 0u ? sizing.Height : aaOptions.InputHeight);
        const FrameRecipeIntrospection declaration = DescribeDefaultFrameRecipe(features, aaOptions, temporalOptions);

        auto importBackbuffer = [&graph](std::string name,
                                         const RHI::TextureHandle handle,
                                         const FrameRecipeResourceKind kind) {
            const TextureRef ref = graph.ImportBackbuffer(std::move(name), handle);
            (void)graph.SetTextureResourceId(ref, ToFrameResourceId(kind));
            return ref;
        };
        auto importTexture = [&graph](std::string name,
                                      const RHI::TextureHandle handle,
                                      const TextureState initial,
                                      const TextureState finalState,
                                      const FrameRecipeResourceKind kind) {
            const TextureRef ref = graph.ImportTexture(std::move(name), handle, initial, finalState);
            (void)graph.SetTextureResourceId(ref, ToFrameResourceId(kind));
            return ref;
        };
        auto createTexture = [&graph](std::string name,
                                      const RHI::TextureDesc& desc,
                                      const FrameRecipeResourceKind kind) {
            const TextureRef ref = graph.CreateTexture(std::move(name), desc);
            (void)graph.SetTextureResourceId(ref, ToFrameResourceId(kind));
            return ref;
        };
        auto importBuffer = [&graph](std::string name,
                                     const RHI::BufferHandle handle,
                                     const BufferState initial,
                                     const BufferState finalState,
                                     const FrameRecipeResourceKind kind) {
            const BufferRef ref = graph.ImportBuffer(std::move(name), handle, initial, finalState);
            (void)graph.SetBufferResourceId(ref, ToFrameResourceId(kind));
            return ref;
        };
        auto createBuffer = [&graph](std::string name,
                                     const RHI::BufferDesc& desc,
                                     const FrameRecipeResourceKind kind) {
            const BufferRef ref = graph.CreateBuffer(std::move(name), desc);
            (void)graph.SetBufferResourceId(ref, ToFrameResourceId(kind));
            return ref;
        };

        const auto backbuffer = importBackbuffer("Backbuffer", imports.Backbuffer, FrameRecipeResourceKind::Backbuffer);
        const auto sceneTable = importBuffer("GpuWorld.SceneTable", imports.SceneTable, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::SceneTable);
        const auto instanceStatic = importBuffer("GpuWorld.InstanceStatic", imports.InstanceStatic, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::InstanceStatic);
        const auto instanceDynamic = importBuffer("GpuWorld.InstanceDynamic", imports.InstanceDynamic, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::InstanceDynamic);
        const auto entityConfig = importBuffer("GpuWorld.EntityConfig", imports.EntityConfig, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::EntityConfig);
        const auto geometryRecords = importBuffer("GpuWorld.GeometryRecords", imports.GeometryRecords, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::GeometryRecords);
        const auto bounds = importBuffer("GpuWorld.Bounds", imports.Bounds, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::Bounds);
        const auto lights = importBuffer("GpuWorld.Lights", imports.Lights, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::Lights);
        const auto materialBuffer = importBuffer("Material.Buffer", imports.MaterialBuffer, BufferState::ShaderRead, BufferState::ShaderRead, FrameRecipeResourceKind::MaterialBuffer);
        const auto drawIndirect = importBuffer("Cull.SurfaceOpaque.IndexedArgs", imports.SurfaceOpaqueIndexedArgs, BufferState::ShaderWrite, BufferState::IndirectRead, FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs);
        const auto drawCount = importBuffer("Cull.SurfaceOpaque.Count", imports.SurfaceOpaqueCount, BufferState::ShaderWrite, BufferState::IndirectRead, FrameRecipeResourceKind::SurfaceOpaqueCount);
        const auto lineDrawIndirect = importBuffer("Cull.Lines.IndexedArgs", imports.LinesIndexedArgs, BufferState::ShaderWrite, BufferState::IndirectRead, FrameRecipeResourceKind::LinesIndexedArgs);
        const auto lineDrawCount = importBuffer("Cull.Lines.Count", imports.LinesCount, BufferState::ShaderWrite, BufferState::IndirectRead, FrameRecipeResourceKind::LinesCount);
        const auto pointDrawIndirect = importBuffer("Cull.Points.NonIndexedArgs", imports.PointsNonIndexedArgs, BufferState::ShaderWrite, BufferState::IndirectRead, FrameRecipeResourceKind::PointsNonIndexedArgs);
        const auto pointDrawCount = importBuffer("Cull.Points.Count", imports.PointsCount, BufferState::ShaderWrite, BufferState::IndirectRead, FrameRecipeResourceKind::PointsCount);

        const auto depth = createTexture("SceneDepth", DepthTargetDesc(inputWidth, inputHeight, sizing.DepthFormat, "SceneDepth"), FrameRecipeResourceKind::SceneDepth);
        const auto hdr = createTexture("SceneColorHDR", ColorTargetDesc(inputWidth, inputHeight, RHI::Format::RGBA16_FLOAT, "SceneColorHDR"), FrameRecipeResourceKind::SceneColorHDR);
        TextureRef entityId{};
        TextureRef primitiveId{};
        TextureRef sceneNormal{};
        TextureRef albedo{};
        TextureRef material0{};
        TextureRef motionVectors{};
        TextureRef shadowAtlas{};
        TextureRef ldr{};
        TextureRef postProcessBloomScratch{};
        TextureRef postProcessAATempEdges{};
        TextureRef postProcessAATempWeights{};
        TextureRef postProcessAATempResolved{};
        TextureRef reconstructionHistoryPrevious{};
        TextureRef reconstructionHistoryCurrent{};
        TextureRef reconstructionResolvedHDR{};
        TextureRef selectionOutline{};
        TextureRef debugView{};
        BufferRef postProcessHistogram{};
        BufferRef pickingReadback{};
        BufferRef histogramReadback{};
        BufferRef clusterGridAABBs{};
        BufferRef clusterLightHeaders{};
        BufferRef clusterLightIndices{};
        BufferRef clusterLightCounter{};
        TextureRef hzbCurrent{};

        if (hzbBuildActive)
        {
            // Retained HZB images are recreated by the renderer and arrive in
            // VK_IMAGE_LAYOUT_UNDEFINED; import them that way so the graph emits
            // the first ShaderWrite transition before the build pass binds the
            // storage image descriptor.
            hzbCurrent = importTexture("HZB.Current",
                                       imports.HZBCurrent,
                                       TextureState::Undefined,
                                       TextureState::ShaderWrite,
                                       FrameRecipeResourceKind::HZBCurrent);
        }
        if (clusterGridBuildActive)
        {
            clusterGridAABBs = importBuffer("ClusterGrid.AABBs",
                                            imports.ClusterGridAABBs,
                                            BufferState::ShaderWrite,
                                            BufferState::ShaderRead,
                                            FrameRecipeResourceKind::ClusterGridAABBs);
        }
        if (clusterLightAssignmentActive)
        {
            clusterLightHeaders = importBuffer("ClusterLights.Headers",
                                               imports.ClusterLightHeaders,
                                               BufferState::ShaderWrite,
                                               BufferState::ShaderRead,
                                               FrameRecipeResourceKind::ClusterLightHeaders);
            clusterLightIndices = importBuffer("ClusterLights.Indices",
                                               imports.ClusterLightIndices,
                                               BufferState::ShaderWrite,
                                               BufferState::ShaderRead,
                                               FrameRecipeResourceKind::ClusterLightIndices);
            clusterLightCounter = importBuffer("ClusterLights.Counter",
                                               imports.ClusterLightCounter,
                                               BufferState::ShaderWrite,
                                               BufferState::ShaderRead,
                                               FrameRecipeResourceKind::ClusterLightCounter);
        }

        if (pickingActive || features.EnableSelectionOutline)
        {
            entityId = createTexture("EntityId",
                                     ColorTargetDesc(width, height, RHI::Format::R32_UINT, "EntityId"),
                                     FrameRecipeResourceKind::EntityId);
        }
        if (pickingActive)
        {
            primitiveId = createTexture("PrimitiveId",
                                        ColorTargetDesc(width, height, RHI::Format::R32_UINT, "PrimitiveId"),
                                        FrameRecipeResourceKind::PrimitiveId);
            // GRAPHICS-074 Slice D.2 — import the renderer-owned host-visible
            // `Picking.Readback` buffer rather than allocating a transient
            // one (Slice D.1 owns the lease; this slice wires it into the
            // recipe). The `TransferDst` initial state satisfies the
            // framegraph's imported-write contract (`BufferStateAllowsWrite`
            // requires `ShaderWrite`/`TransferDst` on initial or final state
            // when an imported buffer is written), and the `HostReadback`
            // final state leaves the buffer host-readable for Slice D.3's
            // `BeginFrame()` drain. Renderer wiring guarantees
            // `imports.PickingReadback` is valid when `pickingActive` is true
            // (publisher fail-closed otherwise), so we always import here.
            pickingReadback = importBuffer("Picking.Readback",
                                           imports.PickingReadback,
                                           BufferState::TransferDst,
                                           BufferState::HostReadback,
                                           FrameRecipeResourceKind::PickingReadback);
        }
        if (usesDeferred)
        {
            sceneNormal = createTexture("SceneNormal", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "SceneNormal"), FrameRecipeResourceKind::SceneNormal);
            albedo = createTexture("Albedo", ColorTargetDesc(width, height, RHI::Format::RGBA8_UNORM, "Albedo"), FrameRecipeResourceKind::Albedo);
            material0 = createTexture("Material0", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "Material0"), FrameRecipeResourceKind::Material0);
        }
        if (motionVectorsActive)
        {
            motionVectors = createTexture("MotionVectors",
                                          ColorTargetDesc(inputWidth, inputHeight, RHI::Format::RG16_FLOAT, "MotionVectors"),
                                          FrameRecipeResourceKind::MotionVectors);
        }
        if (reconstructionActive)
        {
            reconstructionHistoryPrevious = importTexture("Reconstruction.HistoryPrevious",
                                                          aaOptions.ReconstructionHistoryPrevious,
                                                          TextureState::ShaderRead,
                                                          TextureState::ShaderRead,
                                                          FrameRecipeResourceKind::ReconstructionHistoryPrevious);
            reconstructionHistoryCurrent = importTexture("Reconstruction.HistoryCurrent",
                                                         aaOptions.ReconstructionHistoryCurrent,
                                                         TextureState::ShaderWrite,
                                                         TextureState::ShaderRead,
                                                         FrameRecipeResourceKind::ReconstructionHistoryCurrent);
            reconstructionResolvedHDR = createTexture(
                "Reconstruction.ResolvedHDR",
                StorageTextureDesc(width, height, RHI::Format::RGBA16_FLOAT, "Reconstruction.ResolvedHDR"),
                FrameRecipeResourceKind::ReconstructionResolvedHDR);
        }
        if (features.EnableShadows)
        {
            // GRAPHICS-073 Slice B — prefer the `ShadowSystem`-owned atlas
            // when the caller plumbs a valid handle into
            // `imports.ShadowAtlas`. The imported initial state is `Undefined`
            // and the final state is `DepthWrite` — the same idiom the
            // Backbuffer uses (`Undefined/Present`). Reasoning:
            //
            //  * The render-graph compiler seeds imported texture state from
            //    `InitialState` on *every* frame; cross-frame state has to
            //    match what the prior frame's `FinalState` transition left
            //    the resource in.
            //  * Declaring `InitialState=Undefined` lets the compiler emit a
            //    fresh `Undefined→DepthWrite` barrier at the start of each
            //    `Pass.Shadows` recording. Vulkan treats `Undefined→X`
            //    transitions as "discard contents and transition to X",
            //    which is correct for the shadow atlas (it is overwritten by
            //    the depth-only shadow pipeline at the start of every frame).
            //  * Declaring `FinalState=DepthWrite` satisfies the framegraph
            //    builder's "imported && write" writability contract (one of
            //    `InitialState` / `FinalState` must be a write-capable
            //    state). It also leaves the atlas in `DepthWrite` at frame
            //    end, which keeps the cross-frame loop closed — the next
            //    frame's `Undefined→DepthWrite` transition is a no-op at the
            //    Vulkan level because the real layout already matches.
            //
            // The deferred-lighting shadow-sampler binding (GRAPHICS-072,
            // absorbed from GRAPHICS-073 Slice C) — at `set 1, binding 1`
            // in `deferred_lighting.frag`, i.e. binding 1 of the same
            // global descriptor set as the deferred-path CameraUBO per
            // GRAPHICS-009Q — will read the atlas mid-frame, which routes
            // through the within-frame `DepthWrite→DepthRead` transition
            // emitted by the surface/composition pass; no change to the
            // imported `InitialState/FinalState` is needed for that.
            //
            // When the import is absent (headless contract tests, or
            // ShadowSystem allocation deferred), fall back to the Slice A
            // viewport-sized transient atlas. Slice A's transient sizing is
            // *not* viewport-correct for production shadow mapping, but it
            // keeps the recipe build deterministic without a ShadowSystem.
            if (imports.ShadowAtlas.IsValid())
            {
                shadowAtlas = importTexture("ShadowAtlas",
                                            imports.ShadowAtlas,
                                            TextureState::Undefined,
                                            TextureState::DepthWrite,
                                            FrameRecipeResourceKind::ShadowAtlas);
            }
            else
            {
                const std::uint32_t atlasWidth = (shadowSizing.AtlasResolution > 0u && shadowSizing.CascadeCount > 0u)
                                                     ? shadowSizing.AtlasResolution * shadowSizing.CascadeCount
                                                     : width;
                const std::uint32_t atlasHeight = (shadowSizing.AtlasResolution > 0u)
                                                      ? shadowSizing.AtlasResolution
                                                      : height;
                shadowAtlas = createTexture("ShadowAtlas",
                                            DepthTargetDesc(atlasWidth, atlasHeight, RHI::Format::D32_FLOAT, "ShadowAtlas"),
                                            FrameRecipeResourceKind::ShadowAtlas);
            }
        }
        if (features.EnablePostProcess)
        {
            ldr = createTexture("SceneColorLDR",
                                ColorTargetDesc(width, height, sizing.BackbufferFormat, "SceneColorLDR"),
                                FrameRecipeResourceKind::SceneColorLDR);
            // GRAPHICS-075 Slice B.2 — `BloomScratch` is a mip pyramid capped
            // at `kBloomMipChainLevels = 6` per
            // `docs/architecture/rendering-three-pass.md` ("capped at six
            // mips, truncating at extents below 8x8"). The effective depth
            // for a given viewport is clamped via `ComputeBloomMipChainLevels`
            // — Vulkan's `VkImageCreateInfo::mipLevels` rule
            // (`mipLevels <= floor(log2(max(W, H))) + 1`) would otherwise
            // fail texture allocation for tiny / minimised viewports.
            // `PostProcessBloomPass::Execute` consumes the same helper via
            // `SetBloomScratch(handle, mipLevels)` so the recipe-side
            // storage and the pass-side iteration stay in lock-step.
            RHI::TextureDesc bloomScratchDesc = ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "PostProcess.BloomScratch");
            bloomScratchDesc.MipLevels = ComputeBloomMipChainLevels(width, height);
            postProcessBloomScratch = createTexture("PostProcess.BloomScratch",
                                                    bloomScratchDesc,
                                                    FrameRecipeResourceKind::PostProcessBloomScratch);
            // GRAPHICS-075 Slice D.2a — three matched-format AA attachments
            // replace the single `PostProcess.AATemp` transient. The
            // edge / blend / resolve graph passes each declare a single
            // matched-format `Write`, so the framegraph compiler emits
            // correct layout transitions between them and the AA umbrella
            // never aliases two pipelines with incompatible color formats
            // inside a single render-pass scope on Vulkan.
            if (smaaActive)
            {
                postProcessAATempEdges = createTexture(
                    "PostProcess.AATemp.Edges",
                    ColorTargetDesc(width, height, RHI::Format::RG8_UNORM, "PostProcess.AATemp.Edges"),
                    FrameRecipeResourceKind::PostProcessAATempEdges);
                postProcessAATempWeights = createTexture(
                    "PostProcess.AATemp.Weights",
                    ColorTargetDesc(width, height, RHI::Format::RGBA8_UNORM, "PostProcess.AATemp.Weights"),
                    FrameRecipeResourceKind::PostProcessAATempWeights);
            }
            if (spatialAAActive)
            {
                postProcessAATempResolved = createTexture(
                    "PostProcess.AATemp.Resolved",
                    ColorTargetDesc(width, height, sizing.BackbufferFormat, "PostProcess.AATemp.Resolved"),
                    FrameRecipeResourceKind::PostProcessAATempResolved);
            }
            // GRAPHICS-075 Slice E.1 — `TransferDst` is required so the
            // histogram pass body can `vkCmdFillBuffer` the 256 uint32
            // bins to zero before each frame's dispatch (the shader
            // accumulates via `atomicAdd`, so any non-zero contents
            // from transient-allocator reuse would corrupt the
            // distribution and the downstream exposure-adaptation
            // readback Slice E.2 wires). `TransferSrc` stays for the
            // Slice E.2 host-visible readback copy.
            postProcessHistogram = createBuffer("PostProcess.Histogram", RHI::BufferDesc{
                .SizeBytes = 256u * sizeof(std::uint32_t),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .DebugName = "PostProcess.Histogram",
            }, FrameRecipeResourceKind::PostProcessHistogram);
            // GRAPHICS-075 Slice E.2 — import the renderer-owned host-visible
            // `Histogram.Readback` buffer rather than allocating a transient
            // one. The `TransferDst` initial state satisfies the framegraph's
            // imported-write contract (the recipe records
            // `CopyBuffer(PostProcess.Histogram → Histogram.Readback)` after
            // the compute dispatch), and the `HostReadback` final state leaves
            // the buffer host-readable for the next `BeginFrame()`-side drain.
            // The import is conditional on `imports.HistogramReadback` being
            // valid so headless contract tests / non-operational devices that
            // skip the renderer-side allocation still build a valid recipe.
            if (imports.HistogramReadback.IsValid())
            {
                histogramReadback = importBuffer("Histogram.Readback",
                                                 imports.HistogramReadback,
                                                 BufferState::TransferDst,
                                                 BufferState::HostReadback,
                                                 FrameRecipeResourceKind::HistogramReadback);
            }
        }
        if (features.EnableSelectionOutline)
        {
            selectionOutline = createTexture("SelectionOutline",
                                             ColorTargetDesc(width, height, sizing.BackbufferFormat, "SelectionOutline"),
                                             FrameRecipeResourceKind::SelectionOutline);
        }
        if (features.EnableDebugView)
        {
            debugView = createTexture("DebugViewRGBA",
                                      ColorTargetDesc(width, height, RHI::Format::RGBA8_UNORM, "DebugViewRGBA"),
                                      FrameRecipeResourceKind::DebugViewRGBA);
        }

        auto addRecipePass = [&graph](const FrameRecipePassKind kind,
                                      std::string name,
                                      auto setup,
                                      const bool sideEffect = false) {
            PassRef pass = graph.AddPass(std::move(name), [setup](RenderGraphBuilder& builder) mutable {
                setup(builder);
            }, sideEffect);
            (void)graph.SetPassId(pass, ToFramePassId(kind));
            return pass;
        };

        addRecipePass(FrameRecipePassKind::Culling, "CullingPass", [=](RenderGraphBuilder& builder) {
            builder.Read(sceneTable, BufferUsage::ShaderRead);
            builder.Read(instanceStatic, BufferUsage::ShaderRead);
            builder.Read(instanceDynamic, BufferUsage::ShaderRead);
            builder.Read(entityConfig, BufferUsage::ShaderRead);
            builder.Read(geometryRecords, BufferUsage::ShaderRead);
            builder.Read(bounds, BufferUsage::ShaderRead);
            builder.Read(materialBuffer, BufferUsage::ShaderRead);
            builder.Read(lights, BufferUsage::ShaderRead);
            builder.Write(drawIndirect, BufferUsage::ShaderWrite);
            builder.Write(drawCount, BufferUsage::ShaderWrite);
            builder.Write(lineDrawIndirect, BufferUsage::ShaderWrite);
            builder.Write(lineDrawCount, BufferUsage::ShaderWrite);
            builder.Write(pointDrawIndirect, BufferUsage::ShaderWrite);
            builder.Write(pointDrawCount, BufferUsage::ShaderWrite);
        });

        if (features.EnableDepthPrepass)
        {
            addRecipePass(FrameRecipePassKind::DepthPrepass, "DepthPrepass", [=](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Write(depth, TextureUsage::DepthWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .Depth = RHI::DepthAttachment{
                        .Target = RenderPassAttachmentToken(),
                        .Load = RHI::LoadOp::Clear,
                        .Store = RHI::StoreOp::Store,
                    },
                });
            });
        }

        if (hzbBuildActive)
        {
            addRecipePass(FrameRecipePassKind::HZBBuild, "HZBBuildPass", [=](RenderGraphBuilder& builder) {
                builder.Read(depth, TextureUsage::ShaderRead);
                builder.Write(hzbCurrent, TextureUsage::ShaderWrite);
            });
        }

        if (clusterGridBuildActive)
        {
            addRecipePass(FrameRecipePassKind::ClusterGridBuild, "ClusterGridBuildPass", [=](RenderGraphBuilder& builder) {
                // GRAPHICS-039D — prefer async compute for clustered-light
                // compute work; the framegraph/RHI resolver demotes to
                // graphics on single-queue devices.
                builder.SetQueue(RenderQueue::AsyncCompute);
                builder.Write(clusterGridAABBs, BufferUsage::ShaderWrite);
            });
        }

        if (clusterLightAssignmentActive)
        {
            addRecipePass(FrameRecipePassKind::LightClusterAssignment, "LightClusterAssignmentPass", [=](RenderGraphBuilder& builder) {
                // GRAPHICS-039D — same affinity as the cluster-grid build so
                // both passes request async compute and demote together on
                // single-queue devices. Resource reads/writes own ordering.
                builder.SetQueue(RenderQueue::AsyncCompute);
                builder.Read(clusterGridAABBs, BufferUsage::ShaderRead);
                builder.Read(lights, BufferUsage::ShaderRead);
                builder.Write(clusterLightHeaders, BufferUsage::ShaderWrite);
                builder.Write(clusterLightIndices, BufferUsage::ShaderWrite);
                builder.Write(clusterLightCounter, BufferUsage::ShaderWrite);
            });
        }

        // GRAPHICS-074 recipe-side follow-up — picking is ordered after
        // `DepthPrepass` and reads `SceneDepth` as `DepthRead` so the
        // selection-ID pipelines can depth-equal-test against the
        // nearest-surface depth populated by the prepass. The pass and its
        // `PrimitiveId` / `Picking.Readback` resources are gated on
        // `pickingActive` (`EnablePicking && EnableDepthPrepass`): without
        // `DepthPrepass` the picking render pass would lack a depth
        // attachment and the depth-equal pipeline would be render-pass-
        // incompatible. The matching introspection gate in
        // `DescribeDefaultFrameRecipe` keeps the declared and built pass
        // sets aligned.
        if (pickingActive)
        {
            addRecipePass(FrameRecipePassKind::Picking, "PickingPass", [=](RenderGraphBuilder& builder) {
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Read(lineDrawIndirect, BufferUsage::IndirectRead);
                builder.Read(lineDrawCount, BufferUsage::IndirectRead);
                builder.Read(pointDrawIndirect, BufferUsage::IndirectRead);
                builder.Read(pointDrawCount, BufferUsage::IndirectRead);
                builder.Write(entityId, TextureUsage::ColorAttachmentWrite);
                builder.Write(primitiveId, TextureUsage::ColorAttachmentWrite);
                builder.Write(pickingReadback, BufferUsage::TransferDst);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kDefaultClearTwoColorAttachments,
                    .Depth = RHI::DepthAttachment{
                        .Target = RenderPassAttachmentToken(),
                        .Load = RHI::LoadOp::Load,
                        .Store = RHI::StoreOp::Store,
                    },
                });
                builder.SideEffect();
            });
        }

        if (features.EnableShadows)
        {
            addRecipePass(FrameRecipePassKind::Shadow, "ShadowPass", [=](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Write(shadowAtlas, TextureUsage::DepthWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .Depth = RHI::DepthAttachment{
                        .Target = RenderPassAttachmentToken(),
                        .Load = RHI::LoadOp::Clear,
                        .Store = RHI::StoreOp::Store,
                    },
                });
            });
        }

        addRecipePass(FrameRecipePassKind::Surface, "SurfacePass", [=](RenderGraphBuilder& builder) {
            builder.Read(sceneTable, BufferUsage::ShaderRead);
            builder.Read(instanceStatic, BufferUsage::ShaderRead);
            builder.Read(instanceDynamic, BufferUsage::ShaderRead);
            builder.Read(geometryRecords, BufferUsage::ShaderRead);
            builder.Read(materialBuffer, BufferUsage::ShaderRead);
            if (clusterLightAssignmentActive && !usesDeferred)
            {
                builder.Read(clusterLightHeaders, BufferUsage::ShaderRead);
                builder.Read(clusterLightIndices, BufferUsage::ShaderRead);
            }
            builder.Read(drawIndirect, BufferUsage::IndirectRead);
            builder.Read(drawCount, BufferUsage::IndirectRead);
            if (features.EnableDepthPrepass)
            {
                builder.Read(depth, TextureUsage::DepthRead);
            }
            else
            {
                builder.Write(depth, TextureUsage::DepthWrite);
            }
            // GRAPHICS-072 Slice C — the deferred `SurfacePass` is the
            // GBuffer pass; it writes `SceneNormal/Albedo/Material0` and does
            // *not* sample the shadow atlas. Shadow sampling moved entirely
            // to the deferred `CompositionPass` (see below), where the
            // bindless atlas is pushed through `DeferredLightingPushConstants`.
            // Keep the forward path's shadow-atlas read on `SurfacePass` so
            // `Pass.Forward.Surface`'s `sampler2DShadow` path stays valid.
            if (features.EnableShadows && !usesDeferred)
            {
                builder.Read(shadowAtlas, TextureUsage::DepthRead);
            }
            if (usesDeferred)
            {
                builder.Write(sceneNormal, TextureUsage::ColorAttachmentWrite);
                builder.Write(albedo, TextureUsage::ColorAttachmentWrite);
                builder.Write(material0, TextureUsage::ColorAttachmentWrite);
                if (motionVectorsActive)
                {
                    builder.Write(motionVectors, TextureUsage::ColorAttachmentWrite);
                }
            }
            else
            {
                builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
                if (motionVectorsActive)
                {
                    builder.Write(motionVectors, TextureUsage::ColorAttachmentWrite);
                }
            }
            const RHI::DepthAttachment surfaceDepthAttachment{
                .Target = RenderPassAttachmentToken(),
                .Load = features.EnableDepthPrepass ? RHI::LoadOp::Load : RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::Store,
            };
            if (usesDeferred)
            {
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = motionVectorsActive
                        ? std::span<const RHI::ColorAttachment>{kDefaultClearFourColorAttachments}
                        : std::span<const RHI::ColorAttachment>{kDefaultClearThreeColorAttachments},
                    .Depth = surfaceDepthAttachment,
                });
            }
            else
            {
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = motionVectorsActive
                        ? std::span<const RHI::ColorAttachment>{kDefaultClearTwoColorAttachments}
                        : std::span<const RHI::ColorAttachment>{kDefaultClearColorAttachments},
                    .Depth = surfaceDepthAttachment,
                });
            }
        });

        if (usesDeferred)
        {
            addRecipePass(FrameRecipePassKind::Composition, "CompositionPass", [=](RenderGraphBuilder& builder) {
                builder.Read(sceneNormal, TextureUsage::ShaderRead);
                builder.Read(albedo, TextureUsage::ShaderRead);
                builder.Read(material0, TextureUsage::ShaderRead);
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Read(lights, BufferUsage::ShaderRead);
                if (clusterLightAssignmentActive)
                {
                    builder.Read(clusterLightHeaders, BufferUsage::ShaderRead);
                    builder.Read(clusterLightIndices, BufferUsage::ShaderRead);
                }
                if (features.EnableShadows)
                {
                    // GRAPHICS-072 Slice C — the deferred lighting fragment
                    // shader samples the `ShadowSystem`-owned shadow atlas
                    // through the global bindless heap (`set = 0`, indexed
                    // by `DeferredLightingPushConstants::ShadowAtlasBindlessIndex`).
                    // Declaring the read as `ShaderRead` causes the frame-
                    // graph compiler to emit a `DepthAttachment →
                    // ShaderReadOnly` layout transition between `ShadowPass`
                    // (which wrote the atlas as `DepthWrite`) and this pass.
                    // The legacy `set 1, binding 1` `sampler2DShadow` model
                    // from `assets/shaders/deferred_lighting.frag` is not
                    // representable on the engine's bindless-only pipeline
                    // layout; the bindless-index push-constant is the
                    // equivalent wiring. See `src/graphics/renderer/README.md`
                    // ("Slice C: shadow-atlas binding") for the durable rule.
                    builder.Read(shadowAtlas, TextureUsage::ShaderRead);
                }
                builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kDefaultClearColorAttachments,
                });
            });
        }

        addRecipePass(FrameRecipePassKind::Line, "LinePass", [=](RenderGraphBuilder& builder) {
            builder.Read(depth, TextureUsage::DepthRead);
            builder.Read(lineDrawIndirect, BufferUsage::IndirectRead);
            builder.Read(lineDrawCount, BufferUsage::IndirectRead);
            builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
            builder.SetRenderPass(RHI::RenderPassDesc{
                .ColorTargets = kDefaultLoadColorAttachments,
                .Depth = RHI::DepthAttachment{
                    .Target = RenderPassAttachmentToken(),
                    .Load = RHI::LoadOp::Load,
                    .Store = RHI::StoreOp::Store,
                },
            });
        });

        addRecipePass(FrameRecipePassKind::Point, "PointPass", [=](RenderGraphBuilder& builder) {
            builder.Read(depth, TextureUsage::DepthRead);
            builder.Read(pointDrawIndirect, BufferUsage::IndirectRead);
            builder.Read(pointDrawCount, BufferUsage::IndirectRead);
            builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
            builder.SetRenderPass(RHI::RenderPassDesc{
                .ColorTargets = kDefaultLoadColorAttachments,
                .Depth = RHI::DepthAttachment{
                    .Target = RenderPassAttachmentToken(),
                    .Load = RHI::LoadOp::Load,
                    .Store = RHI::StoreOp::Store,
                },
            });
        });

        // GRAPHICS-077 Slice A — scaffold-only `TransientDebugSurfacePass`.
        // The recipe declares the pass after the lit composition / Line /
        // Point passes so transient debug primitives reach the postprocess
        // chain inputs when present. `SetRenderPass(...)` is required so
        // the framegraph compiler emits a real `CompiledRenderPassAttachment`
        // pair (one color + one depth) — without it the executor would
        // record any future bind/draw outside an active render-pass scope,
        // which is invalid Vulkan command-buffer usage and mirrors the same
        // wiring rationale documented on the canonical `Pass.Present`
        // (GRAPHICS-076 Slice A follow-up). The LOAD-store color
        // attachment preserves the lit color from the prior passes; the
        // depth attachment is depth-read-only (Slice B/C variants test
        // against the prepass depth without overwriting it). The depth
        // `StoreOp` is `Store` (not `DontCare`) because downstream
        // passes that still read `SceneDepth` — `SelectionOutlinePass`
        // when `EnableSelectionOutline` is set declares
        // `Read(depth, TextureUsage::DepthRead)` — must see the same
        // depth contents the prepass populated; `DontCare` would let
        // Vulkan discard the depth contents at the end of this pass
        // and produce undefined outline/depth-based results when both
        // transient debug primitives and selection outlining are
        // active in the same frame. Slice A records no commands
        // inside the pass body — the executor branch returns
        // `SkippedUnavailable` because no pipelines exist yet.
        if (features.EnableTransientDebugSurface)
        {
            addRecipePass(FrameRecipePassKind::TransientDebugSurface, "TransientDebugSurfacePass", [=](RenderGraphBuilder& builder) {
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kTransientDebugSurfaceRenderPassColorAttachments,
                    .Depth = RHI::DepthAttachment{
                        .Target = RenderPassAttachmentToken(),
                        .Load = RHI::LoadOp::Load,
                        .Store = RHI::StoreOp::Store,
                    },
                });
            });
        }

        // GRAPHICS-078 Slice A — scaffold-only `VisualizationOverlayPass`.
        // Same shape as `TransientDebugSurfacePass`: declared with a
        // real LOAD-store color attachment + LOAD/Store depth attachment
        // so the framegraph compiler emits a valid render-pass scope
        // before the executor records anything inside. Slice A records
        // no commands inside the pass body — the executor branch
        // returns `SkippedUnavailable` because no pipelines exist yet.
        // Slice B wires the vector-field lane; Slice C wires the
        // isoline lane.
        if (features.EnableVisualizationOverlay)
        {
            addRecipePass(FrameRecipePassKind::VisualizationOverlay, "VisualizationOverlayPass", [=](RenderGraphBuilder& builder) {
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kVisualizationOverlayRenderPassColorAttachments,
                    .Depth = RHI::DepthAttachment{
                        .Target = RenderPassAttachmentToken(),
                        .Load = RHI::LoadOp::Load,
                        .Store = RHI::StoreOp::Store,
                    },
                });
            });
        }

        TextureRef postProcessInput = hdr;
        if (reconstructionActive)
        {
            addRecipePass(FrameRecipePassKind::Reconstruction, "ReconstructionPass", [=](RenderGraphBuilder& builder) {
                builder.SetQueue(RenderQueue::AsyncCompute);
                builder.Read(hdr, TextureUsage::ShaderRead);
                builder.Read(depth, TextureUsage::ShaderRead);
                builder.Read(motionVectors, TextureUsage::ShaderRead);
                builder.Read(reconstructionHistoryPrevious, TextureUsage::ShaderRead);
                builder.Write(reconstructionHistoryCurrent, TextureUsage::ShaderWrite);
                builder.Write(reconstructionResolvedHDR, TextureUsage::ShaderWrite);
            });
            postProcessInput = reconstructionResolvedHDR;
        }

        TextureRef presentSource = hdr;
        if (features.EnablePostProcess)
        {
            // GRAPHICS-075 Slice C — split the postprocess chain into two
            // ordered graph passes so the FXAA/SMAA legs sample the
            // freshly-written `SceneColorLDR` through a proper framegraph
            // read-after-write barrier rather than reading the umbrella
            // pass's own color attachment mid-render-pass. `PostProcessPass`
            // owns Bloom (Slice B) + ToneMap (Slice A). Slice D.2a then
            // splits the AA umbrella into three ordered graph passes so
            // edge / blend / resolve pipelines can target format-
            // incompatible color attachments (`RG8_UNORM` / `RGBA8_UNORM`
            // / backbuffer format). Each AA pass declares a single
            // matched-format `Write`; the framegraph compiler emits the
            // `SceneColorLDR ColorAttachment → ShaderRead` transition
            // between `PostProcessPass` and `PostProcessAAEdgePass`, the
            // `AATemp.Edges ColorAttachment → ShaderRead` transition
            // between edge and blend, and so on. FXAA records under the
            // resolve pass only (its sampled-image read is the freshly-
            // written `SceneColorLDR`); SMAA records under all three.
            // With `features.EnableAntiAliasing` set, `presentSource`
            // flips to `PostProcess.AATemp.Resolved` so the AA-resolved
            // color reaches present; otherwise it stays on
            // `SceneColorLDR` (the AA pass bodies short-circuit to no-op
            // when `PostProcessSettings::AntiAliasing == None`).
            // GRAPHICS-075 Slice E.1 — the histogram compute dispatch
            // lives in its own ordered graph pass before
            // `"PostProcessPass"`. Vulkan rejects `vkCmdDispatch` inside
            // an active render-pass scope, and `"PostProcessPass"` is a
            // render-pass-scope pass (bloom + tonemap write color
            // attachments), so collapsing the histogram dispatch back
            // into the umbrella would re-introduce the same dispatch-
            // inside-render-pass hazard that the AA umbrella split in
            // Slice D.2a guarded against from the other direction
            // (format-incompatible attachments in one render-pass
            // scope). The framegraph compiler emits the
            // `SceneColorHDR ShaderRead` declaration on both this pass
            // and `"PostProcessPass"` so they share a single
            // `SceneColorHDR ColorAttachment → ShaderRead` transition
            // and the dispatch barrier lands before the bloom +
            // tonemap render-pass scope opens.
            addRecipePass(FrameRecipePassKind::PostProcessHistogram, "PostProcessHistogramPass", [=](RenderGraphBuilder& builder) {
                // GRAPHICS-037D Slice D — the histogram dispatch is the
                // default recipe's existing compute-only pass. Prefer the
                // optional async-compute queue here; the framegraph/RHI
                // resolver demotes to graphics on capability-absent hosts.
                builder.SetQueue(RenderQueue::AsyncCompute);
                builder.Read(postProcessInput, TextureUsage::ShaderRead);
                builder.Write(postProcessHistogram, BufferUsage::ShaderWrite);
                // GRAPHICS-075 Slice E.2 — when the renderer owns a valid
                // host-visible `Histogram.Readback` buffer, declare it as a
                // `TransferDst` write so the framegraph compiler authorises
                // the post-dispatch `CopyBuffer` the executor records.
                if (histogramReadback.IsValid())
                {
                    builder.Write(histogramReadback, BufferUsage::TransferDst);
                }
            });
            addRecipePass(FrameRecipePassKind::PostProcess, "PostProcessPass", [=](RenderGraphBuilder& builder) {
                builder.Read(postProcessInput, TextureUsage::ShaderRead);
                builder.Write(ldr, TextureUsage::ColorAttachmentWrite);
                builder.Write(postProcessBloomScratch, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kDefaultClearColorAttachments,
                });
            });
            if (smaaActive)
            {
                addRecipePass(FrameRecipePassKind::PostProcessAAEdge, "PostProcessAAEdgePass", [=](RenderGraphBuilder& builder) {
                    builder.Read(ldr, TextureUsage::ShaderRead);
                    builder.Write(postProcessAATempEdges, TextureUsage::ColorAttachmentWrite);
                    builder.SetRenderPass(RHI::RenderPassDesc{
                        .ColorTargets = kDefaultClearColorAttachments,
                    });
                });
                addRecipePass(FrameRecipePassKind::PostProcessAABlend, "PostProcessAABlendPass", [=](RenderGraphBuilder& builder) {
                    builder.Read(postProcessAATempEdges, TextureUsage::ShaderRead);
                    builder.Write(postProcessAATempWeights, TextureUsage::ColorAttachmentWrite);
                    builder.SetRenderPass(RHI::RenderPassDesc{
                        .ColorTargets = kDefaultClearColorAttachments,
                    });
                });
            }
            if (spatialAAActive)
            {
                addRecipePass(FrameRecipePassKind::PostProcessAAResolve, "PostProcessAAResolvePass", [=](RenderGraphBuilder& builder) {
                    builder.Read(ldr, TextureUsage::ShaderRead);
                    if (smaaActive)
                    {
                        builder.Read(postProcessAATempWeights, TextureUsage::ShaderRead);
                    }
                    builder.Write(postProcessAATempResolved, TextureUsage::ColorAttachmentWrite);
                    builder.SetRenderPass(RHI::RenderPassDesc{
                        .ColorTargets = kDefaultClearColorAttachments,
                    });
                });
            }
            presentSource =
                (features.EnableAntiAliasing && spatialAAActive) ? postProcessAATempResolved : ldr;
        }

        if (features.EnableSelectionOutline)
        {
            const TextureRef input = presentSource;
            addRecipePass(FrameRecipePassKind::SelectionOutline, "SelectionOutlinePass", [=](RenderGraphBuilder& builder) {
                builder.Read(input, TextureUsage::ShaderRead);
                builder.Read(entityId, TextureUsage::ShaderRead);
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Write(selectionOutline, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kDefaultClearColorAttachments,
                    .Depth = RHI::DepthAttachment{
                        .Target = RenderPassAttachmentToken(),
                        .Load = RHI::LoadOp::Load,
                        .Store = RHI::StoreOp::Store,
                    },
                });
            });
            presentSource = selectionOutline;
        }

        if (features.EnableDebugView)
        {
            const TextureRef input = presentSource;
            addRecipePass(FrameRecipePassKind::DebugView, "DebugViewPass", [=](RenderGraphBuilder& builder) {
                builder.Read(input, TextureUsage::ShaderRead);
                builder.Write(debugView, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kDefaultClearColorAttachments,
                });
            });
            presentSource = debugView;
        }

        if (features.EnableImGui)
        {
            const TextureRef input = presentSource;
            addRecipePass(FrameRecipePassKind::ImGui, "ImGuiPass", [=](RenderGraphBuilder& builder) {
                builder.Read(input, TextureUsage::ShaderRead);
                builder.Write(input, TextureUsage::ColorAttachmentWrite);
                builder.SetRenderPass(RHI::RenderPassDesc{
                    .ColorTargets = kDefaultLoadColorAttachments,
                });
                builder.SideEffect();
            });
        }

        // GRAPHICS-076 Slice A follow-up — the canonical default-recipe
        // `Pass.Present` records `BindPipeline + Draw(3, 1, 0, 0)` against
        // the backbuffer, so the framegraph must declare it as a real
        // color-attachment pass. Without `Write(backbuffer,
        // ColorAttachmentWrite)` + `SetRenderPass(...)` the compiler
        // emits zero `CompiledRenderPassAttachment` entries for this
        // pass, `BuildActiveRenderPassDesc` reports `HasAttachments=false`,
        // and the executor issues the present draw outside any render
        // pass. The post-pass `ColorAttachmentWrite -> Present`
        // transition is emitted by the compiler from the imported
        // backbuffer's `FinalState = Present` contract (see
        // `RenderGraph::ImportBackbuffer`), so no `TextureUsage::Present`
        // read is needed on this pass.
        addRecipePass(FrameRecipePassKind::Present, "Present", [=](RenderGraphBuilder& builder) {
            builder.Read(presentSource, TextureUsage::ShaderRead);
            builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
            builder.SetRenderPass(RHI::RenderPassDesc{
                .ColorTargets = kMinimalRenderPassColorAttachments,
            });
            builder.SideEffect();
        }, true);

        std::uint32_t enabledPassCount = 0u;
        std::uint32_t enabledResourceCount = 0u;
        for (const FrameRecipePassDeclaration& pass : declaration.Passes)
        {
            if (pass.Enabled)
            {
                ++enabledPassCount;
            }
        }
        for (const FrameRecipeResourceDeclaration& resource : declaration.Resources)
        {
            if (resource.Enabled)
            {
                ++enabledResourceCount;
            }
        }

        return FrameRecipeBuildResult{
            .Succeeded = true,
            .DeclaredPassCount = enabledPassCount,
            .DeclaredResourceCount = enabledResourceCount,
        };
    }
}
