// RUNTIME-095 Slice 3 — opt-in `gpu;vulkan;integration` working-sandbox present
// smoke.
//
// This is the `Operational` leg of the working-sandbox acceptance. Slices 1 and
// 2 proved the CPU/null residency, camera, selection, outline, and UI contracts
// (`Test.RuntimeSandboxAcceptance.cpp`); this slice drives the runtime `Engine`
// for a small bounded number of frames on a Vulkan-capable host with the
// acceptance families (one mesh, one graph, one point cloud) composed into the
// scene and the same runtime-owned `SandboxEditorUi` shell attached as the
// `ExtrinsicSandbox` app. It asserts that the default recipe reaches the
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
#include <cstdint>
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
import Extrinsic.Asset.Registry;
import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
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
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorUi;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

namespace ECSC = Extrinsic::ECS::Components;
namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace G = Extrinsic::Graphics::Components;
namespace Assets = Extrinsic::Assets;
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
    explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
        : m_TargetFrames(targetFrames)
    {
    }

    void OnInitialize(Engine& engine) override
    {
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
        m_EditorUi.Detach();
    }

private:
    Extrinsic::Runtime::SandboxEditorUi m_EditorUi{};
    std::uint32_t m_TargetFrames{1u};
    std::uint32_t m_Frames{0u};
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

    SeedAcceptanceScene(enginePtr->GetScene());
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

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    auto enginePtr = std::make_unique<Engine>(config, std::move(app));
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
        std::make_unique<ExitAfterFramesApp>(kTargetFrames));
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
    std::uint32_t LineInstances{0u};
    std::uint32_t PointInstances{0u};
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
        if (!hasLine && !hasPoint)
            continue;

        const bool validGeometry =
            inst.GeometrySlot != Extrinsic::RHI::GpuInstanceStatic::InvalidGeometrySlot &&
            inst.GeometrySlot < geometries.size();
        const auto& slotBounds = bounds[slot];
        const auto geometry = validGeometry ? geometries[inst.GeometrySlot]
                                            : Extrinsic::RHI::GpuGeometryRecord{};

        if (hasLine)
        {
            ++summary.LineInstances;
            if (!validGeometry)
                ++summary.LineInstancesWithInvalidGeometry;
            if (slotBounds.WorldSphere.w <= 0.0f)
                ++summary.LineInstancesWithZeroBounds;
            if (validGeometry && geometry.LineIndexCount == 0u)
                ++summary.LineInstancesWithZeroIndexCount;
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

    Registry& scene = engine.GetScene();
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
    const auto adapterDiagnostics = engine.GetImGuiAdapter().GetDiagnostics();

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
    EXPECT_GT(adapterDiagnostics.FramesProduced, 0u);
    EXPECT_GT(adapterDiagnostics.LastVertexCount, 0u);
    EXPECT_GT(adapterDiagnostics.LastIndexCount, 0u);
    EXPECT_GT(adapterDiagnostics.LastCommandCount, 0u);

    const std::uint64_t nonBlackPixels =
        CountNonBlackRgbPixels(device, readbackBuffer, readbackSize, bytesPerPixel);
    const std::uint64_t totalPixels =
        static_cast<std::uint64_t>(extent.Width) * static_cast<std::uint64_t>(extent.Height);
    EXPECT_GT(nonBlackPixels, 0u)
        << "Sandbox default-config frame read back as entirely black despite recorded Present/ImGui passes. "
        << "adapter frames=" << adapterDiagnostics.FramesProduced
        << " drawLists=" << adapterDiagnostics.LastDrawListCount
        << " vertices=" << adapterDiagnostics.LastVertexCount
        << " indices=" << adapterDiagnostics.LastIndexCount
        << " commands=" << adapterDiagnostics.LastCommandCount
        << " display=" << adapterDiagnostics.DisplayWidth << "x" << adapterDiagnostics.DisplayHeight
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

TEST(RuntimeSandboxAcceptanceGpuSmoke, ExtrinsicSandboxDefaultConfigPresentsReferenceTriangleAtFrameCenter)
{
    auto bootstrap = BootstrapDefaultSandboxAppEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const EntityHandle triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(engine.GetScene(), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(engine.GetScene(), triangle);

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
// coordinates, mirroring Runtime.ReferenceScene's BuildReferenceCameraViewInput.
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

    const EntityHandle triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(engine.GetScene(), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(engine.GetScene(), triangle);

    auto& raw = engine.GetScene().Raw();
    G::RenderEdges edges{};
    edges.WidthSource = kReferenceTriangleLineWidthSmokePx;
    raw.emplace_or_replace<G::RenderEdges>(triangle, edges);
    G::RenderPoints points{};
    raw.emplace_or_replace<G::RenderPoints>(triangle, points);

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
    std::uint32_t pointCullCount = 0u;
    Extrinsic::RHI::GpuDrawIndexedCommand firstLineCmd{};
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
    EXPECT_GE(pointCullCount, 1u)
        << "ReferenceTriangle mesh point lane did not emit a point draw command."
        << " pointCmd={vertexCount=" << firstPointCmd.VertexCount
        << ", instanceCount=" << firstPointCmd.InstanceCount
        << ", firstVertex=" << firstPointCmd.FirstVertex
        << ", firstInstance=" << firstPointCmd.FirstInstance << "}";

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

    const EntityHandle triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(engine.GetScene(), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(engine.GetScene(), triangle);

    auto& raw = engine.GetScene().Raw();

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

// Sandbox app that applies the promoted Inspector transform-edit command (the
// live EditorCommandHistory path) to the ReferenceTriangle on a mid-run frame
// — i.e. after that frame's fixed-step bundle already ran — and exits after a
// bounded number of frames so the readback captures post-edit pixels.
class EditTriangleViaInspectorApp final : public IApplication
{
public:
    void OnInitialize(Engine& engine) override
    {
        m_EditorUi.Attach(engine);
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Frames == kBug024EditFrame)
        {
            const EntityHandle triangle =
                FindEntityByName(engine.GetScene(), "ReferenceTriangle");
            if (triangle != Extrinsic::ECS::InvalidEntityHandle)
            {
                const Extrinsic::Runtime::SandboxEditorContext context{
                    .Scene = &engine.GetScene(),
                    .Selection = &engine.GetSelectionController(),
                    .CommandHistory = &engine.GetEditorCommandHistory(),
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

    void OnShutdown(Engine&) override
    {
        m_EditorUi.Detach();
    }

    Extrinsic::Runtime::SandboxEditorCommandStatus EditStatus{
        Extrinsic::Runtime::SandboxEditorCommandStatus::NoChange};

private:
    Extrinsic::Runtime::SandboxEditorUi m_EditorUi{};
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

    const EntityHandle triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
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
    auto& raw = engine.GetScene().Raw();
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
    SetEntityPosition(engine.GetScene(),
                      FindEntityByName(engine.GetScene(), "ReferenceTriangle"),
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

    auto imported = engine.ImportAssetFromPath(
        Extrinsic::Runtime::RuntimeAssetImportRequest{
            .Path = obj.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());
    EXPECT_EQ(imported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(imported->PrimitiveEntitiesCreated, 1u);

    const EntityHandle importedEntity =
        FindEntityByName(engine.GetScene(), obj.Path.filename().string());
    ASSERT_NE(importedEntity, Extrinsic::ECS::InvalidEntityHandle);
    ASSERT_TRUE(engine.GetScene().IsValid(importedEntity));

    auto& raw = engine.GetScene().Raw();
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

// --- BUG-026B: Vulkan click-pick readback round trip -----------------------

namespace
{
constexpr std::uint32_t kBug026MaxFrames = 24u;
constexpr float kBug026PlaneTolerance = 0.05f;

class ClickPickRoundTripApp final : public IApplication
{
public:
    void OnInitialize(Engine& engine) override
    {
        m_EditorUi.Attach(engine);
        m_Triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
    }

    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Triangle == Extrinsic::ECS::InvalidEntityHandle ||
            !engine.GetScene().IsValid(m_Triangle))
        {
            FailureReason = "ReferenceTriangle was not present while driving the click-pick smoke.";
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
            TrianglePixel = ProjectReferenceCameraPixel(glm::vec3{0.0f, 0.0f, 0.0f}, extent);
            engine.GetSelectionController().RequestClickPick(TrianglePixel.first, TrianglePixel.second);
            TriangleClickSubmitted = true;
            m_Phase = Phase::AwaitTriangleHit;
            break;
        }
        case Phase::AwaitTriangleHit:
            if (ObserveTriangleHit(engine))
            {
                const auto diagnostics = engine.GetSelectionController().GetDiagnostics();
                m_NoHitsBeforeBackground = diagnostics.NoHits;
                BackgroundPixel = FarBackgroundPixel(extent);
                engine.GetSelectionController().RequestClickPick(BackgroundPixel.first, BackgroundPixel.second);
                BackgroundClickSubmitted = true;
                m_Phase = Phase::AwaitBackgroundNoHit;
            }
            break;
        case Phase::AwaitBackgroundNoHit:
            if (ObserveBackgroundNoHit(engine))
            {
                BackgroundNoHitObserved = true;
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

    void OnShutdown(Engine&) override
    {
        m_EditorUi.Detach();
    }

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
        auto& scene = engine.GetScene();
        auto& selection = engine.GetSelectionController();
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

        const auto& primitive = engine.GetLastRefinedPrimitiveSelection();
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
        const auto& selection = engine.GetSelectionController();
        const auto diagnostics = selection.GetDiagnostics();
        return diagnostics.NoHits > m_NoHitsBeforeBackground &&
               selection.SelectedCount() == 0u &&
               !engine.GetScene().Raw().all_of<ECSC::Selection::SelectedTag>(m_Triangle) &&
               !engine.GetLastRefinedPrimitiveSelection().has_value();
    }

    Extrinsic::Runtime::SandboxEditorUi m_EditorUi{};
    EntityHandle m_Triangle{Extrinsic::ECS::InvalidEntityHandle};
    Phase m_Phase{Phase::SubmitTriangleClick};
    std::uint32_t m_Frames{0u};
    std::uint32_t m_NoHitsBeforeBackground{0u};
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

    const EntityHandle triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
    ASSERT_TRUE(IsReferenceTriangleEntityValid(engine.GetScene(), triangle))
        << "ReferenceTriangle is not a valid first-class mesh renderable entity: "
        << BuildReferenceTriangleEntityDiagnostic(engine.GetScene(), triangle);

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

    const auto diagnostics = engine.GetSelectionController().GetDiagnostics();
    EXPECT_EQ(diagnostics.ClickRequestsSubmitted, 2u);
    EXPECT_EQ(diagnostics.PicksDrained, 2u);
    EXPECT_EQ(diagnostics.Hits, 1u);
    EXPECT_EQ(diagnostics.NoHits, 1u);
    EXPECT_EQ(engine.GetSelectionController().SelectedCount(), 0u);
    EXPECT_FALSE(engine.GetScene().Raw().all_of<ECSC::Selection::SelectedTag>(triangle));
    EXPECT_FALSE(engine.GetLastRefinedPrimitiveSelection().has_value());
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(run.Stats);

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

    EntityHandle triangle = FindEntityByName(engine.GetScene(), "ReferenceTriangle");
    ASSERT_NE(triangle, Extrinsic::ECS::InvalidEntityHandle);
    ASSERT_TRUE(engine.GetSelectionController().SetSelectedEntity(engine.GetScene(), triangle));
    ASSERT_EQ(engine.GetSelectionController().SelectedCount(), 1u);
    ASSERT_EQ(engine.GetSelectionController().SelectedStableIds().size(), 1u);
    EXPECT_EQ(engine.GetSelectionController().SelectedStableIds()[0],
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
