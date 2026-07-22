// RUNTIME-095 Slice 3 — opt-in `gpu;vulkan;integration` working-sandbox present
// smoke.
//
// This is the `Operational` leg of the working-sandbox acceptance. Slices 1 and
// 2 proved the CPU/null residency, camera, selection, outline, and UI contracts
// (`Test.RuntimeSandboxAcceptance.cpp`); this slice drives the runtime `Engine`
// for a small bounded number of frames on a Vulkan-capable host with the
// acceptance families (one mesh, one graph, one point cloud) composed into the
// scene and the same app-owned `SandboxEditorController` composition attached
// by `ExtrinsicSandbox`. It asserts that the default recipe reaches the
// canonical `"Present"` pass with no canonical pass falling through the
// `SkippedUnavailable` branch and with the Vulkan fallback counters stable
// across the operational frames.
//
// The fixture self-skips on non-Vulkan hosts (no GLFW, no operational promoted
// Vulkan device), so it is excluded from the default CPU gate by its
// `gpu;vulkan` labels and additionally no-ops where the backend cannot reach
// operational readiness. The acceptance families are authored on top of the
// reference camera so the live loop has a valid frame camera; exact per-family
// residency counts are covered by the Slice 1 CPU acceptance test.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "OperationalCounterStability.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Types;
import Extrinsic.Sandbox;
import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxConfigSections;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Sandbox.ConfigSections;
import Geometry.Properties;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

namespace ECSC = Extrinsic::ECS::Components;
namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace G = Extrinsic::Graphics::Components;
namespace Assets = Extrinsic::Assets;
namespace Config = Extrinsic::Core::Config;
namespace RT = Extrinsic::Runtime;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kTargetFrames = 4u;

[[nodiscard]] RT::SelectionController& Selection(
    Engine& engine)
{
    return *engine.Services()
        .Find<RT::SelectionController>();
}

[[nodiscard]] RT::SceneInteractionModule& Interaction(
    Engine& engine)
{
    return *engine.Services()
        .Find<RT::SceneInteractionModule>();
}

class TempObjFile final
{
public:
    TempObjFile(std::string_view stem, std::string_view contents)
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        Path = std::filesystem::temp_directory_path() /
               (std::string{stem} + "_" + std::to_string(tick) + ".obj");
        std::ofstream out(Path);
        out << contents;
    }

    ~TempObjFile()
    {
        std::error_code ignored;
        std::filesystem::remove(Path, ignored);
    }

    std::filesystem::path Path;
};

// Bounded `engine.Run()` driver so the smoke cannot hang on a misconfigured
// swapchain loop even when the operational Vulkan gate flips green. Mirrors the
// GRAPHICS-076 Slice D default-recipe smoke driver.
class ExitAfterFramesApp final : public IApplication
{
public:
    explicit ExitAfterFramesApp(
        const std::uint32_t targetFrames,
        const bool attachEditor = true) noexcept
        : m_TargetFrames(targetFrames)
        , m_AttachEditor(attachEditor)
    {
    }

    void OnInitialize(Engine& engine) override
    {
        if (m_AttachEditor)
            m_EditorUi.Attach(engine);
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Frames >= m_TargetFrames)
        {
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override
    {
        if (m_AttachEditor)
            m_EditorUi.Detach();
    }

private:
    Extrinsic::Sandbox::Editor::SandboxEditorController m_EditorUi{};
    std::uint32_t m_TargetFrames{1u};
    std::uint32_t m_Frames{0u};
    bool m_AttachEditor{true};
};

// Compose the real Sandbox application before the caller-specific probe.
// Keeping that order is load-bearing for inner applications that select or
// edit ReferenceTriangle in `OnInitialize`.
class SandboxCompositionApp final : public IApplication
{
public:
    explicit SandboxCompositionApp(std::unique_ptr<IApplication> inner) noexcept
        : m_Sandbox(Extrinsic::Sandbox::CreateSandboxApp())
        , m_Inner(std::move(inner))
    {
    }

    void OnInitialize(Engine& engine) override
    {
        if (m_Sandbox)
            m_Sandbox->OnInitialize(engine);
        if (m_Inner)
            m_Inner->OnInitialize(engine);
    }

    void OnSimTick(Engine& engine, double fixedDt) override
    {
        if (m_Sandbox)
            m_Sandbox->OnSimTick(engine, fixedDt);
        if (m_Inner)
            m_Inner->OnSimTick(engine, fixedDt);
    }

    void OnVariableTick(Engine& engine, double alpha, double dt) override
    {
        if (m_Sandbox)
            m_Sandbox->OnVariableTick(engine, alpha, dt);
        if (m_Inner)
            m_Inner->OnVariableTick(engine, alpha, dt);
    }

    void OnShutdown(Engine& engine) override
    {
        if (m_Inner)
            m_Inner->OnShutdown(engine);
        if (m_Sandbox)
            m_Sandbox->OnShutdown(engine);
    }

private:
    std::unique_ptr<IApplication> m_Sandbox{};
    std::unique_ptr<IApplication> m_Inner{};
};

Counters::Snapshot ToCounterSnapshot(
    const Extrinsic::Backends::Vulkan::VulkanOperationalDiagnosticsSnapshot& vk) noexcept
{
    return Counters::Snapshot{
        vk.VulkanFallbackToNullCount,
        vk.VulkanInitFailureCount,
        vk.VulkanValidationErrorCount,
        vk.VulkanOperationalGateFailureCount,
    };
}

// --- Acceptance scene authoring (mirrors Slice 1 `Test.RuntimeSandboxAcceptance.cpp`) ---

void StampCommon(Registry& scene, EntityHandle entity, std::string name, std::uint32_t stableId)
{
    auto& raw = scene.Raw();
    raw.emplace<ECSC::MetaData>(entity, std::move(name));
    raw.emplace<ECSC::Transform::Component>(entity);
    raw.emplace<ECSC::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<ECSC::Selection::SelectableTag>(entity);
    raw.emplace<ECSC::StableId>(entity, ECSC::StableId{stableId, 1u});
}

void SetPositions(Geometry::PropertySet& props, const std::vector<glm::vec3>& positions)
{
    props.Resize(positions.size());
    auto pos = props.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
    pos.Vector() = positions;
}

void SetTexcoords(Geometry::PropertySet& props, const std::vector<glm::vec2>& texcoords)
{
    auto uv = props.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
    uv.Vector() = texcoords;
}

[[nodiscard]] Assets::AssetTexture2DPayload MakeGeneratedUvSmokeAlbedoPayload()
{
    Assets::AssetTexture2DPayload payload{};
    payload.Metadata.Width = 4u;
    payload.Metadata.Height = 4u;
    payload.Metadata.Components =
        Assets::ComponentCountFor(Assets::AssetTexturePixelFormat::Rgba8Unorm);
    payload.Metadata.PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm;
    payload.Metadata.ColorSpace = Assets::AssetTextureColorSpace::SRGB;
    payload.Metadata.SourceKind = Assets::AssetTextureSourceKind::Generated;
    payload.Metadata.SourceFormat = Assets::AssetFileFormat::Unknown;
    payload.Metadata.SourcePath = "generated://graphics-089/generated-uv-albedo";
    payload.Metadata.DebugName = "graphics-089-generated-uv-albedo";

    payload.PixelBytes.reserve(
        static_cast<std::size_t>(payload.Metadata.Width) *
        static_cast<std::size_t>(payload.Metadata.Height) *
        static_cast<std::size_t>(payload.Metadata.Components));
    for (std::uint32_t y = 0u; y < payload.Metadata.Height; ++y)
    {
        for (std::uint32_t x = 0u; x < payload.Metadata.Width; ++x)
        {
            const bool zeroUvTexel = (x == 0u && y == 0u);
            payload.PixelBytes.push_back(zeroUvTexel ? std::byte{0x00} : std::byte{0xF8});
            payload.PixelBytes.push_back(std::byte{0x00});
            payload.PixelBytes.push_back(zeroUvTexel ? std::byte{0x00} : std::byte{0xF8});
            payload.PixelBytes.push_back(std::byte{0xFF});
        }
    }
    return payload;
}

[[nodiscard]] RT::ProgressivePresentationBindings MakeGeneratedAlbedoPresentationBindings(
    const Assets::AssetId generatedAlbedo)
{
    RT::ProgressiveSlotBinding albedo{};
    albedo.Semantic = RT::ProgressiveSlotSemantic::Albedo;
    albedo.SourceKind = RT::ProgressiveSlotSourceKind::GeneratedTextureAsset;
    albedo.GeneratedTexture = generatedAlbedo;
    albedo.GeneratedPolicy =
        RT::ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
    albedo.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::GeneratedTextureAsset;
    albedo.Readiness = RT::ProgressiveReadinessState::Ready;

    return RT::ProgressivePresentationBindings{
        .Shape = RT::ProgressiveEntityShape::MeshLeaf,
        .Lanes = {
            RT::ProgressiveRenderLaneBinding{
                .Lane = RT::ProgressiveRenderLane::Surface,
                .PresentationKey = "graphics089.generated.surface",
            },
        },
        .Presentations = {
            RT::ProgressivePresentationBinding{
                .Key = "graphics089.generated.surface",
                .Kind = RT::ProgressivePresentationKind::SurfaceMaterial,
                .Slots = {albedo},
            },
        },
        .BindingGeneration = 1u,
    };
}

[[nodiscard]] Extrinsic::RHI::SamplerDesc GeneratedUvSmokeSamplerDesc() noexcept
{
    return Extrinsic::RHI::SamplerDesc{
        .MagFilter = Extrinsic::RHI::FilterMode::Nearest,
        .MinFilter = Extrinsic::RHI::FilterMode::Nearest,
        .MipFilter = Extrinsic::RHI::MipmapMode::Nearest,
        .AddressU = Extrinsic::RHI::AddressMode::ClampToEdge,
        .AddressV = Extrinsic::RHI::AddressMode::ClampToEdge,
        .AddressW = Extrinsic::RHI::AddressMode::ClampToEdge,
        .DebugName = "Graphics089.GeneratedAlbedoSampler",
    };
}

class GeneratedUvTextureSmokeApp final : public IApplication
{
public:
    void SetGeneratedTexture(const Assets::AssetId generatedTexture) noexcept
    {
        m_GeneratedTexture = generatedTexture;
    }

    void OnInitialize(Engine&) override {}

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++Frames;

        if (!UploadRequested &&
            m_GeneratedTexture.IsValid() &&
            engine.GetDevice().IsOperational())
        {
            UploadRequested = true;
            RT::AssetModelTextureHandoffOptions uploadOptions{};
            uploadOptions.TextureSamplerDesc = GeneratedUvSmokeSamplerDesc();
            auto upload = RT::RequestTextureAssetUpload(RequiredEngineService<Extrinsic::Assets::AssetService>(engine),
                                                        RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine),
                                                        m_GeneratedTexture,
                                                        uploadOptions);
            if (!upload.has_value())
            {
                UploadError = upload.error();
                engine.RequestExit();
                return;
            }
        }

        if (m_GeneratedTexture.IsValid() &&
            RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine).GetState(m_GeneratedTexture) ==
                Extrinsic::Graphics::GpuAssetState::Ready)
        {
            TextureReadyObserved = true;
            ++FramesAfterTextureReady;
            if (FramesAfterTextureReady >= 3u)
            {
                engine.RequestExit();
                return;
            }
        }

        if (Frames >= kMaxFrames)
        {
            TimedOut = true;
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override {}

    bool UploadRequested{false};
    bool TextureReadyObserved{false};
    bool TimedOut{false};
    std::optional<Extrinsic::Core::ErrorCode> UploadError{};
    std::uint32_t Frames{0u};
    std::uint32_t FramesAfterTextureReady{0u};

private:
    static constexpr std::uint32_t kMaxFrames = 48u;

    Assets::AssetId m_GeneratedTexture{};
};

EntityHandle MakeMesh(Registry& scene,
                      std::string name = "AcceptanceMesh",
                      const std::uint32_t stableId = 101u)
{
    const EntityHandle entity = scene.Create();
    StampCommon(scene, entity, std::move(name), stableId);
    auto& raw = scene.Raw();
    raw.emplace<G::RenderSurface>(entity);

    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
    SetTexcoords(vertices.Properties, {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}});
    raw.emplace<gs::Edges>(entity);
    auto& halfedges = raw.emplace<gs::Halfedges>(entity);
    halfedges.Properties.Resize(6);
    halfedges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeToVertex}, kInvalidIndex).Vector() =
        {1u, 2u, 0u, 0u, 2u, 1u};
    halfedges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeNext}, kInvalidIndex).Vector() =
        {1u, 2u, 0u, 5u, 3u, 4u};
    halfedges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeFace}, kInvalidIndex).Vector() =
        {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex};
    auto& faces = raw.emplace<gs::Faces>(entity);
    faces.Properties.Resize(1);
    faces.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kFaceHalfedge}, kInvalidIndex).Vector() = {0u};
    raw.emplace<gs::HasMeshTopology>(entity);
    return entity;
}

EntityHandle MakeGraph(Registry& scene,
                       std::string name = "AcceptanceGraph",
                       const std::uint32_t stableId = 102u)
{
    const EntityHandle entity = scene.Create();
    StampCommon(scene, entity, std::move(name), stableId);
    auto& raw = scene.Raw();
    raw.emplace<G::RenderEdges>(entity);

    auto& nodes = raw.emplace<gs::Nodes>(entity);
    SetPositions(nodes.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
    auto& edges = raw.emplace<gs::Edges>(entity);
    edges.Properties.Resize(2);
    edges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV0}, kInvalidIndex).Vector() = {0u, 1u};
    edges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV1}, kInvalidIndex).Vector() = {1u, 2u};
    raw.emplace<gs::HasGraphTopology>(entity);
    return entity;
}

EntityHandle MakePointCloud(Registry& scene)
{
    const EntityHandle entity = scene.Create();
    StampCommon(scene, entity, "AcceptanceCloud", 103u);
    auto& raw = scene.Raw();
    raw.emplace<G::RenderPoints>(entity); // default uniform float SizeSource.

    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}});
    return entity;
}

// Compose one mesh + one graph + one point cloud onto the scene the reference
// config already populated (which owns the active frame camera).
void SeedAcceptanceScene(Registry& scene)
{
    (void)MakeMesh(scene);
    (void)MakeGraph(scene);
    (void)MakePointCloud(scene);
}

[[nodiscard]] RT::ProgressivePresentationBindings MakeProgressiveMeshBindings(
    const bool normalReady)
{
    RT::ProgressiveSlotBinding albedo{};
    albedo.Semantic = RT::ProgressiveSlotSemantic::Albedo;
    albedo.SourceKind = RT::ProgressiveSlotSourceKind::UniformDefault;
    albedo.UniformDefault = RT::ProgressiveDefaultValue{
        .Kind = RT::ProgressivePropertyValueKind::Vec4,
        .Vector = glm::vec4{0.25f, 0.45f, 0.85f, 1.0f},
    };
    albedo.Readiness = RT::ProgressiveReadinessState::DefaultValue;

    RT::ProgressiveSlotBinding normal{};
    normal.Semantic = RT::ProgressiveSlotSemantic::Normal;
    normal.SourceKind = RT::ProgressiveSlotSourceKind::PropertyBake;
    normal.Property = RT::ProgressivePropertyBindingDescriptor{
        .Domain = RT::ProgressiveGeometryDomain::MeshVertex,
        .PropertyName = "v:normal",
        .ExpectedValueKind = RT::ProgressivePropertyValueKind::Vec3,
        .ExpectedElementCount = 3u,
    };
    normal.GeneratedPolicy =
        RT::ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
    normal.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::PropertyBinding;
    normal.Readiness = normalReady
        ? RT::ProgressiveReadinessState::Ready
        : RT::ProgressiveReadinessState::Pending;
    if (normalReady)
        normal.GeneratedTexture = Assets::AssetId{501u, 1u};

    RT::ProgressiveSlotBinding roughness{};
    roughness.Semantic = RT::ProgressiveSlotSemantic::Roughness;
    roughness.SourceKind = RT::ProgressiveSlotSourceKind::AuthoredTextureAsset;
    roughness.Readiness = RT::ProgressiveReadinessState::Unset;
    roughness.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::AuthoredAsset;

    RT::ProgressiveSlotBinding metallic{};
    metallic.Semantic = RT::ProgressiveSlotSemantic::Metallic;
    metallic.SourceKind = RT::ProgressiveSlotSourceKind::PropertyBake;
    metallic.Property = RT::ProgressivePropertyBindingDescriptor{
        .Domain = RT::ProgressiveGeometryDomain::MeshVertex,
        .PropertyName = "v:metallic",
        .ExpectedValueKind = RT::ProgressivePropertyValueKind::ScalarFloat,
        .ExpectedElementCount = 3u,
    };
    metallic.GeneratedTexture = Assets::AssetId{502u, 1u};
    metallic.GeneratedPolicy =
        RT::ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
    metallic.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::PropertyBinding;
    metallic.Readiness = RT::ProgressiveReadinessState::Failed;
    metallic.LastDiagnostic = "previous metallic texture retained after failed bake";

    RT::ProgressiveSlotBinding faceScalar{};
    faceScalar.Semantic = RT::ProgressiveSlotSemantic::ScalarField;
    faceScalar.SourceKind = RT::ProgressiveSlotSourceKind::PropertyBuffer;
    faceScalar.Property = RT::ProgressivePropertyBindingDescriptor{
        .Domain = RT::ProgressiveGeometryDomain::MeshFace,
        .PropertyName = "f:heat",
        .ExpectedValueKind = RT::ProgressivePropertyValueKind::ScalarFloat,
        .ExpectedElementCount = 1u,
    };
    faceScalar.Readiness = RT::ProgressiveReadinessState::Ready;
    faceScalar.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::PropertyBuffer;

    return RT::ProgressivePresentationBindings{
        .Shape = RT::ProgressiveEntityShape::MeshLeaf,
        .Lanes = {
            RT::ProgressiveRenderLaneBinding{
                .Lane = RT::ProgressiveRenderLane::Surface,
                .PresentationKey = "progressive.mesh.surface",
            },
        },
        .Presentations = {
            RT::ProgressivePresentationBinding{
                .Key = "progressive.mesh.surface",
                .Kind = RT::ProgressivePresentationKind::SurfaceMaterial,
                .Slots = {albedo, normal, roughness, metallic, faceScalar},
            },
        },
        .BindingGeneration = normalReady ? 2u : 1u,
    };
}

[[nodiscard]] RT::ProgressivePresentationBindings MakeProgressiveGraphBindings()
{
    RT::ProgressiveSlotBinding edgeColor{};
    edgeColor.Semantic = RT::ProgressiveSlotSemantic::LineColor;
    edgeColor.SourceKind = RT::ProgressiveSlotSourceKind::PropertyBuffer;
    edgeColor.Property = RT::ProgressivePropertyBindingDescriptor{
        .Domain = RT::ProgressiveGeometryDomain::GraphEdge,
        .PropertyName = "e:debug_color",
        .ExpectedValueKind = RT::ProgressivePropertyValueKind::Vec4,
        .ExpectedElementCount = 2u,
    };
    edgeColor.Readiness = RT::ProgressiveReadinessState::Ready;
    edgeColor.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::PropertyBuffer;

    return RT::ProgressivePresentationBindings{
        .Shape = RT::ProgressiveEntityShape::GraphLeaf,
        .Lanes = {
            RT::ProgressiveRenderLaneBinding{
                .Lane = RT::ProgressiveRenderLane::Edges,
                .PresentationKey = "progressive.graph.lines",
            },
        },
        .Presentations = {
            RT::ProgressivePresentationBinding{
                .Key = "progressive.graph.lines",
                .Kind = RT::ProgressivePresentationKind::LinePresentation,
                .Slots = {edgeColor},
            },
        },
        .BindingGeneration = 1u,
    };
}

[[nodiscard]] EntityHandle SeedProgressiveMeshScene(Registry& scene)
{
    const EntityHandle mesh = MakeMesh(scene, "ProgressiveGpuMesh", 201u);
    auto& raw = scene.Raw();
    auto& vertices = raw.get<gs::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f}).Vector() =
        {glm::vec3{0.0f, 0.0f, 1.0f},
         glm::vec3{0.0f, 0.0f, 1.0f},
         glm::vec3{0.0f, 0.0f, 1.0f}};
    vertices.Properties.GetOrAdd<float>("v:metallic", 0.0f).Vector() =
        {0.0f, 0.0f, 0.0f};
    auto& faces = raw.get<gs::Faces>(mesh);
    faces.Properties.GetOrAdd<float>("f:heat", 0.0f).Vector() = {0.5f};
    raw.emplace<RT::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshBindings(false));
    return mesh;
}

[[nodiscard]] EntityHandle SeedProgressiveGraphScene(Registry& scene)
{
    const EntityHandle graph = MakeGraph(scene, "ProgressiveGpuGraph", 202u);
    auto& edges = scene.Raw().get<gs::Edges>(graph);
    edges.Properties.GetOrAdd<glm::vec4>("e:debug_color", glm::vec4{1.0f}).Vector() =
        {glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
         glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};
    scene.Raw().emplace<RT::ProgressivePresentationBindings>(
        graph,
        MakeProgressiveGraphBindings());
    return graph;
}

void MarkProgressiveMeshNormalReady(Registry& scene, const EntityHandle mesh)
{
    auto& bindings =
        scene.Raw().get<RT::ProgressivePresentationBindings>(mesh);
    RT::ProgressivePresentationBinding* presentation =
        RT::FindPresentationBinding(bindings, "progressive.mesh.surface");
    ASSERT_NE(presentation, nullptr);
    RT::ProgressiveSlotBinding* normal =
        RT::FindSlotBinding(*presentation, RT::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    normal->Readiness = RT::ProgressiveReadinessState::Ready;
    normal->GeneratedTexture = Assets::AssetId{501u, 1u};
    ++bindings.BindingGeneration;
}

// --- Pass-status helpers (local to this fixture) ---

[[nodiscard]] bool ContainsPass(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats,
    const std::string_view passName) noexcept
{
    return std::any_of(
        stats.CommandRecords.Passes.begin(),
        stats.CommandRecords.Passes.end(),
        [passName](const auto& pass) { return pass.Name == passName; });
}

[[nodiscard]] RenderCommandPassStatus FindPassStatus(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats,
    const std::string_view passName) noexcept
{
    for (const auto& pass : stats.CommandRecords.Passes)
    {
        if (pass.Name == passName)
        {
            return pass.Status;
        }
    }
    return RenderCommandPassStatus::SkippedNonOperational;
}

[[nodiscard]] std::string BuildPassStatusSummary(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats)
{
    std::string summary;
    for (const auto& pass : stats.CommandRecords.Passes)
    {
        if (!summary.empty())
        {
            summary += ", ";
        }
        summary += pass.Name;
        summary += "=";
        switch (pass.Status)
        {
        case RenderCommandPassStatus::Recorded:
            summary += "Recorded";
            break;
        case RenderCommandPassStatus::SkippedNonOperational:
            summary += "SkippedNonOperational";
            break;
        case RenderCommandPassStatus::SkippedUnavailable:
            summary += "SkippedUnavailable";
            break;
        }
    }
    return summary;
}

struct AcceptanceBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    bool Skipped{false};
    std::string SkipReason;
};

// Bootstrap an operational promoted-Vulkan engine with the sandbox editor shell
// attached, then compose the acceptance families into the already-initialized
// reference scene.
[[nodiscard]] AcceptanceBootstrap BootstrapAcceptanceEngine()
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return AcceptanceBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan sandbox acceptance smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic Working-sandbox gpu;vulkan acceptance smoke";
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    auto enginePtr = std::make_unique<Engine>(
        config, std::make_unique<ExitAfterFramesApp>(kTargetFrames));
    enginePtr->EmplaceModule<Extrinsic::Runtime::CameraModule>();
    enginePtr->EmplaceModule<Extrinsic::Runtime::EditorUiModule>();
    enginePtr->EmplaceModule<
        Extrinsic::Runtime::SceneDocumentModule>();
    enginePtr->EmplaceModule<
        Extrinsic::Runtime::SceneInteractionModule>();
    enginePtr->EmplaceModule<
        Extrinsic::Runtime::AssetWorkflowModule>();
    enginePtr->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return AcceptanceBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    SeedAcceptanceScene(*enginePtr->Worlds().Get(enginePtr->ActiveWorld()));
    return AcceptanceBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

// Reproduce the actual `src/app/Sandbox/main.cpp` config path: keep
// `CreateReferenceEngineConfig()` defaults, including validation and VSync.
[[nodiscard]] AcceptanceBootstrap BootstrapDefaultSandboxAppEngineWithApp(
    std::unique_ptr<IApplication> app)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return AcceptanceBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan sandbox app smoke is opt-in.",
        };
    }

    auto configControl =
        std::make_unique<Extrinsic::Runtime::EngineConfigControl>(
            Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry());
    auto config =
        Extrinsic::Runtime::CreateReferenceEngineConfig(
            configControl->SectionRegistry());
    auto enginePtr = std::make_unique<Engine>(
        config,
        std::make_unique<SandboxCompositionApp>(std::move(app)));
    enginePtr->AddModule(std::move(configControl));
    Extrinsic::Sandbox::RegisterSandboxRuntimeModules(*enginePtr);
    enginePtr->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return AcceptanceBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return AcceptanceBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

[[nodiscard]] AcceptanceBootstrap BootstrapDefaultSandboxAppEngine()
{
    return BootstrapDefaultSandboxAppEngineWithApp(
        std::make_unique<ExitAfterFramesApp>(
            kTargetFrames,
            false));
}

struct AcceptanceRunCapture
{
    Counters::Snapshot Before{};
    Counters::Snapshot After{};
    Extrinsic::Backends::Vulkan::VulkanOperationalStatus Status{};
    Extrinsic::Graphics::RenderGraphFrameStats Stats{};
    bool DeviceOperational{false};
};

[[nodiscard]] AcceptanceRunCapture DriveAcceptanceAndCapture(Engine& engine)
{
    AcceptanceRunCapture capture;
    capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    capture.Status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    capture.DeviceOperational = engine.GetDevice().IsOperational();
    capture.Stats = engine.GetRenderer().GetLastRenderGraphStats();
    capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    return capture;
}

[[nodiscard]] std::uint64_t CountNonBlackRgbPixels(
    Extrinsic::RHI::IDevice& device,
    const Extrinsic::RHI::BufferHandle readbackBuffer,
    const std::uint64_t readbackSize,
    const std::uint32_t bytesPerPixel)
{
    std::vector<std::uint8_t> readbackBytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, readbackBytes.data(), readbackSize, 0u);

    std::uint64_t nonBlack = 0u;
    for (std::uint64_t offset = 0u; offset + 2u < readbackSize; offset += bytesPerPixel)
    {
        if (readbackBytes[static_cast<std::size_t>(offset + 0u)] != 0u ||
            readbackBytes[static_cast<std::size_t>(offset + 1u)] != 0u ||
            readbackBytes[static_cast<std::size_t>(offset + 2u)] != 0u)
        {
            ++nonBlack;
        }
    }
    return nonBlack;
}

struct RgbaPixel
{
    std::uint8_t R{0u};
    std::uint8_t G{0u};
    std::uint8_t B{0u};
    std::uint8_t A{0u};
};

[[nodiscard]] RgbaPixel ReorderToRgba(
    const Extrinsic::RHI::Format format,
    const std::uint8_t b0,
    const std::uint8_t b1,
    const std::uint8_t b2,
    const std::uint8_t b3) noexcept
{
    switch (format)
    {
    case Extrinsic::RHI::Format::BGRA8_UNORM:
    case Extrinsic::RHI::Format::BGRA8_SRGB:
        return RgbaPixel{.R = b2, .G = b1, .B = b0, .A = b3};
    case Extrinsic::RHI::Format::RGBA8_UNORM:
    case Extrinsic::RHI::Format::RGBA8_SRGB:
    default:
        return RgbaPixel{.R = b0, .G = b1, .B = b2, .A = b3};
    }
}

[[nodiscard]] RgbaPixel ReadPixel(
    const std::vector<std::uint8_t>& bytes,
    const Extrinsic::RHI::Format format,
    const std::uint32_t bytesPerPixel,
    const Extrinsic::Core::Extent2D extent,
    const std::uint32_t x,
    const std::uint32_t y)
{
    const std::uint64_t rowStride =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width);
    const std::uint64_t offset =
        static_cast<std::uint64_t>(y) * rowStride +
        static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(bytesPerPixel);
    if (offset + 4u > bytes.size())
    {
        return {};
    }
    return ReorderToRgba(format,
                         bytes[static_cast<std::size_t>(offset + 0u)],
                         bytes[static_cast<std::size_t>(offset + 1u)],
                         bytes[static_cast<std::size_t>(offset + 2u)],
                         bytes[static_cast<std::size_t>(offset + 3u)]);
}

[[nodiscard]] int RgbDistance(const RgbaPixel a, const RgbaPixel b) noexcept
{
    const auto absDiff = [](const std::uint8_t lhs, const std::uint8_t rhs) noexcept
    {
        return lhs > rhs
            ? static_cast<int>(lhs - rhs)
            : static_cast<int>(rhs - lhs);
    };
    return absDiff(a.R, b.R) + absDiff(a.G, b.G) + absDiff(a.B, b.B);
}

[[nodiscard]] bool IsDarkOverlayPixel(const RgbaPixel pixel) noexcept
{
    const std::uint8_t minChannel = std::min({pixel.R, pixel.G, pixel.B});
    const std::uint8_t maxChannel = std::max({pixel.R, pixel.G, pixel.B});
    return maxChannel <= 112u && static_cast<std::uint32_t>(maxChannel - minChannel) <= 16u;
}

[[nodiscard]] std::uint32_t CountDarkOverlayPixels(
    const std::vector<std::uint8_t>& bytes,
    const Extrinsic::RHI::Format format,
    const std::uint32_t bytesPerPixel,
    const Extrinsic::Core::Extent2D extent,
    const std::uint32_t centerX,
    const std::uint32_t centerY,
    const std::uint32_t radius)
{
    if (extent.Width == 0u || extent.Height == 0u)
        return 0u;

    const auto minX = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerX) - static_cast<int>(radius)));
    const auto minY = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerY) - static_cast<int>(radius)));
    const auto maxX = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Width) - 1,
                      static_cast<int>(centerX) + static_cast<int>(radius)));
    const auto maxY = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Height) - 1,
                      static_cast<int>(centerY) + static_cast<int>(radius)));

    std::uint32_t count = 0u;
    for (std::uint32_t y = minY; y <= maxY; ++y)
    {
        for (std::uint32_t x = minX; x <= maxX; ++x)
        {
            if (IsDarkOverlayPixel(ReadPixel(bytes, format, bytesPerPixel, extent, x, y)))
                ++count;
        }
    }
    return count;
}

struct GpuSceneInputSummary
{
    std::uint32_t InstanceCapacity{0u};
    std::uint32_t GeometryCapacity{0u};
    std::uint32_t LiveInstanceCount{0u};
    std::uint32_t VisibleInstances{0u};
    std::uint32_t SurfaceInstances{0u};
    std::uint32_t SurfaceInstancesWithColorBuffer{0u};
    std::uint32_t SurfaceInstancesWithSoaChannels{0u};
    std::uint32_t SurfaceInstancesWithInvalidGeometry{0u};
    std::uint32_t LineInstances{0u};
    std::uint32_t PointInstances{0u};
    std::uint32_t LineInstancesWithPositionBuffer{0u};
    std::uint32_t PointInstancesWithPositionBuffer{0u};
    std::uint32_t LineInstancesWithZeroBounds{0u};
    std::uint32_t PointInstancesWithZeroBounds{0u};
    std::uint32_t LineInstancesWithZeroIndexCount{0u};
    std::uint32_t PointInstancesWithZeroVertexCount{0u};
    std::uint32_t LineInstancesWithInvalidGeometry{0u};
    std::uint32_t PointInstancesWithInvalidGeometry{0u};
    std::uint32_t FirstLineSlot{kInvalidIndex};
    std::uint32_t FirstPointSlot{kInvalidIndex};
    Extrinsic::RHI::GpuInstanceStatic FirstLineInstance{};
    Extrinsic::RHI::GpuInstanceStatic FirstPointInstance{};
    Extrinsic::RHI::GpuGeometryRecord FirstLineGeometry{};
    Extrinsic::RHI::GpuGeometryRecord FirstPointGeometry{};
    Extrinsic::RHI::GpuBounds FirstLineBounds{};
    Extrinsic::RHI::GpuBounds FirstPointBounds{};
};

struct GpuInstanceConfigReadback
{
    bool Found{false};
    std::uint32_t Slot{kInvalidIndex};
    Extrinsic::RHI::GpuInstanceStatic Instance{};
    Extrinsic::RHI::GpuEntityConfig Config{};
};

[[nodiscard]] GpuInstanceConfigReadback ReadVisibleInstanceConfigByEntityId(
    Extrinsic::RHI::IDevice& device,
    Extrinsic::Graphics::IRenderer& renderer,
    const std::uint32_t entityId,
    const std::uint32_t requiredRenderFlags)
{
    auto& gpuWorld = renderer.GetGpuWorld();
    const std::uint32_t capacity = gpuWorld.GetInstanceCapacity();
    std::vector<Extrinsic::RHI::GpuInstanceStatic> instances(capacity);

    if (instances.empty() ||
        !gpuWorld.GetInstanceStaticBuffer().IsValid() ||
        !gpuWorld.GetEntityConfigBuffer().IsValid())
    {
        return {};
    }

    device.ReadBuffer(gpuWorld.GetInstanceStaticBuffer(),
                      instances.data(),
                      static_cast<std::uint64_t>(instances.size() * sizeof(instances.front())),
                      0u);

    for (std::uint32_t slot = 0u; slot < instances.size(); ++slot)
    {
        const auto& inst = instances[slot];
        if (inst.EntityID != entityId ||
            (inst.RenderFlags & Extrinsic::RHI::GpuRender_Visible) == 0u ||
            (inst.RenderFlags & requiredRenderFlags) != requiredRenderFlags)
        {
            continue;
        }

        GpuInstanceConfigReadback result{};
        result.Found = true;
        result.Slot = slot;
        result.Instance = inst;
        device.ReadBuffer(gpuWorld.GetEntityConfigBuffer(),
                          &result.Config,
                          sizeof(result.Config),
                          static_cast<std::uint64_t>(inst.ConfigSlot) *
                              sizeof(result.Config));
        return result;
    }

    return {};
}

[[nodiscard]] GpuSceneInputSummary SummarizeGpuSceneInputs(
    Extrinsic::RHI::IDevice& device,
    Extrinsic::Graphics::IRenderer& renderer)
{
    auto& gpuWorld = renderer.GetGpuWorld();
    GpuSceneInputSummary summary{};
    summary.InstanceCapacity = gpuWorld.GetInstanceCapacity();
    summary.GeometryCapacity = gpuWorld.GetGeometryCapacity();
    summary.LiveInstanceCount = gpuWorld.GetLiveInstanceCount();

    std::vector<Extrinsic::RHI::GpuInstanceStatic> instances(summary.InstanceCapacity);
    std::vector<Extrinsic::RHI::GpuBounds> bounds(summary.InstanceCapacity);
    std::vector<Extrinsic::RHI::GpuGeometryRecord> geometries(summary.GeometryCapacity);

    if (!instances.empty() && gpuWorld.GetInstanceStaticBuffer().IsValid())
    {
        device.ReadBuffer(gpuWorld.GetInstanceStaticBuffer(),
                          instances.data(),
                          static_cast<std::uint64_t>(instances.size() * sizeof(instances.front())),
                          0u);
    }
    if (!bounds.empty() && gpuWorld.GetBoundsBuffer().IsValid())
    {
        device.ReadBuffer(gpuWorld.GetBoundsBuffer(),
                          bounds.data(),
                          static_cast<std::uint64_t>(bounds.size() * sizeof(bounds.front())),
                          0u);
    }
    if (!geometries.empty() && gpuWorld.GetGeometryRecordBuffer().IsValid())
    {
        device.ReadBuffer(gpuWorld.GetGeometryRecordBuffer(),
                          geometries.data(),
                          static_cast<std::uint64_t>(geometries.size() * sizeof(geometries.front())),
                          0u);
    }

    for (std::uint32_t slot = 0u; slot < instances.size(); ++slot)
    {
        const auto& inst = instances[slot];
        if ((inst.RenderFlags & Extrinsic::RHI::GpuRender_Visible) == 0u)
            continue;

        ++summary.VisibleInstances;
        const bool hasLine = (inst.RenderFlags & Extrinsic::RHI::GpuRender_Line) != 0u;
        const bool hasPoint = (inst.RenderFlags & Extrinsic::RHI::GpuRender_Point) != 0u;
        const bool hasSurface = (inst.RenderFlags & Extrinsic::RHI::GpuRender_Surface) != 0u;
        const bool validGeometry =
            inst.GeometrySlot != Extrinsic::RHI::GpuInstanceStatic::InvalidGeometrySlot &&
            inst.GeometrySlot < geometries.size();
        const auto& slotBounds = bounds[slot];
        const auto geometry = validGeometry ? geometries[inst.GeometrySlot]
                                            : Extrinsic::RHI::GpuGeometryRecord{};

        if (hasSurface)
        {
            ++summary.SurfaceInstances;
            if (!validGeometry)
            {
                ++summary.SurfaceInstancesWithInvalidGeometry;
            }
            else if (geometry.ColorBufferBDA != 0u)
            {
                ++summary.SurfaceInstancesWithColorBuffer;
            }
            if (validGeometry &&
                geometry.VertexBufferBDA != 0u &&
                geometry.TexcoordBufferBDA != 0u &&
                geometry.NormalBufferBDA != 0u)
            {
                ++summary.SurfaceInstancesWithSoaChannels;
            }
        }

        if (!hasLine && !hasPoint)
            continue;

        if (hasLine)
        {
            ++summary.LineInstances;
            if (!validGeometry)
                ++summary.LineInstancesWithInvalidGeometry;
            if (slotBounds.WorldSphere.w <= 0.0f)
                ++summary.LineInstancesWithZeroBounds;
            if (validGeometry && geometry.LineIndexCount == 0u)
                ++summary.LineInstancesWithZeroIndexCount;
            if (validGeometry && geometry.VertexBufferBDA != 0u)
                ++summary.LineInstancesWithPositionBuffer;
            if (summary.FirstLineSlot == kInvalidIndex)
            {
                summary.FirstLineSlot = slot;
                summary.FirstLineInstance = inst;
                summary.FirstLineGeometry = geometry;
                summary.FirstLineBounds = slotBounds;
            }
        }

        if (hasPoint)
        {
            ++summary.PointInstances;
            if (!validGeometry)
                ++summary.PointInstancesWithInvalidGeometry;
            if (slotBounds.WorldSphere.w <= 0.0f)
                ++summary.PointInstancesWithZeroBounds;
            if (validGeometry && geometry.PointVertexCount == 0u)
                ++summary.PointInstancesWithZeroVertexCount;
            if (validGeometry && geometry.VertexBufferBDA != 0u)
                ++summary.PointInstancesWithPositionBuffer;
            if (summary.FirstPointSlot == kInvalidIndex)
            {
                summary.FirstPointSlot = slot;
                summary.FirstPointInstance = inst;
                summary.FirstPointGeometry = geometry;
                summary.FirstPointBounds = slotBounds;
            }
        }
    }

    return summary;
}

struct PixelNeighborhoodStats
{
    RgbaPixel Min{};
    RgbaPixel Max{};
    RgbaPixel Center{};
};

[[nodiscard]] PixelNeighborhoodStats DescribePixelNeighborhood(
    const std::vector<std::uint8_t>& bytes,
    const Extrinsic::RHI::Format format,
    const std::uint32_t bytesPerPixel,
    const Extrinsic::Core::Extent2D extent,
    const std::uint32_t centerX,
    const std::uint32_t centerY,
    const std::uint32_t radius)
{
    PixelNeighborhoodStats stats{};
    stats.Min = RgbaPixel{.R = 255u, .G = 255u, .B = 255u, .A = 255u};
    stats.Center = ReadPixel(bytes, format, bytesPerPixel, extent, centerX, centerY);
    if (extent.Width == 0u || extent.Height == 0u)
        return stats;

    const auto minX = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerX) - static_cast<int>(radius)));
    const auto minY = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerY) - static_cast<int>(radius)));
    const auto maxX = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Width) - 1,
                      static_cast<int>(centerX) + static_cast<int>(radius)));
    const auto maxY = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Height) - 1,
                      static_cast<int>(centerY) + static_cast<int>(radius)));

    for (std::uint32_t y = minY; y <= maxY; ++y)
    {
        for (std::uint32_t x = minX; x <= maxX; ++x)
        {
            const RgbaPixel pixel = ReadPixel(bytes, format, bytesPerPixel, extent, x, y);
            stats.Min.R = std::min(stats.Min.R, pixel.R);
            stats.Min.G = std::min(stats.Min.G, pixel.G);
            stats.Min.B = std::min(stats.Min.B, pixel.B);
            stats.Min.A = std::min(stats.Min.A, pixel.A);
            stats.Max.R = std::max(stats.Max.R, pixel.R);
            stats.Max.G = std::max(stats.Max.G, pixel.G);
            stats.Max.B = std::max(stats.Max.B, pixel.B);
            stats.Max.A = std::max(stats.Max.A, pixel.A);
        }
    }
    return stats;
}

void SetEntityPosition(Registry& scene, const EntityHandle entity, const glm::vec3 position)
{
    if (entity == Extrinsic::ECS::InvalidEntityHandle || !scene.IsValid(entity))
    {
        return;
    }

    auto& raw = scene.Raw();
    auto* local = raw.try_get<ECSC::Transform::Component>(entity);
    if (local != nullptr)
    {
        local->Position = position;
        raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);
    }

    glm::mat4 world{1.0f};
    if (local != nullptr)
    {
        world = ECSC::Transform::GetMatrix(*local);
    }
    else
    {
        world = glm::translate(glm::mat4{1.0f}, position);
    }
    raw.emplace_or_replace<ECSC::Transform::WorldMatrix>(entity).Matrix = world;
}

[[nodiscard]] EntityHandle FindEntityByName(
    Registry& scene,
    const std::string_view name)
{
    auto view = scene.Raw().view<ECSC::MetaData>();
    EntityHandle found = Extrinsic::ECS::InvalidEntityHandle;
    view.each([&found, name](const EntityHandle entity, const ECSC::MetaData& meta)
    {
        if (meta.EntityName == name)
        {
            found = entity;
        }
    });
    return found;
}

[[nodiscard]] bool IsReferenceTriangleEntityValid(Registry& scene, const EntityHandle triangle)
{
    if (triangle == Extrinsic::ECS::InvalidEntityHandle || !scene.IsValid(triangle))
    {
        return false;
    }
    auto& raw = scene.Raw();
    const gs::ConstSourceView view = gs::BuildConstView(raw, triangle);
    return raw.all_of<ECSC::MetaData,
                      ECSC::Transform::Component,
                      ECSC::Transform::WorldMatrix,
                      ECSC::StableId,
                      ECSC::Selection::SelectableTag,
                      G::RenderSurface,
                      G::VisualizationConfig,
                      gs::Vertices,
                      gs::Edges,
                      gs::Halfedges,
                      gs::Faces>(triangle) &&
           view.Valid() &&
           view.ActiveDomain == gs::Domain::Mesh;
}

[[nodiscard]] std::string BuildReferenceTriangleEntityDiagnostic(Registry& scene, const EntityHandle triangle)
{
    if (triangle == Extrinsic::ECS::InvalidEntityHandle)
    {
        return "handle=InvalidEntityHandle";
    }
    if (!scene.IsValid(triangle))
    {
        return "handle is not valid in registry";
    }

    auto& raw = scene.Raw();
    std::string out;
    const auto append = [&out](const char* name, const bool present)
    {
        if (!out.empty())
            out += ", ";
        out += name;
        out += "=";
        out += present ? "yes" : "no";
    };

    append("MetaData", raw.all_of<ECSC::MetaData>(triangle));
    append("Transform", raw.all_of<ECSC::Transform::Component>(triangle));
    append("WorldMatrix", raw.all_of<ECSC::Transform::WorldMatrix>(triangle));
    append("StableId", raw.all_of<ECSC::StableId>(triangle));
    append("SelectableTag", raw.all_of<ECSC::Selection::SelectableTag>(triangle));
    append("RenderSurface", raw.all_of<G::RenderSurface>(triangle));
    append("VisualizationConfig", raw.all_of<G::VisualizationConfig>(triangle));
    append("Vertices", raw.all_of<gs::Vertices>(triangle));
    append("Edges", raw.all_of<gs::Edges>(triangle));
    append("Halfedges", raw.all_of<gs::Halfedges>(triangle));
    append("Faces", raw.all_of<gs::Faces>(triangle));
    append("HasMeshTopology", raw.all_of<gs::HasMeshTopology>(triangle));

    const gs::ConstSourceView view = gs::BuildConstView(raw, triangle);
    out += ", sourceViewValid=";
    out += view.Valid() ? "yes" : "no";
    out += ", activeDomain=";
    out += std::to_string(static_cast<int>(view.ActiveDomain));
    out += ", vertices=";
    out += std::to_string(view.VerticesAlive());
    out += ", edges=";
    out += std::to_string(view.EdgesAlive());
    out += ", halfedges=";
    out += std::to_string(view.HalfedgesTotal());
    out += ", faces=";
    out += std::to_string(view.FacesAlive());
    return out;
}

struct ParameterizationUvViewRuntimePathState
{
    bool ConfigPreviewUsable{false};
    bool ConfigApplied{false};
    bool ConfigAppliedFromAgentCli{false};
    bool ParameterizationConfigChanged{false};
    bool ActiveConfigMatchesRequest{false};
    bool WindowOpened{false};
    bool ReferenceTriangleSelected{false};
    bool SawGpuReadyUvView{false};
    bool SawImGuiUserTexture{false};
    bool ExitedAfterOperationalPath{false};
    std::uint32_t Frames{0u};
    std::uint64_t ReadyRequestToken{0u};
    std::uint64_t ReadyRecordedPassCount{0u};
    std::string LastUvViewDiagnostic{};
};

// GRAPHICS-122 acceptance driver: reproduce the real app-owned presentation
// composition instead of submitting renderer state from the test. The app
// configures the engine through the agent/CLI config lane, selects the actual
// reference-scene mesh, opens the contributed parameterization window, and
// waits until that window consumes the renderer-owned UV target as an ImGui
// user texture. Pixel content remains covered by Test.UvViewGpuSmoke.cpp.
class ParameterizationUvViewRuntimePathApp final : public IApplication
{
public:
    explicit ParameterizationUvViewRuntimePathApp(
        std::shared_ptr<ParameterizationUvViewRuntimePathState> state) noexcept
        : m_State(std::move(state))
    {
    }

    void OnInitialize(Engine& engine) override
    {
        Config::EngineConfig candidate = engine.GetEngineConfig();
        auto parameterization =
            RT::GetParameterizationConfig(candidate);
        if (!parameterization.has_value())
        {
            return;
        }
        parameterization->View.RenderMode =
            RT::ParameterizationUvRenderMode::GpuShaded;
        parameterization->View.BackgroundMode =
            RT::ParameterizationUvBackgroundMode::Checker;
        parameterization->View.ShowDistortionHeatmap = false;
        RT::SetParameterizationConfig(candidate, *parameterization);

        RT::EngineConfigControl* const configControl =
            engine.Services().Find<RT::EngineConfigControl>();
        if (configControl == nullptr)
        {
            return;
        }
        const Config::EngineConfigLoadResult preview =
            configControl->PreviewEngineConfigControlDocument(
                Config::SerializeEngineConfig(candidate),
                "graphics-122-runtime-path-gpu-smoke.json");
        m_State->ConfigPreviewUsable = Config::IsConfigUsable(preview);
        if (m_State->ConfigPreviewUsable)
        {
            const RT::RuntimeEngineConfigApplyResult apply =
                configControl->ApplyEngineConfigHotSubset(
                    preview,
                    RT::RuntimeConfigControlSource::AgentCli);
            m_State->ConfigApplied = apply.Succeeded();
            m_State->ConfigAppliedFromAgentCli =
                apply.Source == RT::RuntimeConfigControlSource::AgentCli;
            m_State->ParameterizationConfigChanged =
                apply.SectionChanged(RT::kParameterizationConfigSectionName);
            const auto active =
                RT::GetParameterizationConfig(
                    configControl->GetEngineConfigControlState().ActiveConfig);
            m_State->ActiveConfigMatchesRequest =
                active.has_value() &&
                active->View.RenderMode ==
                    RT::ParameterizationUvRenderMode::GpuShaded &&
                active->View.BackgroundMode ==
                    RT::ParameterizationUvBackgroundMode::Checker &&
                !active->View.ShowDistortionHeatmap;
        }

        const EntityHandle triangle =
            FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
        m_State->ReferenceTriangleSelected =
            IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle) &&
            Selection(engine).SetSelectedEntity(
                *engine.Worlds().Get(engine.ActiveWorld()), triangle);

        RT::EditorUiHost* const editorUi =
            engine.Services().Find<RT::EditorUiHost>();
        m_State->WindowOpened =
            editorUi != nullptr &&
            editorUi->Windows().SetOpen(
                "mesh.processing.parameterize_uv",
                true);
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_State->Frames;

        const Extrinsic::Graphics::UvViewOutput output =
            engine.GetRenderer().GetUvViewOutput();
        m_State->LastUvViewDiagnostic = output.Diagnostic;
        if (output.IsGpuReady())
        {
            m_State->SawGpuReadyUvView = true;
            m_State->ReadyRequestToken = output.RequestToken;
            m_State->ReadyRecordedPassCount = output.RecordedPassCount;
        }

        const RT::EditorUiHost* const editorUi =
            engine.Services().Find<RT::EditorUiHost>();
        m_State->SawImGuiUserTexture =
            m_State->SawImGuiUserTexture ||
            (editorUi != nullptr &&
             editorUi->IsOperational() &&
             editorUi->GetDiagnostics().LastFrameUsedUserTexture);

        constexpr std::uint32_t kMaximumFrames = 12u;
        if (m_State->SawGpuReadyUvView && m_State->SawImGuiUserTexture)
        {
            m_State->ExitedAfterOperationalPath = true;
            engine.RequestExit();
        }
        else if (m_State->Frames >= kMaximumFrames)
        {
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override {}

private:
    std::shared_ptr<ParameterizationUvViewRuntimePathState> m_State{};
};
} // namespace

// The working-sandbox acceptance scene (one mesh, one graph, one point cloud)
// reaches the canonical default-recipe present on an operational promoted-Vulkan
// device, with no canonical pass falling through the `SkippedUnavailable`
// branch and the Vulkan fallback counters stable across the bounded run.
TEST(RuntimeSandboxAcceptanceGpuSmoke, AcceptanceSceneReachesOperationalDefaultRecipePresent)
{
    auto bootstrap = BootstrapAcceptanceEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after driving the sandbox acceptance scene: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a RUNTIME-095 Slice 3 regression, not a skip condition.";
        return;
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.DeviceOperational);

    // The canonical default-recipe present must be recorded on the operational
    // Vulkan command stream. Assert presence before status so a missing pass
    // shows as a clear recipe-shape regression rather than a status mismatch.
    ASSERT_TRUE(ContainsPass(run.Stats, "Present"))
        << "Sandbox acceptance scene did not emit a \"Present\" command record; the recipe shape regressed. "
        << "pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << "Canonical \"Present\" pass did not record on the operational Vulkan command stream. "
        << "pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    // No canonical pass fell through the unavailable branch: every pass the
    // recipe declared and the executor entered must have either recorded or
    // been a deliberate non-operational skip, never a soft `SkippedUnavailable`.
    for (const auto& pass : run.Stats.CommandRecords.Passes)
    {
        EXPECT_NE(pass.Status, RenderCommandPassStatus::SkippedUnavailable)
            << "Canonical pass \"" << pass.Name << "\" fell through the SkippedUnavailable branch on an "
            << "operational device. pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
    }

    // Each acceptance family resided on the operational path, asserted per
    // family rather than by a total live count. The reference triangle from
    // CreateReferenceEngineConfig() rides the separate `Procedural` residency
    // lane, so these mesh/graph/point-cloud lane counters exclude it: if any one
    // acceptance family stopped extracting or uploading, its (upload + reuse)
    // total stays 0 and this gate fails -- a total-count check would instead be
    // satisfied by the reference triangle plus the two surviving families. Every
    // resident, non-dirty family increments its own lane's reuse hit on each
    // steady-state extraction (Runtime.RenderExtraction.cpp), so the last frame's
    // stats carry one reuse (or, on the first frame, one upload) per family.
    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.MeshGeometryUploads + ex.MeshGeometryReuseHits, 1u)
        << "Acceptance mesh family did not reside on the operational mesh lane.";
    EXPECT_GE(ex.GraphGeometryUploads + ex.GraphGeometryReuseHits, 1u)
        << "Acceptance graph family did not reside on the operational graph lane.";
    EXPECT_GE(ex.PointCloudGeometryUploads + ex.PointCloudGeometryReuseHits, 1u)
        << "Acceptance point-cloud family did not reside on the operational point-cloud lane.";
    EXPECT_EQ(ex.MeshGeometryFailedPack, 0u) << "Acceptance mesh lane reported a failed pack.";
    EXPECT_EQ(ex.GraphGeometryFailedPack, 0u) << "Acceptance graph lane reported a failed pack.";
    EXPECT_EQ(ex.PointCloudGeometryFailedPack, 0u) << "Acceptance point-cloud lane reported a failed pack.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across operational sandbox acceptance frames: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, ProgressiveRenderDataReachesOperationalFrame)
{
    auto bootstrap = BootstrapAcceptanceEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    Registry& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle mesh = SeedProgressiveMeshScene(scene);
    const EntityHandle graph = SeedProgressiveGraphScene(scene);

    const gs::ConstSourceView initialMeshView =
        gs::BuildConstView(scene.Raw(), mesh);
    const auto& initialMeshBindings =
        scene.Raw().get<RT::ProgressivePresentationBindings>(mesh);
    const RT::ProgressivePresentationExtractionSnapshot initialMesh =
        RT::BuildProgressivePresentationSnapshot(initialMeshView,
                                                initialMeshBindings);
    EXPECT_GE(initialMesh.Stats.DefaultSlotCount, 1u);
    EXPECT_GE(initialMesh.Stats.PendingSlotCount, 1u);
    EXPECT_GE(initialMesh.Stats.FailedSlotCount, 1u);
    EXPECT_GE(initialMesh.Stats.UnsupportedSlotCount, 1u);
    EXPECT_GE(initialMesh.Stats.PreviousOutputRetainedCount, 1u);

    MarkProgressiveMeshNormalReady(scene, mesh);

    const gs::ConstSourceView readyMeshView =
        gs::BuildConstView(scene.Raw(), mesh);
    const auto& readyMeshBindings =
        scene.Raw().get<RT::ProgressivePresentationBindings>(mesh);
    const RT::ProgressivePresentationExtractionSnapshot readyMesh =
        RT::BuildProgressivePresentationSnapshot(readyMeshView,
                                                readyMeshBindings);
    EXPECT_GE(readyMesh.Stats.ReadyTextureSlotCount, 2u);
    EXPECT_EQ(readyMesh.Stats.PendingSlotCount, 0u);
    EXPECT_GE(readyMesh.Stats.UnsupportedSlotCount, 1u);
    EXPECT_GE(readyMesh.Stats.PreviousOutputRetainedCount, 1u);

    const gs::ConstSourceView graphView = gs::BuildConstView(scene.Raw(), graph);
    const auto& graphBindings =
        scene.Raw().get<RT::ProgressivePresentationBindings>(graph);
    const RT::ProgressivePresentationExtractionSnapshot graphSnapshot =
        RT::BuildProgressivePresentationSnapshot(graphView, graphBindings);
    EXPECT_GE(graphSnapshot.Stats.PropertyBufferReadyCount, 1u);

    const auto run = DriveAcceptanceAndCapture(engine);
    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Progressive render-data smoke did not reach operational Vulkan: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.ProgressivePresentationEntityCount, 2u);
    EXPECT_GE(ex.ProgressivePresentationSlotCount,
              readyMesh.Stats.SlotCount + graphSnapshot.Stats.SlotCount);
    EXPECT_GE(ex.ProgressiveDefaultSlotCount, readyMesh.Stats.DefaultSlotCount);
    EXPECT_GE(ex.ProgressiveReadyTextureSlotCount,
              readyMesh.Stats.ReadyTextureSlotCount);
    EXPECT_GE(ex.ProgressivePropertyBufferReadyCount,
              readyMesh.Stats.PropertyBufferReadyCount +
                  graphSnapshot.Stats.PropertyBufferReadyCount);
    EXPECT_GE(ex.ProgressiveUnsupportedSlotCount,
              readyMesh.Stats.UnsupportedSlotCount);
    EXPECT_GE(ex.ProgressivePreviousOutputRetainedCount,
              readyMesh.Stats.PreviousOutputRetainedCount);
    EXPECT_GE(ex.ProgressiveDiagnosticCount,
              readyMesh.Stats.DiagnosticCount);
    EXPECT_GE(ex.ProgressiveMaterialTextureBindingResolveFailureCount, 1u)
        << "Generated progressive texture slots should attempt material binding resolution and fail closed when the smoke uses synthetic AssetIds.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across operational progressive frames: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width == 0u || extent.Height == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.DefaultConfig.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);
    const RT::EditorUiHost* const editorUi =
        engine.Services().Find<RT::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    ASSERT_TRUE(editorUi->IsOperational());
    const RT::EditorUiDiagnostics editorUiDiagnostics =
        editorUi->GetDiagnostics();

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan after bounded frames: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);
    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "ImGuiPass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox default-config readback triplet did not record on any operational frame.";
    EXPECT_GT(editorUiDiagnostics.FramesProduced, 0u);
    EXPECT_GT(editorUiDiagnostics.LastVertexCount, 0u);
    EXPECT_GT(editorUiDiagnostics.LastIndexCount, 0u);
    EXPECT_GT(editorUiDiagnostics.LastCommandCount, 0u);

    const std::uint64_t nonBlackPixels =
        CountNonBlackRgbPixels(device, readbackBuffer, readbackSize, bytesPerPixel);
    const std::uint64_t totalPixels =
        static_cast<std::uint64_t>(extent.Width) * static_cast<std::uint64_t>(extent.Height);
    EXPECT_GT(nonBlackPixels, 0u)
        << "Sandbox default-config frame read back as entirely black despite recorded Present/ImGui passes. "
        << "editor UI frames=" << editorUiDiagnostics.FramesProduced
        << " drawLists=" << editorUiDiagnostics.LastDrawListCount
        << " vertices=" << editorUiDiagnostics.LastVertexCount
        << " indices=" << editorUiDiagnostics.LastIndexCount
        << " commands=" << editorUiDiagnostics.LastCommandCount
        << " display=" << editorUiDiagnostics.DisplayWidth << "x" << editorUiDiagnostics.DisplayHeight
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    // BUG-016: the operational default recipe must present the lit scene-color
    // background (the recipe's blue clear tonemapped and presented), not just a
    // few ImGui overlay pixels. Before the fix the postprocess tonemap sampled
    // its own (black) output via the destabilised bindless bridge slot 0, so the
    // whole backbuffer read back black. Require a substantial majority of pixels
    // to be non-black so a regression that only leaves ImGui (or nothing)
    // visible is caught, not just a single stray non-black texel.
    EXPECT_GT(nonBlackPixels, totalPixels / 2u)
        << "Sandbox default-config frame did not present a full-screen lit background; only "
        << nonBlackPixels << "/" << totalPixels << " pixels were non-black. This indicates the "
        << "postprocess present source regressed to black again (BUG-016).";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke,
     ParameterizationUvViewWindowUsesOperationalGpuTargetThroughRuntimePath)
{
    auto state = std::make_shared<ParameterizationUvViewRuntimePathState>();
    auto bootstrap = BootstrapDefaultSandboxAppEngineWithApp(
        std::make_unique<ParameterizationUvViewRuntimePathApp>(state));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const AcceptanceRunCapture run = DriveAcceptanceAndCapture(engine);
    const Extrinsic::Graphics::UvViewOutput output =
        engine.GetRenderer().GetUvViewOutput();
    const RT::EditorUiHost* const editorUi =
        engine.Services().Find<RT::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    ASSERT_TRUE(editorUi->IsOperational());
    const RT::EditorUiDiagnostics editorUiDiagnostics =
        editorUi->GetDiagnostics();

    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE()
            << "Promoted Vulkan became non-operational while driving the real parameterization UV-view window: status="
            << ToString(run.Status.Code)
            << " reason=" << ToString(run.Status.Reason)
            << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_TRUE(state->ConfigPreviewUsable)
        << "Agent/CLI UV-view config preview was rejected.";
    EXPECT_TRUE(state->ConfigApplied)
        << "Agent/CLI UV-view config did not apply through EngineConfigControl.";
    EXPECT_TRUE(state->ConfigAppliedFromAgentCli)
        << "UV-view config apply lost its AgentCli provenance.";
    EXPECT_TRUE(state->ParameterizationConfigChanged)
        << "EngineConfigControl did not report the parameterization hot subset as changed.";
    EXPECT_TRUE(state->ActiveConfigMatchesRequest)
        << "The active config lane does not match the requested GPU/checker/no-heatmap view.";
    EXPECT_TRUE(state->ReferenceTriangleSelected)
        << "The real ReferenceTriangle was not selected through SelectionController.";
    EXPECT_TRUE(state->WindowOpened)
        << "The contributed mesh.processing.parameterize_uv window did not open.";
    const RT::EngineConfigControl* const configControl =
        engine.Services().Find<RT::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    const RT::RuntimeEngineConfigControlState& configState =
        configControl->GetEngineConfigControlState();
    const auto activeParameterization =
        RT::GetParameterizationConfig(configState.ActiveConfig);
    ASSERT_TRUE(activeParameterization.has_value());
    EXPECT_EQ(activeParameterization->View.RenderMode,
              RT::ParameterizationUvRenderMode::GpuShaded);
    EXPECT_EQ(activeParameterization->View.BackgroundMode,
              RT::ParameterizationUvBackgroundMode::Checker);
    EXPECT_TRUE(configState.HasLastApply);
    EXPECT_EQ(configState.LastApply.Source,
              RT::RuntimeConfigControlSource::AgentCli);

    EXPECT_TRUE(state->SawGpuReadyUvView)
        << "The real runtime/editor path never observed a completed renderer-owned UV target after "
        << state->Frames << " bounded frames. last diagnostic="
        << state->LastUvViewDiagnostic;
    EXPECT_TRUE(state->SawImGuiUserTexture)
        << "The parameterization panel never emitted its ready UV target as an ImGui user texture.";
    EXPECT_TRUE(state->ExitedAfterOperationalPath)
        << "The bounded app timed out before both UV readiness and ImGui consumption were observed.";
    EXPECT_GT(state->ReadyRequestToken, 0u);
    EXPECT_GT(state->ReadyRecordedPassCount, 0u);

    EXPECT_TRUE(output.IsGpuReady()) << output.Diagnostic;
    EXPECT_EQ(output.ActiveBackground,
              Extrinsic::Graphics::UvViewBackgroundMode::Checker);
    EXPECT_TRUE(output.HasCompletedContents);
    EXPECT_GT(output.RequestToken, 0u);
    EXPECT_GT(output.RecordedPassCount, 0u);

    EXPECT_EQ(run.Status.Code,
              Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason,
              Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);
    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(ContainsPass(run.Stats, "UvViewPass"))
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "UvViewPass"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_TRUE(ContainsPass(run.Stats, "ImGuiPass"))
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "ImGuiPass"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);

    EXPECT_TRUE(editorUiDiagnostics.LastFrameUsedUserTexture);
    EXPECT_GT(editorUiDiagnostics.LastVertexCount, 0u);
    EXPECT_GT(editorUiDiagnostics.LastIndexCount, 0u);
    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across the operational UV-view runtime path: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> "
        << run.After.FallbackToNull << ", initFailure "
        << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> "
        << run.After.ValidationError << ", gateFailure "
        << run.Before.OperationalGateFailure << " -> "
        << run.After.OperationalGateFailure;

    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, ExtrinsicSandboxDefaultConfigPresentsReferenceTriangleAtFrameCenter)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ReferenceTriangleCenter.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for triangle readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.CandidateRenderableCount, 1u);
    EXPECT_GE(ex.SubmittedTransformCount, 1u);
    EXPECT_GE(ex.SubmittedVisualizationCount, 1u);
    EXPECT_GE(ex.MeshGeometryUploads + ex.MeshGeometryReuseHits, 1u)
        << "ReferenceTriangle did not remain resident on the mesh extraction lane.";

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "DepthPrepass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox triangle readback triplet did not record on an operational frame.";

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const std::uint32_t centerX = static_cast<std::uint32_t>(extent.Width / 2);
    const std::uint32_t centerY = static_cast<std::uint32_t>(extent.Height / 2);
    const RgbaPixel center =
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent, centerX, centerY);

    const std::array<RgbaPixel, 4> backgroundSamples{{
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  static_cast<std::uint32_t>((extent.Width * 7) / 8),
                  centerY),
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  static_cast<std::uint32_t>((extent.Width * 7) / 8),
                  static_cast<std::uint32_t>(extent.Height / 4)),
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  centerX,
                  static_cast<std::uint32_t>((extent.Height * 7) / 8)),
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  static_cast<std::uint32_t>((extent.Width * 15) / 16),
                  static_cast<std::uint32_t>((extent.Height * 15) / 16)),
    }};

    int nearestBackgroundDistance = RgbDistance(center, backgroundSamples[0]);
    for (const RgbaPixel sample : backgroundSamples)
    {
        nearestBackgroundDistance = std::min(nearestBackgroundDistance, RgbDistance(center, sample));
    }

    EXPECT_GT(nearestBackgroundDistance, 48)
        << "The actual ECS ReferenceTriangle did not contribute a distinguishable center pixel. "
        << "center=(" << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ","
        << static_cast<int>(center.A) << ") "
        << "background samples=("
        << static_cast<int>(backgroundSamples[0].R) << ","
        << static_cast<int>(backgroundSamples[0].G) << ","
        << static_cast<int>(backgroundSamples[0].B) << "),("
        << static_cast<int>(backgroundSamples[1].R) << ","
        << static_cast<int>(backgroundSamples[1].G) << ","
        << static_cast<int>(backgroundSamples[1].B) << "),("
        << static_cast<int>(backgroundSamples[2].R) << ","
        << static_cast<int>(backgroundSamples[2].G) << ","
        << static_cast<int>(backgroundSamples[2].B) << "),("
        << static_cast<int>(backgroundSamples[3].R) << ","
        << static_cast<int>(backgroundSamples[3].G) << ","
        << static_cast<int>(backgroundSamples[3].B) << ") "
        << "extent=" << extent.Width << "x" << extent.Height
        << " backbuffer format=" << static_cast<int>(backbufferFormat)
        << " extraction candidates=" << ex.CandidateRenderableCount
        << " transforms=" << ex.SubmittedTransformCount
        << " visualizations=" << ex.SubmittedVisualizationCount
        << " mesh uploads=" << ex.MeshGeometryUploads
        << " mesh reuse=" << ex.MeshGeometryReuseHits
        << " nearest background distance=" << nearestBackgroundDistance
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, ReferenceTriangleVertexColorStreamShadesDeferredSurface)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    auto& vertices = raw.get<gs::Vertices>(triangle);
    vertices.Properties
        .GetOrAdd<glm::vec4>("v:color", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
        };
    raw.get<G::VisualizationConfig>(triangle).Source =
        G::VisualizationConfig::ColorSource::Material;

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ReferenceTriangleVertexColor.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for vertex-color readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.MeshGeometryUploads + ex.MeshGeometryReuseHits, 1u)
        << "ReferenceTriangle did not remain resident on the mesh extraction lane.";

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "DepthPrepass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Vertex-color readback triplet did not record on an operational frame.";

    const GpuSceneInputSummary gpuScene = SummarizeGpuSceneInputs(device, renderer);
    EXPECT_GE(gpuScene.SurfaceInstances, 1u);
    EXPECT_EQ(gpuScene.SurfaceInstancesWithInvalidGeometry, 0u);
    EXPECT_GE(gpuScene.SurfaceInstancesWithSoaChannels, 1u)
        << "No visible surface instance published position, texcoord, and normal channel BDAs.";
    EXPECT_GE(gpuScene.SurfaceInstancesWithColorBuffer, 1u)
        << "No visible surface instance published a GpuGeometryRecord::ColorBufferBDA.";

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const std::uint32_t centerX = static_cast<std::uint32_t>(extent.Width / 2);
    const std::uint32_t centerY = static_cast<std::uint32_t>(extent.Height / 2);
    const RgbaPixel center =
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent, centerX, centerY);
    const RgbaPixel background = ReadPixel(
        bytes, backbufferFormat, bytesPerPixel, extent,
        centerX,
        static_cast<std::uint32_t>((extent.Height * 7) / 8));
    const RgbaPixel authoredRed{.R = 255u, .G = 0u, .B = 0u, .A = 255u};
    const RgbaPixel materialWhite{.R = 255u, .G = 255u, .B = 255u, .A = 255u};

    const int redDominance =
        static_cast<int>(center.R) -
        static_cast<int>(std::max(center.G, center.B));
    EXPECT_GT(RgbDistance(center, background), 48)
        << "The vertex-colored ReferenceTriangle did not contribute a distinguishable center pixel. "
        << "center=(" << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ") background=("
        << static_cast<int>(background.R) << ","
        << static_cast<int>(background.G) << ","
        << static_cast<int>(background.B) << ")"
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
    EXPECT_GT(redDominance, 48)
        << "The center pixel was not red-dominant after binding v:color. center=("
        << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ") redDominance=" << redDominance
        << " surface instances with color BDA="
        << gpuScene.SurfaceInstancesWithColorBuffer;
    EXPECT_LT(RgbDistance(center, authoredRed), RgbDistance(center, materialWhite))
        << "The center pixel is closer to the white/material fallback than to the authored red vertex color. center=("
        << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ")";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

namespace
{
class PartialVertexColorMutationApp final : public IApplication
{
public:
    PartialVertexColorMutationApp(const std::uint32_t mutateFrame,
                                  const std::uint32_t targetFrames) noexcept
        : m_MutateFrame(mutateFrame)
        , m_TargetFrames(targetFrames)
    {
    }

    void OnInitialize(Engine&) override {}

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Frames == m_MutateFrame)
        {
            Registry& scene = *engine.Worlds().Get(engine.ActiveWorld());
            const EntityHandle triangle = FindEntityByName(scene, "ReferenceTriangle");
            if (IsReferenceTriangleEntityValid(scene, triangle))
            {
                auto& raw = scene.Raw();
                auto& vertices = raw.get<gs::Vertices>(triangle);
                vertices.Properties
                    .GetOrAdd<glm::vec4>("v:color", glm::vec4{1.0f})
                    .Vector() = {
                        glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
                        glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
                        glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
                    };
                Extrinsic::ECS::Components::DirtyTags::MarkVertexColorsDirty(
                    raw,
                    triangle);
                m_Mutated = true;
            }
        }

        if (m_Frames >= m_TargetFrames)
        {
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override {}

    [[nodiscard]] bool Mutated() const noexcept { return m_Mutated; }

private:
    std::uint32_t m_MutateFrame{1u};
    std::uint32_t m_TargetFrames{1u};
    std::uint32_t m_Frames{0u};
    bool m_Mutated{false};
};
} // namespace

TEST(RuntimeSandboxAcceptanceGpuSmoke, VertexColorDirtyChannelPartiallyUploadsAndShadesDeferredSurface)
{
    auto app = std::make_unique<PartialVertexColorMutationApp>(
        /*mutateFrame=*/kTargetFrames,
        /*targetFrames=*/kTargetFrames);
    const PartialVertexColorMutationApp* appView = app.get();
    auto bootstrap = BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    auto& vertices = raw.get<gs::Vertices>(triangle);
    vertices.Properties
        .GetOrAdd<glm::vec4>("v:color", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
        };
    raw.get<G::VisualizationConfig>(triangle).Source =
        G::VisualizationConfig::ColorSource::Material;

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ReferenceTriangleVertexColorPartial.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for partial vertex-color readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    ASSERT_TRUE(appView->Mutated())
        << "Partial vertex-color smoke did not run its final-frame mutation.";

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_EQ(ex.MeshGeometryReuploads, 1u);
    EXPECT_EQ(ex.MeshGeometryPartialUploads, 1u);
    EXPECT_EQ(ex.MeshGeometryReleases, 0u);

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Partial vertex-color readback did not record on an operational frame.";

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const std::uint32_t centerX = static_cast<std::uint32_t>(extent.Width / 2);
    const std::uint32_t centerY = static_cast<std::uint32_t>(extent.Height / 2);
    const RgbaPixel center =
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent, centerX, centerY);
    const RgbaPixel authoredGreen{.R = 0u, .G = 255u, .B = 0u, .A = 255u};
    const RgbaPixel authoredRed{.R = 255u, .G = 0u, .B = 0u, .A = 255u};

    const int greenDominance =
        static_cast<int>(center.G) -
        static_cast<int>(std::max(center.R, center.B));
    EXPECT_GT(greenDominance, 48)
        << "The center pixel was not green-dominant after a DirtyVertexColors partial upload. center=("
        << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ") greenDominance=" << greenDominance;
    EXPECT_LT(RgbDistance(center, authoredGreen), RgbDistance(center, authoredRed))
        << "The final center pixel is closer to the pre-mutation red stream than to the green partial update. center=("
        << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ")";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

// --- BUG-024B: inspector transform edit shifts rendered pixels --------------

namespace
{
// World-space offset applied to the ReferenceTriangle through the promoted
// editor command path. Chosen so the shifted triangle stays well inside the
// default reference camera frustum while clearing the frame center.
constexpr glm::vec3 kBug024Shift{1.5f, 0.0f, 0.0f};
constexpr std::uint32_t kBug024EditFrame = 3u;
constexpr std::uint32_t kBug024TotalFrames = 8u;

// Project a world point on the z=0 plane through the reference camera
// (position (0,0,3), forward -z, fovy 45 deg, Vulkan Y-flip) to pixel
// coordinates, matching the canonical reference seed after the orbit
// controller constructs its viewport-dependent matrices.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> ProjectReferenceCameraPixel(
    const glm::vec3 world,
    const Extrinsic::Core::Extent2D extent) noexcept
{
    const float aspect = static_cast<float>(extent.Width) /
                         static_cast<float>(extent.Height > 0 ? extent.Height : 1);
    const float tanHalfFovY = std::tan(glm::radians(45.0f) * 0.5f);
    const float viewDepth = 3.0f - world.z;
    const float ndcX = world.x / (tanHalfFovY * aspect * viewDepth);
    // The reference projection flips Y for Vulkan clip space, so world +y
    // maps to screen-up (smaller pixel y); ndcY here is already screen-space.
    const float ndcY = -world.y / (tanHalfFovY * viewDepth);
    const auto toPixel = [](const float ndc, const std::uint32_t size) noexcept
    {
        const float clamped = std::clamp(ndc, -0.99f, 0.99f);
        return static_cast<std::uint32_t>((clamped + 1.0f) * 0.5f *
                                          static_cast<float>(size));
    };
    return {toPixel(ndcX, static_cast<std::uint32_t>(extent.Width)),
            toPixel(ndcY, static_cast<std::uint32_t>(extent.Height))};
}

// Model-scene completion focuses the runtime-owned camera on aggregate import
// bounds, so the fixed reference-camera projection above is no longer valid.
// Project through the active controller's actual post-focus view instead.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
ProjectMainCameraPixel(
    Engine& engine,
    const glm::vec3 world,
    const Extrinsic::Core::Extent2D extent) noexcept
{
    if (extent.Width == 0u || extent.Height == 0u)
    {
        return std::nullopt;
    }

    const Extrinsic::Runtime::ICameraController* controller =
        [&engine]()
        {
            auto* registry =
                engine.Services().Find<
                    Extrinsic::Runtime::CameraControllerRegistry>();
            return registry == nullptr
                ? nullptr
                : registry->ResolveOrNull(
                      Extrinsic::Runtime::CameraControllerSlot::Main);
        }();
    if (controller == nullptr)
    {
        return std::nullopt;
    }

    const auto camera = controller->GetView(extent);
    const glm::vec4 clip =
        camera.Projection * camera.View * glm::vec4{world, 1.0f};
    if (!camera.Valid || !std::isfinite(clip.x) || !std::isfinite(clip.y) ||
        !std::isfinite(clip.w) || clip.w <= 0.000001f)
    {
        return std::nullopt;
    }

    const glm::vec2 ndc{clip.x / clip.w, clip.y / clip.w};
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) ||
        ndc.x < -1.0f || ndc.x > 1.0f ||
        ndc.y < -1.0f || ndc.y > 1.0f)
    {
        return std::nullopt;
    }

    const auto toPixel = [](const float value, const std::uint32_t size) noexcept
    {
        const float lastPixel = static_cast<float>(size - 1u);
        return static_cast<std::uint32_t>(
            std::clamp((value + 1.0f) * 0.5f * lastPixel,
                       0.0f,
                       lastPixel));
    };
    return std::pair{
        toPixel(ndc.x, static_cast<std::uint32_t>(extent.Width)),
        toPixel(ndc.y, static_cast<std::uint32_t>(extent.Height)),
    };
}

inline constexpr float kReferenceTriangleLineWidthSmokePx = 12.0f;
inline constexpr std::uint32_t kReferenceTriangleLineWidthSampleRadius = 7u;
inline constexpr std::uint32_t kReferenceTriangleLineWidthMinDarkPixels = 12u;

TEST(RuntimeSandboxAcceptanceGpuSmoke, ReferenceTriangleMeshConfiguredLineWidthAndPointDrawLanesPresent)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    G::RenderEdges edges{};
    edges.WidthSource = kReferenceTriangleLineWidthSmokePx;
    raw.emplace_or_replace<G::RenderEdges>(triangle, edges);
    G::RenderPoints points{};
    raw.emplace_or_replace<G::RenderPoints>(triangle, points);
    G::VisualizationLaneOverrides laneOverrides{};
    G::VisualizationConfig darkEdgeVisualization{};
    darkEdgeVisualization.Source = G::VisualizationConfig::ColorSource::UniformColor;
    darkEdgeVisualization.Color = glm::vec4{0.02f, 0.02f, 0.02f, 1.0f};
    laneOverrides.Edges = darkEdgeVisualization;
    raw.emplace_or_replace<G::VisualizationLaneOverrides>(triangle, std::move(laneOverrides));

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ReferenceTriangleEdgePoint.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for edge/point readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "LinePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "PointPass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox edge/point readback copy did not record on an operational frame.";

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.MeshEdgeViewUploads + ex.MeshEdgeViewReuseHits, 1u)
        << "ReferenceTriangle did not submit the mesh edge sidecar.";
    EXPECT_GE(ex.MeshVertexViewUploads + ex.MeshVertexViewReuseHits, 1u)
        << "ReferenceTriangle did not submit the mesh vertex sidecar.";
    EXPECT_EQ(ex.MeshEdgeViewFailedPack, 0u);
    EXPECT_EQ(ex.MeshVertexViewFailedPack, 0u);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    std::uint32_t lineCullCount = 0u;
    std::uint32_t lineQuadCullCount = 0u;
    std::uint32_t pointCullCount = 0u;
    Extrinsic::RHI::GpuDrawIndexedCommand firstLineCmd{};
    Extrinsic::RHI::GpuDrawCommand firstLineQuadCmd{};
    Extrinsic::RHI::GpuDrawCommand firstPointCmd{};
    const GpuSceneInputSummary sceneInputs = SummarizeGpuSceneInputs(device, renderer);
    Extrinsic::RHI::GpuEntityConfig firstLineConfig{};
    bool firstLineConfigRead = false;
    const Extrinsic::RHI::BufferHandle entityConfigBuffer =
        renderer.GetGpuWorld().GetEntityConfigBuffer();
    if (sceneInputs.FirstLineSlot != kInvalidIndex &&
        entityConfigBuffer.IsValid())
    {
        device.ReadBuffer(entityConfigBuffer,
                          &firstLineConfig,
                          sizeof(firstLineConfig),
                          static_cast<std::uint64_t>(sceneInputs.FirstLineInstance.ConfigSlot) *
                              sizeof(firstLineConfig));
        firstLineConfigRead = true;
    }
    {
        const auto& lineBucket = renderer.GetCullingSystem().GetBucket(
            Extrinsic::RHI::GpuDrawBucketKind::Lines);
        const auto& lineQuadBucket = renderer.GetCullingSystem().GetBucket(
            Extrinsic::RHI::GpuDrawBucketKind::LineQuads);
        const auto& pointBucket = renderer.GetCullingSystem().GetBucket(
            Extrinsic::RHI::GpuDrawBucketKind::Points);
        if (lineBucket.CountBuffer.IsValid())
        {
            device.ReadBuffer(lineBucket.CountBuffer, &lineCullCount, sizeof(lineCullCount), 0u);
        }
        if (lineBucket.IndexedArgsBuffer.IsValid())
        {
            device.ReadBuffer(lineBucket.IndexedArgsBuffer, &firstLineCmd, sizeof(firstLineCmd), 0u);
        }
        if (lineQuadBucket.CountBuffer.IsValid())
        {
            device.ReadBuffer(lineQuadBucket.CountBuffer, &lineQuadCullCount, sizeof(lineQuadCullCount), 0u);
        }
        if (lineQuadBucket.NonIndexedArgsBuffer.IsValid())
        {
            device.ReadBuffer(lineQuadBucket.NonIndexedArgsBuffer,
                              &firstLineQuadCmd,
                              sizeof(firstLineQuadCmd),
                              0u);
        }
        if (pointBucket.CountBuffer.IsValid())
        {
            device.ReadBuffer(pointBucket.CountBuffer, &pointCullCount, sizeof(pointCullCount), 0u);
        }
        if (pointBucket.NonIndexedArgsBuffer.IsValid())
        {
            device.ReadBuffer(pointBucket.NonIndexedArgsBuffer, &firstPointCmd, sizeof(firstPointCmd), 0u);
        }
    }

    const auto [edgeX, edgeY] = ProjectReferenceCameraPixel(glm::vec3{0.0f, -0.5f, 0.0f}, extent);
    const std::uint32_t darkEdgePixels =
        CountDarkOverlayPixels(bytes,
                               backbufferFormat,
                               bytesPerPixel,
                               extent,
                               edgeX,
                               edgeY,
                               kReferenceTriangleLineWidthSampleRadius);
    const PixelNeighborhoodStats edgeStats =
        DescribePixelNeighborhood(bytes,
                                  backbufferFormat,
                                  bytesPerPixel,
                                  extent,
                                  edgeX,
                                  edgeY,
                                  kReferenceTriangleLineWidthSampleRadius);

    EXPECT_GE(lineCullCount, 1u)
        << "ReferenceTriangle mesh edge lane did not emit an indexed line draw command."
        << " lineCmd={indexCount=" << firstLineCmd.IndexCount
        << ", instanceCount=" << firstLineCmd.InstanceCount
        << ", firstIndex=" << firstLineCmd.FirstIndex
        << ", vertexOffset=" << firstLineCmd.VertexOffset
        << ", firstInstance=" << firstLineCmd.FirstInstance << "}";
    ASSERT_TRUE(firstLineConfigRead)
        << "ReferenceTriangle mesh edge lane did not expose a readable first line config.";
    EXPECT_FLOAT_EQ(firstLineConfig.Line.LineWidth, kReferenceTriangleLineWidthSmokePx);
    EXPECT_EQ(firstLineConfig.Line.LineWidthBDA, 0u);
    EXPECT_GE(lineQuadCullCount, 1u)
        << "ReferenceTriangle mesh edge lane did not emit the non-indexed line-quad draw consumed by LinePass."
        << " lineQuadCmd={vertexCount=" << firstLineQuadCmd.VertexCount
        << ", instanceCount=" << firstLineQuadCmd.InstanceCount
        << ", firstVertex=" << firstLineQuadCmd.FirstVertex
        << ", firstInstance=" << firstLineQuadCmd.FirstInstance << "}";
    EXPECT_GE(pointCullCount, 1u)
        << "ReferenceTriangle mesh point lane did not emit a point draw command."
        << " pointCmd={vertexCount=" << firstPointCmd.VertexCount
        << ", instanceCount=" << firstPointCmd.InstanceCount
        << ", firstVertex=" << firstPointCmd.FirstVertex
        << ", firstInstance=" << firstPointCmd.FirstInstance << "}";
    EXPECT_GE(sceneInputs.LineInstancesWithPositionBuffer, 1u)
        << "ReferenceTriangle mesh edge lane did not publish a position channel BDA.";
    EXPECT_GE(sceneInputs.PointInstancesWithPositionBuffer, 1u)
        << "ReferenceTriangle mesh vertex lane did not publish a position channel BDA.";

    EXPECT_GE(darkEdgePixels, kReferenceTriangleLineWidthMinDarkPixels)
        << "ReferenceTriangle mesh edge lane did not leave a configured-width dark overlay near the projected bottom-edge midpoint. "
        << "sample=(" << edgeX << "," << edgeY << ")"
        << " darkPixels=" << darkEdgePixels
        << " minExpectedDarkPixels=" << kReferenceTriangleLineWidthMinDarkPixels
        << " configuredWidthPx=" << firstLineConfig.Line.LineWidth
        << " center=(" << static_cast<int>(edgeStats.Center.R) << ","
        << static_cast<int>(edgeStats.Center.G) << ","
        << static_cast<int>(edgeStats.Center.B) << ")"
        << " min=(" << static_cast<int>(edgeStats.Min.R) << ","
        << static_cast<int>(edgeStats.Min.G) << ","
        << static_cast<int>(edgeStats.Min.B) << ")"
        << " max=(" << static_cast<int>(edgeStats.Max.R) << ","
        << static_cast<int>(edgeStats.Max.G) << ","
        << static_cast<int>(edgeStats.Max.B) << ")"
        << " lineCullCount=" << lineCullCount
        << " lineCmd={indexCount=" << firstLineCmd.IndexCount
        << ", instanceCount=" << firstLineCmd.InstanceCount
        << ", firstIndex=" << firstLineCmd.FirstIndex
        << ", vertexOffset=" << firstLineCmd.VertexOffset
        << ", firstInstance=" << firstLineCmd.FirstInstance << "}"
        << " lineQuadCullCount=" << lineQuadCullCount
        << " lineQuadCmd={vertexCount=" << firstLineQuadCmd.VertexCount
        << ", instanceCount=" << firstLineQuadCmd.InstanceCount
        << ", firstVertex=" << firstLineQuadCmd.FirstVertex
        << ", firstInstance=" << firstLineQuadCmd.FirstInstance << "}"
        << " pointCullCount=" << pointCullCount
        << " pointCmd={vertexCount=" << firstPointCmd.VertexCount
        << ", instanceCount=" << firstPointCmd.InstanceCount
        << ", firstVertex=" << firstPointCmd.FirstVertex
        << ", firstInstance=" << firstPointCmd.FirstInstance << "}"
        << " sceneInputs={live=" << sceneInputs.LiveInstanceCount
        << ", capacity=" << sceneInputs.InstanceCapacity
        << ", visible=" << sceneInputs.VisibleInstances
        << ", lines=" << sceneInputs.LineInstances
        << ", points=" << sceneInputs.PointInstances
        << ", linePositionBuffers=" << sceneInputs.LineInstancesWithPositionBuffer
        << ", pointPositionBuffers=" << sceneInputs.PointInstancesWithPositionBuffer
        << ", lineZeroBounds=" << sceneInputs.LineInstancesWithZeroBounds
        << ", pointZeroBounds=" << sceneInputs.PointInstancesWithZeroBounds
        << ", lineZeroIndices=" << sceneInputs.LineInstancesWithZeroIndexCount
        << ", pointZeroVertices=" << sceneInputs.PointInstancesWithZeroVertexCount
        << ", lineInvalidGeometry=" << sceneInputs.LineInstancesWithInvalidGeometry
        << ", pointInvalidGeometry=" << sceneInputs.PointInstancesWithInvalidGeometry
        << ", firstLineSlot=" << sceneInputs.FirstLineSlot
        << ", firstLineFlags=" << sceneInputs.FirstLineInstance.RenderFlags
        << ", firstLineGeometrySlot=" << sceneInputs.FirstLineInstance.GeometrySlot
        << ", firstLineIndexCount=" << sceneInputs.FirstLineGeometry.LineIndexCount
        << ", firstLineFirstIndex=" << sceneInputs.FirstLineGeometry.LineFirstIndex
        << ", firstLineVertexOffset=" << sceneInputs.FirstLineGeometry.VertexOffset
        << ", firstLineBoundsRadius=" << sceneInputs.FirstLineBounds.WorldSphere.w
        << ", firstPointSlot=" << sceneInputs.FirstPointSlot
        << ", firstPointFlags=" << sceneInputs.FirstPointInstance.RenderFlags
        << ", firstPointGeometrySlot=" << sceneInputs.FirstPointInstance.GeometrySlot
        << ", firstPointVertexCount=" << sceneInputs.FirstPointGeometry.PointVertexCount
        << ", firstPointFirstVertex=" << sceneInputs.FirstPointGeometry.PointFirstVertex
        << ", firstPointBoundsRadius=" << sceneInputs.FirstPointBounds.WorldSphere.w << "}"
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

// --- GRAPHICS-091 Slice C: scalar-field colormap on forward line/point ------

namespace
{
// GRAPHICS-091 Slice C scalar-field colormap smoke parameters. The reference
// triangle's source vertices carry a uniform vertex-domain scalar at the range
// maximum so the shared `common/gpu_scene.glsl` colormap resolver paints one
// predictable colour (the LUT maximum) on every forward line and point fragment,
// independent of interpolation or the exact sampled pixel.
inline constexpr std::string_view kScalarFieldSmokeProperty = "v:graphics091_scalar";
inline constexpr float kScalarFieldSmokeRangeMin = 0.0f;
inline constexpr float kScalarFieldSmokeRangeMax = 1.0f;
inline constexpr float kScalarFieldSmokeLineWidthPx = 8.0f;
inline constexpr float kScalarFieldSmokePointSizePx = 12.0f;
inline constexpr std::uint32_t kScalarFieldSmokeSampleRadius = 8u;
inline constexpr std::uint32_t kScalarFieldSurfaceSmokeSampleRadius = 3u;
// Minimum RGB distance from the background clear for a sampled neighborhood
// pixel to count as a drawn line/point fragment.
inline constexpr int kScalarFieldSmokeMinForeground = 60;
// `ColorSourceMode` value for `ScalarField` in the `GpuEntityConfig` contract
// (mirrors `VisualizationSyncSystem` `kMode_ScalarField` and the GLSL
// `GpuColorSource_ScalarField`); `VisDomain` 0 is the vertex domain.
inline constexpr std::uint32_t kColorSourceScalarFieldMode = 2u;
inline constexpr std::uint32_t kVisDomainVertex = 0u;

[[nodiscard]] std::string DescribeColormapConfig(
    const std::string_view lane,
    const Extrinsic::RHI::GpuEntityConfig& cfg,
    const std::uint32_t expectedColormapId)
{
    std::string out{lane};
    out += " cfg{mode=" + std::to_string(cfg.ColorSourceMode);
    out += ", colormapId=" + std::to_string(cfg.ColormapID);
    out += ", expectedColormapId=" + std::to_string(expectedColormapId);
    out += ", scalarBDA=" + std::to_string(cfg.ScalarBDA);
    out += ", rangeMin=" + std::to_string(cfg.ScalarRangeMin);
    out += ", rangeMax=" + std::to_string(cfg.ScalarRangeMax);
    out += ", visDomain=" + std::to_string(cfg.VisDomain);
    out += ", elementCount=" + std::to_string(cfg.ElementCount) + "}";
    return out;
}

[[nodiscard]] std::string PixelText(const RgbaPixel p)
{
    return "(" + std::to_string(static_cast<int>(p.R)) + "," +
           std::to_string(static_cast<int>(p.G)) + "," +
           std::to_string(static_cast<int>(p.B)) + ")";
}

[[nodiscard]] bool IsSrgbBackbuffer(const Extrinsic::RHI::Format format) noexcept
{
    return format == Extrinsic::RHI::Format::RGBA8_SRGB ||
           format == Extrinsic::RHI::Format::BGRA8_SRGB;
}

[[nodiscard]] std::uint8_t SrgbByteToLinearByte(const std::uint8_t srgb) noexcept
{
    const float s = static_cast<float>(srgb) / 255.0f;
    const float linear = (s <= 0.04045f) ? (s / 12.92f)
                                         : std::pow((s + 0.055f) / 1.055f, 2.4f);
    const float clamped = std::clamp(linear, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

// Bring a sampled backbuffer pixel into the colormap LUT's linear space so it can
// be compared against ColormapSystem::SampleCpu (which returns linear LUT bytes).
[[nodiscard]] RgbaPixel ToLinearPixel(const Extrinsic::RHI::Format format,
                                      const RgbaPixel px) noexcept
{
    if (!IsSrgbBackbuffer(format))
    {
        return px;
    }
    return RgbaPixel{
        .R = SrgbByteToLinearByte(px.R),
        .G = SrgbByteToLinearByte(px.G),
        .B = SrgbByteToLinearByte(px.B),
        .A = px.A,
    };
}

struct ForegroundSample
{
    RgbaPixel Pixel{};
    int BackgroundDistance{0};
};

struct ClosestPixelSample
{
    RgbaPixel Pixel{};
    int TargetDistance{std::numeric_limits<int>::max()};
};

[[nodiscard]] ClosestPixelSample FindClosestPixelTo(
    const std::vector<std::uint8_t>& bytes,
    const Extrinsic::RHI::Format format,
    const std::uint32_t bytesPerPixel,
    const Extrinsic::Core::Extent2D extent,
    const std::uint32_t centerX,
    const std::uint32_t centerY,
    const std::uint32_t radius,
    const RgbaPixel target)
{
    ClosestPixelSample best{};
    if (extent.Width == 0u || extent.Height == 0u)
    {
        return best;
    }

    best.Pixel = ReadPixel(bytes, format, bytesPerPixel, extent, centerX, centerY);
    best.TargetDistance = RgbDistance(ToLinearPixel(format, best.Pixel), target);

    const auto minX = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerX) - static_cast<int>(radius)));
    const auto minY = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerY) - static_cast<int>(radius)));
    const auto maxX = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Width) - 1, static_cast<int>(centerX) + static_cast<int>(radius)));
    const auto maxY = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Height) - 1, static_cast<int>(centerY) + static_cast<int>(radius)));
    for (std::uint32_t y = minY; y <= maxY; ++y)
    {
        for (std::uint32_t x = minX; x <= maxX; ++x)
        {
            const RgbaPixel px = ReadPixel(bytes, format, bytesPerPixel, extent, x, y);
            const int d = RgbDistance(ToLinearPixel(format, px), target);
            if (d < best.TargetDistance)
            {
                best.TargetDistance = d;
                best.Pixel = px;
            }
        }
    }
    return best;
}

// Return the neighborhood pixel furthest from the background clear (the most
// strongly drawn fragment), so the colormap comparison targets the actual
// line/point draw rather than a background or partially-covered edge texel.
[[nodiscard]] ForegroundSample FindMostForegroundPixel(
    const std::vector<std::uint8_t>& bytes,
    const Extrinsic::RHI::Format format,
    const std::uint32_t bytesPerPixel,
    const Extrinsic::Core::Extent2D extent,
    const std::uint32_t centerX,
    const std::uint32_t centerY,
    const std::uint32_t radius,
    const RgbaPixel background)
{
    ForegroundSample best{};
    best.Pixel = ReadPixel(bytes, format, bytesPerPixel, extent, centerX, centerY);
    best.BackgroundDistance = RgbDistance(best.Pixel, background);
    if (extent.Width == 0u || extent.Height == 0u)
    {
        return best;
    }

    const auto minX = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerX) - static_cast<int>(radius)));
    const auto minY = static_cast<std::uint32_t>(
        std::max<int>(0, static_cast<int>(centerY) - static_cast<int>(radius)));
    const auto maxX = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Width) - 1, static_cast<int>(centerX) + static_cast<int>(radius)));
    const auto maxY = static_cast<std::uint32_t>(
        std::min<int>(static_cast<int>(extent.Height) - 1, static_cast<int>(centerY) + static_cast<int>(radius)));
    for (std::uint32_t y = minY; y <= maxY; ++y)
    {
        for (std::uint32_t x = minX; x <= maxX; ++x)
        {
            const RgbaPixel px = ReadPixel(bytes, format, bytesPerPixel, extent, x, y);
            const int d = RgbDistance(px, background);
            if (d > best.BackgroundDistance)
            {
                best.BackgroundDistance = d;
                best.Pixel = px;
            }
        }
    }
    return best;
}
} // namespace

// The unified scalar-field colormap resolution (Slice A's shared
// `common/gpu_scene.glsl` helper) is live on the modern forward LINE and POINT
// passes at parity with the surface path. Slice B proved the CPU/null
// `VisualizationSyncSystem` config parity (`Test.MinimalTriangleAcceptance.cpp`);
// this slice drives the promoted-Vulkan default recipe with a `ScalarField`
// `VisualizationConfig` on a line+point renderable and asserts the per-line and
// per-point `GpuEntityConfig` carry the scalar-field colormap config end to end
// to the GPU entity-config buffer, the `LinePass`/`PointPass` record on the
// operational command stream, and — with the surface lane removed so only the
// line/point draws contribute pixels — each lane's strongest sampled fragment
// resolves closer to the expected Viridis colormap colour than to the
// white/material fallback a colormap-ignoring shader would emit.
//
// The fixture self-skips on non-Vulkan hosts via
// `BootstrapDefaultSandboxAppEngine`, so it is excluded from the default CPU
// gate by its `gpu;vulkan` labels. The hard config-readback assertions are
// correct-by-construction from the Slice B sync contract; the `ScalarBDA`
// residency and colormap-pixel assertions are the operational proof finalized
// on a Vulkan-capable host.
TEST(RuntimeSandboxAcceptanceGpuSmoke, ReferenceTriangleScalarFieldColormapResolvesOnLineAndPointLanes)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();

    // Author a uniform per-vertex scalar at the colormap range maximum: the whole
    // line/point geometry then resolves to one predictable colour (the LUT
    // maximum), so the sampled pixel proof does not depend on the exact
    // interpolated scalar at a sample location.
    auto& vertices = raw.get<gs::Vertices>(triangle);
    vertices.Properties
        .GetOrAdd<float>(std::string{kScalarFieldSmokeProperty}, 0.0f)
        .Vector() = {kScalarFieldSmokeRangeMax,
                     kScalarFieldSmokeRangeMax,
                     kScalarFieldSmokeRangeMax};

    // Draw the edge and point lanes with sampleable width/size so the scalar-field
    // colormap is proven on the forward LinePass and PointPass, not only the
    // surface pass.
    G::RenderEdges edges{};
    edges.WidthSource = kScalarFieldSmokeLineWidthPx;
    raw.emplace_or_replace<G::RenderEdges>(triangle, edges);
    G::RenderPoints points{};
    points.SizeSource = kScalarFieldSmokePointSizePx;
    raw.emplace_or_replace<G::RenderPoints>(triangle, points);

    auto& visualization = raw.get<G::VisualizationConfig>(triangle);
    visualization.Source = G::VisualizationConfig::ColorSource::ScalarField;
    visualization.ScalarFieldName = std::string{kScalarFieldSmokeProperty};
    visualization.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
    visualization.Scalar.Map = Extrinsic::Graphics::Colormap::Type::Viridis;
    visualization.Scalar.AutoRange = false;
    visualization.Scalar.RangeMin = kScalarFieldSmokeRangeMin;
    visualization.Scalar.RangeMax = kScalarFieldSmokeRangeMax;
    visualization.Scalar.BinCount = 0u;
    visualization.Scalar.Isolines.Num = 0u;

    // Isolate the line and point lanes: remove the surface lane so the sampled
    // pixels can only come from the forward LinePass/PointPass draws. Surface,
    // line, and point of one entity share a GpuEntityConfig, so a retained
    // scalar-coloured surface fill would otherwise paint the same colormap colour
    // over the sampled neighborhood and the proof could not attribute the colour
    // to the line/point path.
    raw.remove<G::RenderSurface>(triangle);

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ScalarFieldColormap.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for scalar-field colormap readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "LinePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "PointPass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox scalar-field colormap readback copy did not record on an operational frame.";

    // Read back the per-line and per-point GpuEntityConfig from the GPU
    // entity-config buffer and assert the unified scalar-field colormap config
    // resolved for BOTH domains.
    const GpuSceneInputSummary sceneInputs = SummarizeGpuSceneInputs(device, renderer);
    const Extrinsic::RHI::BufferHandle entityConfigBuffer =
        renderer.GetGpuWorld().GetEntityConfigBuffer();
    ASSERT_TRUE(entityConfigBuffer.IsValid())
        << "GPU entity-config buffer is not allocated on the operational device.";

    const std::uint32_t expectedColormapId =
        renderer.GetColormapSystem().GetBindlessIndex(Extrinsic::Graphics::Colormap::Type::Viridis);

    const auto readConfig = [&](const std::uint32_t configSlot) noexcept
    {
        Extrinsic::RHI::GpuEntityConfig cfg{};
        device.ReadBuffer(entityConfigBuffer,
                          &cfg,
                          sizeof(cfg),
                          static_cast<std::uint64_t>(configSlot) * sizeof(cfg));
        return cfg;
    };

    ASSERT_NE(sceneInputs.FirstLineSlot, kInvalidIndex)
        << "ReferenceTriangle did not emit a forward line instance for the scalar-field colormap smoke.";
    ASSERT_NE(sceneInputs.FirstPointSlot, kInvalidIndex)
        << "ReferenceTriangle did not emit a forward point instance for the scalar-field colormap smoke.";

    const Extrinsic::RHI::GpuEntityConfig lineCfg = readConfig(sceneInputs.FirstLineInstance.ConfigSlot);
    const Extrinsic::RHI::GpuEntityConfig pointCfg = readConfig(sceneInputs.FirstPointInstance.ConfigSlot);

    const std::array<std::pair<std::string_view, const Extrinsic::RHI::GpuEntityConfig*>, 2> laneConfigs{{
        {"line", &lineCfg},
        {"point", &pointCfg},
    }};
    for (const auto& [lane, cfgPtr] : laneConfigs)
    {
        const Extrinsic::RHI::GpuEntityConfig& cfg = *cfgPtr;
        EXPECT_EQ(cfg.ColorSourceMode, kColorSourceScalarFieldMode)
            << DescribeColormapConfig(lane, cfg, expectedColormapId);
        EXPECT_EQ(cfg.ColormapID, expectedColormapId)
            << DescribeColormapConfig(lane, cfg, expectedColormapId);
        EXPECT_FLOAT_EQ(cfg.ScalarRangeMin, kScalarFieldSmokeRangeMin)
            << DescribeColormapConfig(lane, cfg, expectedColormapId);
        EXPECT_FLOAT_EQ(cfg.ScalarRangeMax, kScalarFieldSmokeRangeMax)
            << DescribeColormapConfig(lane, cfg, expectedColormapId);
        EXPECT_EQ(cfg.VisDomain, kVisDomainVertex)
            << DescribeColormapConfig(lane, cfg, expectedColormapId);
        // The scalar buffer must be resident so the forward line/point shaders
        // resolve per-element scalars (not the range-min fallback) through the
        // colormap LUT. If this fails, the runtime did not wire a scalar
        // visualization adapter for the line/point domains — the residency gap
        // GRAPHICS-091 Slice C closes.
        EXPECT_NE(cfg.ScalarBDA, 0u)
            << "scalar-field buffer not resident for " << lane << " lane; "
            << DescribeColormapConfig(lane, cfg, expectedColormapId);
    }

    // Colormap pixel proof with the line/point lanes isolated (surface removed).
    // Each lane's strongest fragment must resolve to the Viridis LUT maximum
    // colour and sit closer to it than to the white/material colour a
    // colormap-ignoring shader would emit. A bare "something was drawn" threshold
    // is intentionally avoided — it would also pass for the fallback colour.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const RgbaPixel background = ReadPixel(
        bytes, backbufferFormat, bytesPerPixel, extent,
        static_cast<std::uint32_t>((extent.Width * 15) / 16),
        static_cast<std::uint32_t>((extent.Height * 15) / 16));
    // The ScalarField fallback colour, when a shader ignores the colormap, is the
    // base colour vec4(1.0) (gpu_scene.glsl GpuResolveVisualizationColorFallback,
    // consumed by forward/line.frag and forward/point.*).
    const RgbaPixel fallbackColor{255u, 255u, 255u, 255u};
    // Uniform scalar at RangeMax -> normalized t = 1 -> Viridis LUT maximum.
    const auto lutMax = renderer.GetColormapSystem().SampleCpu(
        Extrinsic::Graphics::Colormap::Type::Viridis, 1.0f);
    const RgbaPixel expectedColormap{
        .R = lutMax.R, .G = lutMax.G, .B = lutMax.B, .A = 255u};

    const auto expectColormapLane = [&](const std::string_view lane,
                                        const glm::vec3 worldSample)
    {
        const auto [px, py] = ProjectReferenceCameraPixel(worldSample, extent);
        const ForegroundSample fg = FindMostForegroundPixel(
            bytes, backbufferFormat, bytesPerPixel, extent, px, py,
            kScalarFieldSmokeSampleRadius, background);
        const RgbaPixel lit = ToLinearPixel(backbufferFormat, fg.Pixel);
        const int toColormap = RgbDistance(lit, expectedColormap);
        const int toFallback = RgbDistance(lit, fallbackColor);
        const Extrinsic::RHI::GpuEntityConfig& cfg = (lane == "line") ? lineCfg : pointCfg;

        EXPECT_GE(fg.BackgroundDistance, kScalarFieldSmokeMinForeground)
            << lane << " lane left no drawn fragment near (" << px << "," << py
            << "); cannot prove the colormap path. background=" << PixelText(background)
            << " strongest=" << PixelText(fg.Pixel)
            << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        EXPECT_LT(toColormap, toFallback)
            << lane << " lane fragment is closer to the white/material fallback than to the expected "
            << "Viridis colormap colour — the line/point colormap path did not resolve on the GPU. "
            << "strongest(linearized)=" << PixelText(lit)
            << " expectedViridis=" << PixelText(expectedColormap)
            << " fallback=" << PixelText(fallbackColor)
            << " toColormap=" << toColormap << " toFallback=" << toFallback
            << " backbufferFormat=" << static_cast<int>(backbufferFormat)
            << " cfg=[" << DescribeColormapConfig(lane, cfg, expectedColormapId) << "]";
    };

    expectColormapLane("line", glm::vec3{0.0f, -0.5f, 0.0f});  // bottom-edge midpoint (v0-v1)
    expectColormapLane("point", glm::vec3{0.0f, 0.5f, 0.0f});  // apex vertex v2 (point impostor)

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

// BUG-060 operational surface proof for the sandbox Appearance Scalar /
// Isolines preset path. The scalar field is intentionally binned with
// `BinCount == IsolineCount`: the non-contour probe has raw t=0.4 but binned
// t=0.5, so a shader that still applies isolines to the binned value paints it
// as a false contour instead of the Viridis mid-point colour.
TEST(RuntimeSandboxAcceptanceGpuSmoke, ReferenceTriangleScalarFieldSurfaceAndIsolinesResolveOnGpu)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    auto& vertices = raw.get<gs::Vertices>(triangle);
    vertices.Properties
        .GetOrAdd<float>(std::string{kScalarFieldSmokeProperty}, 0.0f)
        .Vector() = {kScalarFieldSmokeRangeMin,
                     kScalarFieldSmokeRangeMax,
                     kScalarFieldSmokeRangeMin};

    G::VisualizationConfig surfaceVisualization{};
    surfaceVisualization.Source = G::VisualizationConfig::ColorSource::ScalarField;
    surfaceVisualization.ScalarFieldName = std::string{kScalarFieldSmokeProperty};
    surfaceVisualization.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
    surfaceVisualization.Scalar.Map = Extrinsic::Graphics::Colormap::Type::Viridis;
    surfaceVisualization.Scalar.AutoRange = false;
    surfaceVisualization.Scalar.RangeMin = kScalarFieldSmokeRangeMin;
    surfaceVisualization.Scalar.RangeMax = kScalarFieldSmokeRangeMax;
    surfaceVisualization.Scalar.BinCount = 3u;
    surfaceVisualization.Scalar.Isolines.Num = 3u;
    surfaceVisualization.Scalar.Isolines.Width = 32.0f;
    surfaceVisualization.Scalar.Isolines.Color = glm::vec4{0.0f, 0.95f, 1.0f, 1.0f};
    raw.emplace_or_replace<G::VisualizationLaneOverrides>(triangle).Surface =
        surfaceVisualization;

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width == 0u || extent.Height == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ScalarFieldSurfaceIsolines.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for scalar-field surface/isolines readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox scalar-field surface/isolines readback copy did not record on an operational frame.";

    const std::uint32_t triangleRenderId =
        Extrinsic::Runtime::SelectionController::ToStableEntityId(triangle);
    const GpuInstanceConfigReadback surfaceInstance =
        ReadVisibleInstanceConfigByEntityId(
            device, renderer, triangleRenderId, Extrinsic::RHI::GpuRender_Surface);
    ASSERT_TRUE(surfaceInstance.Found)
        << "ReferenceTriangle did not emit a visible surface instance with render id "
        << triangleRenderId << ".";

    const std::uint32_t expectedColormapId =
        renderer.GetColormapSystem().GetBindlessIndex(Extrinsic::Graphics::Colormap::Type::Viridis);
    const Extrinsic::RHI::GpuEntityConfig& cfg = surfaceInstance.Config;
    EXPECT_EQ(cfg.ColorSourceMode, kColorSourceScalarFieldMode)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_EQ(cfg.ColormapID, expectedColormapId)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_NE(cfg.ScalarBDA, 0u)
        << "surface scalar-field buffer not resident; "
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.ScalarRangeMin, kScalarFieldSmokeRangeMin)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.ScalarRangeMax, kScalarFieldSmokeRangeMax)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_EQ(cfg.BinCount, 3u)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.IsolineCount, 3.0f)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.IsolineWidth, 32.0f)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.IsolineColor.x, surfaceVisualization.Scalar.Isolines.Color.x)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.IsolineColor.y, surfaceVisualization.Scalar.Isolines.Color.y)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.IsolineColor.z, surfaceVisualization.Scalar.Isolines.Color.z)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);
    EXPECT_FLOAT_EQ(cfg.IsolineColor.w, surfaceVisualization.Scalar.Isolines.Color.w)
        << DescribeColormapConfig("surface", cfg, expectedColormapId);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const auto lutMid = renderer.GetColormapSystem().SampleCpu(
        Extrinsic::Graphics::Colormap::Type::Viridis, 0.5f);
    const RgbaPixel expectedColormap{
        .R = lutMid.R, .G = lutMid.G, .B = lutMid.B, .A = 255u};
    const RgbaPixel expectedIsoline{0u, 242u, 255u, 255u};

    const auto expectProbe = [&](const std::string_view label,
                                 const glm::vec3 worldSample,
                                 const RgbaPixel expected,
                                 const RgbaPixel rejected)
    {
        const auto [px, py] = ProjectReferenceCameraPixel(worldSample, extent);
        const ClosestPixelSample rawSample = FindClosestPixelTo(
            bytes, backbufferFormat, bytesPerPixel, extent, px, py,
            kScalarFieldSurfaceSmokeSampleRadius, expected);
        const RgbaPixel lit = ToLinearPixel(backbufferFormat, rawSample.Pixel);
        const int toExpected = RgbDistance(lit, expected);
        const int toRejected = RgbDistance(lit, rejected);
        EXPECT_LT(toExpected, toRejected)
            << label << " surface probe resolved closer to the rejected colour than the expected colour. "
            << "sample=(" << px << "," << py << ")"
            << " pixel(linearized)=" << PixelText(lit)
            << " expected=" << PixelText(expected)
            << " rejected=" << PixelText(rejected)
            << " toExpected=" << toExpected
            << " toRejected=" << toRejected
            << " cfg=[" << DescribeColormapConfig("surface", cfg, expectedColormapId) << "]";
    };

    // raw t = 0.4, binned t = 0.5: must stay Viridis, not a false contour.
    expectProbe("non-isoline", glm::vec3{0.025f, -0.25f, 0.0f},
                expectedColormap, expectedIsoline);
    // raw t = 0.5: actual evenly-spaced isoline at the interior level boundary.
    expectProbe("isoline", glm::vec3{0.125f, -0.25f, 0.0f},
                expectedIsoline, expectedColormap);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

// Sandbox app that applies the promoted Inspector transform-edit command (the
// live EditorCommandHistory path) to the ReferenceTriangle on a mid-run frame
// — i.e. after that frame's fixed-step bundle already ran — and exits after a
// bounded number of frames so the readback captures post-edit pixels.
class EditTriangleViaInspectorApp final : public IApplication
{
public:
    void OnInitialize(Engine&) override {}

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Frames == kBug024EditFrame)
        {
            const EntityHandle triangle =
                FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
            if (triangle != Extrinsic::ECS::InvalidEntityHandle)
            {
                const Extrinsic::Runtime::SandboxEditorContext context{
                    .Scene = &*engine.Worlds().Get(engine.ActiveWorld()),
                    .Selection = &Selection(engine),
                    .CommandHistory =
                        &*engine.Services()
                              .Find<RT::EditorCommandHistory>(),
                };
                EditStatus = Extrinsic::Runtime::ApplySandboxEditorTransformEdit(
                    context,
                    Extrinsic::Runtime::SandboxEditorTransformEditCommand{
                        .StableEntityId =
                            Extrinsic::Runtime::SelectionController::ToStableEntityId(triangle),
                        .SetPosition = true,
                        .Position = kBug024Shift,
                    });
            }
        }
        if (m_Frames >= kBug024TotalFrames)
        {
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override {}

    Extrinsic::Runtime::SandboxEditorCommandStatus EditStatus{
        Extrinsic::Runtime::SandboxEditorCommandStatus::NoChange};

private:
    std::uint32_t m_Frames{0u};
};
} // namespace

// BUG-024B — Operational proof of the BUG-024 fix: a mid-run Inspector
// transform edit through the promoted editor command path (not a direct
// WorldMatrix write) moves the rendered ReferenceTriangle. The frame center
// returns to the background color and the projected shifted location contains
// the triangle. Without the runtime pre-render transform flush, the high-fps
// bounded run schedules no fixed substep after the edit, so the triangle
// would keep rendering at the origin.
TEST(RuntimeSandboxAcceptanceGpuSmoke, InspectorTransformEditShiftsReferenceTrianglePixels)
{
    auto app = std::make_unique<EditTriangleViaInspectorApp>();
    auto* appPtr = app.get();
    auto bootstrap = BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_NE(triangle, Extrinsic::ECS::InvalidEntityHandle);

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.Bug024TransformEdit.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for the BUG-024B edit smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    ASSERT_EQ(appPtr->EditStatus, Extrinsic::Runtime::SandboxEditorCommandStatus::Applied)
        << "Inspector transform-edit command did not apply during the bounded run.";

    // The CPU-side contract from BUG-024 must hold on the live engine: the
    // edited local position reached the world matrix and the dirty tags were
    // flushed + drained within the run.
    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    const glm::mat4& world = raw.get<ECSC::Transform::WorldMatrix>(triangle).Matrix;
    EXPECT_FLOAT_EQ(world[3].x, kBug024Shift.x);
    EXPECT_FLOAT_EQ(world[3].y, kBug024Shift.y);
    EXPECT_FLOAT_EQ(world[3].z, kBug024Shift.z);
    EXPECT_FALSE(raw.any_of<ECSC::Transform::IsDirtyTag>(triangle));

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "BUG-024B readback triplet did not record on an operational frame.";

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const std::uint32_t centerX = static_cast<std::uint32_t>(extent.Width / 2);
    const std::uint32_t centerY = static_cast<std::uint32_t>(extent.Height / 2);
    const RgbaPixel center =
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent, centerX, centerY);

    // Triangle interior sample: the shifted triangle's centroid column on the
    // z=0 plane, projected through the reference camera.
    const auto [shiftX, shiftY] = ProjectReferenceCameraPixel(kBug024Shift, extent);
    const RgbaPixel shifted =
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent, shiftX, shiftY);

    // Background probes far from both the original and shifted triangle and
    // from the proven-background sample columns of the sibling smokes.
    const RgbaPixel backgroundBottom = ReadPixel(
        bytes, backbufferFormat, bytesPerPixel, extent,
        centerX, static_cast<std::uint32_t>((extent.Height * 7) / 8));
    const RgbaPixel backgroundCorner = ReadPixel(
        bytes, backbufferFormat, bytesPerPixel, extent,
        static_cast<std::uint32_t>((extent.Width * 15) / 16),
        static_cast<std::uint32_t>((extent.Height * 15) / 16));

    const auto pixelText = [](const RgbaPixel p)
    {
        return "(" + std::to_string(static_cast<int>(p.R)) + "," +
               std::to_string(static_cast<int>(p.G)) + "," +
               std::to_string(static_cast<int>(p.B)) + ")";
    };

    // The frame center returned to the background after the edit.
    EXPECT_LT(RgbDistance(center, backgroundBottom), 48)
        << "Frame center did not return to the background after the inspector edit. center="
        << pixelText(center) << " backgroundBottom=" << pixelText(backgroundBottom)
        << " backgroundCorner=" << pixelText(backgroundCorner)
        << " shifted=" << pixelText(shifted) << " at (" << shiftX << "," << shiftY << ")"
        << " extent=" << extent.Width << "x" << extent.Height;

    // The shifted sample location contains the triangle.
    const int shiftedToBackground = std::min(RgbDistance(shifted, backgroundBottom),
                                             RgbDistance(shifted, backgroundCorner));
    EXPECT_GT(shiftedToBackground, 48)
        << "The rendered triangle did not appear at the shifted location after the inspector edit. "
        << "shifted=" << pixelText(shifted) << " at (" << shiftX << "," << shiftY << ")"
        << " center=" << pixelText(center)
        << " backgroundBottom=" << pixelText(backgroundBottom)
        << " backgroundCorner=" << pixelText(backgroundCorner)
        << " extent=" << extent.Width << "x" << extent.Height
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, ImportedOffOriginObjTriangleAutoFramesAtCenter)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    // The default reference triangle already proves the authored in-memory mesh
    // path. Move it out of the center so this test can only pass if the file-
    // backed OBJ import contributes pixels. The imported OBJ is deliberately
    // outside the default camera frustum; this test requires import-time bounds
    // plus the one-shot camera focus path to make it visible.
    SetEntityPosition(*engine.Worlds().Get(engine.ActiveWorld()),
                      FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle"),
                      glm::vec3{4.0f, 0.0f, 0.0f});

    TempObjFile obj{
        "intrinsic_visible_imported_triangle",
        "v 7.25 -0.75 0\n"
        "v 8.75 -0.75 0\n"
        "v 8 0.75 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0.5 1\n"
        "f 1/1 2/2 3/3\n",
    };

    auto imported = RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).ImportAssetFromPath(
        Extrinsic::Runtime::RuntimeAssetImportRequest{
            .Path = obj.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);

    const EntityHandle importedEntity =
        FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), obj.Path.filename().string());
    ASSERT_NE(importedEntity, Extrinsic::ECS::InvalidEntityHandle);
    ASSERT_TRUE(engine.Worlds().Get(engine.ActiveWorld())->IsValid(importedEntity));

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    ASSERT_TRUE((raw.all_of<ECSC::Transform::Component,
                            ECSC::Transform::WorldMatrix,
                            ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds,
                            ECSC::Selection::SelectableTag,
                            G::RenderSurface,
                            G::VisualizationConfig,
                            gs::Vertices,
                            gs::Edges,
                            gs::Halfedges,
                            gs::Faces>(importedEntity)));
    const gs::ConstSourceView view = gs::BuildConstView(raw, importedEntity);
    ASSERT_TRUE(view.Valid());
    ASSERT_EQ(view.ActiveDomain, gs::Domain::Mesh);
    const auto& worldBounds =
        raw.get<ECSC::Culling::World::Bounds>(importedEntity);
    EXPECT_NEAR(worldBounds.WorldBoundingSphere.Center.x, 8.0f, 0.001f);
    EXPECT_NEAR(worldBounds.WorldBoundingSphere.Center.y, 0.0f, 0.001f);
    EXPECT_GT(worldBounds.WorldBoundingSphere.Radius, 1.0f);

    auto& visualization = raw.get<G::VisualizationConfig>(importedEntity);
    visualization.Source = G::VisualizationConfig::ColorSource::UniformColor;
    visualization.Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.ImportedObjTriangle.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for imported OBJ readback: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.CandidateRenderableCount, 1u);
    EXPECT_GE(ex.MeshGeometryUploads + ex.MeshGeometryReuseHits, 1u)
        << "Imported OBJ did not remain resident on the mesh extraction lane.";
    EXPECT_EQ(ex.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(ex.MeshGeometryInvalidTopology, 0u);
    EXPECT_EQ(ex.MeshGeometryMissingPositions, 0u);

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "DepthPrepass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox imported OBJ readback triplet did not record on an operational frame.";

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const std::uint32_t centerX = static_cast<std::uint32_t>(extent.Width / 2);
    const std::uint32_t centerY = static_cast<std::uint32_t>(extent.Height / 2);
    const RgbaPixel center =
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent, centerX, centerY);
    const std::array<RgbaPixel, 4> backgroundSamples{{
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  static_cast<std::uint32_t>((extent.Width * 7) / 8),
                  centerY),
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  static_cast<std::uint32_t>((extent.Width * 7) / 8),
                  static_cast<std::uint32_t>(extent.Height / 4)),
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  centerX,
                  static_cast<std::uint32_t>((extent.Height * 7) / 8)),
        ReadPixel(bytes, backbufferFormat, bytesPerPixel, extent,
                  static_cast<std::uint32_t>((extent.Width * 15) / 16),
                  static_cast<std::uint32_t>((extent.Height * 15) / 16)),
    }};

    int nearestBackgroundDistance = RgbDistance(center, backgroundSamples[0]);
    for (const RgbaPixel sample : backgroundSamples)
    {
        nearestBackgroundDistance = std::min(nearestBackgroundDistance, RgbDistance(center, sample));
    }

    EXPECT_GT(nearestBackgroundDistance, 48)
        << "The imported OBJ triangle did not contribute a distinguishable center pixel. "
        << "center=(" << static_cast<int>(center.R) << ","
        << static_cast<int>(center.G) << ","
        << static_cast<int>(center.B) << ","
        << static_cast<int>(center.A) << ") "
        << "background samples=("
        << static_cast<int>(backgroundSamples[0].R) << ","
        << static_cast<int>(backgroundSamples[0].G) << ","
        << static_cast<int>(backgroundSamples[0].B) << "),("
        << static_cast<int>(backgroundSamples[1].R) << ","
        << static_cast<int>(backgroundSamples[1].G) << ","
        << static_cast<int>(backgroundSamples[1].B) << "),("
        << static_cast<int>(backgroundSamples[2].R) << ","
        << static_cast<int>(backgroundSamples[2].G) << ","
        << static_cast<int>(backgroundSamples[2].B) << "),("
        << static_cast<int>(backgroundSamples[3].R) << ","
        << static_cast<int>(backgroundSamples[3].G) << ","
        << static_cast<int>(backgroundSamples[3].B) << ") "
        << "extent=" << extent.Width << "x" << extent.Height
        << " extraction candidates=" << ex.CandidateRenderableCount
        << " mesh uploads=" << ex.MeshGeometryUploads
        << " mesh reuse=" << ex.MeshGeometryReuseHits
        << " nearest background distance=" << nearestBackgroundDistance
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, ImportedObjWithoutAuthoredUvsSamplesGeneratedAlbedoTexture)
{
    auto app = std::make_unique<GeneratedUvTextureSmokeApp>();
    auto* appPtr = app.get();
    auto bootstrap = BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    SetEntityPosition(*engine.Worlds().Get(engine.ActiveWorld()),
                      FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle"),
                      glm::vec3{4.0f, 0.0f, 0.0f});

    TempObjFile obj{
        "intrinsic_generated_uv_texture_triangle",
        "v 7.25 -0.75 0\n"
        "v 8.75 -0.75 0\n"
        "v 8 0.75 0\n"
        "f 1 2 3\n",
    };

    auto imported = RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).ImportAssetFromPath(
        Extrinsic::Runtime::RuntimeAssetImportRequest{
            .Path = obj.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);

    const EntityHandle importedEntity =
        FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), obj.Path.filename().string());
    ASSERT_NE(importedEntity, Extrinsic::ECS::InvalidEntityHandle);
    ASSERT_TRUE(engine.Worlds().Get(engine.ActiveWorld())->IsValid(importedEntity));

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    ASSERT_TRUE((raw.all_of<G::RenderSurface,
                            G::VisualizationConfig,
                            gs::Vertices,
                            gs::Edges,
                            gs::Halfedges,
                            gs::Faces>(importedEntity)));
    const gs::ConstSourceView initialView = gs::BuildConstView(raw, importedEntity);
    ASSERT_TRUE(initialView.Valid());
    ASSERT_NE(initialView.VertexSource, nullptr);
    EXPECT_FALSE(initialView.VertexSource->Properties.Get<glm::vec2>("v:texcoord"))
        << "The GRAPHICS-089 smoke OBJ must omit authored texture coordinates.";

    const Assets::AssetTexture2DPayload payload =
        MakeGeneratedUvSmokeAlbedoPayload();
    const std::string generatedTexturePath =
        obj.Path.string() + ".graphics089-generated-albedo.texture";
    auto generatedTexture =
        RequiredEngineService<Extrinsic::Assets::AssetService>(engine).Load<Assets::AssetTexture2DPayload>(
            generatedTexturePath,
            [payload](std::string_view,
                      Assets::AssetId) -> Extrinsic::Core::Expected<Assets::AssetTexture2DPayload>
            {
                return payload;
            });
    ASSERT_TRUE(generatedTexture.has_value())
        << static_cast<int>(generatedTexture.error());
    appPtr->SetGeneratedTexture(*generatedTexture);

    raw.emplace_or_replace<RT::ProgressivePresentationBindings>(
        importedEntity,
        MakeGeneratedAlbedoPresentationBindings(*generatedTexture));
    auto& visualization = raw.get<G::VisualizationConfig>(importedEntity);
    visualization.Source = G::VisualizationConfig::ColorSource::Material;

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width <= 0 || extent.Height <= 0)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.GeneratedUvTexture.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Generated-UV texture smoke did not reach operational Vulkan: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    ASSERT_TRUE(appPtr->UploadRequested)
        << "Generated texture upload was never requested after promoted Vulkan became operational.";
    ASSERT_FALSE(appPtr->UploadError.has_value())
        << "Generated texture upload failed after promoted Vulkan became operational: "
        << static_cast<int>(*appPtr->UploadError);
    EXPECT_TRUE(appPtr->TextureReadyObserved)
        << "Generated texture upload did not reach ready GPU residency within the bounded smoke run.";
    EXPECT_FALSE(appPtr->TimedOut)
        << "Generated-UV texture smoke timed out after " << appPtr->Frames << " frames.";

    const gs::ConstSourceView resolvedView = gs::BuildConstView(raw, importedEntity);
    ASSERT_TRUE(resolvedView.Valid());
    ASSERT_NE(resolvedView.VertexSource, nullptr);
    const auto resolvedTexcoords =
        resolvedView.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(resolvedTexcoords);
    ASSERT_EQ(resolvedTexcoords.Vector().size(), 3u);

    bool sawNonZeroTexcoord = false;
    for (const glm::vec2 uv : resolvedTexcoords.Vector())
    {
        EXPECT_TRUE(std::isfinite(uv.x));
        EXPECT_TRUE(std::isfinite(uv.y));
        sawNonZeroTexcoord = sawNonZeroTexcoord ||
            std::abs(uv.x) > 1.0e-6f ||
            std::abs(uv.y) > 1.0e-6f;
    }
    EXPECT_TRUE(sawNonZeroTexcoord)
        << "ASSETIO-008 did not publish non-zero generated UVs for the imported OBJ.";

    EXPECT_EQ(RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine).GetState(*generatedTexture),
              Extrinsic::Graphics::GpuAssetState::Ready)
        << "Generated albedo texture never reached ready GPU residency.";

    const auto& ex = engine.GetLastRenderExtractionStats();
    EXPECT_GE(ex.MeshGeometryUploads + ex.MeshGeometryReuseHits, 1u)
        << "Imported OBJ did not remain resident on the mesh extraction lane.";
    EXPECT_EQ(ex.MeshGeometryMissingTexcoords, 0u)
        << "Mesh extraction fell back to default zero UVs instead of ASSETIO-008 generated UVs.";
    EXPECT_EQ(ex.MeshGeometryNonFiniteTexcoords, 0u);
    EXPECT_GE(ex.ProgressivePresentationEntityCount, 1u);
    EXPECT_GE(ex.ProgressiveReadyTextureSlotCount, 1u);
    EXPECT_GE(ex.ProgressiveMaterialTextureBindingResolveCount, 1u)
        << "Generated texture slot did not resolve through the material texture binding path.";
    EXPECT_EQ(ex.ProgressiveMaterialTextureBindingResolveFailureCount, 0u);

    const auto materialDiagnostics =
        renderer.GetMaterialSystem().GetDiagnostics();
    EXPECT_GE(materialDiagnostics.TextureAssetResolveCount, 1u);
    EXPECT_EQ(materialDiagnostics.TextureAssetFallbackResolveCount, 0u)
        << "Generated texture binding resolved through fallback instead of the uploaded texture.";

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Generated-UV texture smoke did not record a backbuffer readback copy.";

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);

    const std::uint32_t centerX = static_cast<std::uint32_t>(extent.Width / 2);
    const std::uint32_t centerY = static_cast<std::uint32_t>(extent.Height / 2);
    const RgbaPixel background = ReadPixel(
        bytes, backbufferFormat, bytesPerPixel, extent,
        static_cast<std::uint32_t>((extent.Width * 15) / 16),
        static_cast<std::uint32_t>((extent.Height * 15) / 16));
    const ForegroundSample foreground = FindMostForegroundPixel(
        bytes, backbufferFormat, bytesPerPixel, extent, centerX, centerY,
        16u, background);
    const int redBlue =
        static_cast<int>(foreground.Pixel.R) + static_cast<int>(foreground.Pixel.B);
    const int green = static_cast<int>(foreground.Pixel.G);

    EXPECT_GE(foreground.BackgroundDistance, 48)
        << "Imported OBJ did not contribute a distinguishable center-region pixel. "
        << "background=" << PixelText(background)
        << " foreground=" << PixelText(foreground.Pixel)
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
    EXPECT_GT(redBlue, green * 2 + 48)
        << "Imported OBJ center-region pixel is not magenta-hued; generated albedo texture "
        << "was not sampled through the material path. foreground="
        << PixelText(foreground.Pixel)
        << " background=" << PixelText(background);
    EXPECT_GT(redBlue, 80)
        << "Imported OBJ sampled the generated texture's zero-UV texel or no texture at all. "
        << "foreground=" << PixelText(foreground.Pixel)
        << " background=" << PixelText(background);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

// --- BUG-026B: Vulkan click-pick readback round trip -----------------------

namespace
{
constexpr std::uint32_t kBug026MaxFrames = 24u;
constexpr float kBug026PlaneTolerance = 0.05f;

class ClickPickRoundTripApp final : public IApplication
{
public:
    void SetTarget(
        const EntityHandle entity,
        const glm::vec3 worldPoint,
        std::string targetName,
        const std::uint32_t postBackgroundSettleFrames = 0u)
    {
        m_Triangle = entity;
        m_TargetWorldPoint = worldPoint;
        m_TargetName = std::move(targetName);
        m_UseFocusedCameraProjection = true;
        m_PostBackgroundSettleFrames = postBackgroundSettleFrames;
    }

    void OnInitialize(Engine& engine) override
    {
        if (m_Triangle == Extrinsic::ECS::InvalidEntityHandle)
        {
            m_Triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
        }
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Triangle == Extrinsic::ECS::InvalidEntityHandle ||
            !engine.Worlds().Get(engine.ActiveWorld())->IsValid(m_Triangle))
        {
            FailureReason = m_TargetName +
                " was not present while driving the click-pick smoke.";
            engine.RequestExit();
            return;
        }

        const Extrinsic::Core::Extent2D extent = engine.GetDevice().GetBackbufferExtent();
        if (extent.Width == 0u || extent.Height == 0u)
        {
            FailureReason = "Backbuffer extent was zero while driving the click-pick smoke.";
            engine.RequestExit();
            return;
        }

        switch (m_Phase)
        {
        case Phase::SubmitTriangleClick:
        {
            if (!engine.GetDevice().IsOperational())
            {
                break;
            }
            if (m_UseFocusedCameraProjection)
            {
                const auto projected =
                    ProjectMainCameraPixel(engine, m_TargetWorldPoint, extent);
                if (!projected.has_value())
                {
                    FailureReason = "Could not project " + m_TargetName +
                        " through the focused main camera.";
                    engine.RequestExit();
                    return;
                }
                TrianglePixel = *projected;
            }
            else
            {
                TrianglePixel = ProjectReferenceCameraPixel(
                    glm::vec3{0.0f, 0.0f, 0.0f}, extent);
            }
            Selection(engine).RequestClickPick(TrianglePixel.first, TrianglePixel.second);
            TriangleClickSubmitted = true;
            m_Phase = Phase::AwaitTriangleHit;
            break;
        }
        case Phase::AwaitTriangleHit:
            if (ObserveTriangleHit(engine))
            {
                const auto diagnostics =
                    Selection(engine).GetDiagnostics();
                m_NoHitsBeforeBackground = diagnostics.NoHits;
                BackgroundPixel = FarBackgroundPixel(extent);
                Selection(engine).RequestClickPick(BackgroundPixel.first, BackgroundPixel.second);
                BackgroundClickSubmitted = true;
                m_Phase = Phase::AwaitBackgroundNoHit;
            }
            break;
        case Phase::AwaitBackgroundNoHit:
            if (ObserveBackgroundNoHit(engine))
            {
                BackgroundNoHitObserved = true;
                if (m_PostBackgroundSettleFrames == 0u)
                {
                    m_Phase = Phase::Done;
                    engine.RequestExit();
                }
                else
                {
                    m_Phase = Phase::SettleAfterBackground;
                }
            }
            break;
        case Phase::SettleAfterBackground:
            ++m_BackgroundSettleFrames;
            if (m_BackgroundSettleFrames >= m_PostBackgroundSettleFrames)
            {
                m_Phase = Phase::Done;
                engine.RequestExit();
            }
            break;
        case Phase::Done:
            engine.RequestExit();
            break;
        }

        if (m_Frames >= kBug026MaxFrames && m_Phase != Phase::Done)
        {
            FailureReason = "Timed out waiting for Vulkan click-pick readbacks to complete.";
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override {}

    bool TriangleClickSubmitted{false};
    bool TriangleHitObserved{false};
    bool BackgroundClickSubmitted{false};
    bool BackgroundNoHitObserved{false};
    std::string FailureReason;
    std::pair<std::uint32_t, std::uint32_t> TrianglePixel{};
    std::pair<std::uint32_t, std::uint32_t> BackgroundPixel{};
    std::optional<Extrinsic::Runtime::PrimitiveSelectionResult> TriangleHitResult{};

private:
    enum class Phase
    {
        SubmitTriangleClick,
        AwaitTriangleHit,
        AwaitBackgroundNoHit,
        SettleAfterBackground,
        Done,
    };

    [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> FarBackgroundPixel(
        const Extrinsic::Core::Extent2D extent) noexcept
    {
        return {
            std::min(static_cast<std::uint32_t>(extent.Width - 1u),
                     static_cast<std::uint32_t>((extent.Width * 15u) / 16u)),
            std::min(static_cast<std::uint32_t>(extent.Height - 1u),
                     static_cast<std::uint32_t>((extent.Height * 15u) / 16u)),
        };
    }

    [[nodiscard]] bool ObserveTriangleHit(Engine& engine)
    {
        auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
        auto& selection = Selection(engine);
        const std::uint32_t triangleId =
            Extrinsic::Runtime::SelectionController::ToStableEntityId(m_Triangle);

        const std::span<const std::uint32_t> selected = selection.SelectedStableIds();
        if (selection.SelectedCount() != 1u || selected.size() != 1u || selected[0] != triangleId)
        {
            return false;
        }
        if (!scene.Raw().all_of<ECSC::Selection::SelectedTag>(m_Triangle))
        {
            return false;
        }

        const auto& primitive =
            Interaction(engine).LastRefinedPrimitive();
        if (!primitive.has_value())
        {
            return false;
        }
        const Extrinsic::Runtime::PrimitiveSelectionResult& result = *primitive;
        if (!result.Resolved() ||
            result.EntityId != triangleId ||
            result.Domain != gs::Domain::Mesh ||
            result.Kind != Extrinsic::Runtime::RefinedPrimitiveKind::Face ||
            result.FaceId == Extrinsic::Runtime::kInvalidPrimitiveIndex ||
            result.EdgeId == Extrinsic::Runtime::kInvalidPrimitiveIndex ||
            result.VertexId == Extrinsic::Runtime::kInvalidPrimitiveIndex ||
            !result.CursorFromDepth ||
            result.Depth >= 0.999f ||
            std::abs(result.WorldCursor.z) > kBug026PlaneTolerance ||
            std::abs(result.LocalCursor.z) > kBug026PlaneTolerance)
        {
            return false;
        }

        TriangleHitResult = result;
        TriangleHitObserved = true;
        return true;
    }

    [[nodiscard]] bool ObserveBackgroundNoHit(Engine& engine) const
    {
        const auto& selection = Selection(engine);
        const auto diagnostics = selection.GetDiagnostics();
        return diagnostics.NoHits > m_NoHitsBeforeBackground &&
               selection.SelectedCount() == 0u &&
               !engine.Worlds().Get(engine.ActiveWorld())->Raw().all_of<ECSC::Selection::SelectedTag>(m_Triangle) &&
               !Interaction(engine).LastRefinedPrimitive().has_value();
    }

    EntityHandle m_Triangle{Extrinsic::ECS::InvalidEntityHandle};
    glm::vec3 m_TargetWorldPoint{0.0f};
    std::string m_TargetName{"ReferenceTriangle"};
    Phase m_Phase{Phase::SubmitTriangleClick};
    std::uint32_t m_Frames{0u};
    std::uint32_t m_NoHitsBeforeBackground{0u};
    std::uint32_t m_PostBackgroundSettleFrames{0u};
    std::uint32_t m_BackgroundSettleFrames{0u};
    bool m_UseFocusedCameraProjection{false};
};
} // namespace

TEST(RuntimeSandboxAcceptanceGpuSmoke, ClickPickReadbackSelectsReferenceTriangleAndBackgroundClears)
{
    auto app = std::make_unique<ClickPickRoundTripApp>();
    auto* appPtr = app.get();
    auto bootstrap = BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(*engine.Worlds().Get(engine.ActiveWorld()), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(*engine.Worlds().Get(engine.ActiveWorld()), triangle);

    const Extrinsic::Core::Extent2D extent = engine.GetDevice().GetBackbufferExtent();
    if (extent.Width == 0u || extent.Height == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer extent cannot support click-pick smoke coordinates.";
    }

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan for the BUG-026B click-pick smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    ASSERT_TRUE(appPtr->FailureReason.empty()) << appPtr->FailureReason;
    ASSERT_TRUE(appPtr->TriangleClickSubmitted)
        << "The click-pick smoke never submitted its center-pixel triangle click.";
    ASSERT_TRUE(appPtr->TriangleHitObserved)
        << "The Vulkan pick readback did not select ReferenceTriangle before timeout. "
        << "triangle pixel=(" << appPtr->TrianglePixel.first << "," << appPtr->TrianglePixel.second << ") "
        << "background pixel=(" << appPtr->BackgroundPixel.first << "," << appPtr->BackgroundPixel.second << ")";
    ASSERT_TRUE(appPtr->TriangleHitResult.has_value())
        << "The triangle click selected the entity but did not publish a refined primitive.";
    ASSERT_TRUE(appPtr->BackgroundClickSubmitted)
        << "The smoke observed the triangle hit but never submitted its background click.";
    ASSERT_TRUE(appPtr->BackgroundNoHitObserved)
        << "The background pick did not publish a no-hit clear before timeout.";

    const Extrinsic::Runtime::PrimitiveSelectionResult& hit = *appPtr->TriangleHitResult;
    EXPECT_EQ(hit.Status, Extrinsic::Runtime::PrimitiveRefineStatus::Success);
    EXPECT_EQ(hit.EntityId, Extrinsic::Runtime::SelectionController::ToStableEntityId(triangle));
    EXPECT_EQ(hit.Domain, gs::Domain::Mesh);
    EXPECT_EQ(hit.Kind, Extrinsic::Runtime::RefinedPrimitiveKind::Face);
    EXPECT_EQ(hit.FaceId, 0u);
    EXPECT_NE(hit.EdgeId, Extrinsic::Runtime::kInvalidPrimitiveIndex);
    EXPECT_NE(hit.VertexId, Extrinsic::Runtime::kInvalidPrimitiveIndex);
    EXPECT_TRUE(hit.CursorFromDepth);
    EXPECT_GT(hit.Depth, 0.0f);
    EXPECT_LT(hit.Depth, 0.999f);
    EXPECT_NEAR(hit.WorldCursor.z, 0.0f, kBug026PlaneTolerance);
    EXPECT_NEAR(hit.LocalCursor.z, 0.0f, kBug026PlaneTolerance);

    const auto diagnostics = Selection(engine).GetDiagnostics();
    EXPECT_EQ(diagnostics.ClickRequestsSubmitted, 2u);
    EXPECT_EQ(diagnostics.PicksDrained, 2u);
    EXPECT_EQ(diagnostics.Hits, 1u);
    EXPECT_EQ(diagnostics.NoHits, 1u);
    EXPECT_EQ(Selection(engine).SelectedCount(), 0u);
    EXPECT_FALSE(engine.Worlds().Get(engine.ActiveWorld())->Raw().all_of<ECSC::Selection::SelectedTag>(triangle));
    EXPECT_FALSE(
        Interaction(engine).LastRefinedPrimitive().has_value());
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);

    engine.Shutdown();
}

// BUG-094 Slice C — operational proof that the model-scene route preserves
// transformed mesh instances all the way through sandbox authoring, surface
// rendering, and Vulkan selection readback. The target is the second instance:
// import completion initially selects the first, so observing the second as the
// sole selected entity proves the click readback changed authoritative state.
TEST(RuntimeSandboxAcceptanceGpuSmoke, ImportedModelSceneIsVisibleAndClickPickable)
{
    auto app = std::make_unique<ClickPickRoundTripApp>();
    auto* appPtr = app.get();
    auto bootstrap = BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const std::filesystem::path fixturePath =
        std::filesystem::path{__FILE__}
            .parent_path()
            .parent_path()
            .parent_path()
            .parent_path() /
        "assets/models/__test_bug094_instanced_triangle.gltf";
    ASSERT_TRUE(std::filesystem::exists(fixturePath))
        << "Missing checked-in BUG-094 model-scene fixture: "
        << fixturePath.string();

    auto imported = RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).ImportAssetFromPath(
        Extrinsic::Runtime::RuntimeAssetImportRequest{
            .Path = fixturePath.string(),
            .PayloadKind = Assets::AssetPayloadKind::ModelScene,
        });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_TRUE(imported->MaterializedModelScene);
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 2u);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    std::vector<EntityHandle> instances{};
    raw.view<ECSC::MetaData>().each(
        [&](const EntityHandle entity, const ECSC::MetaData& metadata)
        {
            if (metadata.EntityName != "BUG094SharedTriangle")
            {
                return;
            }
            const gs::ConstSourceView source = gs::BuildConstView(raw, entity);
            if (source.Valid() && source.ActiveDomain == gs::Domain::Mesh)
            {
                instances.push_back(entity);
            }
        });
    ASSERT_EQ(instances.size(), 2u);
    std::sort(
        instances.begin(),
        instances.end(),
        [&](const EntityHandle lhs, const EntityHandle rhs)
        {
            return raw.get<ECSC::Culling::World::Bounds>(lhs)
                       .WorldBoundingSphere.Center.x <
                   raw.get<ECSC::Culling::World::Bounds>(rhs)
                       .WorldBoundingSphere.Center.x;
        });

    for (const EntityHandle instance : instances)
    {
        ASSERT_TRUE(engine.Worlds().Get(engine.ActiveWorld())->IsValid(instance));
        ASSERT_TRUE((raw.all_of<
            ECSC::Transform::Component,
            ECSC::Transform::WorldMatrix,
            ECSC::Culling::Local::Bounds,
            ECSC::Culling::World::Bounds,
            ECSC::Selection::SelectableTag,
            G::RenderSurface,
            G::VisualizationConfig,
            gs::Vertices,
            gs::Edges,
            gs::Halfedges,
            gs::Faces>(instance)));
        const gs::ConstSourceView source = gs::BuildConstView(raw, instance);
        ASSERT_TRUE(source.Valid());
        EXPECT_EQ(source.ActiveDomain, gs::Domain::Mesh);
        EXPECT_EQ(source.VerticesAlive(), 3u);
        EXPECT_EQ(source.FacesAlive(), 1u);
        EXPECT_EQ(raw.get<G::VisualizationConfig>(instance).Source,
                  G::VisualizationConfig::ColorSource::Material);
    }

    const EntityHandle firstInstance = instances[0];
    const EntityHandle targetInstance = instances[1];
    const auto& firstWorld =
        raw.get<ECSC::Transform::WorldMatrix>(firstInstance).Matrix;
    const auto& targetWorld =
        raw.get<ECSC::Transform::WorldMatrix>(targetInstance).Matrix;
    EXPECT_NEAR(firstWorld[3].x, -1.0f, 0.0001f);
    EXPECT_NEAR(targetWorld[3].x, 1.0f, 0.0001f);
    EXPECT_NE(firstWorld[3].x, targetWorld[3].x);
    EXPECT_TRUE(Selection(engine).IsSelected(firstInstance));
    EXPECT_FALSE(Selection(engine).IsSelected(targetInstance));

    const glm::vec4 targetWorldPoint4 =
        targetWorld * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    ASSERT_TRUE(std::isfinite(targetWorldPoint4.x));
    ASSERT_TRUE(std::isfinite(targetWorldPoint4.y));
    ASSERT_TRUE(std::isfinite(targetWorldPoint4.z));
    ASSERT_GT(std::abs(targetWorldPoint4.w), 0.000001f);
    const glm::vec3 targetWorldPoint =
        glm::vec3{targetWorldPoint4} / targetWorldPoint4.w;
    // Two clear frames after the background no-hit keep the final color
    // readback free of the target's selection outline.
    appPtr->SetTarget(
        targetInstance,
        targetWorldPoint,
        "BUG094RightInstance primitive",
        2u);

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat =
        device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel =
        Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width == 0u || extent.Height == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Backbuffer format or extent cannot support the BUG-094 readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer =
        device.CreateBuffer(Extrinsic::RHI::BufferDesc{
            .SizeBytes = readbackSize,
            .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
            .HostVisible = true,
            .DebugName = "Sandbox.Bug094ModelScene.Readback",
        });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);
    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(
            Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE()
            << "BUG-094 model-scene smoke did not reach operational Vulkan: status="
            << ToString(run.Status.Code)
            << " reason=" << ToString(run.Status.Reason)
            << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    ASSERT_TRUE(appPtr->FailureReason.empty()) << appPtr->FailureReason;
    ASSERT_TRUE(appPtr->TriangleClickSubmitted)
        << "The model-scene smoke never submitted its projected primitive click.";
    ASSERT_TRUE(appPtr->TriangleHitObserved)
        << "The Vulkan pick readback did not select the second model instance. "
        << "target pixel=(" << appPtr->TrianglePixel.first << ","
        << appPtr->TrianglePixel.second << ") background pixel=("
        << appPtr->BackgroundPixel.first << ","
        << appPtr->BackgroundPixel.second << ")";
    ASSERT_TRUE(appPtr->TriangleHitResult.has_value());
    ASSERT_TRUE(appPtr->BackgroundClickSubmitted);
    ASSERT_TRUE(appPtr->BackgroundNoHitObserved);

    const Extrinsic::Runtime::PrimitiveSelectionResult& hit =
        *appPtr->TriangleHitResult;
    EXPECT_EQ(hit.Status,
              Extrinsic::Runtime::PrimitiveRefineStatus::Success);
    EXPECT_EQ(
        hit.EntityId,
        Extrinsic::Runtime::SelectionController::ToStableEntityId(
            targetInstance));
    EXPECT_EQ(hit.Domain, gs::Domain::Mesh);
    EXPECT_EQ(hit.Kind, Extrinsic::Runtime::RefinedPrimitiveKind::Face);
    EXPECT_EQ(hit.FaceId, 0u);
    EXPECT_TRUE(hit.CursorFromDepth);
    EXPECT_GT(hit.Depth, 0.0f);
    EXPECT_LT(hit.Depth, 0.999f);

    const auto selectionDiagnostics =
        Selection(engine).GetDiagnostics();
    EXPECT_EQ(selectionDiagnostics.ClickRequestsSubmitted, 2u);
    EXPECT_EQ(selectionDiagnostics.PicksDrained, 2u);
    EXPECT_EQ(selectionDiagnostics.Hits, 1u);
    EXPECT_EQ(selectionDiagnostics.NoHits, 1u);
    EXPECT_EQ(selectionDiagnostics.NonSelectableHitsRejected, 0u);
    EXPECT_EQ(Selection(engine).SelectedCount(), 0u);
    EXPECT_FALSE(raw.all_of<ECSC::Selection::SelectedTag>(targetInstance));

    const auto& extraction = engine.GetLastRenderExtractionStats();
    EXPECT_GE(extraction.CandidateRenderableCount, 2u);
    EXPECT_GE(extraction.MeshGeometryUploads + extraction.MeshGeometryReuseHits,
              2u);
    EXPECT_EQ(extraction.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(extraction.MeshGeometryInvalidTopology, 0u);
    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "DepthPrepass"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "SurfacePass"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u);

    const GpuInstanceConfigReadback targetGpu =
        ReadVisibleInstanceConfigByEntityId(
            device,
            renderer,
            Extrinsic::Runtime::SelectionController::ToStableEntityId(
                targetInstance),
            Extrinsic::RHI::GpuRender_Surface);
    EXPECT_TRUE(targetGpu.Found)
        << "The imported target instance did not reach the visible GPU surface lane.";

    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(readbackSize),
        0u);
    device.ReadBuffer(readbackBuffer, bytes.data(), readbackSize, 0u);
    const RgbaPixel background = ReadPixel(
        bytes,
        backbufferFormat,
        bytesPerPixel,
        extent,
        static_cast<std::uint32_t>((extent.Width * 15u) / 16u),
        static_cast<std::uint32_t>((extent.Height * 15u) / 16u));
    const ForegroundSample targetPixel = FindMostForegroundPixel(
        bytes,
        backbufferFormat,
        bytesPerPixel,
        extent,
        appPtr->TrianglePixel.first,
        appPtr->TrianglePixel.second,
        8u,
        background);
    EXPECT_GT(targetPixel.BackgroundDistance, 48)
        << "The transformed model instance did not contribute a visible pixel at "
        << "its focused-camera projection. target=" << PixelText(targetPixel.Pixel)
        << " background=" << PixelText(background)
        << " projected=(" << appPtr->TrianglePixel.first << ","
        << appPtr->TrianglePixel.second << ") extent="
        << extent.Width << "x" << extent.Height
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(
        Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(RuntimeSandboxAcceptanceGpuSmoke, HierarchySelectionKeepsDefaultSandboxVisibleWithOutline)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    EntityHandle triangle = FindEntityByName(*engine.Worlds().Get(engine.ActiveWorld()), "ReferenceTriangle");
    ASSERT_NE(triangle, Extrinsic::ECS::InvalidEntityHandle);
    ASSERT_TRUE(Selection(engine).SetSelectedEntity(
        *engine.Worlds().Get(engine.ActiveWorld()), triangle));
    ASSERT_EQ(Selection(engine).SelectedCount(), 1u);
    ASSERT_EQ(Selection(engine).SelectedStableIds().size(), 1u);
    EXPECT_EQ(Selection(engine).SelectedStableIds()[0],
              Extrinsic::Runtime::SelectionController::ToStableEntityId(triangle));

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D extent = device.GetBackbufferExtent();
    if (bytesPerPixel < 4u || extent.Width == 0u || extent.Height == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format or extent cannot support rgba-style smoke readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(extent.Width) *
        static_cast<std::uint64_t>(extent.Height);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "Sandbox.HierarchySelection.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveAcceptanceAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "ExtrinsicSandbox default config did not reach operational Vulkan with hierarchy selection: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);
    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(FindPassStatus(run.Stats, "SelectionOutlinePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_GE(run.Stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Sandbox hierarchy-selection readback triplet did not record on any operational frame.";

    const std::uint64_t nonBlackPixels =
        CountNonBlackRgbPixels(device, readbackBuffer, readbackSize, bytesPerPixel);
    const std::uint64_t totalPixels =
        static_cast<std::uint64_t>(extent.Width) * static_cast<std::uint64_t>(extent.Height);
    EXPECT_GT(nonBlackPixels, totalPixels / 2u)
        << "Sandbox hierarchy selection turned the frame black; only "
        << nonBlackPixels << "/" << totalPixels << " pixels were non-black. "
        << "pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

// --- RUNTIME-129: real-app object-space normal bake ---------------------

namespace
{
constexpr std::uint32_t kRuntime129BakeExtent = 64u;
constexpr std::uint32_t kRuntime129PixelBytes = 4u;
constexpr std::uint32_t kRuntime129MaxFrames = 72u;

class Runtime129ObjectSpaceNormalBakeApp final : public IApplication
{
public:
    void OnInitialize(Engine& engine) override
    {
        m_Device = &engine.GetDevice();
        m_Renderer = &engine.GetRenderer();
        m_Cache =
            engine.Services().Find<Extrinsic::Graphics::GpuAssetCache>();
        m_Extraction =
            engine.Services().Find<RT::RenderExtractionCache>();
        m_Scene = engine.Worlds().Get(engine.ActiveWorld());

        if (m_Device == nullptr ||
            m_Renderer == nullptr ||
            m_Cache == nullptr ||
            m_Extraction == nullptr ||
            m_Scene == nullptr)
        {
            Fail(
                "Sandbox composition did not publish the services required by the RUNTIME-129 smoke.");
            return;
        }

        m_Participant = engine.Jobs().RegisterGpuQueueParticipant(
            RT::GpuQueueParticipantDesc{
                .DebugName =
                    "RuntimeSandboxAcceptanceGpuSmoke.ObjectSpaceNormalBakeReadback",
                .Scope = engine.ActiveWorld(),
                .RecordFrameCommands =
                    [this](Extrinsic::RHI::ICommandContext& commandContext)
                    {
                        RecordReadback(commandContext);
                    },
                .DrainCompletedTransfers =
                    [this]
                    {
                        if (ReadbackRecorded)
                            MaintenanceDrainObserved = true;
                    },
                .HasInFlightWork =
                    [this]
                    {
                        return m_Configured &&
                               m_ReadbackBuffer.IsValid();
                    },
                .ShutdownAfterDeviceIdle =
                    [this]
                    {
                        DeviceIdleObserved = true;
                    },
            });
        if (!m_Participant.IsValid())
        {
            Fail(
                "JobService rejected the RUNTIME-129 readback participant.");
        }
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++Frames;
        if (ReadbackRecorded || !FailureReason.empty())
        {
            engine.RequestExit();
            return;
        }

        if (Frames > kRuntime129MaxFrames)
        {
            TimedOut = true;
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override
    {
        m_Participant = {};
    }

    void ConfigureTarget(
        const EntityHandle entity,
        const std::uint32_t stableEntityId,
        const Extrinsic::Graphics::MaterialTextureAssetBindings&
            expectedBindings,
        const Extrinsic::RHI::BufferHandle readbackBuffer) noexcept
    {
        m_Target = entity;
        m_StableEntityId = stableEntityId;
        m_ExpectedBindings = expectedBindings;
        m_ReadbackBuffer = readbackBuffer;
        m_Configured =
            m_Target != Extrinsic::ECS::InvalidEntityHandle &&
            m_StableEntityId != 0u &&
            m_ReadbackBuffer.IsValid();
    }

    void Detach(Engine& engine)
    {
        if (!m_Participant.IsValid())
            return;

        Extrinsic::RHI::IDevice* const device = m_Device;
        engine.Jobs().UnregisterGpuQueueParticipant(
            m_Participant,
            [device]
            {
                if (device != nullptr)
                    device->WaitIdle();
            });
        m_Participant = {};
    }

    [[nodiscard]] Assets::AssetId GeneratedNormalTexture() const noexcept
    {
        return m_GeneratedNormalTexture;
    }

    [[nodiscard]] std::uint64_t ReadyGeneration() const noexcept
    {
        return m_ReadyGeneration;
    }

    [[nodiscard]] std::uint32_t SurfaceFirstIndex() const noexcept
    {
        return m_SurfaceFirstIndex;
    }

    bool ReadbackRecorded{false};
    bool MaintenanceDrainObserved{false};
    bool DeviceIdleObserved{false};
    bool TimedOut{false};
    bool PreservedMaterialObserved{false};
    bool ObjectSpaceBindingObserved{false};
    std::uint32_t Frames{0u};
    std::string FailureReason{};

private:
    void Fail(std::string reason)
    {
        if (FailureReason.empty())
            FailureReason = std::move(reason);
    }

    void RecordReadback(
        Extrinsic::RHI::ICommandContext& commandContext)
    {
        if (!m_Configured ||
            ReadbackRecorded ||
            !FailureReason.empty())
        {
            return;
        }

        if (m_Device == nullptr || !m_Device->IsOperational())
        {
            Fail(
                "Promoted Vulkan became non-operational before the RUNTIME-129 readback.");
            return;
        }
        if (m_Scene == nullptr || !m_Scene->IsValid(m_Target))
        {
            Fail(
                "Imported target entity became stale before the RUNTIME-129 readback.");
            return;
        }

        const std::optional<
            Extrinsic::Graphics::MaterialTextureAssetBindings>
            bindings =
                m_Extraction->GetMaterialTextureAssetBindings(
                    m_StableEntityId);
        if (!bindings.has_value() || !bindings->Normal.IsValid())
        {
            return;
        }

        ObjectSpaceBindingObserved =
            bindings->NormalSpace ==
            Extrinsic::Graphics::MaterialNormalTextureSpace::
                ObjectSpaceNormal;
        if (!ObjectSpaceBindingObserved)
        {
            Fail(
                "Generated normal texture bound without object-space normal metadata.");
            return;
        }

        PreservedMaterialObserved =
            bindings->Albedo == m_ExpectedBindings.Albedo &&
            bindings->MetallicRoughness ==
                m_ExpectedBindings.MetallicRoughness &&
            bindings->Emissive == m_ExpectedBindings.Emissive;
        if (!PreservedMaterialObserved)
        {
            Fail(
                "Exact-ready normal binding replaced an unrelated material texture slot.");
            return;
        }

        m_GeneratedNormalTexture = bindings->Normal;
        if (m_Cache->GetState(m_GeneratedNormalTexture) !=
            Extrinsic::Graphics::GpuAssetState::Ready)
        {
            Fail(
                "Material referenced a generated normal whose cache state was not Ready.");
            return;
        }

        const auto readyView =
            m_Cache->GetView(m_GeneratedNormalTexture);
        if (!readyView.has_value() ||
            readyView->Kind !=
                Extrinsic::Graphics::GpuAssetKind::Texture ||
            !readyView->Texture.IsValid() ||
            readyView->Generation == 0u)
        {
            Fail(
                "Ready generated normal did not expose an exact nonzero texture generation.");
            return;
        }

        const Extrinsic::RHI::TextureDesc* const textureDesc =
            m_Renderer->GetTextureManager().GetDesc(
                readyView->Texture);
        const bool hasTransferSourceUsage =
            textureDesc != nullptr &&
            (static_cast<std::uint32_t>(textureDesc->Usage) &
             static_cast<std::uint32_t>(
                 Extrinsic::RHI::TextureUsage::TransferSrc)) != 0u;
        if (textureDesc == nullptr ||
            textureDesc->Width != kRuntime129BakeExtent ||
            textureDesc->Height != kRuntime129BakeExtent ||
            textureDesc->Fmt !=
                Extrinsic::RHI::Format::RGBA8_UNORM ||
            !hasTransferSourceUsage)
        {
            Fail(
                "Generated normal texture did not expose the production 64x64 RGBA8 TransferSrc contract.");
            return;
        }

        const auto renderable =
            m_Extraction->FindGpuRenderableAvailability(
                m_StableEntityId);
        if (!renderable.has_value() ||
            !renderable->HasRenderable ||
            !renderable->Surface.HasGeometry)
        {
            Fail(
                "Imported target had no live GPU surface when its normal bake became ready.");
            return;
        }

        Extrinsic::Graphics::GpuGeometryResidencyView residency{};
        if (!m_Renderer->GetGpuWorld().TryGetGeometryResidencyView(
                renderable->Surface.Geometry,
                residency))
        {
            Fail(
                "Imported target surface lost its live GpuGeometryResidencyView before readback.");
            return;
        }
        m_SurfaceFirstIndex = residency.Record.SurfaceFirstIndex;
        if (m_SurfaceFirstIndex == 0u)
        {
            Fail(
                "Target normal bake did not retain a nonzero shared-index-buffer slice.");
            return;
        }

        m_ReadyGeneration = readyView->Generation;
        commandContext.TextureBarrier(
            readyView->Texture,
            Extrinsic::RHI::TextureLayout::ShaderReadOnly,
            Extrinsic::RHI::TextureLayout::TransferSrc);
        commandContext.CopyTextureToBuffer(
            readyView->Texture,
            Extrinsic::RHI::TextureLayout::TransferSrc,
            0u,
            0u,
            m_ReadbackBuffer,
            0u,
            0u,
            0u,
            textureDesc->Width,
            textureDesc->Height);
        commandContext.TextureBarrier(
            readyView->Texture,
            Extrinsic::RHI::TextureLayout::TransferSrc,
            Extrinsic::RHI::TextureLayout::ShaderReadOnly);
        ReadbackRecorded = true;
    }

    Extrinsic::RHI::IDevice* m_Device{nullptr};
    Extrinsic::Graphics::IRenderer* m_Renderer{nullptr};
    Extrinsic::Graphics::GpuAssetCache* m_Cache{nullptr};
    RT::RenderExtractionCache* m_Extraction{nullptr};
    Registry* m_Scene{nullptr};
    RT::GpuQueueParticipantHandle m_Participant{};
    EntityHandle m_Target{Extrinsic::ECS::InvalidEntityHandle};
    std::uint32_t m_StableEntityId{0u};
    Extrinsic::Graphics::MaterialTextureAssetBindings
        m_ExpectedBindings{};
    Extrinsic::RHI::BufferHandle m_ReadbackBuffer{};
    Assets::AssetId m_GeneratedNormalTexture{};
    std::uint64_t m_ReadyGeneration{0u};
    std::uint32_t m_SurfaceFirstIndex{0u};
    bool m_Configured{false};
};

[[nodiscard]] std::optional<
    Extrinsic::Graphics::GpuGeometryResidencyView>
FindRuntime129Residency(
    RT::RenderExtractionCache& extraction,
    Extrinsic::Graphics::IRenderer& renderer,
    const std::uint32_t stableEntityId)
{
    const auto renderable =
        extraction.FindGpuRenderableAvailability(stableEntityId);
    if (!renderable.has_value() ||
        !renderable->HasRenderable ||
        !renderable->Surface.HasGeometry)
    {
        return std::nullopt;
    }

    Extrinsic::Graphics::GpuGeometryResidencyView residency{};
    if (!renderer.GetGpuWorld().TryGetGeometryResidencyView(
            renderable->Surface.Geometry,
            residency))
    {
        return std::nullopt;
    }
    return residency;
}

struct Runtime129LiveBakeMesh
{
    std::vector<
        Extrinsic::Graphics::ObjectSpaceNormalTextureBakeVertex>
        Vertices{};
    std::vector<
        Extrinsic::Graphics::ObjectSpaceNormalTextureBakeTriangle>
        Triangles{};
    std::string Diagnostic{};

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return !Vertices.empty() && !Triangles.empty() &&
               Diagnostic.empty();
    }
};

[[nodiscard]] Runtime129LiveBakeMesh
SnapshotRuntime129LiveBakeMesh(
    const Registry& scene,
    const EntityHandle entity)
{
    Runtime129LiveBakeMesh snapshot{};
    const gs::ConstSourceView view =
        gs::BuildConstView(scene.Raw(), entity);
    if (!view.Valid() || view.VertexSource == nullptr)
    {
        snapshot.Diagnostic =
            "target GeometrySources view was invalid";
        return snapshot;
    }

    const auto texcoords =
        view.VertexSource->Properties.Get<glm::vec2>(
            "v:texcoord");
    const auto normals =
        view.VertexSource->Properties.Get<glm::vec3>(
            pn::kNormal);
    if (!texcoords || !normals ||
        texcoords.Vector().empty() ||
        texcoords.Vector().size() != normals.Vector().size())
    {
        snapshot.Diagnostic =
            "target resolved texcoord/normal streams were absent or mismatched";
        return snapshot;
    }

    std::vector<std::uint32_t> surfaceIndices{};
    std::vector<std::uint32_t> triangleToFace{};
    const RT::MeshPackStatus topologyStatus =
        RT::BuildSurfaceTriangleTopology(
            view,
            surfaceIndices,
            triangleToFace);
    if (topologyStatus != RT::MeshPackStatus::Success ||
        surfaceIndices.empty() ||
        surfaceIndices.size() % 3u != 0u)
    {
        snapshot.Diagnostic =
            std::string{
                "target canonical topology could not be reconstructed: "} +
            RT::DebugNameForMeshPackStatus(topologyStatus);
        return snapshot;
    }

    snapshot.Vertices.reserve(texcoords.Vector().size());
    for (std::size_t index = 0u;
         index < texcoords.Vector().size();
         ++index)
    {
        snapshot.Vertices.push_back(
            Extrinsic::Graphics::
                ObjectSpaceNormalTextureBakeVertex{
                    .Uv = texcoords.Vector()[index],
                    .Normal = normals.Vector()[index],
                });
    }

    snapshot.Triangles.reserve(surfaceIndices.size() / 3u);
    for (std::size_t index = 0u;
         index < surfaceIndices.size();
         index += 3u)
    {
        const std::uint32_t a = surfaceIndices[index + 0u];
        const std::uint32_t b = surfaceIndices[index + 1u];
        const std::uint32_t c = surfaceIndices[index + 2u];
        if (a >= snapshot.Vertices.size() ||
            b >= snapshot.Vertices.size() ||
            c >= snapshot.Vertices.size())
        {
            snapshot.Vertices.clear();
            snapshot.Triangles.clear();
            snapshot.Diagnostic =
                "target canonical topology referenced an invalid vertex";
            return snapshot;
        }
        snapshot.Triangles.push_back(
            Extrinsic::Graphics::
                ObjectSpaceNormalTextureBakeTriangle{
                    .A = a,
                    .B = b,
                    .C = c,
                });
    }
    return snapshot;
}

void ExpectRuntime129TargetNormalPixel(
    const RgbaPixel pixel,
    const std::string_view label)
{
    constexpr int kTolerance = 4;
    EXPECT_NEAR(static_cast<int>(pixel.R), 255, kTolerance)
        << label << " pixel=" << PixelText(pixel);
    EXPECT_NEAR(static_cast<int>(pixel.G), 128, kTolerance)
        << label << " pixel=" << PixelText(pixel);
    EXPECT_NEAR(static_cast<int>(pixel.B), 128, kTolerance)
        << label << " pixel=" << PixelText(pixel);
    EXPECT_NEAR(static_cast<int>(pixel.A), 255, kTolerance)
        << label << " pixel=" << PixelText(pixel);
    EXPECT_GT(pixel.R, 240u)
        << label
        << " encoded the decoy -X normal instead of the target +X normal: "
        << PixelText(pixel);
}
} // namespace

TEST(RuntimeSandboxAcceptanceGpuSmoke,
     ImportedObjectSpaceNormalBakeBindsAndReadsBackExactTargetSlice)
{
    auto app =
        std::make_unique<Runtime129ObjectSpaceNormalBakeApp>();
    auto* const appPtr = app.get();
    auto bootstrap =
        BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());

    // The Sandbox seed surface is intentionally made ineligible before the
    // first extraction. The decoy import therefore occupies the earlier live
    // shared-index slice and the target import must retain a later nonzero
    // SurfaceFirstIndex.
    const EntityHandle reference =
        FindEntityByName(scene, "ReferenceTriangle");
    ASSERT_NE(reference, Extrinsic::ECS::InvalidEntityHandle);
    scene.Raw().remove<G::RenderSurface>(reference);

    TempObjFile decoyObj{
        "intrinsic_runtime129_normal_bake_decoy",
        "v -0.75 -0.75 0\n"
        "v 0.75 -0.75 0\n"
        "v -0.75 0.75 0\n"
        "vt 0.25 0.25\n"
        "vt 0.75 0.25\n"
        "vt 0.25 0.75\n"
        "vn -1 0 0\n"
        "f 1/1/1 2/2/1 3/3/1\n",
    };
    TempObjFile targetObj{
        "intrinsic_runtime129_normal_bake_target",
        "v -0.75 -0.75 0\n"
        "v 0.75 -0.75 0\n"
        "v -0.75 0.75 0\n"
        "vt 0.25 0.25\n"
        "vt 0.75 0.25\n"
        "vt 0.25 0.75\n"
        "vn 1 0 0\n"
        "f 1/1/1 2/2/1 3/3/1\n",
    };

    RT::AssetImportPipeline& importPipeline =
        RequiredEngineService<RT::AssetImportPipeline>(engine);
    auto decoyImport = importPipeline.ImportAssetFromPath(
        RT::RuntimeAssetImportRequest{
            .Path = decoyObj.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(decoyImport.has_value())
        << static_cast<int>(decoyImport.error());
    ASSERT_EQ(decoyImport->PrimitiveEntitiesCreated, 1u);

    const EntityHandle decoyEntity =
        FindEntityByName(
            scene,
            decoyObj.Path.filename().string());
    ASSERT_NE(decoyEntity, Extrinsic::ECS::InvalidEntityHandle);
    const std::uint32_t decoyStableId =
        RT::SelectionController::ToStableEntityId(decoyEntity);
    ASSERT_NE(decoyStableId, 0u);

    RT::RenderExtractionCache& extraction =
        RequiredEngineService<RT::RenderExtractionCache>(engine);
    Extrinsic::Graphics::GpuAssetCache& cache =
        RequiredEngineService<
            Extrinsic::Graphics::GpuAssetCache>(engine);
    const RT::RuntimeRenderExtractionStats decoyExtraction =
        extraction.ExtractAndSubmit(
            scene,
            engine.GetRenderer(),
            &cache,
            0u,
            engine.ActiveWorld());
    ASSERT_EQ(decoyExtraction.MeshGeometryFailedPack, 0u);
    ASSERT_GE(decoyExtraction.MeshGeometryUploads, 1u);
    const auto decoyResidencyBeforeTarget =
        FindRuntime129Residency(
            extraction,
            engine.GetRenderer(),
            decoyStableId);
    ASSERT_TRUE(decoyResidencyBeforeTarget.has_value())
        << "The decoy must own a live shared-index slice before the target entity exists.";

    auto targetImport = importPipeline.ImportAssetFromPath(
        RT::RuntimeAssetImportRequest{
            .Path = targetObj.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(targetImport.has_value())
        << static_cast<int>(targetImport.error());
    ASSERT_EQ(targetImport->PrimitiveEntitiesCreated, 1u);

    const EntityHandle targetEntity =
        FindEntityByName(
            scene,
            targetObj.Path.filename().string());
    ASSERT_NE(targetEntity, Extrinsic::ECS::InvalidEntityHandle);

    const std::uint32_t targetStableId =
        RT::SelectionController::ToStableEntityId(targetEntity);
    ASSERT_NE(targetStableId, 0u);
    ASSERT_NE(decoyStableId, targetStableId);

    Assets::AssetService& assetService =
        RequiredEngineService<Assets::AssetService>(engine);
    const Assets::AssetTexture2DPayload preservedPayload =
        MakeGeneratedUvSmokeAlbedoPayload();
    const auto loadPreservedTexture =
        [&assetService, &preservedPayload](
            const std::string& path)
        {
            return assetService.Load<
                Assets::AssetTexture2DPayload>(
                path,
                [preservedPayload](
                    std::string_view,
                    Assets::AssetId)
                    -> Extrinsic::Core::Expected<
                        Assets::AssetTexture2DPayload>
                {
                    return preservedPayload;
                });
        };

    auto albedo = loadPreservedTexture(
        targetObj.Path.string() + ".preserved-albedo");
    auto metallicRoughness = loadPreservedTexture(
        targetObj.Path.string() +
        ".preserved-metallic-roughness");
    auto emissive = loadPreservedTexture(
        targetObj.Path.string() + ".preserved-emissive");
    ASSERT_TRUE(albedo.has_value())
        << static_cast<int>(albedo.error());
    ASSERT_TRUE(metallicRoughness.has_value())
        << static_cast<int>(metallicRoughness.error());
    ASSERT_TRUE(emissive.has_value())
        << static_cast<int>(emissive.error());

    const Extrinsic::Graphics::MaterialTextureAssetBindings
        preservedBindings{
            .Albedo = *albedo,
            .Normal = {},
            .MetallicRoughness = *metallicRoughness,
            .Emissive = *emissive,
            .NormalSpace =
                Extrinsic::Graphics::MaterialNormalTextureSpace::
                    TangentSpaceNormal,
        };
    extraction.SetMaterialTextureAssetBindings(
        targetStableId,
        preservedBindings);

    Extrinsic::RHI::IDevice& device = engine.GetDevice();
    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(kRuntime129BakeExtent) *
        static_cast<std::uint64_t>(kRuntime129BakeExtent) *
        kRuntime129PixelBytes;
    Extrinsic::RHI::BufferHandle readbackBuffer =
        device.CreateBuffer(
            Extrinsic::RHI::BufferDesc{
                .SizeBytes = readbackSize,
                .Usage =
                    Extrinsic::RHI::BufferUsage::TransferDst,
                .HostVisible = true,
                .DebugName =
                    "Sandbox.Runtime129ObjectSpaceNormalBake.Readback",
            });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        ADD_FAILURE()
            << "Operational Vulkan device failed to allocate the RUNTIME-129 readback buffer.";
        return;
    }

    appPtr->ConfigureTarget(
        targetEntity,
        targetStableId,
        preservedBindings,
        readbackBuffer);
    const AcceptanceRunCapture run =
        DriveAcceptanceAndCapture(engine);
    appPtr->Detach(engine);

    if (!run.DeviceOperational)
    {
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE()
            << "RUNTIME-129 Sandbox bake smoke lost operational Vulkan: status="
            << ToString(run.Status.Code)
            << " reason=" << ToString(run.Status.Reason)
            << ". pass statuses=["
            << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    const bool readbackRecorded = appPtr->ReadbackRecorded;
    const bool maintenanceDrainObserved =
        appPtr->MaintenanceDrainObserved;
    const bool deviceIdleObserved =
        appPtr->DeviceIdleObserved;
    const bool timedOut = appPtr->TimedOut;
    const bool preservedMaterialObserved =
        appPtr->PreservedMaterialObserved;
    const bool objectSpaceBindingObserved =
        appPtr->ObjectSpaceBindingObserved;
    const std::uint32_t frames = appPtr->Frames;
    const std::string failureReason =
        appPtr->FailureReason;
    const Assets::AssetId generatedNormal =
        appPtr->GeneratedNormalTexture();
    const std::uint64_t readyGeneration =
        appPtr->ReadyGeneration();
    const std::uint32_t targetFirstIndex =
        appPtr->SurfaceFirstIndex();

    const Extrinsic::Graphics::GpuAssetState finalCacheState =
        generatedNormal.IsValid()
            ? cache.GetState(generatedNormal)
            : Extrinsic::Graphics::GpuAssetState::NotRequested;
    std::optional<Extrinsic::Graphics::GpuAssetView>
        finalCacheView{};
    if (generatedNormal.IsValid())
    {
        const auto view = cache.GetView(generatedNormal);
        if (view.has_value())
            finalCacheView = *view;
    }
    const auto finalBindings =
        extraction.GetMaterialTextureAssetBindings(
            targetStableId);
    const auto decoyResidency =
        FindRuntime129Residency(
            extraction,
            engine.GetRenderer(),
            decoyStableId);
    const auto targetResidency =
        FindRuntime129Residency(
            extraction,
            engine.GetRenderer(),
            targetStableId);
    const Runtime129LiveBakeMesh targetBakeMesh =
        SnapshotRuntime129LiveBakeMesh(
            scene,
            targetEntity);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(readbackSize),
        0u);
    device.ReadBuffer(
        readbackBuffer,
        pixels.data(),
        readbackSize,
        0u);
    device.DestroyBuffer(readbackBuffer);
    readbackBuffer = {};
    engine.Shutdown();

    EXPECT_TRUE(run.Stats.Compile.Succeeded)
        << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded)
        << run.Stats.Diagnostic;
    EXPECT_EQ(
        FindPassStatus(run.Stats, "Present"),
        RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_FALSE(timedOut)
        << "RUNTIME-129 bake did not become exact-ready in "
        << frames << " bounded frames.";
    EXPECT_TRUE(failureReason.empty())
        << failureReason;
    EXPECT_TRUE(readbackRecorded);
    EXPECT_TRUE(maintenanceDrainObserved);
    EXPECT_TRUE(deviceIdleObserved);
    EXPECT_TRUE(preservedMaterialObserved);
    EXPECT_TRUE(objectSpaceBindingObserved);
    EXPECT_TRUE(generatedNormal.IsValid());
    EXPECT_GT(readyGeneration, 0u);
    EXPECT_EQ(
        finalCacheState,
        Extrinsic::Graphics::GpuAssetState::Ready);
    ASSERT_TRUE(finalCacheView.has_value());
    EXPECT_EQ(
        finalCacheView->Kind,
        Extrinsic::Graphics::GpuAssetKind::Texture);
    EXPECT_EQ(
        finalCacheView->Generation,
        readyGeneration);

    ASSERT_TRUE(finalBindings.has_value());
    EXPECT_EQ(finalBindings->Albedo, *albedo);
    EXPECT_EQ(
        finalBindings->MetallicRoughness,
        *metallicRoughness);
    EXPECT_EQ(finalBindings->Emissive, *emissive);
    EXPECT_EQ(finalBindings->Normal, generatedNormal);
    EXPECT_EQ(
        finalBindings->NormalSpace,
        Extrinsic::Graphics::MaterialNormalTextureSpace::
            ObjectSpaceNormal);

    ASSERT_TRUE(decoyResidency.has_value());
    ASSERT_TRUE(targetResidency.has_value());
    EXPECT_GT(targetFirstIndex, 0u);
    EXPECT_EQ(
        targetResidency->Record.SurfaceFirstIndex,
        targetFirstIndex);
    EXPECT_LT(
        decoyResidencyBeforeTarget->Record.SurfaceFirstIndex,
        targetResidency->Record.SurfaceFirstIndex)
        << "The decoy's pre-target allocation must occupy an earlier shared-index slice than the target.";

    ASSERT_TRUE(targetBakeMesh.Succeeded())
        << targetBakeMesh.Diagnostic;
    using BakeVertex =
        Extrinsic::Graphics::ObjectSpaceNormalTextureBakeVertex;
    using BakeTriangle =
        Extrinsic::Graphics::ObjectSpaceNormalTextureBakeTriangle;
    for (const BakeVertex& vertex : targetBakeMesh.Vertices)
    {
        EXPECT_NEAR(vertex.Normal.x, 1.0f, 1.0e-5f);
        EXPECT_NEAR(vertex.Normal.y, 0.0f, 1.0e-5f);
        EXPECT_NEAR(vertex.Normal.z, 0.0f, 1.0e-5f);
    }

    const Extrinsic::Graphics::
        ObjectSpaceNormalTextureBakeOptions bakeOptions{
            .Width = kRuntime129BakeExtent,
            .Height = kRuntime129BakeExtent,
            .PaddingTexels = 4u,
        };
    const auto resolvedOptions =
        Extrinsic::Graphics::
            ResolveObjectSpaceNormalTextureBakeOptions(
                bakeOptions);
    const auto sampleAt =
        [&](const std::uint32_t x,
            const std::uint32_t y)
        {
            return Extrinsic::Graphics::
                SampleObjectSpaceNormalTextureBakeAtUv(
                    std::span<const BakeVertex>{
                        targetBakeMesh.Vertices},
                    std::span<const BakeTriangle>{
                        targetBakeMesh.Triangles},
                    Extrinsic::Graphics::
                        UvForObjectSpaceNormalBakeTexelCenter(
                            x,
                            y,
                            resolvedOptions),
                    bakeOptions);
        };

    const Extrinsic::Core::Extent2D bakeExtent{
        kRuntime129BakeExtent,
        kRuntime129BakeExtent};
    const auto readAt =
        [&](const std::uint32_t x,
            const std::uint32_t y)
        {
            return ReadPixel(
                pixels,
                Extrinsic::RHI::Format::RGBA8_UNORM,
                kRuntime129PixelBytes,
                bakeExtent,
                x,
                y);
        };

    struct Runtime129Texel
    {
        std::uint32_t X{0u};
        std::uint32_t Y{0u};
        std::uint32_t Distance{0u};
    };
    std::vector<Runtime129Texel> cpuCovered{};
    std::vector<std::uint8_t> cpuCoverage(
        static_cast<std::size_t>(kRuntime129BakeExtent) *
            kRuntime129BakeExtent,
        0u);
    std::optional<Runtime129Texel> coveredTexel{};
    float bestInteriorScore =
        std::numeric_limits<float>::lowest();
    std::uint32_t unexpectedCpuSamples = 0u;
    for (std::uint32_t y = 0u;
         y < kRuntime129BakeExtent;
         ++y)
    {
        for (std::uint32_t x = 0u;
             x < kRuntime129BakeExtent;
             ++x)
        {
            const auto sample = sampleAt(x, y);
            if (sample.Succeeded())
            {
                cpuCoverage[
                    static_cast<std::size_t>(y) *
                        kRuntime129BakeExtent +
                    x] = 1u;
                cpuCovered.push_back(
                    Runtime129Texel{.X = x, .Y = y});
                const float interiorScore =
                    std::min(
                        sample.Barycentric.x,
                        std::min(
                            sample.Barycentric.y,
                            sample.Barycentric.z));
                if (!coveredTexel.has_value() ||
                    interiorScore > bestInteriorScore)
                {
                    coveredTexel =
                        Runtime129Texel{.X = x, .Y = y};
                    bestInteriorScore = interiorScore;
                }
            }
            else if (
                sample.Status !=
                Extrinsic::Graphics::
                    ObjectSpaceNormalTextureBakeStatus::
                        NoContainingTriangle)
            {
                ++unexpectedCpuSamples;
            }
        }
    }
    ASSERT_EQ(unexpectedCpuSamples, 0u);
    ASSERT_TRUE(coveredTexel.has_value());
    ASSERT_FALSE(cpuCovered.empty());

    const auto distanceToCpuCoverage =
        [&](const std::uint32_t x,
            const std::uint32_t y)
        {
            std::uint32_t distance =
                std::numeric_limits<std::uint32_t>::max();
            for (const Runtime129Texel covered :
                 cpuCovered)
            {
                const std::uint32_t dx =
                    x > covered.X
                        ? x - covered.X
                        : covered.X - x;
                const std::uint32_t dy =
                    y > covered.Y
                        ? y - covered.Y
                        : covered.Y - y;
                distance =
                    std::min(distance, std::max(dx, dy));
            }
            return distance;
        };

    std::optional<Runtime129Texel> gutterTexel{};
    std::optional<Runtime129Texel> farTexel{};
    for (std::uint32_t y = 0u;
         y < kRuntime129BakeExtent;
         ++y)
    {
        for (std::uint32_t x = 0u;
             x < kRuntime129BakeExtent;
             ++x)
        {
            if (cpuCoverage[
                    static_cast<std::size_t>(y) *
                        kRuntime129BakeExtent +
                    x] != 0u)
            {
                continue;
            }

            const std::uint32_t distance =
                distanceToCpuCoverage(x, y);
            const RgbaPixel pixel = readAt(x, y);
            if (distance >= 1u &&
                distance <= bakeOptions.PaddingTexels &&
                pixel.A >= 251u &&
                (!gutterTexel.has_value() ||
                 distance > gutterTexel->Distance))
            {
                gutterTexel = Runtime129Texel{
                    .X = x,
                    .Y = y,
                    .Distance = distance,
                };
            }
            if (distance > bakeOptions.PaddingTexels &&
                (!farTexel.has_value() ||
                 distance > farTexel->Distance))
            {
                farTexel = Runtime129Texel{
                    .X = x,
                    .Y = y,
                    .Distance = distance,
                };
            }
        }
    }
    ASSERT_TRUE(gutterTexel.has_value())
        << "No GPU-covered texel existed outside the live CPU raster footprint.";
    ASSERT_TRUE(farTexel.has_value());
    EXPECT_EQ(
        gutterTexel->Distance,
        bakeOptions.PaddingTexels)
        << "The selected dilation witness did not reach the requested four-texel gutter.";

    const RgbaPixel covered =
        readAt(coveredTexel->X, coveredTexel->Y);
    const RgbaPixel gutter =
        readAt(gutterTexel->X, gutterTexel->Y);
    const RgbaPixel farUncovered =
        readAt(farTexel->X, farTexel->Y);
    ExpectRuntime129TargetNormalPixel(
        covered,
        "covered target");
    ExpectRuntime129TargetNormalPixel(
        gutter,
        "dilated gutter");
    EXPECT_LE(farUncovered.A, 4u)
        << "Far uncovered texel gained coverage outside the four-texel padding gutter: "
        << "coordinate=(" << farTexel->X << ","
        << farTexel->Y << ") distance="
        << farTexel->Distance << " pixel="
        << PixelText(farUncovered);
}

// --- RUNTIME-190: generalized GPU property-texture baking --------------

namespace
{
constexpr std::uint32_t kRuntime190BakeExtent = 32u;
constexpr std::uint32_t kRuntime190BakePixelBytes = 4u;
constexpr std::uint32_t kRuntime190MaxFrames = 120u;
constexpr std::string_view kRuntime190Presentation =
    "runtime190.surface";
constexpr std::string_view kRuntime190VertexOutput =
    "runtime190.vertex.scalar";
constexpr std::string_view kRuntime190FaceOutput =
    "runtime190.face.color";
constexpr std::string_view kRuntime190EdgeOutput =
    "runtime190.edge.color";
constexpr std::string_view kRuntime190SavedEdgeOutput =
    "runtime190.edge.color.saved";

[[nodiscard]] RT::ProgressivePresentationBindings
MakeRuntime190PresentationBindings()
{
    RT::ProgressiveSlotBinding albedo{};
    albedo.Semantic = RT::ProgressiveSlotSemantic::Albedo;
    albedo.SourceKind = RT::ProgressiveSlotSourceKind::PropertyBuffer;
    albedo.Property = RT::ProgressivePropertyBindingDescriptor{
        .Domain = RT::ProgressiveGeometryDomain::MeshVertex,
        .PropertyName = "v:runtime190_scalar",
        .ExpectedValueKind =
            RT::ProgressivePropertyValueKind::ScalarFloat,
        .ExpectedElementCount = 3u,
    };
    albedo.Readiness = RT::ProgressiveReadinessState::Ready;
    albedo.Provenance =
        RT::ProgressiveGeneratedOutputProvenance::PropertyBuffer;

    RT::ProgressiveSlotBinding scalar = albedo;
    scalar.Semantic = RT::ProgressiveSlotSemantic::ScalarField;

    return RT::ProgressivePresentationBindings{
        .Shape = RT::ProgressiveEntityShape::MeshLeaf,
        .Lanes = {
            RT::ProgressiveRenderLaneBinding{
                .Lane = RT::ProgressiveRenderLane::Surface,
                .PresentationKey = std::string{kRuntime190Presentation},
            },
        },
        .Presentations = {
            RT::ProgressivePresentationBinding{
                .Key = std::string{kRuntime190Presentation},
                .Kind =
                    RT::ProgressivePresentationKind::SurfaceMaterial,
                .Slots = {albedo, scalar},
            },
        },
        .BindingGeneration = 1u,
    };
}

[[nodiscard]] const RT::BakedPropertyTextureRecord*
FindRuntime190Record(
    const RT::TextureBakeSnapshot& snapshot,
    const std::string_view outputName)
{
    const auto found = std::ranges::find(
        snapshot.Textures,
        outputName,
        &RT::BakedPropertyTextureRecord::OutputName);
    return found != snapshot.Textures.end() ? &*found : nullptr;
}

class Runtime190PropertyTextureBakeApp final : public IApplication
{
public:
    void OnInitialize(Engine& engine) override
    {
        m_Device = &engine.GetDevice();
        m_Renderer = &engine.GetRenderer();
        m_Cache = engine.Services().Find<
            Extrinsic::Graphics::GpuAssetCache>();
        m_Extraction = engine.Services().Find<
            RT::RenderExtractionCache>();
        m_TextureBake = engine.Services().Find<
            RT::TextureBakeService>();
        m_Assets = engine.Services().Find<Assets::AssetService>();
        m_History = engine.Services().Find<RT::EditorCommandHistory>();
        m_Scene = engine.Worlds().Get(engine.ActiveWorld());
        m_World = engine.ActiveWorld();
        if (m_Device == nullptr ||
            m_Renderer == nullptr ||
            m_Cache == nullptr ||
            m_Extraction == nullptr ||
            m_TextureBake == nullptr ||
            m_Assets == nullptr ||
            m_History == nullptr ||
            m_Scene == nullptr)
        {
            Fail(
                "Sandbox composition did not publish the services required by the RUNTIME-190 smoke.");
            return;
        }

        m_Participant = engine.Jobs().RegisterGpuQueueParticipant(
            RT::GpuQueueParticipantDesc{
                .DebugName =
                    "RuntimeSandboxAcceptanceGpuSmoke.PropertyTextureBakeReadback",
                .Scope = m_World,
                .RecordFrameCommands =
                    [this](Extrinsic::RHI::ICommandContext& commands)
                    {
                        RecordReadbacks(commands);
                    },
                .DrainCompletedTransfers =
                    [this]
                    {
                        if (InitialReadbacksRecorded)
                            InitialReadbackDrainObserved = true;
                        if (RebakeReadbackRecorded)
                            RebakeReadbackDrainObserved = true;
                    },
                .HasInFlightWork =
                    [this]
                    {
                        return Configured &&
                               m_Stage != Stage::Finished;
                    },
                .ShutdownAfterDeviceIdle =
                    [this]
                    {
                        DeviceIdleObserved = true;
                    },
            });
        if (!m_Participant.IsValid())
            Fail("JobService rejected the RUNTIME-190 readback participant.");
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++Frames;
        if (!FailureReason.empty())
        {
            engine.RequestExit();
            return;
        }
        if (!Configured)
        {
            if (Frames >= kRuntime190MaxFrames)
            {
                TimedOut = true;
                engine.RequestExit();
            }
            return;
        }
        if (Frames >= kRuntime190MaxFrames)
        {
            TimedOut = true;
            engine.RequestExit();
            return;
        }

        switch (m_Stage)
        {
        case Stage::WaitingToSchedule:
            if (m_Device->IsOperational() &&
                m_TextureBake->Available())
            {
                if (!ScheduleInitialBakes())
                {
                    engine.RequestExit();
                    return;
                }
                m_Stage = Stage::WaitingForInitialReadback;
            }
            break;
        case Stage::WaitingForInitialReadback:
            if (InitialReadbacksRecorded &&
                InitialReadbackDrainObserved)
            {
                ObserveInitialBinding();
                m_SettleFrames = 3u;
                m_Stage = Stage::ViridisSettle;
            }
            break;
        case Stage::ViridisSettle:
            if (m_SettleFrames > 0u)
            {
                --m_SettleFrames;
                break;
            }
            if (!RenameEdgeAndSelectInferno())
            {
                engine.RequestExit();
                return;
            }
            m_Renderer->SetDefaultRecipeBackbufferReadbackBuffer(
                m_InfernoBackbuffer);
            m_SettleFrames = 4u;
            m_Stage = Stage::InfernoSettle;
            break;
        case Stage::InfernoSettle:
            if (m_SettleFrames > 0u)
            {
                --m_SettleFrames;
                break;
            }
            if (!ObserveInfernoAndScheduleRebake())
                break;
            m_Renderer->SetDefaultRecipeBackbufferReadbackBuffer({});
            m_Stage = Stage::WaitingForRebakeReadback;
            break;
        case Stage::WaitingForRebakeReadback:
            if (RebakeReadbackRecorded &&
                RebakeReadbackDrainObserved)
            {
                m_SettleFrames = 2u;
                m_Stage = Stage::RetireReadback;
            }
            break;
        case Stage::RetireReadback:
            if (m_SettleFrames > 0u)
            {
                --m_SettleFrames;
                break;
            }
            if (!RemoveAllOutputs())
            {
                engine.RequestExit();
                return;
            }
            m_Stage = Stage::Finished;
            engine.RequestExit();
            break;
        case Stage::Finished:
            engine.RequestExit();
            break;
        }
    }

    void OnShutdown(Engine&) override
    {
        m_Participant = {};
    }

    void Configure(
        const EntityHandle target,
        const std::uint32_t stableEntityId,
        const Extrinsic::RHI::BufferHandle initialVertexReadback,
        const Extrinsic::RHI::BufferHandle rebakedVertexReadback,
        const Extrinsic::RHI::BufferHandle faceReadback,
        const Extrinsic::RHI::BufferHandle edgeReadback,
        const Extrinsic::RHI::BufferHandle infernoBackbuffer) noexcept
    {
        m_Target = target;
        m_StableEntityId = stableEntityId;
        m_InitialVertexReadback = initialVertexReadback;
        m_RebakedVertexReadback = rebakedVertexReadback;
        m_FaceReadback = faceReadback;
        m_EdgeReadback = edgeReadback;
        m_InfernoBackbuffer = infernoBackbuffer;
        Configured =
            m_Target != Extrinsic::ECS::InvalidEntityHandle &&
            m_StableEntityId != 0u &&
            m_InitialVertexReadback.IsValid() &&
            m_RebakedVertexReadback.IsValid() &&
            m_FaceReadback.IsValid() &&
            m_EdgeReadback.IsValid() &&
            m_InfernoBackbuffer.IsValid();
    }

    void Detach(Engine& engine)
    {
        if (!m_Participant.IsValid())
            return;
        Extrinsic::RHI::IDevice* const device = m_Device;
        engine.Jobs().UnregisterGpuQueueParticipant(
            m_Participant,
            [device]
            {
                if (device != nullptr)
                    device->WaitIdle();
            });
        m_Participant = {};
    }

    bool Configured{false};
    bool InitialReadbacksRecorded{false};
    bool InitialReadbackDrainObserved{false};
    bool RebakeReadbackRecorded{false};
    bool RebakeReadbackDrainObserved{false};
    bool DeviceIdleObserved{false};
    bool InitialViridisBindingObserved{false};
    bool InfernoBindingObserved{false};
    bool ColormapChangedWithoutRebake{false};
    bool RenamePreservedOldAsset{false};
    bool RebakeReusedAsset{false};
    bool AssetsDestroyedObserved{false};
    bool PropertyBufferFallbackObserved{false};
    bool TimedOut{false};
    std::uint32_t Frames{0u};
    std::string FailureReason{};
    Assets::AssetId VertexAsset{};
    Assets::AssetId FaceAsset{};
    Assets::AssetId SavedEdgeAsset{};
    Assets::AssetId NewEdgeAsset{};
    std::uint64_t InitialVertexCacheGeneration{0u};
    std::uint64_t RebakedVertexCacheGeneration{0u};

private:
    enum class Stage : std::uint8_t
    {
        WaitingToSchedule,
        WaitingForInitialReadback,
        ViridisSettle,
        InfernoSettle,
        WaitingForRebakeReadback,
        RetireReadback,
        Finished,
    };

    void Fail(std::string reason)
    {
        if (FailureReason.empty())
            FailureReason = std::move(reason);
    }

    [[nodiscard]] RT::SandboxEditorContext CommandContext() const
    {
        return RT::SandboxEditorContext{
            .Scene = m_Scene,
            .World = m_World,
            .CommandHistory = m_History,
            .AssetService = m_Assets,
            .Device = m_Device,
            .TextureBake = m_TextureBake,
        };
    }

    [[nodiscard]] std::vector<RT::BakedPropertyTextureConsumer>
    Consumers(const Extrinsic::Graphics::Colormap::Type colormap) const
    {
        return {
            RT::BakedPropertyTextureConsumer{
                .PresentationKey =
                    std::string{kRuntime190Presentation},
                .Semantic = RT::ProgressiveSlotSemantic::Albedo,
                .Colormap = colormap,
            },
            RT::BakedPropertyTextureConsumer{
                .PresentationKey =
                    std::string{kRuntime190Presentation},
                .Semantic =
                    RT::ProgressiveSlotSemantic::ScalarField,
                .Colormap = colormap,
            },
        };
    }

    [[nodiscard]] RT::SandboxEditorTextureBakeCommand
    VertexCommand(const Extrinsic::Graphics::Colormap::Type colormap) const
    {
        RT::SandboxEditorTextureBakeCommand command{};
        command.StableEntityId = m_StableEntityId;
        command.PresentationKey =
            std::string{kRuntime190Presentation};
        command.TargetSemantic =
            RT::ProgressiveSlotSemantic::Albedo;
        command.SourceDomain =
            RT::ProgressiveGeometryDomain::MeshVertex;
        command.ExpectedValueKind =
            RT::ProgressivePropertyValueKind::ScalarFloat;
        command.PropertyName = "v:runtime190_scalar";
        command.RangePolicy =
            RT::MeshAttributeTextureBakeRangePolicy::Manual;
        command.RangeMin = 0.0f;
        command.RangeMax = 1.0f;
        command.Width = kRuntime190BakeExtent;
        command.Height = kRuntime190BakeExtent;
        command.OutputName = std::string{kRuntime190VertexOutput};
        command.Storage =
            RT::SelectedMeshTextureBakeStorage::RawFloat;
        command.Consumers = Consumers(colormap);
        command.BindGeneratedTexture = true;
        return command;
    }

    [[nodiscard]] RT::SandboxEditorTextureBakeCommand
    EncodedCommand(
        const RT::ProgressiveGeometryDomain domain,
        std::string property,
        std::string output) const
    {
        RT::SandboxEditorTextureBakeCommand command{};
        command.StableEntityId = m_StableEntityId;
        command.SourceDomain = domain;
        command.ExpectedValueKind =
            RT::ProgressivePropertyValueKind::Vec4;
        command.PropertyName = std::move(property);
        command.Encoder =
            RT::MeshAttributeTextureBakeEncoder::RgbaColor;
        command.Width = kRuntime190BakeExtent;
        command.Height = kRuntime190BakeExtent;
        command.OutputName = std::move(output);
        command.Storage =
            RT::SelectedMeshTextureBakeStorage::EncodedRgba;
        command.BindGeneratedTexture = false;
        return command;
    }

    [[nodiscard]] bool ScheduleInitialBakes()
    {
        const RT::SandboxEditorContext context = CommandContext();
        m_VertexCommand = VertexCommand(
            Extrinsic::Graphics::Colormap::Type::Viridis);
        const RT::SandboxEditorTextureBakeCommand face = EncodedCommand(
            RT::ProgressiveGeometryDomain::MeshFace,
            "f:runtime190_color",
            std::string{kRuntime190FaceOutput});
        const RT::SandboxEditorTextureBakeCommand edge = EncodedCommand(
            RT::ProgressiveGeometryDomain::MeshEdge,
            "e:runtime190_color",
            std::string{kRuntime190EdgeOutput});

        const RT::SandboxEditorTextureBakeCommandResult vertexResult =
            RT::ApplySandboxEditorTextureBakeCommand(
                context,
                m_VertexCommand);
        const RT::SandboxEditorTextureBakeCommandResult faceResult =
            RT::ApplySandboxEditorTextureBakeCommand(context, face);
        const RT::SandboxEditorTextureBakeCommandResult edgeResult =
            RT::ApplySandboxEditorTextureBakeCommand(context, edge);
        if (!vertexResult.Succeeded() || !vertexResult.Scheduled ||
            !faceResult.Succeeded() || !faceResult.Scheduled ||
            !edgeResult.Succeeded() || !edgeResult.Scheduled)
        {
            Fail(
                "The shared Sandbox texture-bake command did not schedule all vertex/face/edge requests: vertex=" +
                vertexResult.Diagnostic + " face=" +
                faceResult.Diagnostic + " edge=" +
                edgeResult.Diagnostic);
            return false;
        }
        VertexAsset = vertexResult.GeneratedTexture;
        FaceAsset = faceResult.GeneratedTexture;
        SavedEdgeAsset = edgeResult.GeneratedTexture;
        return VertexAsset.IsValid() &&
               FaceAsset.IsValid() &&
               SavedEdgeAsset.IsValid();
    }

    void ObserveInitialBinding()
    {
        const auto bindings =
            m_Extraction->GetMaterialTextureAssetBindings(
                m_StableEntityId);
        InitialViridisBindingObserved =
            bindings.has_value() &&
            bindings->Albedo == VertexAsset &&
            bindings->AlbedoInterpretation ==
                Extrinsic::Graphics::
                    MaterialAlbedoTextureInterpretation::Scalar &&
            bindings->AlbedoScalarColormap ==
                Extrinsic::Graphics::Colormap::Type::Viridis &&
            std::abs(bindings->AlbedoScalarRangeMin - 0.0f) <
                1.0e-6f &&
            std::abs(bindings->AlbedoScalarRangeMax - 1.0f) <
                1.0e-6f;
        if (!InitialViridisBindingObserved)
            Fail("Ready raw scalar texture did not bind as Viridis albedo.");
    }

    [[nodiscard]] bool RenameEdgeAndSelectInferno()
    {
        const RT::TextureBakeMutationResult renamed =
            m_TextureBake->Rename(
                m_StableEntityId,
                kRuntime190EdgeOutput,
                kRuntime190SavedEdgeOutput);
        if (!renamed.Succeeded())
        {
            Fail("Ready edge texture could not be renamed: " +
                 renamed.Diagnostic);
            return false;
        }

        const RT::SandboxEditorTextureBakeCommand edge = EncodedCommand(
            RT::ProgressiveGeometryDomain::MeshEdge,
            "e:runtime190_color",
            std::string{kRuntime190EdgeOutput});
        const RT::SandboxEditorTextureBakeCommandResult edgeResult =
            RT::ApplySandboxEditorTextureBakeCommand(
                CommandContext(),
                edge);
        if (!edgeResult.Succeeded() || !edgeResult.Scheduled ||
            !edgeResult.GeneratedTexture.IsValid())
        {
            Fail("Rebaking the old edge output name after rename failed: " +
                 edgeResult.Diagnostic);
            return false;
        }
        NewEdgeAsset = edgeResult.GeneratedTexture;
        RenamePreservedOldAsset =
            NewEdgeAsset != SavedEdgeAsset &&
            m_Assets->IsAlive(SavedEdgeAsset);
        if (!RenamePreservedOldAsset)
        {
            Fail("Renaming did not preserve a distinct live edge texture asset.");
            return false;
        }

        const std::vector<RT::BakedPropertyTextureConsumer> inferno =
            Consumers(Extrinsic::Graphics::Colormap::Type::Inferno);
        const RT::TextureBakeMutationResult changed =
            m_TextureBake->SetConsumers(
                RT::TextureBakeConsumerUpdateRequest{
                    .StableEntityId = m_StableEntityId,
                    .OutputName = std::string{kRuntime190VertexOutput},
                    .Consumers = inferno,
                });
        if (!changed.Succeeded())
        {
            Fail("Raw scalar colormap update failed: " +
                 changed.Diagnostic);
            return false;
        }
        m_VertexCommand.Consumers = inferno;
        return true;
    }

    [[nodiscard]] bool ObserveInfernoAndScheduleRebake()
    {
        const auto bindings =
            m_Extraction->GetMaterialTextureAssetBindings(
                m_StableEntityId);
        const auto vertexView = m_Cache->GetView(VertexAsset);
        InfernoBindingObserved =
            bindings.has_value() &&
            bindings->Albedo == VertexAsset &&
            bindings->AlbedoInterpretation ==
                Extrinsic::Graphics::
                    MaterialAlbedoTextureInterpretation::Scalar &&
            bindings->AlbedoScalarColormap ==
                Extrinsic::Graphics::Colormap::Type::Inferno;
        ColormapChangedWithoutRebake =
            vertexView.has_value() &&
            vertexView->Generation == InitialVertexCacheGeneration;
        if (!InfernoBindingObserved ||
            !ColormapChangedWithoutRebake)
        {
            Fail(
                "Changing the raw scalar colormap changed texture identity/generation or failed to update the material binding.");
            return false;
        }

        const RT::TextureBakeSnapshot snapshot =
            m_TextureBake->Snapshot(m_StableEntityId);
        const RT::BakedPropertyTextureRecord* savedEdge =
            FindRuntime190Record(snapshot, kRuntime190SavedEdgeOutput);
        const RT::BakedPropertyTextureRecord* newEdge =
            FindRuntime190Record(snapshot, kRuntime190EdgeOutput);
        if (savedEdge == nullptr || newEdge == nullptr ||
            savedEdge->State != RT::BakedPropertyTextureState::Ready ||
            newEdge->State != RT::BakedPropertyTextureState::Ready)
        {
            return false;
        }

        auto& values = m_Scene->Raw()
            .get<gs::Vertices>(m_Target)
            .Properties.Get<float>("v:runtime190_scalar")
            .Vector();
        values = {0.75f, 0.75f, 0.75f};
        const RT::SandboxEditorTextureBakeCommandResult rebake =
            RT::ApplySandboxEditorTextureBakeCommand(
                CommandContext(),
                m_VertexCommand);
        if (!rebake.Succeeded() || !rebake.Scheduled)
        {
            Fail("Mutated scalar rebake was not scheduled: " +
                 rebake.Diagnostic);
            return false;
        }
        RebakeReusedAsset = rebake.GeneratedTexture == VertexAsset;
        if (!RebakeReusedAsset)
        {
            Fail("Rebaking the same named output allocated a different asset.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool RemoveAllOutputs()
    {
        const std::array<std::string_view, 4u> names{
            kRuntime190VertexOutput,
            kRuntime190FaceOutput,
            kRuntime190SavedEdgeOutput,
            kRuntime190EdgeOutput,
        };
        const std::array<Assets::AssetId, 4u> assets{
            VertexAsset,
            FaceAsset,
            SavedEdgeAsset,
            NewEdgeAsset,
        };
        for (const std::string_view name : names)
        {
            const RT::TextureBakeMutationResult removed =
                m_TextureBake->Remove(m_StableEntityId, name);
            if (!removed.Succeeded())
            {
                Fail("Generated texture removal failed: " +
                     removed.Diagnostic);
                return false;
            }
        }

        AssetsDestroyedObserved = std::ranges::none_of(
            assets,
            [this](const Assets::AssetId asset)
            {
                return m_Assets->IsAlive(asset);
            });
        const RT::TextureBakeSnapshot snapshot =
            m_TextureBake->Snapshot(m_StableEntityId);
        const auto* progressive = m_Scene->Raw()
            .try_get<RT::ProgressivePresentationBindings>(m_Target);
        const RT::ProgressivePresentationBinding* presentation =
            progressive != nullptr
                ? RT::FindPresentationBinding(
                      *progressive,
                      kRuntime190Presentation)
                : nullptr;
        const RT::ProgressiveSlotBinding* albedo =
            presentation != nullptr
                ? RT::FindSlotBinding(
                      *presentation,
                      RT::ProgressiveSlotSemantic::Albedo)
                : nullptr;
        const RT::ProgressiveSlotBinding* scalar =
            presentation != nullptr
                ? RT::FindSlotBinding(
                      *presentation,
                      RT::ProgressiveSlotSemantic::ScalarField)
                : nullptr;
        const auto material =
            m_Extraction->GetMaterialTextureAssetBindings(
                m_StableEntityId);
        PropertyBufferFallbackObserved =
            snapshot.Textures.empty() &&
            albedo != nullptr &&
            scalar != nullptr &&
            albedo->SourceKind ==
                RT::ProgressiveSlotSourceKind::PropertyBuffer &&
            scalar->SourceKind ==
                RT::ProgressiveSlotSourceKind::PropertyBuffer &&
            (!material.has_value() || !material->Albedo.IsValid());
        if (!AssetsDestroyedObserved ||
            !PropertyBufferFallbackObserved)
        {
            Fail(
                "Removing generated textures did not destroy assets and restore property-buffer consumers.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool CopyReadyTexture(
        Extrinsic::RHI::ICommandContext& commands,
        const RT::BakedPropertyTextureRecord& record,
        const Extrinsic::RHI::Format expectedFormat,
        const Extrinsic::RHI::BufferHandle readback,
        std::uint64_t& generation)
    {
        const auto view = m_Cache->GetView(record.Texture);
        if (!view.has_value() ||
            view->Kind != Extrinsic::Graphics::GpuAssetKind::Texture ||
            !view->Texture.IsValid())
        {
            Fail("Ready property texture has no exact GPU cache view.");
            return false;
        }
        const Extrinsic::RHI::TextureDesc* const desc =
            m_Renderer->GetTextureManager().GetDesc(view->Texture);
        if (desc == nullptr ||
            desc->Width != kRuntime190BakeExtent ||
            desc->Height != kRuntime190BakeExtent ||
            desc->Fmt != expectedFormat)
        {
            Fail("Ready property texture has the wrong extent or format.");
            return false;
        }
        generation = view->Generation;
        commands.TextureBarrier(
            view->Texture,
            Extrinsic::RHI::TextureLayout::ShaderReadOnly,
            Extrinsic::RHI::TextureLayout::TransferSrc);
        commands.CopyTextureToBuffer(
            view->Texture,
            Extrinsic::RHI::TextureLayout::TransferSrc,
            0u,
            0u,
            readback,
            0u,
            0u,
            0u,
            desc->Width,
            desc->Height);
        commands.TextureBarrier(
            view->Texture,
            Extrinsic::RHI::TextureLayout::TransferSrc,
            Extrinsic::RHI::TextureLayout::ShaderReadOnly);
        return true;
    }

    void RecordReadbacks(
        Extrinsic::RHI::ICommandContext& commands)
    {
        if (!Configured ||
            !FailureReason.empty() ||
            !m_Device->IsOperational())
        {
            return;
        }
        const RT::TextureBakeSnapshot snapshot =
            m_TextureBake->Snapshot(m_StableEntityId);
        if (!InitialReadbacksRecorded)
        {
            const RT::BakedPropertyTextureRecord* vertex =
                FindRuntime190Record(snapshot, kRuntime190VertexOutput);
            const RT::BakedPropertyTextureRecord* face =
                FindRuntime190Record(snapshot, kRuntime190FaceOutput);
            const RT::BakedPropertyTextureRecord* edge =
                FindRuntime190Record(snapshot, kRuntime190EdgeOutput);
            if (vertex == nullptr || face == nullptr || edge == nullptr ||
                vertex->State != RT::BakedPropertyTextureState::Ready ||
                face->State != RT::BakedPropertyTextureState::Ready ||
                edge->State != RT::BakedPropertyTextureState::Ready)
            {
                return;
            }
            std::uint64_t ignoredFaceGeneration = 0u;
            std::uint64_t ignoredEdgeGeneration = 0u;
            if (!CopyReadyTexture(
                    commands,
                    *vertex,
                    Extrinsic::RHI::Format::R32_FLOAT,
                    m_InitialVertexReadback,
                    InitialVertexCacheGeneration) ||
                !CopyReadyTexture(
                    commands,
                    *face,
                    Extrinsic::RHI::Format::RGBA8_UNORM,
                    m_FaceReadback,
                    ignoredFaceGeneration) ||
                !CopyReadyTexture(
                    commands,
                    *edge,
                    Extrinsic::RHI::Format::RGBA8_UNORM,
                    m_EdgeReadback,
                    ignoredEdgeGeneration))
            {
                return;
            }
            InitialReadbacksRecorded = true;
            return;
        }

        if (m_Stage != Stage::WaitingForRebakeReadback ||
            RebakeReadbackRecorded)
        {
            return;
        }
        const RT::BakedPropertyTextureRecord* vertex =
            FindRuntime190Record(snapshot, kRuntime190VertexOutput);
        if (vertex == nullptr ||
            vertex->State != RT::BakedPropertyTextureState::Ready)
        {
            return;
        }
        const auto view = m_Cache->GetView(vertex->Texture);
        if (!view.has_value() ||
            view->Generation <= InitialVertexCacheGeneration)
        {
            return;
        }
        if (CopyReadyTexture(
                commands,
                *vertex,
                Extrinsic::RHI::Format::R32_FLOAT,
                m_RebakedVertexReadback,
                RebakedVertexCacheGeneration))
        {
            RebakeReadbackRecorded = true;
        }
    }

    Extrinsic::RHI::IDevice* m_Device{nullptr};
    Extrinsic::Graphics::IRenderer* m_Renderer{nullptr};
    Extrinsic::Graphics::GpuAssetCache* m_Cache{nullptr};
    RT::RenderExtractionCache* m_Extraction{nullptr};
    RT::TextureBakeService* m_TextureBake{nullptr};
    Assets::AssetService* m_Assets{nullptr};
    RT::EditorCommandHistory* m_History{nullptr};
    Registry* m_Scene{nullptr};
    RT::WorldHandle m_World{};
    RT::GpuQueueParticipantHandle m_Participant{};
    EntityHandle m_Target{Extrinsic::ECS::InvalidEntityHandle};
    std::uint32_t m_StableEntityId{0u};
    Extrinsic::RHI::BufferHandle m_InitialVertexReadback{};
    Extrinsic::RHI::BufferHandle m_RebakedVertexReadback{};
    Extrinsic::RHI::BufferHandle m_FaceReadback{};
    Extrinsic::RHI::BufferHandle m_EdgeReadback{};
    Extrinsic::RHI::BufferHandle m_InfernoBackbuffer{};
    RT::SandboxEditorTextureBakeCommand m_VertexCommand{};
    Stage m_Stage{Stage::WaitingToSchedule};
    std::uint32_t m_SettleFrames{0u};
};

[[nodiscard]] float ReadRuntime190FloatTexel(
    const std::vector<std::uint8_t>& bytes,
    const std::uint32_t x,
    const std::uint32_t y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * kRuntime190BakeExtent + x) *
        sizeof(float);
    if (offset + sizeof(float) > bytes.size())
        return std::numeric_limits<float>::quiet_NaN();
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
} // namespace

TEST(RuntimeSandboxAcceptanceGpuSmoke,
     PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan)
{
    auto app = std::make_unique<Runtime190PropertyTextureBakeApp>();
    auto* const appPtr = app.get();
    auto bootstrap =
        BootstrapDefaultSandboxAppEngineWithApp(std::move(app));
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    Registry& scene =
        *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle triangle =
        FindEntityByName(scene, "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(scene, triangle))
        << BuildReferenceTriangleEntityDiagnostic(scene, triangle);

    auto& raw = scene.Raw();
    auto& vertices = raw.get<gs::Vertices>(triangle);
    ASSERT_EQ(vertices.Properties.Size(), 3u);
    vertices.Properties
        .GetOrAdd<float>("v:runtime190_scalar", 0.25f)
        .Vector() = {0.25f, 0.25f, 0.25f};
    vertices.Properties
        .GetOrAdd<glm::vec3>(
            std::string{pn::kNormal},
            glm::vec3{0.0f, 0.0f, 1.0f})
        .Vector() = {
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        };

    auto& faces = raw.get<gs::Faces>(triangle);
    ASSERT_EQ(faces.Properties.Size(), 1u);
    faces.Properties
        .GetOrAdd<glm::vec4>(
            "f:runtime190_color",
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f})
        .Vector() = {
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
        };

    auto& edges = raw.get<gs::Edges>(triangle);
    edges.Properties.Resize(3u);
    edges.Properties
        .GetOrAdd<std::uint32_t>(
            std::string{pn::kEdgeV0},
            kInvalidIndex)
        .Vector() = {0u, 1u, 2u};
    edges.Properties
        .GetOrAdd<std::uint32_t>(
            std::string{pn::kEdgeV1},
            kInvalidIndex)
        .Vector() = {1u, 2u, 0u};
    edges.Properties
        .GetOrAdd<glm::vec4>(
            "e:runtime190_color",
            glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    raw.emplace_or_replace<RT::ProgressivePresentationBindings>(
        triangle,
        MakeRuntime190PresentationBindings());
    auto& visualization =
        raw.get_or_emplace<G::VisualizationConfig>(triangle);
    visualization.Source =
        G::VisualizationConfig::ColorSource::Material;

    const std::uint32_t stableEntityId =
        RT::SelectionController::ToStableEntityId(triangle);
    ASSERT_NE(stableEntityId, 0u);

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat =
        device.GetBackbufferFormat();
    const std::uint32_t backbufferPixelBytes =
        Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    const Extrinsic::Core::Extent2D backbufferExtent =
        device.GetBackbufferExtent();
    if (backbufferPixelBytes < 4u ||
        backbufferExtent.Width == 0u ||
        backbufferExtent.Height == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Backbuffer format or extent cannot support the RUNTIME-190 color-map readback.";
    }

    const std::uint64_t bakeReadbackSize =
        static_cast<std::uint64_t>(kRuntime190BakeExtent) *
        kRuntime190BakeExtent *
        kRuntime190BakePixelBytes;
    const std::uint64_t backbufferReadbackSize =
        static_cast<std::uint64_t>(backbufferPixelBytes) *
        backbufferExtent.Width *
        backbufferExtent.Height;
    const auto createReadback =
        [&device](
            const std::uint64_t size,
            const char* const name)
        {
            return device.CreateBuffer(
                Extrinsic::RHI::BufferDesc{
                    .SizeBytes = size,
                    .Usage =
                        Extrinsic::RHI::BufferUsage::TransferDst,
                    .HostVisible = true,
                    .DebugName = name,
                });
        };

    std::array<Extrinsic::RHI::BufferHandle, 6u> readbacks{
        createReadback(
            bakeReadbackSize,
            "Sandbox.Runtime190.VertexInitial"),
        createReadback(
            bakeReadbackSize,
            "Sandbox.Runtime190.VertexRebaked"),
        createReadback(
            bakeReadbackSize,
            "Sandbox.Runtime190.Face"),
        createReadback(
            bakeReadbackSize,
            "Sandbox.Runtime190.Edge"),
        createReadback(
            backbufferReadbackSize,
            "Sandbox.Runtime190.ViridisBackbuffer"),
        createReadback(
            backbufferReadbackSize,
            "Sandbox.Runtime190.InfernoBackbuffer"),
    };
    if (std::ranges::any_of(
            readbacks,
            [](const Extrinsic::RHI::BufferHandle handle)
            {
                return !handle.IsValid();
            }))
    {
        for (Extrinsic::RHI::BufferHandle handle : readbacks)
        {
            if (handle.IsValid())
                device.DestroyBuffer(handle);
        }
        engine.Shutdown();
        GTEST_SKIP()
            << "Pre-run RUNTIME-190 readback allocation was unavailable.";
    }

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbacks[4]);
    appPtr->Configure(
        triangle,
        stableEntityId,
        readbacks[0],
        readbacks[1],
        readbacks[2],
        readbacks[3],
        readbacks[5]);

    const AcceptanceRunCapture run =
        DriveAcceptanceAndCapture(engine);
    appPtr->Detach(engine);
    renderer.SetDefaultRecipeBackbufferReadbackBuffer({});

    if (!run.DeviceOperational)
    {
        for (const Extrinsic::RHI::BufferHandle handle : readbacks)
            device.DestroyBuffer(handle);
        engine.Shutdown();
        ADD_FAILURE()
            << "RUNTIME-190 property-bake smoke lost operational Vulkan: status="
            << ToString(run.Status.Code)
            << " reason=" << ToString(run.Status.Reason)
            << ". pass statuses=["
            << BuildPassStatusSummary(run.Stats) << "]";
        return;
    }

    std::array<std::vector<std::uint8_t>, 4u> bakeBytes{};
    for (std::size_t index = 0u; index < bakeBytes.size(); ++index)
    {
        bakeBytes[index].resize(
            static_cast<std::size_t>(bakeReadbackSize));
        device.ReadBuffer(
            readbacks[index],
            bakeBytes[index].data(),
            bakeReadbackSize,
            0u);
    }
    std::vector<std::uint8_t> viridisBytes(
        static_cast<std::size_t>(backbufferReadbackSize));
    std::vector<std::uint8_t> infernoBytes(
        static_cast<std::size_t>(backbufferReadbackSize));
    device.ReadBuffer(
        readbacks[4],
        viridisBytes.data(),
        backbufferReadbackSize,
        0u);
    device.ReadBuffer(
        readbacks[5],
        infernoBytes.data(),
        backbufferReadbackSize,
        0u);
    for (const Extrinsic::RHI::BufferHandle handle : readbacks)
        device.DestroyBuffer(handle);

    EXPECT_TRUE(run.Stats.Compile.Succeeded)
        << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded)
        << run.Stats.Diagnostic;
    EXPECT_EQ(
        FindPassStatus(run.Stats, "SurfacePass"),
        RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_EQ(
        FindPassStatus(run.Stats, "Present"),
        RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);
    EXPECT_FALSE(appPtr->TimedOut)
        << "RUNTIME-190 timed out after " << appPtr->Frames
        << " bounded frames.";
    EXPECT_TRUE(appPtr->FailureReason.empty())
        << appPtr->FailureReason;
    EXPECT_TRUE(appPtr->Configured);
    EXPECT_TRUE(appPtr->InitialReadbacksRecorded);
    EXPECT_TRUE(appPtr->RebakeReadbackRecorded);
    EXPECT_TRUE(appPtr->DeviceIdleObserved);
    EXPECT_TRUE(appPtr->InitialViridisBindingObserved);
    EXPECT_TRUE(appPtr->InfernoBindingObserved);
    EXPECT_TRUE(appPtr->ColormapChangedWithoutRebake);
    EXPECT_TRUE(appPtr->RenamePreservedOldAsset);
    EXPECT_TRUE(appPtr->RebakeReusedAsset);
    EXPECT_TRUE(appPtr->AssetsDestroyedObserved);
    EXPECT_TRUE(appPtr->PropertyBufferFallbackObserved);
    EXPECT_GT(appPtr->InitialVertexCacheGeneration, 0u);
    EXPECT_GT(
        appPtr->RebakedVertexCacheGeneration,
        appPtr->InitialVertexCacheGeneration);

    EXPECT_NEAR(
        ReadRuntime190FloatTexel(bakeBytes[0], 8u, 8u),
        0.25f,
        1.0e-5f);
    EXPECT_NEAR(
        ReadRuntime190FloatTexel(bakeBytes[1], 8u, 8u),
        0.75f,
        1.0e-5f);

    const Extrinsic::Core::Extent2D bakeExtent{
        kRuntime190BakeExtent,
        kRuntime190BakeExtent};
    const RgbaPixel facePixel = ReadPixel(
        bakeBytes[2],
        Extrinsic::RHI::Format::RGBA8_UNORM,
        kRuntime190BakePixelBytes,
        bakeExtent,
        8u,
        8u);
    EXPECT_LE(facePixel.R, 4u) << PixelText(facePixel);
    EXPECT_GE(facePixel.G, 251u) << PixelText(facePixel);
    EXPECT_LE(facePixel.B, 4u) << PixelText(facePixel);
    EXPECT_GE(facePixel.A, 251u) << PixelText(facePixel);

    std::array<std::uint32_t, 3u> edgeColorCounts{};
    for (std::uint32_t y = 0u; y < kRuntime190BakeExtent; ++y)
    {
        for (std::uint32_t x = 0u; x < kRuntime190BakeExtent; ++x)
        {
            const RgbaPixel pixel = ReadPixel(
                bakeBytes[3],
                Extrinsic::RHI::Format::RGBA8_UNORM,
                kRuntime190BakePixelBytes,
                bakeExtent,
                x,
                y);
            if (pixel.R >= 251u && pixel.G <= 4u && pixel.B <= 4u)
                ++edgeColorCounts[0];
            if (pixel.G >= 251u && pixel.R <= 4u && pixel.B <= 4u)
                ++edgeColorCounts[1];
            if (pixel.B >= 251u && pixel.R <= 4u && pixel.G <= 4u)
                ++edgeColorCounts[2];
        }
    }
    EXPECT_GT(edgeColorCounts[0], 0u);
    EXPECT_GT(edgeColorCounts[1], 0u);
    EXPECT_GT(edgeColorCounts[2], 0u);

    const auto viridisLut = renderer.GetColormapSystem().SampleCpu(
        Extrinsic::Graphics::Colormap::Type::Viridis,
        0.25f);
    const auto infernoLut = renderer.GetColormapSystem().SampleCpu(
        Extrinsic::Graphics::Colormap::Type::Inferno,
        0.25f);
    const RgbaPixel expectedViridis{
        .R = viridisLut.R,
        .G = viridisLut.G,
        .B = viridisLut.B,
        .A = 255u,
    };
    const RgbaPixel expectedInferno{
        .R = infernoLut.R,
        .G = infernoLut.G,
        .B = infernoLut.B,
        .A = 255u,
    };
    const std::uint32_t centerX = backbufferExtent.Width / 2u;
    const std::uint32_t centerY = backbufferExtent.Height / 2u;
    const RgbaPixel viridisPixel = ToLinearPixel(
        backbufferFormat,
        ReadPixel(
            viridisBytes,
            backbufferFormat,
            backbufferPixelBytes,
            backbufferExtent,
            centerX,
            centerY));
    const RgbaPixel infernoPixel = ToLinearPixel(
        backbufferFormat,
        ReadPixel(
            infernoBytes,
            backbufferFormat,
            backbufferPixelBytes,
            backbufferExtent,
            centerX,
            centerY));
    EXPECT_LT(viridisPixel.R, viridisPixel.G)
        << "Tone-mapped Viridis frame center=" << PixelText(viridisPixel)
        << " source LUT=" << PixelText(expectedViridis);
    EXPECT_LT(viridisPixel.G, viridisPixel.B)
        << "Tone-mapped Viridis frame center=" << PixelText(viridisPixel)
        << " source LUT=" << PixelText(expectedViridis);
    EXPECT_LT(infernoPixel.G, infernoPixel.R)
        << "Tone-mapped Inferno frame center=" << PixelText(infernoPixel)
        << " source LUT=" << PixelText(expectedInferno);
    EXPECT_LT(infernoPixel.R, infernoPixel.B)
        << "Tone-mapped Inferno frame center=" << PixelText(infernoPixel)
        << " source LUT=" << PixelText(expectedInferno);
    EXPECT_GT(RgbDistance(viridisPixel, infernoPixel), 24)
        << "Render-time colormap edit did not visibly change the raw scalar texture: Viridis="
        << PixelText(viridisPixel)
        << " Inferno=" << PixelText(infernoPixel);

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters changed across RUNTIME-190: fallbackToNull "
        << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> "
        << run.After.InitFailure << ", validationError "
        << run.Before.ValidationError << " -> "
        << run.After.ValidationError << ", gateFailure "
        << run.Before.OperationalGateFailure << " -> "
        << run.After.OperationalGateFailure;

    engine.Shutdown();
}
