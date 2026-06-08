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
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;
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

EntityHandle MakeMesh(Registry& scene)
{
    const EntityHandle entity = scene.Create();
    StampCommon(scene, entity, "AcceptanceMesh", 101u);
    auto& raw = scene.Raw();
    raw.emplace<G::RenderSurface>(entity);

    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
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

EntityHandle MakeGraph(Registry& scene)
{
    const EntityHandle entity = scene.Create();
    StampCommon(scene, entity, "AcceptanceGraph", 102u);
    auto& raw = scene.Raw();
    raw.emplace<G::RenderLines>(entity);

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
[[nodiscard]] AcceptanceBootstrap BootstrapDefaultSandboxAppEngine()
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

    return AcceptanceBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
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
