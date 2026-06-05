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
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorUi;
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
    EXPECT_GT(nonBlackPixels, 0u)
        << "Sandbox default-config frame read back as entirely black despite recorded Present/ImGui passes. "
        << "adapter frames=" << adapterDiagnostics.FramesProduced
        << " drawLists=" << adapterDiagnostics.LastDrawListCount
        << " vertices=" << adapterDiagnostics.LastVertexCount
        << " indices=" << adapterDiagnostics.LastIndexCount
        << " commands=" << adapterDiagnostics.LastCommandCount
        << " display=" << adapterDiagnostics.DisplayWidth << "x" << adapterDiagnostics.DisplayHeight
        << " pass statuses=[" << BuildPassStatusSummary(run.Stats) << "]";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}
