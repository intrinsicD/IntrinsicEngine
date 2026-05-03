module;

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Graphics.FrameRecipe;

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    export enum class FrameRecipeLightingPath : std::uint8_t
    {
        Forward = 0,
        Deferred,
        Hybrid,
    };

    export enum class FrameRecipePassKind : std::uint8_t
    {
        Culling = 0,
        Picking,
        DepthPrepass,
        Shadow,
        Surface,
        Composition,
        Line,
        Point,
        PostProcess,
        SelectionOutline,
        DebugView,
        ImGui,
        Present,
    };

    export enum class FrameRecipeResourceKind : std::uint8_t
    {
        Backbuffer = 0,
        SceneDepth,
        EntityId,
        PrimitiveId,
        SceneNormal,
        Albedo,
        Material0,
        SceneColorHDR,
        ShadowAtlas,
        SceneColorLDR,
        SelectionOutline,
        DebugViewRGBA,
        SceneTable,
        InstanceStatic,
        InstanceDynamic,
        EntityConfig,
        GeometryRecords,
        Bounds,
        Lights,
        MaterialBuffer,
        SurfaceOpaqueIndexedArgs,
        SurfaceOpaqueCount,
        PickingReadback,
    };

    export struct FrameRecipeFeatures
    {
        FrameRecipeLightingPath LightingPath{FrameRecipeLightingPath::Deferred};
        bool EnableDepthPrepass{true};
        bool EnablePicking{false};
        bool EnableShadows{false};
        bool EnableSelectionOutline{false};
        bool EnableDebugView{false};
        bool EnablePostProcess{true};
        bool EnableImGui{true};
    };

    export struct FrameRecipeSizing
    {
        std::uint32_t Width{1u};
        std::uint32_t Height{1u};
        RHI::Format BackbufferFormat{RHI::Format::RGBA8_UNORM};
        RHI::Format DepthFormat{RHI::Format::D32_FLOAT};
    };

    export struct FrameRecipeImports
    {
        RHI::TextureHandle Backbuffer{};
        RHI::BufferHandle SceneTable{};
        RHI::BufferHandle InstanceStatic{};
        RHI::BufferHandle InstanceDynamic{};
        RHI::BufferHandle EntityConfig{};
        RHI::BufferHandle GeometryRecords{};
        RHI::BufferHandle Bounds{};
        RHI::BufferHandle Lights{};
        RHI::BufferHandle MaterialBuffer{};
        RHI::BufferHandle SurfaceOpaqueIndexedArgs{};
        RHI::BufferHandle SurfaceOpaqueCount{};
    };

    export struct FrameRecipePassDeclaration
    {
        FrameRecipePassKind Kind{FrameRecipePassKind::Culling};
        std::string_view Name{};
        bool Enabled{false};
        bool FinalizesBackbuffer{false};
        std::vector<std::string_view> Reads{};
        std::vector<std::string_view> Writes{};
    };

    export struct FrameRecipeResourceDeclaration
    {
        FrameRecipeResourceKind Kind{FrameRecipeResourceKind::Backbuffer};
        std::string_view Name{};
        bool Enabled{false};
        bool Imported{false};
        bool Backbuffer{false};
        bool Optional{false};
        bool ImportedWriteAllowed{false};
    };

    export struct FrameRecipeIntrospection
    {
        std::vector<FrameRecipePassDeclaration> Passes{};
        std::vector<FrameRecipeResourceDeclaration> Resources{};
    };

    export struct FrameRecipeBuildResult
    {
        bool Succeeded{false};
        std::uint32_t DeclaredPassCount{0u};
        std::uint32_t DeclaredResourceCount{0u};
        std::string Diagnostic{};
    };

    namespace
    {
        [[nodiscard]] constexpr bool UsesDeferredResources(const FrameRecipeFeatures& features)
        {
            return features.LightingPath == FrameRecipeLightingPath::Deferred ||
                   features.LightingPath == FrameRecipeLightingPath::Hybrid;
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
                .Name = name,
                .Enabled = enabled,
                .Imported = imported,
                .Backbuffer = backbuffer,
                .Optional = optional,
                .ImportedWriteAllowed = importedWriteAllowed,
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
    }

    export [[nodiscard]] FrameRecipeFeatures DeriveDefaultFrameRecipeFeatures(const RenderWorld& world)
    {
        FrameRecipeFeatures features{};
        features.EnablePicking = world.HasPendingPick || world.PickRequest.Pending;
        features.EnableShadows = world.Shadows.Enabled && world.Shadows.CascadeCount > 0u;
        features.EnableSelectionOutline = world.Selection.HasHovered || !world.Selection.SelectedStableIds.empty();
        features.EnableDebugView = world.DebugOverlayEnabled || world.DebugPrimitives.HasTransientDebug;
        features.EnablePostProcess = true;
        features.EnableImGui = true;
        return features;
    }

    export [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features)
    {
        const bool usesDeferred = UsesDeferredResources(features);
        FrameRecipeIntrospection out{};

        AddPass(out, FrameRecipePassKind::Culling, "CullingPass", true, false,
                {"GpuWorld.SceneTable", "GpuWorld.InstanceStatic", "GpuWorld.InstanceDynamic", "GpuWorld.EntityConfig", "GpuWorld.GeometryRecords", "GpuWorld.Bounds", "Material.Buffer", "GpuWorld.Lights"},
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"});
        AddPass(out, FrameRecipePassKind::Picking, "PickingPass", features.EnablePicking, false,
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"},
                {"EntityId", "PrimitiveId", "Picking.Readback"});
        AddPass(out, FrameRecipePassKind::DepthPrepass, "DepthPrepass", features.EnableDepthPrepass, false,
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"}, {"SceneDepth"});
        AddPass(out, FrameRecipePassKind::Shadow, "ShadowPass", features.EnableShadows, false,
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"}, {"ShadowAtlas"});
        if (usesDeferred)
        {
            AddPass(out, FrameRecipePassKind::Surface, "SurfacePass", true, false,
                    {"GpuWorld.SceneTable", "GpuWorld.InstanceStatic", "GpuWorld.InstanceDynamic", "GpuWorld.GeometryRecords", "Material.Buffer", "Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "SceneDepth", "ShadowAtlas"},
                    features.EnableDepthPrepass
                        ? std::initializer_list<std::string_view>{"SceneNormal", "Albedo", "Material0"}
                        : std::initializer_list<std::string_view>{"SceneNormal", "Albedo", "Material0", "SceneDepth"});
        }
        else
        {
            AddPass(out, FrameRecipePassKind::Surface, "SurfacePass", true, false,
                    {"GpuWorld.SceneTable", "GpuWorld.InstanceStatic", "GpuWorld.InstanceDynamic", "GpuWorld.GeometryRecords", "Material.Buffer", "Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "SceneDepth", "ShadowAtlas"},
                    {"SceneColorHDR", "SceneDepth"});
        }
        AddPass(out, FrameRecipePassKind::Composition, "CompositionPass", usesDeferred, false,
                {"SceneNormal", "Albedo", "Material0", "SceneDepth", "GpuWorld.Lights", "ShadowAtlas"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::Line, "LinePass", true, false, {"SceneDepth"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::Point, "PointPass", true, false, {"SceneDepth"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::PostProcess, "PostProcessPass", features.EnablePostProcess, false,
                {"SceneColorHDR"}, {"SceneColorLDR"});
        AddPass(out, FrameRecipePassKind::SelectionOutline, "SelectionOutlinePass", features.EnableSelectionOutline, false,
                {"FrameRecipe.PresentSource", "EntityId", "SceneDepth"}, {"SelectionOutline"});
        AddPass(out, FrameRecipePassKind::DebugView, "DebugViewPass", features.EnableDebugView, false,
                {"FrameRecipe.PresentSource"}, {"DebugViewRGBA"});
        AddPass(out, FrameRecipePassKind::ImGui, "ImGuiPass", features.EnableImGui, false, {"FrameRecipe.PresentSource"}, {});
        AddPass(out, FrameRecipePassKind::Present, "Present", true, true, {"FrameRecipe.PresentSource", "Backbuffer"}, {});

        AddResource(out, FrameRecipeResourceKind::Backbuffer, "Backbuffer", true, true, true);
        AddResource(out, FrameRecipeResourceKind::SceneDepth, "SceneDepth", true);
        AddResource(out, FrameRecipeResourceKind::EntityId, "EntityId", features.EnablePicking || features.EnableSelectionOutline, false, false, true);
        AddResource(out, FrameRecipeResourceKind::PrimitiveId, "PrimitiveId", features.EnablePicking, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneNormal, "SceneNormal", usesDeferred, false, false, true);
        AddResource(out, FrameRecipeResourceKind::Albedo, "Albedo", usesDeferred, false, false, true);
        AddResource(out, FrameRecipeResourceKind::Material0, "Material0", usesDeferred, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneColorHDR, "SceneColorHDR", true);
        AddResource(out, FrameRecipeResourceKind::ShadowAtlas, "ShadowAtlas", features.EnableShadows, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneColorLDR, "SceneColorLDR", features.EnablePostProcess, false, false, true);
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
        AddResource(out, FrameRecipeResourceKind::PickingReadback, "Picking.Readback", features.EnablePicking, false, false, true);
        return out;
    }

    export [[nodiscard]] FrameRecipeBuildResult BuildDefaultFrameRecipe(RenderGraph& graph,
                                                                        const FrameRecipeFeatures& features,
                                                                        const FrameRecipeImports& imports,
                                                                        const FrameRecipeSizing& sizing)
    {
        if (!imports.Backbuffer.IsValid())
        {
            return FrameRecipeBuildResult{
                .Succeeded = false,
                .Diagnostic = "FrameRecipe requires a valid imported Backbuffer handle.",
            };
        }

        const bool usesDeferred = UsesDeferredResources(features);
        const auto width = ClampExtent(sizing.Width);
        const auto height = ClampExtent(sizing.Height);
        const FrameRecipeIntrospection declaration = DescribeDefaultFrameRecipe(features);

        const auto backbuffer = graph.ImportBackbuffer("Backbuffer", imports.Backbuffer);
        const auto sceneTable = graph.ImportBuffer("GpuWorld.SceneTable", imports.SceneTable, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto instanceStatic = graph.ImportBuffer("GpuWorld.InstanceStatic", imports.InstanceStatic, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto instanceDynamic = graph.ImportBuffer("GpuWorld.InstanceDynamic", imports.InstanceDynamic, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto entityConfig = graph.ImportBuffer("GpuWorld.EntityConfig", imports.EntityConfig, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto geometryRecords = graph.ImportBuffer("GpuWorld.GeometryRecords", imports.GeometryRecords, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto bounds = graph.ImportBuffer("GpuWorld.Bounds", imports.Bounds, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto lights = graph.ImportBuffer("GpuWorld.Lights", imports.Lights, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto materialBuffer = graph.ImportBuffer("Material.Buffer", imports.MaterialBuffer, BufferState::ShaderRead, BufferState::ShaderRead);
        const auto drawIndirect = graph.ImportBuffer("Cull.SurfaceOpaque.IndexedArgs", imports.SurfaceOpaqueIndexedArgs, BufferState::ShaderWrite, BufferState::IndirectRead);
        const auto drawCount = graph.ImportBuffer("Cull.SurfaceOpaque.Count", imports.SurfaceOpaqueCount, BufferState::ShaderWrite, BufferState::IndirectRead);

        const auto depth = graph.CreateTexture("SceneDepth", DepthTargetDesc(width, height, sizing.DepthFormat, "SceneDepth"));
        const auto hdr = graph.CreateTexture("SceneColorHDR", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "SceneColorHDR"));
        TextureRef entityId{};
        TextureRef primitiveId{};
        TextureRef sceneNormal{};
        TextureRef albedo{};
        TextureRef material0{};
        TextureRef shadowAtlas{};
        TextureRef ldr{};
        TextureRef selectionOutline{};
        TextureRef debugView{};
        BufferRef pickingReadback{};

        if (features.EnablePicking || features.EnableSelectionOutline)
        {
            entityId = graph.CreateTexture("EntityId", ColorTargetDesc(width, height, RHI::Format::R32_UINT, "EntityId"));
        }
        if (features.EnablePicking)
        {
            primitiveId = graph.CreateTexture("PrimitiveId", ColorTargetDesc(width, height, RHI::Format::R32_UINT, "PrimitiveId"));
            pickingReadback = graph.CreateBuffer("Picking.Readback", RHI::BufferDesc{
                .SizeBytes = sizeof(std::uint32_t),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .DebugName = "Picking.Readback",
            });
        }
        if (usesDeferred)
        {
            sceneNormal = graph.CreateTexture("SceneNormal", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "SceneNormal"));
            albedo = graph.CreateTexture("Albedo", ColorTargetDesc(width, height, RHI::Format::RGBA8_UNORM, "Albedo"));
            material0 = graph.CreateTexture("Material0", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "Material0"));
        }
        if (features.EnableShadows)
        {
            shadowAtlas = graph.CreateTexture("ShadowAtlas", DepthTargetDesc(width, height, RHI::Format::D32_FLOAT, "ShadowAtlas"));
        }
        if (features.EnablePostProcess)
        {
            ldr = graph.CreateTexture("SceneColorLDR", ColorTargetDesc(width, height, sizing.BackbufferFormat, "SceneColorLDR"));
        }
        if (features.EnableSelectionOutline)
        {
            selectionOutline = graph.CreateTexture("SelectionOutline", ColorTargetDesc(width, height, sizing.BackbufferFormat, "SelectionOutline"));
        }
        if (features.EnableDebugView)
        {
            debugView = graph.CreateTexture("DebugViewRGBA", ColorTargetDesc(width, height, RHI::Format::RGBA8_UNORM, "DebugViewRGBA"));
        }

        PassRef previous{};
        auto addOrderedPass = [&graph, &previous](std::string name, auto setup, const bool sideEffect = false) {
            const PassRef dependency = previous;
            PassRef pass = graph.AddPass(std::move(name), [dependency, setup](RenderGraphBuilder& builder) mutable {
                if (dependency.IsValid())
                {
                    builder.DependsOn(dependency);
                }
                setup(builder);
            }, sideEffect);
            previous = pass;
            return pass;
        };

        addOrderedPass("CullingPass", [=](RenderGraphBuilder& builder) {
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
        });

        if (features.EnablePicking)
        {
            addOrderedPass("PickingPass", [=](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Write(entityId, TextureUsage::ColorAttachmentWrite);
                builder.Write(primitiveId, TextureUsage::ColorAttachmentWrite);
                builder.Write(pickingReadback, BufferUsage::TransferDst);
                builder.SideEffect();
            });
        }

        if (features.EnableDepthPrepass)
        {
            addOrderedPass("DepthPrepass", [=](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Write(depth, TextureUsage::DepthWrite);
            });
        }

        if (features.EnableShadows)
        {
            addOrderedPass("ShadowPass", [=](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Write(shadowAtlas, TextureUsage::DepthWrite);
            });
        }

        addOrderedPass("SurfacePass", [=](RenderGraphBuilder& builder) {
            builder.Read(sceneTable, BufferUsage::ShaderRead);
            builder.Read(instanceStatic, BufferUsage::ShaderRead);
            builder.Read(instanceDynamic, BufferUsage::ShaderRead);
            builder.Read(geometryRecords, BufferUsage::ShaderRead);
            builder.Read(materialBuffer, BufferUsage::ShaderRead);
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
            if (features.EnableShadows)
            {
                builder.Read(shadowAtlas, TextureUsage::DepthRead);
            }
            if (usesDeferred)
            {
                builder.Write(sceneNormal, TextureUsage::ColorAttachmentWrite);
                builder.Write(albedo, TextureUsage::ColorAttachmentWrite);
                builder.Write(material0, TextureUsage::ColorAttachmentWrite);
            }
            else
            {
                builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
            }
        });

        if (usesDeferred)
        {
            addOrderedPass("CompositionPass", [=](RenderGraphBuilder& builder) {
                builder.Read(sceneNormal, TextureUsage::ShaderRead);
                builder.Read(albedo, TextureUsage::ShaderRead);
                builder.Read(material0, TextureUsage::ShaderRead);
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Read(lights, BufferUsage::ShaderRead);
                if (features.EnableShadows)
                {
                    builder.Read(shadowAtlas, TextureUsage::DepthRead);
                }
                builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
            });
        }

        addOrderedPass("LinePass", [=](RenderGraphBuilder& builder) {
            builder.Read(depth, TextureUsage::DepthRead);
            builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
        });

        addOrderedPass("PointPass", [=](RenderGraphBuilder& builder) {
            builder.Read(depth, TextureUsage::DepthRead);
            builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
        });

        TextureRef presentSource = hdr;
        if (features.EnablePostProcess)
        {
            addOrderedPass("PostProcessPass", [=](RenderGraphBuilder& builder) {
                builder.Read(hdr, TextureUsage::ShaderRead);
                builder.Write(ldr, TextureUsage::ColorAttachmentWrite);
            });
            presentSource = ldr;
        }

        if (features.EnableSelectionOutline)
        {
            const TextureRef input = presentSource;
            addOrderedPass("SelectionOutlinePass", [=](RenderGraphBuilder& builder) {
                builder.Read(input, TextureUsage::ShaderRead);
                builder.Read(entityId, TextureUsage::ShaderRead);
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Write(selectionOutline, TextureUsage::ColorAttachmentWrite);
            });
            presentSource = selectionOutline;
        }

        if (features.EnableDebugView)
        {
            const TextureRef input = presentSource;
            addOrderedPass("DebugViewPass", [=](RenderGraphBuilder& builder) {
                builder.Read(input, TextureUsage::ShaderRead);
                builder.Write(debugView, TextureUsage::ColorAttachmentWrite);
            });
            presentSource = debugView;
        }

        if (features.EnableImGui)
        {
            const TextureRef input = presentSource;
            addOrderedPass("ImGuiPass", [=](RenderGraphBuilder& builder) {
                builder.Read(input, TextureUsage::ShaderRead);
                builder.SideEffect();
            });
        }

        addOrderedPass("Present", [=](RenderGraphBuilder& builder) {
            builder.Read(presentSource, TextureUsage::ShaderRead);
            builder.Read(backbuffer, TextureUsage::Present);
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



