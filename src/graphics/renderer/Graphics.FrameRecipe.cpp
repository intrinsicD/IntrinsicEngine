module;

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Graphics.FrameRecipe;

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
        return features;
    }

    [[nodiscard]] FrameRecipeIntrospection DescribeDefaultFrameRecipe(const FrameRecipeFeatures& features)
    {
        const bool usesDeferred = UsesDeferredResources(features);
        FrameRecipeIntrospection out{};

        AddPass(out, FrameRecipePassKind::Culling, "CullingPass", true, false,
                {"GpuWorld.SceneTable", "GpuWorld.InstanceStatic", "GpuWorld.InstanceDynamic", "GpuWorld.EntityConfig", "GpuWorld.GeometryRecords", "GpuWorld.Bounds", "Material.Buffer", "GpuWorld.Lights"},
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "Cull.Lines.IndexedArgs", "Cull.Lines.Count", "Cull.Points.NonIndexedArgs", "Cull.Points.Count"});
        AddPass(out, FrameRecipePassKind::Picking, "PickingPass", features.EnablePicking, false,
                {"Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count", "Cull.Lines.IndexedArgs", "Cull.Lines.Count", "Cull.Points.NonIndexedArgs", "Cull.Points.Count"},
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
        AddPass(out, FrameRecipePassKind::Line, "LinePass", true, false,
                {"SceneDepth", "Cull.Lines.IndexedArgs", "Cull.Lines.Count"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::Point, "PointPass", true, false,
                {"SceneDepth", "Cull.Points.NonIndexedArgs", "Cull.Points.Count"}, {"SceneColorHDR"});
        AddPass(out, FrameRecipePassKind::PostProcess, "PostProcessPass", features.EnablePostProcess, false,
                {"SceneColorHDR"}, {"PostProcess.BloomScratch", "PostProcess.Histogram", "PostProcess.AATemp", "SceneColorLDR"});
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
        AddResource(out, FrameRecipeResourceKind::PostProcessAATemp, "PostProcess.AATemp", features.EnablePostProcess, false, false, true);
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
        AddResource(out, FrameRecipeResourceKind::PickingReadback, "Picking.Readback", features.EnablePicking, false, false, true);
        return out;
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
        const auto lineDrawIndirect = graph.ImportBuffer("Cull.Lines.IndexedArgs", imports.LinesIndexedArgs, BufferState::ShaderWrite, BufferState::IndirectRead);
        const auto lineDrawCount = graph.ImportBuffer("Cull.Lines.Count", imports.LinesCount, BufferState::ShaderWrite, BufferState::IndirectRead);
        const auto pointDrawIndirect = graph.ImportBuffer("Cull.Points.NonIndexedArgs", imports.PointsNonIndexedArgs, BufferState::ShaderWrite, BufferState::IndirectRead);
        const auto pointDrawCount = graph.ImportBuffer("Cull.Points.Count", imports.PointsCount, BufferState::ShaderWrite, BufferState::IndirectRead);

        const auto depth = graph.CreateTexture("SceneDepth", DepthTargetDesc(width, height, sizing.DepthFormat, "SceneDepth"));
        const auto hdr = graph.CreateTexture("SceneColorHDR", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "SceneColorHDR"));
        TextureRef entityId{};
        TextureRef primitiveId{};
        TextureRef sceneNormal{};
        TextureRef albedo{};
        TextureRef material0{};
        TextureRef shadowAtlas{};
        TextureRef ldr{};
        TextureRef postProcessBloomScratch{};
        TextureRef postProcessAATemp{};
        TextureRef selectionOutline{};
        TextureRef debugView{};
        BufferRef postProcessHistogram{};
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
            // GRAPHICS-073 Slice B — prefer the `ShadowSystem`-owned atlas
            // when the caller plumbs a valid handle into
            // `imports.ShadowAtlas`. The imported initial state is `DepthWrite`
            // (matching the ShadowPass's first use of the resource) and the
            // final state is `ShaderRead` (the atlas is sampled by the
            // surface/composition passes and survives across frames in that
            // layout). The `Imported && Write` builder gate requires either
            // the initial or final state to allow writes — `DepthWrite` is
            // that contract assertion; without it `builder.Write(shadowAtlas,
            // DepthWrite)` would be rejected even though the atlas is the
            // canonical writer.
            //
            // When the import is absent (headless contract tests, or
            // ShadowSystem allocation deferred), fall back to the Slice A
            // viewport-sized transient atlas. Slice A's transient sizing is
            // *not* viewport-correct for production shadow mapping, but it
            // keeps the recipe build deterministic without a ShadowSystem.
            if (imports.ShadowAtlas.IsValid())
            {
                shadowAtlas = graph.ImportTexture("ShadowAtlas",
                                                  imports.ShadowAtlas,
                                                  TextureState::DepthWrite,
                                                  TextureState::ShaderRead);
            }
            else
            {
                const std::uint32_t atlasWidth = (shadowSizing.AtlasResolution > 0u && shadowSizing.CascadeCount > 0u)
                                                     ? shadowSizing.AtlasResolution * shadowSizing.CascadeCount
                                                     : width;
                const std::uint32_t atlasHeight = (shadowSizing.AtlasResolution > 0u)
                                                      ? shadowSizing.AtlasResolution
                                                      : height;
                shadowAtlas = graph.CreateTexture("ShadowAtlas",
                                                  DepthTargetDesc(atlasWidth, atlasHeight, RHI::Format::D32_FLOAT, "ShadowAtlas"));
            }
        }
        if (features.EnablePostProcess)
        {
            ldr = graph.CreateTexture("SceneColorLDR", ColorTargetDesc(width, height, sizing.BackbufferFormat, "SceneColorLDR"));
            postProcessBloomScratch = graph.CreateTexture("PostProcess.BloomScratch", ColorTargetDesc(width, height, RHI::Format::RGBA16_FLOAT, "PostProcess.BloomScratch"));
            postProcessAATemp = graph.CreateTexture("PostProcess.AATemp", ColorTargetDesc(width, height, sizing.BackbufferFormat, "PostProcess.AATemp"));
            postProcessHistogram = graph.CreateBuffer("PostProcess.Histogram", RHI::BufferDesc{
                .SizeBytes = 256u * sizeof(std::uint32_t),
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc,
                .DebugName = "PostProcess.Histogram",
            });
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
            builder.Write(lineDrawIndirect, BufferUsage::ShaderWrite);
            builder.Write(lineDrawCount, BufferUsage::ShaderWrite);
            builder.Write(pointDrawIndirect, BufferUsage::ShaderWrite);
            builder.Write(pointDrawCount, BufferUsage::ShaderWrite);
        });

        if (features.EnablePicking)
        {
            addOrderedPass("PickingPass", [=](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Read(lineDrawIndirect, BufferUsage::IndirectRead);
                builder.Read(lineDrawCount, BufferUsage::IndirectRead);
                builder.Read(pointDrawIndirect, BufferUsage::IndirectRead);
                builder.Read(pointDrawCount, BufferUsage::IndirectRead);
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
            builder.Read(lineDrawIndirect, BufferUsage::IndirectRead);
            builder.Read(lineDrawCount, BufferUsage::IndirectRead);
            builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
        });

        addOrderedPass("PointPass", [=](RenderGraphBuilder& builder) {
            builder.Read(depth, TextureUsage::DepthRead);
            builder.Read(pointDrawIndirect, BufferUsage::IndirectRead);
            builder.Read(pointDrawCount, BufferUsage::IndirectRead);
            builder.Write(hdr, TextureUsage::ColorAttachmentWrite);
        });

        TextureRef presentSource = hdr;
        if (features.EnablePostProcess)
        {
            addOrderedPass("PostProcessPass", [=](RenderGraphBuilder& builder) {
                builder.Read(hdr, TextureUsage::ShaderRead);
                builder.Write(postProcessBloomScratch, TextureUsage::ColorAttachmentWrite);
                builder.Write(postProcessHistogram, BufferUsage::ShaderWrite);
                builder.Write(postProcessAATemp, TextureUsage::ColorAttachmentWrite);
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

    [[nodiscard]] FrameRecipeIntrospection DescribeMinimalDebugSurfaceRecipe()
    {
        FrameRecipeIntrospection out{};

        AddPass(out, FrameRecipePassKind::Surface, kMinimalDebugSurfacePassName, true, false,
                {"GpuWorld.SceneTable", "Material.Buffer",
                 "Cull.SurfaceOpaque.IndexedArgs", "Cull.SurfaceOpaque.Count"},
                {});
        AddPass(out, FrameRecipePassKind::Present, kMinimalDebugPresentPassName, true, true,
                {}, {"Backbuffer"});

        AddResource(out, FrameRecipeResourceKind::Backbuffer, "Backbuffer", true, true, true, false, true);
        AddResource(out, FrameRecipeResourceKind::SceneTable, "GpuWorld.SceneTable", true, true);
        AddResource(out, FrameRecipeResourceKind::MaterialBuffer, "Material.Buffer", true, true);
        AddResource(out, FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs, "Cull.SurfaceOpaque.IndexedArgs", true, true, false, false, true);
        AddResource(out, FrameRecipeResourceKind::SurfaceOpaqueCount, "Cull.SurfaceOpaque.Count", true, true, false, false, true);
        return out;
    }

    [[nodiscard]] FrameRecipeBuildResult BuildMinimalDebugSurfaceRecipe(RenderGraph& graph,
                                                                       const FrameRecipeImports& imports,
                                                                       const FrameRecipeSizing& sizing)
    {
        if (!imports.Backbuffer.IsValid())
        {
            return FrameRecipeBuildResult{
                .Succeeded = false,
                .Diagnostic = "MinimalDebugSurface recipe requires a valid imported Backbuffer handle.",
            };
        }

        (void)sizing;
        const FrameRecipeIntrospection declaration = DescribeMinimalDebugSurfaceRecipe();

        // GRAPHICS-032 Decision 12: missing surface-pass prerequisites
        // (material/pipeline residency, surface-opaque bucket) increment a
        // single counter rather than aborting the build. Pass bodies land in
        // GRAPHICS-032B/C and consume the same counter.
        std::uint32_t missingPrerequisites = 0u;
        if (!imports.MaterialBuffer.IsValid())
        {
            ++missingPrerequisites;
        }
        if (!imports.SurfaceOpaqueIndexedArgs.IsValid() || !imports.SurfaceOpaqueCount.IsValid())
        {
            ++missingPrerequisites;
        }
        if (!imports.SceneTable.IsValid())
        {
            ++missingPrerequisites;
        }

        const auto backbuffer = graph.ImportBackbuffer("Backbuffer", imports.Backbuffer);
        BufferRef sceneTable{};
        BufferRef materialBuffer{};
        BufferRef drawIndirect{};
        BufferRef drawCount{};
        if (imports.SceneTable.IsValid())
        {
            sceneTable = graph.ImportBuffer("GpuWorld.SceneTable", imports.SceneTable,
                                            BufferState::ShaderRead, BufferState::ShaderRead);
        }
        if (imports.MaterialBuffer.IsValid())
        {
            materialBuffer = graph.ImportBuffer("Material.Buffer", imports.MaterialBuffer,
                                                BufferState::ShaderRead, BufferState::ShaderRead);
        }
        if (imports.SurfaceOpaqueIndexedArgs.IsValid())
        {
            drawIndirect = graph.ImportBuffer("Cull.SurfaceOpaque.IndexedArgs", imports.SurfaceOpaqueIndexedArgs,
                                              BufferState::ShaderWrite, BufferState::IndirectRead);
        }
        if (imports.SurfaceOpaqueCount.IsValid())
        {
            drawCount = graph.ImportBuffer("Cull.SurfaceOpaque.Count", imports.SurfaceOpaqueCount,
                                           BufferState::ShaderWrite, BufferState::IndirectRead);
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

        addOrderedPass(std::string{kMinimalDebugSurfacePassName}, [=](RenderGraphBuilder& builder) {
            if (sceneTable.IsValid())
            {
                builder.Read(sceneTable, BufferUsage::ShaderRead);
            }
            if (materialBuffer.IsValid())
            {
                builder.Read(materialBuffer, BufferUsage::ShaderRead);
            }
            if (drawIndirect.IsValid())
            {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
            }
            if (drawCount.IsValid())
            {
                builder.Read(drawCount, BufferUsage::IndirectRead);
            }
        });

        addOrderedPass(std::string{kMinimalDebugPresentPassName}, [=](RenderGraphBuilder& builder) {
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
            .MissingPrerequisiteCount = missingPrerequisites,
        };
    }
}
