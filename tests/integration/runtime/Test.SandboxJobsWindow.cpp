// UI-060: the Jobs window over the editor job surface (harness-backed, no engine).
#include <unordered_map>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <format>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
// Config accessors this test round-trips come from their owning config modules,
// not from the operation families that consume them.
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.GeodesicsConfig;
import Extrinsic.Runtime.MeshCurvatureConfig;
import Extrinsic.Runtime.RegistrationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.CoherentPointDriftConfig;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.Curvature;
import Geometry.PointCloud;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SpatialIndexCache;

#include "TestImGuiFrameScope.hpp"
#include "../../../src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp"

namespace R = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;

namespace
{
    template <class Pred>
    bool WaitFor(Pred pred)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!pred())
        {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return true;
    }

    R::JobDesc LongJob(std::string name, std::atomic_bool& release)
    {
        R::JobDesc desc{};
        desc.DebugName = std::move(name);
        desc.Work = [&release](const R::JobCancellation& cancellation) {
            cancellation.ReportProgress(0.25f);
            while (!release.load(std::memory_order_acquire) && !cancellation.IsCancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            return R::JobResultEnvelope::Make(true);
        };
        desc.PublishCompletion = [](R::KernelEventBus&, const R::JobResultEnvelope&) { return true; };
        return desc;
    }

    R::EditorJobIdentity Identity(std::string output, std::uint32_t entity = 7u)
    {
        return {.EntityId = entity, .Scope = R::EditorJobScope::MeshSurface,
                .OutputSemantic = R::GeometryPresentationSlotSemantic::ScalarField, .OutputName = std::move(output)};
    }

    R::EditorJobRecord Row(std::uint32_t index, R::JobState state)
    {
        R::EditorJobRecord row{};
        row.Token = R::JobToken{index, 1u};
        row.State = state;
        row.Name = "row" + std::to_string(index);
        return row;
    }
}

// The long job shows as a row; Cancel cancels it once, the button then explains why it is disabled,
// and the row ends Cancelled.
TEST(SandboxJobsWindow, ListsALongJobAndCancelsItThroughTheSurface)
{
    TestSupport::ImGuiFrameScope frame;
    Extrinsic::Tests::EditorJobHarness harness{2u};
    R::EditorProcessingContext context{};
    harness.Attach(context);
    int cancels = 0;
    context.JobCommands.Cancel = [&, inner = context.JobCommands.Cancel](R::JobToken token) {
        ++cancels;
        return inner(token);
    };
    const R::EditorJobCommandSurface surface = context.JobCommands;
    const R::EditorProcessingCommands commands = R::BindEditorProcessingCommands(std::move(context));
    Editor::JobsWindowState state;
    std::atomic_bool release{false};
    const auto draw = [&] {
        frame.NextFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({900, 400});
        ImGui::Begin("Jobs test");
        Editor::DrawJobsWindow(commands, state);
        ImGui::End();
    };

    draw();
    EXPECT_TRUE(state.History.Rows().empty());

    const R::JobToken token = surface.Submit(LongJob("Long smoothing", release), Identity("smoothed"));
    ASSERT_TRUE(token.IsValid());
    ASSERT_TRUE(WaitFor([&] { return harness.Jobs().GetState(token) == R::JobState::Running; }));
    draw();
    ASSERT_EQ(state.History.Rows().size(), 1u);
    EXPECT_EQ(state.History.Rows()[0].Name, "Long smoothing");
    EXPECT_EQ(state.History.Rows()[0].Identity.OutputName, "smoothed");
    EXPECT_EQ(state.History.Rows()[0].State, R::JobState::Running);
    EXPECT_GE(R::GetEditorJobStats(commands).SubmittedJobs, 1u);
    EXPECT_TRUE(R::ResolveEditorJobCancelReadiness(commands, state.History.Rows()[0]).Enabled);

    ImGuiWindow* window = ImGui::FindWindowByName("Jobs test");
    ASSERT_NE(window, nullptr);
    const std::string key = std::to_string(token.Index) + "." + std::to_string(token.Generation);
    const ImGuiID cancel = ImHashStr(("Cancel##job" + key).c_str(), 0, window->GetID("##jobs_table"));
    ImGui::ActivateItemByID(cancel);
    draw();
    draw();
    EXPECT_EQ(cancels, 1) << "Cancel reaches the surface once";

    // The cancel is requested but the job has not drained: Cancel explains why it is disabled.
    const auto refused = R::ResolveEditorJobCancelReadiness(commands, state.History.Rows()[0]);
    EXPECT_FALSE(refused.Enabled);
    EXPECT_NE(refused.DisabledReason.find("already requested"), std::string::npos);
    ImGui::ActivateItemByID(cancel);
    draw();
    draw();
    EXPECT_EQ(cancels, 1) << "a disabled Cancel does not reach the surface";

    ASSERT_TRUE(harness.DrainUntilTerminal());
    draw();
    ASSERT_EQ(state.History.Rows().size(), 1u);
    EXPECT_EQ(state.History.Rows()[0].State, R::JobState::Cancelled);
    EXPECT_EQ(R::ProjectEditorOperationProgress(state.History.Rows()[0]).State, R::EditorOperationState::Cancelled);
    EXPECT_FALSE(R::ResolveEditorJobCancelReadiness(commands, state.History.Rows()[0]).Enabled);
}

// Cancelling a stage cancels its run (stages queued after it too), but never another run on the same output.
TEST(SandboxJobsWindow, CancelCancelsTheWholeRunNotJustTheRow)
{
    Extrinsic::Tests::EditorJobHarness harness{2u};
    R::EditorProcessingContext context{};
    harness.Attach(context);
    const R::EditorJobCommandSurface surface = context.JobCommands;
    const R::EditorProcessingCommands commands = R::BindEditorProcessingCommands(std::move(context));
    std::atomic_bool release{false};
    const R::JobToken head = surface.Submit(LongJob("head", release), Identity("out"));
    auto stageIdentity = Identity("out");
    stageIdentity.Run = head;
    const R::JobToken stage = surface.Submit(LongJob("stage", release), stageIdentity);
    const R::JobToken other = surface.Submit(LongJob("other run", release), Identity("out"));
    ASSERT_TRUE(head.IsValid() && stage.IsValid() && other.IsValid());
    const auto rows = surface.SnapshotAll();
    const auto stageRow = std::ranges::find(rows, stage, &R::EditorJobRecord::Token);
    ASSERT_NE(stageRow, rows.end());
    EXPECT_EQ(R::CancelEditorJobRun(commands, *stageRow), R::EditorJobCancelStatus::Requested);
    release.store(true, std::memory_order_release);
    ASSERT_TRUE(harness.DrainUntilTerminal());
    EXPECT_EQ(harness.Jobs().GetState(head), R::JobState::Cancelled);
    EXPECT_EQ(harness.Jobs().GetState(stage), R::JobState::Cancelled);
    EXPECT_NE(harness.Jobs().GetState(other), R::JobState::Cancelled);
}

TEST(SandboxJobsWindow, CancelIsDisabledWithTheRuntimeReasonForRowsItCannotCancel)
{
    Extrinsic::Tests::EditorJobHarness harness{1u};
    R::EditorProcessingContext context{};
    harness.Attach(context);
    const R::EditorProcessingCommands commands = R::BindEditorProcessingCommands(std::move(context));
    R::EditorJobRecord service = Row(1u, R::JobState::Running);
    service.CorrelationId = 4u; // a K-Means or consolidation run: no editor identity
    const auto notEditor = R::ResolveEditorJobCancelReadiness(commands, service);
    EXPECT_FALSE(notEditor.Enabled);
    EXPECT_NE(notEditor.DisabledReason.find("Not an editor job"), std::string::npos);
    EXPECT_NE(R::ResolveEditorJobCancelReadiness(commands, Row(2u, R::JobState::Published)).DisabledReason.find("ended"),
              std::string::npos);
    const auto detached = R::ResolveEditorJobCancelReadiness(R::EditorProcessingCommands{}, [] {
        auto row = Row(3u, R::JobState::Running);
        row.Identity = Identity("x");
        return row;
    }());
    EXPECT_FALSE(detached.Enabled);
    EXPECT_NE(detached.DisabledReason.find("unavailable"), std::string::npos);
    EXPECT_EQ(R::CancelEditorJobRun(R::EditorProcessingCommands{}, Row(3u, R::JobState::Running)),
              R::EditorJobCancelStatus::Unavailable);
}

// Finished rows outlive the runtime's reaping (bounded); a job that vanished while active leaves nothing;
// a new scene epoch clears everything.
TEST(SandboxJobsWindow, HistoryKeepsRecentFinishedRowsAndDropsThemWithTheSceneEpoch)
{
    Editor::JobsHistory history;
    history.Observe(std::vector{Row(1u, R::JobState::Running), Row(2u, R::JobState::Running)}, 1u);
    ASSERT_EQ(history.Rows().size(), 2u);
    history.Observe(std::vector{Row(1u, R::JobState::Cancelled), Row(2u, R::JobState::Running)}, 1u);
    history.Observe(std::vector<R::EditorJobRecord>{}, 1u); // both reaped: 1 ended, 2 vanished while running
    ASSERT_EQ(history.Rows().size(), 1u);
    EXPECT_EQ(history.Rows()[0].Token.Index, 1u);
    EXPECT_EQ(history.Rows()[0].State, R::JobState::Cancelled);

    std::vector<R::EditorJobRecord> many;
    for (std::uint32_t i = 10u; i < 10u + 40u; ++i) many.push_back(Row(i, R::JobState::Published));
    history.Observe(many, 1u);
    EXPECT_EQ(history.Rows().size(), Editor::JobsHistory::kFinishedLimit);
    EXPECT_EQ(history.Rows().back().Token.Index, 49u) << "the newest rows stay";

    history.Observe(std::vector{Row(1u, R::JobState::Running)}, 2u);
    ASSERT_EQ(history.Rows().size(), 1u) << "a scene replacement drops the old rows";
    EXPECT_EQ(history.Rows()[0].State, R::JobState::Running);
}

TEST(SandboxJobsWindow, BackendColumnShowsRequestedAndResolvedDomain)
{
    using D = R::EditorJobDomain;
    EXPECT_EQ(Editor::FormatJobBackend(D::Cpu, D::Cpu), "CPU");
    EXPECT_EQ(Editor::FormatJobBackend(D::Auto, D::GpuCompute), "Auto -> GPU compute");
}
