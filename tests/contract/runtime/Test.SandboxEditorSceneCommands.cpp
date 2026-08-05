// ARCH-006 runtime Sandbox editor SceneCommands contract partition.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "ProgressivePoissonReference.hpp"
#include <entt/entity/entity.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include "RuntimeTestModule.hpp"

#include "EditorFeatureTestContext.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Graph.Vertex.Normals;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.KMeans;
import Geometry.PointCloud.Normals;
import Geometry.Properties;
import Geometry.Smoothing;
import Geometry.UvAtlas;

#include "MockRHI.hpp"

namespace Runtime = Extrinsic::Runtime;
namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Graphics = Extrinsic::Graphics;
namespace Plat = Extrinsic::Platform;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace GN = Geometry::HalfedgeMesh::VertexNormals;
namespace GVN = Geometry::Graph::VertexNormals;
namespace PCN = Geometry::PointCloud::Normals;
namespace Smooth = Geometry::Smoothing;
namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
namespace Tests = Extrinsic::Tests;

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

void InstallSandboxDefaultRuntimePolicies(Runtime::Engine& engine)
    {
        auto* const pipeline =
            engine.Services().Find<Runtime::AssetWorkflowModule>();
        auto* const inputActions =
            engine.Services().Find<Runtime::RuntimeInputActionRegistry>();
        ASSERT_NE(pipeline, nullptr);
        ASSERT_NE(inputActions, nullptr);

        auto* const cameraControllers =
            engine.Services().Find<Runtime::CameraControllerRegistry>();
        auto* const selection =
            engine.Services().Find<Runtime::SelectionController>();
        if (cameraControllers != nullptr && selection != nullptr)
        {
            ASSERT_TRUE(
                inputActions
                    ->Register(
                        Runtime::MakeFocusCameraOnSelectionInputAction(
                            *cameraControllers,
                            *selection,
                            "Sandbox.DefaultFocusCameraOnSelection",
                            Runtime::RuntimeInputActionBinding{.KeyCode = 'F'}))
                    .IsValid());
        }
    }

[[nodiscard]] Runtime::SelectionController& Selection(
    Runtime::Engine& engine)
{
    return *engine.Services()
        .Find<Runtime::SelectionController>();
}

[[nodiscard]] bool HasDiagnostic(
        const std::vector<Runtime::EditorDiagnostic>& diagnostics,
        const Runtime::EditorDiagnosticCode code)
{
    for (const Runtime::EditorDiagnostic& diagnostic : diagnostics)
    {
        if (diagnostic.Code == code)
            return true;
    }
    return false;
}

[[nodiscard]] const Runtime::EditorFileImportPayloadOption*
    FindFileImportPayloadOption(
        const Runtime::EditorFileImportModel& model,
        const Assets::AssetPayloadKind kind)
{
    const auto option = std::find_if(
        model.PayloadOptions.begin(),
        model.PayloadOptions.end(),
        [kind](const Runtime::EditorFileImportPayloadOption& candidate)
        {
            return candidate.Kind == kind;
        });
    return option == model.PayloadOptions.end() ? nullptr : &*option;
}

template <std::size_t N>
void ExpectEnabledFileImportPayloadOptions(
        const Runtime::EditorFileImportModel& model,
        const std::array<Assets::AssetPayloadKind, N>& expected)
{
    for (const Runtime::EditorFileImportPayloadOption& option :
         model.PayloadOptions)
    {
        const bool shouldBeEnabled =
            std::find(expected.begin(), expected.end(), option.Kind) !=
            expected.end();
        EXPECT_EQ(option.Enabled, shouldBeEnabled)
            << Assets::DebugNameForAssetPayloadKind(option.Kind);
        if (shouldBeEnabled)
            EXPECT_TRUE(option.DisabledReason.empty());
        else
            EXPECT_FALSE(option.DisabledReason.empty());
    }
}

[[nodiscard]] bool LogSnapshotContains(
        const Core::Log::LogSnapshot& snapshot,
        const std::string_view needle)
    {
        for (const Core::Log::LogEntry& entry : snapshot.Entries)
        {
            if (entry.Message.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

[[nodiscard]] bool LogSnapshotContains(
        const Core::Log::LogSnapshot& snapshot,
        const Core::Log::Level level,
        const std::string_view needle)
    {
        for (const Core::Log::LogEntry& entry : snapshot.Entries)
        {
            if (entry.Lvl == level &&
                entry.Message.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

[[nodiscard]] Graphics::RenderRecipeConfigContext
    MakeRenderRecipeConfigContext()
    {
        Graphics::RenderFrameInput input{};
        input.Viewport = Core::Extent2D{.Width = 1280u, .Height = 720u};
        input.Camera.Valid = true;
        return Graphics::RenderRecipeConfigContext{
            .Renderer = Graphics::MakeCurrentRendererDescriptor(),
            .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput =
                Graphics::MakeCurrentRendererViewOutputRecipe(input),
            .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
        };
    }

[[nodiscard]] std::string ValidSandboxRenderRecipeConfig()
    {
        return std::string{R"json({
  "schema": ")json"} + std::string{Graphics::kRenderRecipeConfigSchemaId} +
               R"json(",
  "version": 1,
  "rendererId": ")json" +
               std::string{Graphics::kCurrentRendererContractId} + R"json(",
  "revision": "sandbox-ui-test",
  "recipe": {
    "recipeId": "current-renderer.user-preview",
    "fixedCoreName": "Extrinsic.Graphics.FrameRecipe.Default",
    "slots": [
      {
        "name": "lighting",
        "schemaId": "intrinsic.graphics.lighting/sandbox-preview/v1",
        "defaults": "sandbox lighting defaults",
        "requiredCapabilities": ["LightingRecipe"],
        "allowedBindingRoles": ["light-snapshots", "material-table"],
        "usedBindingRoles": ["light-snapshots"],
        "validationRules": ["declared-slot-only"],
        "fallbackPolicy": "Degrade"
      }
    ]
  },
  "viewOutput": {
    "recipeId": "current-renderer.preview-output",
    "view": "Preview",
    "viewport": {"width": 640, "height": 360},
    "renderScale": 1.0,
    "target": "OffscreenTexture",
    "captureRequested": true,
    "readbackRequested": true,
    "mode": "Headless",
    "outputs": [
      {"name": "color", "kind": "Color", "format": "RGBA8_UNORM", "required": true},
      {"name": "readback", "kind": "ReadbackBuffer", "format": "Host-visible buffer", "required": false}
    ]
  },
  "bindingOverrides": [
    {
      "semanticName": "light-snapshots",
      "slot": "lighting",
      "sourceDomain": "Scene",
      "sourceIdentity": "RenderWorld.Lights.SandboxPreview",
      "sourceRevision": "sandbox-revision",
      "valueType": "Buffer",
      "valueFormat": "LightSnapshot",
      "fallbackPolicy": "Degrade"
    }
  ]
})json";
    }

[[nodiscard]] Intrinsic::Tests::EditorFeatureTestContext MakeRenderRecipeEditorContext(
        Graphics::RenderRecipeConfigContext& recipeContext,
        Runtime::EditorRenderRecipeEditorState& editorState,
        Runtime::RenderArtifactRegistry* artifacts = nullptr,
        const bool commandsAvailable = true)
    {
        return Intrinsic::Tests::EditorFeatureTestContext{
            .RenderRecipeContext = &recipeContext,
            .RenderRecipeEditorState = &editorState,
            .PreviewRenderRecipeDocument =
                [&recipeContext](const std::string& document,
                                 const std::string& sourceId)
                {
                    return Graphics::PreviewRenderRecipeConfig(
                        document,
                        recipeContext,
                        Graphics::RenderRecipeConfigParseOptions{
                            .SourceId = sourceId,
                        });
                },
            .RenderArtifacts = artifacts,
            .ImGuiAdapterAvailable = true,
            .RenderRecipeCommandsAvailable = commandsAvailable,
        };
    }

[[nodiscard]] Runtime::RenderArtifactDeclaration
    MakeSandboxRenderArtifact(std::string artifactId)
    {
        return Runtime::RenderArtifactDeclaration{
            .Metadata =
                Graphics::RenderArtifactMetadata{
                    .ArtifactId = std::move(artifactId),
                    .RendererId =
                        std::string{Graphics::kCurrentRendererContractId},
                    .SnapshotId = "sandbox-snapshot",
                    .ViewOutputRecipeId =
                        std::string{Graphics::kCurrentRendererDefaultViewRecipeId},
                    .SourceRevisions = {"scene:1"},
                    .Status = Graphics::RenderArtifactStatus::Available,
                    .Lifetime = Graphics::RenderArtifactLifetime::Cached,
                    .Purpose = "color",
                },
            .Kind =
                Runtime::RenderArtifactPublicationKind::CandidateProjectResult,
            .PayloadUri = "memory://sandbox-render-artifact",
            .ProducerLabel = "sandbox editor test",
        };
    }

[[nodiscard]] const Runtime::EditorRenderArtifactRow*
    FindRenderArtifactRow(
        const Runtime::EditorRenderRecipeEditorModel& model,
        const std::string_view artifactId)
    {
        const auto it = std::find_if(
            model.Artifacts.begin(),
            model.Artifacts.end(),
            [artifactId](const Runtime::EditorRenderArtifactRow& row)
            {
                return row.ArtifactId == artifactId;
            });
        return it == model.Artifacts.end() ? nullptr : &*it;
    }

[[nodiscard]] std::string ReadRepositoryTextFile(
        const std::filesystem::path& relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path{ENGINE_ROOT_DIR} / relativePath;
        std::ifstream file{path};
        if (!file)
            return {};
        return std::string{std::istreambuf_iterator<char>{file},
                           std::istreambuf_iterator<char>{}};
    }

[[nodiscard]] Intrinsic::Tests::EditorFeatureTestContext MakeContext(
        ECS::Scene::Registry& registry,
        Runtime::SelectionController& selection,
        const bool imguiAvailable = true,
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr,
        Extrinsic::RHI::IDevice* device = nullptr)
    {
        return Intrinsic::Tests::EditorFeatureTestContext{
            .Scene = &registry,
            .Selection = &selection,
            .LastRefinedPrimitive = lastPrimitive,
            .Device = device,
            .ImGuiAdapterAvailable = imguiAvailable,
            .AssetImportCommandsAvailable = false,
            .CameraRenderCommandsAvailable = false,
            .VisualizationCommandsAvailable = false,
        };
    }

    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            engine.RequestExit();
        }
        void Shutdown() override {}
    };

enum class AssetImportEventWaitExitReason : std::uint8_t
    {
        Running,
        EventObserved,
        DeadlineExceeded,
    };

struct AssetImportEventWaitDiagnostics
    {
        std::atomic_uint32_t ObservedFrames{0u};
        std::atomic_bool WorkerStarted{false};
        std::atomic_bool ReleaseWorker{false};
        std::atomic_bool FormerFrameBudgetCrossed{false};
        std::atomic_bool WorkerStartDeadlineExceeded{false};
        std::atomic<std::int64_t> WorkerStartedAtMicros{-1};
        std::atomic<std::int64_t> WorkerGateReleasedAtMicros{-1};
        std::atomic<std::int64_t> MainThreadApplyAtMicros{-1};
        std::atomic<std::int64_t> EventObservedAtMicros{-1};
        Runtime::RuntimeAssetIngestHandle Operation{};
        Runtime::RuntimeAssetImportQueueStage LastStage{
            Runtime::RuntimeAssetImportQueueStage::Queued};
        Runtime::RuntimeAssetImportQueueTerminalStatus LastTerminalStatus{
            Runtime::RuntimeAssetImportQueueTerminalStatus::None};
        std::string LastStageText{"not observed"};
        std::string LastDiagnosticText{};
        AssetImportEventWaitExitReason ExitReason{
            AssetImportEventWaitExitReason::Running};
        std::chrono::steady_clock::time_point RequestedAt{};
        bool Armed{false};
        bool QueueEntryObserved{false};

        void Arm() noexcept
        {
            ObservedFrames.store(0u, std::memory_order_release);
            WorkerStarted.store(false, std::memory_order_release);
            ReleaseWorker.store(false, std::memory_order_release);
            FormerFrameBudgetCrossed.store(false, std::memory_order_release);
            WorkerStartDeadlineExceeded.store(false, std::memory_order_release);
            WorkerStartedAtMicros.store(-1, std::memory_order_release);
            WorkerGateReleasedAtMicros.store(-1, std::memory_order_release);
            MainThreadApplyAtMicros.store(-1, std::memory_order_release);
            EventObservedAtMicros.store(-1, std::memory_order_release);
            Operation = {};
            LastStage = Runtime::RuntimeAssetImportQueueStage::Queued;
            LastTerminalStatus =
                Runtime::RuntimeAssetImportQueueTerminalStatus::None;
            LastStageText = "not observed";
            LastDiagnosticText.clear();
            ExitReason = AssetImportEventWaitExitReason::Running;
            RequestedAt = std::chrono::steady_clock::now();
            Armed = true;
            QueueEntryObserved = false;
        }

        [[nodiscard]] std::int64_t ElapsedMicros() const noexcept
        {
            if (!Armed)
                return -1;
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - RequestedAt)
                .count();
        }

        [[nodiscard]] std::string Describe() const
        {
            const auto exitName = [this]() -> std::string_view
            {
                switch (ExitReason)
                {
                case AssetImportEventWaitExitReason::Running:
                    return "running";
                case AssetImportEventWaitExitReason::EventObserved:
                    return "event-observed";
                case AssetImportEventWaitExitReason::DeadlineExceeded:
                    return "deadline-exceeded";
                }
                return "unknown";
            }();
            const bool workerCompletionPending =
                QueueEntryObserved &&
                (LastStage == Runtime::RuntimeAssetImportQueueStage::DecodeQueued ||
                 LastStage == Runtime::RuntimeAssetImportQueueStage::Decoding);
            const bool mainThreadApplyPending =
                QueueEntryObserved &&
                LastStage == Runtime::RuntimeAssetImportQueueStage::MainThreadApply;
            const bool terminalCancellation =
                LastTerminalStatus ==
                Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled;

            std::string description{"exit="};
            description += exitName;
            description += ", operation=";
            description += Operation.IsValid()
                ? std::to_string(Operation.Index) + ":" +
                      std::to_string(Operation.Generation)
                : "invalid";
            description += ", frames=" +
                std::to_string(ObservedFrames.load(std::memory_order_acquire));
            description += ", elapsed_us=" + std::to_string(ElapsedMicros());
            description += ", stage=";
            description += QueueEntryObserved
                ? Runtime::DebugNameForRuntimeAssetImportQueueStage(LastStage)
                : "not-observed";
            description += ", terminal=";
            description += QueueEntryObserved
                ? Runtime::DebugNameForRuntimeAssetImportQueueTerminalStatus(
                      LastTerminalStatus)
                : "not-observed";
            description += ", event_observed=";
            description +=
                EventObservedAtMicros.load(std::memory_order_acquire) >= 0
                    ? "true"
                    : "false";
            description += ", former_frame_budget_crossed=";
            description += FormerFrameBudgetCrossed.load(
                               std::memory_order_acquire)
                ? "true"
                : "false";
            description += ", worker_start_deadline_exceeded=";
            description += WorkerStartDeadlineExceeded.load(
                               std::memory_order_acquire)
                ? "true"
                : "false";
            description += ", worker_completion_pending=";
            description += workerCompletionPending ? "true" : "false";
            description += ", main_thread_apply_pending=";
            description += mainThreadApplyPending ? "true" : "false";
            description += ", terminal_cancellation=";
            description += terminalCancellation ? "true" : "false";
            description += ", worker_started_us=" +
                std::to_string(
                    WorkerStartedAtMicros.load(std::memory_order_acquire));
            description += ", worker_gate_released_us=" +
                std::to_string(
                    WorkerGateReleasedAtMicros.load(std::memory_order_acquire));
            description += ", main_apply_us=" +
                std::to_string(
                    MainThreadApplyAtMicros.load(std::memory_order_acquire));
            description += ", event_us=" +
                std::to_string(
                    EventObservedAtMicros.load(std::memory_order_acquire));
            description += ", stage_text='" + LastStageText + "'";
            description += ", diagnostic='" + LastDiagnosticText + "'";
            return description;
        }
    };

void CaptureAssetImportEventWaitPhase(
    Runtime::Engine& engine,
    AssetImportEventWaitDiagnostics& diagnostics)
    {
        const Runtime::RuntimeAssetImportQueueSnapshot snapshot =
            RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
        const auto entry = std::find_if(
            snapshot.Entries.begin(),
            snapshot.Entries.end(),
            [&diagnostics](const Runtime::RuntimeAssetImportQueueEntry& candidate)
            {
                return !diagnostics.Operation.IsValid() ||
                    candidate.Operation == diagnostics.Operation;
            });
        if (entry == snapshot.Entries.end())
            return;

        if (!diagnostics.Operation.IsValid())
            diagnostics.Operation = entry->Operation;
        diagnostics.QueueEntryObserved = true;
        diagnostics.LastStage = entry->Stage;
        diagnostics.LastTerminalStatus = entry->TerminalStatus;
        diagnostics.LastStageText = entry->StageText;
        diagnostics.LastDiagnosticText = entry->DiagnosticText;
        if (entry->Stage == Runtime::RuntimeAssetImportQueueStage::MainThreadApply ||
            entry->TerminalStatus ==
                Runtime::RuntimeAssetImportQueueTerminalStatus::Complete)
        {
            std::int64_t expected = -1;
            (void)diagnostics.MainThreadApplyAtMicros.compare_exchange_strong(
                expected,
                diagnostics.ElapsedMicros(),
                std::memory_order_acq_rel);
        }
    }

    class WaitForAssetImportEventApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit WaitForAssetImportEventApplication(
            std::chrono::milliseconds timeout = std::chrono::seconds(10),
            std::shared_ptr<AssetImportEventWaitDiagnostics> diagnostics = {},
            bool waitForWorkerStart = false)
            : m_Timeout(timeout)
            , m_Diagnostics(
                  diagnostics
                      ? std::move(diagnostics)
                      : std::make_shared<AssetImportEventWaitDiagnostics>())
            , m_WaitForWorkerStart(waitForWorkerStart)
        {
        }

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine   = Kernel();
            const auto now = std::chrono::steady_clock::now();
            if (!m_Diagnostics->Armed)
                m_Diagnostics->Arm();
            CaptureAssetImportEventWaitPhase(engine, *m_Diagnostics);
            if (RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent().has_value())
            {
                m_Diagnostics->EventObservedAtMicros.store(
                    m_Diagnostics->ElapsedMicros(),
                    std::memory_order_release);
                m_Diagnostics->ExitReason =
                    AssetImportEventWaitExitReason::EventObserved;
                engine.RequestExit();
                return;
            }
            if (!m_WaitStarted)
            {
                if (!m_WorkerStartWindowStarted)
                {
                    m_WorkerStartDeadline = now + std::chrono::seconds(10);
                    m_WorkerStartWindowStarted = true;
                }
                if (m_WaitForWorkerStart &&
                    !m_Diagnostics->WorkerStarted.load(
                        std::memory_order_acquire))
                {
                    if (now >= m_WorkerStartDeadline)
                    {
                        m_Diagnostics->WorkerStartDeadlineExceeded.store(
                            true,
                            std::memory_order_release);
                        m_Diagnostics->ExitReason =
                            AssetImportEventWaitExitReason::DeadlineExceeded;
                        engine.RequestExit();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    return;
                }
                m_Deadline = now + m_Timeout;
                m_WaitStarted = true;
            }

            (void)m_Diagnostics->ObservedFrames.fetch_add(
                1u,
                std::memory_order_acq_rel);
            if (now >= m_Deadline)
            {
                m_Diagnostics->ExitReason =
                    AssetImportEventWaitExitReason::DeadlineExceeded;
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void Shutdown() override
        {
            auto& engine = Kernel();
            CaptureAssetImportEventWaitPhase(engine, *m_Diagnostics);
            m_Diagnostics->ReleaseWorker.store(true, std::memory_order_release);
        }

    private:
        std::chrono::milliseconds m_Timeout{std::chrono::seconds(10)};
        std::shared_ptr<AssetImportEventWaitDiagnostics> m_Diagnostics{};
        bool m_WaitForWorkerStart{false};
        bool m_WaitStarted{false};
        bool m_WorkerStartWindowStarted{false};
        std::chrono::steady_clock::time_point m_Deadline{};
        std::chrono::steady_clock::time_point m_WorkerStartDeadline{};
    };

    class FixedFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit FixedFrameApplication(std::uint32_t maxFrames)
            : m_MaxFrames(maxFrames)
        {
        }

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++m_ObservedFrames;
            if (m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
            }
        }
        void Shutdown() override {}

    private:
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    class WaitForConditionApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit WaitForConditionApplication(
            std::function<bool(Runtime::Engine&)> ready,
            std::uint32_t maxFrames = 512u)
            : m_Ready(std::move(ready))
            , m_MaxFrames(maxFrames)
        {
        }

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++m_ObservedFrames;
            if ((m_Ready && m_Ready(engine)) || m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void Shutdown() override {}

    private:
        std::function<bool(Runtime::Engine&)> m_Ready{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

struct BlockingGeometryDecodeState
    {
        std::atomic_bool Started{false};
        std::atomic_bool Release{false};
        std::atomic_bool MutationObservedWhileBlocked{false};
        std::atomic_uint32_t FramesWhileBlocked{0u};
        std::atomic_uint32_t ObservedPayloadKind{0u};
        Runtime::RuntimeAssetIngestHandle Operation{};
        std::size_t BaselineLiveAssetCount{0u};
        std::uint32_t ImGuiFramesProducedAtBlockStart{0u};
        std::uint32_t ImGuiFramesProducedWhileBlocked{0u};
        bool ImGuiFrameBaselineCaptured{false};
        bool CancelAttempted{false};
        bool CancelSucceeded{false};
    };

enum class BlockedGeometryImportAction : std::uint8_t
    {
        Release,
        Cancel,
    };

class DriveBlockedGeometryImportApplication final : public Intrinsic::Tests::RuntimeTestModule
{
public:
    explicit DriveBlockedGeometryImportApplication(
        std::shared_ptr<BlockingGeometryDecodeState> state,
        const BlockedGeometryImportAction action = BlockedGeometryImportAction::Release,
        const std::uint32_t maxFrames            = 512u)
        : m_State(std::move(state))
        , m_Action(action)
        , m_MaxFrames(maxFrames)
    {
    }

    void Resolve() override {}
    void Frame(double, double) override
    {
        auto& engine = Kernel();
        ++m_ObservedFrames;
        if (m_State->Started.load(std::memory_order_acquire) &&
            !m_State->Release.load(std::memory_order_acquire))
        {
            const std::uint32_t framesProduced =
                engine.Services().Find<Runtime::EditorUiHost>()->GetDiagnostics().FramesProduced;
            if (!m_State->ImGuiFrameBaselineCaptured)
            {
                m_State->ImGuiFramesProducedAtBlockStart = framesProduced;
                m_State->ImGuiFrameBaselineCaptured      = true;
            }
            m_State->ImGuiFramesProducedWhileBlocked = framesProduced;

            if (engine.Services().Find<Runtime::EditorCommandHistory>()->IsDirty() ||
                !Selection(engine).SelectedStableIds().empty() ||
                RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount() !=
                    m_State->BaselineLiveAssetCount)
            {
                m_State->MutationObservedWhileBlocked.store(true, std::memory_order_release);
            }
            const std::uint32_t blockedFrames =
                m_State->FramesWhileBlocked.fetch_add(1u, std::memory_order_acq_rel) + 1u;
            if (blockedFrames >= 3u)
            {
                if (m_Action == BlockedGeometryImportAction::Cancel)
                {
                    m_State->CancelAttempted = true;
                    m_State->CancelSucceeded =
                        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
                            .CancelAssetImport(m_State->Operation)
                            .has_value();
                }
                m_State->Release.store(true, std::memory_order_release);
            }
        }

        if ((m_Action == BlockedGeometryImportAction::Release &&
             RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
                 .GetLastAssetImportEvent()
                 .has_value()) ||
            (m_Action == BlockedGeometryImportAction::Cancel && m_State->CancelAttempted &&
             ++m_FramesAfterAction >= 16u) ||
            m_ObservedFrames >= m_MaxFrames)
        {
            m_State->Release.store(true, std::memory_order_release);
            engine.RequestExit();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void Shutdown() override { m_State->Release.store(true, std::memory_order_release); }

private:
    std::shared_ptr<BlockingGeometryDecodeState> m_State{};
    BlockedGeometryImportAction m_Action{BlockedGeometryImportAction::Release};
    std::uint32_t m_MaxFrames{1u};
    std::uint32_t m_ObservedFrames{0u};
    std::uint32_t m_FramesAfterAction{0u};
};

struct ShutdownBlockedGeometryImportState
    {
        std::atomic_bool Started{false};
        std::atomic_bool Release{false};
        Runtime::RuntimeAssetIngestHandle Operation{};
        std::size_t BaselineLiveAssetCount{0u};
        std::uint32_t InitializeCalls{0u};
        std::uint32_t ShutdownCalls{0u};
        bool RuntimeReady{false};
        bool ExitRequestedWhileWorkerBlocked{false};
        bool SessionDetachedBeforeWorkerRelease{false};
        bool WorkerReleasedFromOnShutdown{false};
    };

    class ShutdownWhileGeometryImportBlockedApplication final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit ShutdownWhileGeometryImportBlockedApplication(
            std::shared_ptr<ShutdownBlockedGeometryImportState> state,
            const std::uint32_t maxFrames = 512u)
            : m_State(std::move(state))
            , m_MaxFrames(maxFrames)
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            ++m_State->InitializeCalls;
            m_ObservedFrames = 0u;
            m_ExitRequested = false;
            m_State->RuntimeReady =
                engine.Services().Find<Runtime::AssetWorkflowModule>() !=
                nullptr;
            m_Editor.Attach(engine.Worlds(), engine.Services());
        }


        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++m_ObservedFrames;
            if (!m_ExitRequested &&
                m_State->Started.load(std::memory_order_acquire))
            {
                m_State->ExitRequestedWhileWorkerBlocked =
                    !m_State->Release.load(std::memory_order_acquire);
                m_ExitRequested = true;
                engine.RequestExit();
            }
            else if (m_ObservedFrames >= m_MaxFrames)
            {
                m_ExitRequested = true;
                engine.RequestExit();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        void Shutdown() override
        {
            auto& engine = Kernel();
            (void)engine;
            m_Editor.Detach();

            if (m_State->ShutdownCalls == 0u)
            {
                m_State->SessionDetachedBeforeWorkerRelease =
                    !m_State->Release.load(std::memory_order_acquire);
                m_State->WorkerReleasedFromOnShutdown = true;
            }
            m_State->Release.store(true, std::memory_order_release);
            ++m_State->ShutdownCalls;
        }

        [[nodiscard]] Runtime::EditorWorkspaceAttachment& Editor() noexcept
        {
            return m_Editor;
        }

    private:
        std::shared_ptr<ShutdownBlockedGeometryImportState> m_State{};
        Runtime::EditorWorkspaceAttachment m_Editor{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
        bool m_ExitRequested{false};
    };

class RecordingImportCameraController final : public Runtime::ICameraController
    {
    public:
        void Seed(const Graphics::CameraViewInput&) noexcept override {}

        void Focus(const Runtime::CameraFocusTarget target) noexcept override
        {
            LastFocus = target;
            ++FocusCalls;
        }

        void Update(const Plat::Input::Context&, double) noexcept override {}

        [[nodiscard]] Graphics::CameraViewInput GetView(
            Core::Extent2D) const noexcept override
        {
            return Runtime::DefaultCameraControllerSeed();
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind()
            const noexcept override
        {
            return Core::Config::CameraControllerKind::Orbit;
        }

        std::optional<Runtime::CameraFocusTarget> LastFocus{};
        std::uint32_t FocusCalls{0u};
    };

[[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

void ComposeAsyncWorkAndInitialize(Runtime::Engine& engine)
    {
        engine.EmplaceModule<Runtime::AsyncWorkModule>();
        engine.EmplaceModule<Runtime::CameraModule>();
        engine.EmplaceModule<Runtime::EditorUiModule>();
        engine.EmplaceModule<Runtime::SceneDocumentModule>();
        engine.EmplaceModule<Runtime::SceneInteractionModule>();
        engine.EmplaceModule<Runtime::AssetWorkflowModule>();
        engine.Initialize();
    }

[[nodiscard]] bool MeshHasVertexProperty(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        if (!engine.Worlds().Get(engine.ActiveWorld())->IsValid(entity))
        {
            return false;
        }

        auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        return view.Valid() &&
            view.ActiveDomain == GS::Domain::Mesh &&
            view.VertexSource != nullptr &&
            view.VertexSource->Properties.Exists(propertyName);
    }

[[nodiscard]] bool DirectMeshPostProcessReady(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity)
    {
        return MeshHasVertexProperty(engine, entity, "v:texcoord") &&
            MeshHasVertexProperty(engine, entity, "v:normal") &&
            RequiredEngineService<
                Extrinsic::Runtime::AssetWorkflowModule>(engine)
                .GetLastAssetImportEvent()
                .has_value();
    }

struct TmpFile
    {
        std::filesystem::path Path;

        TmpFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << contents;
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

[[nodiscard]] std::optional<ECS::EntityHandle> FindFirstEntityWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        auto& raw = registry.Raw();
        std::optional<ECS::EntityHandle> found{};
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                found = entity;
        });
        return found;
    }

[[nodiscard]] std::size_t CountEntitiesWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        std::size_t count = 0u;
        auto& raw = registry.Raw();
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                ++count;
        });
        return count;
    }
}
TEST(SandboxEditorUi, FileImportPrerequisitesAreDeterministic)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const auto buildModel =
        [&](const std::string_view path,
            const Assets::AssetPayloadKind payloadKind,
            const bool commandSurfaceAvailable = true)
        {
            Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);
            if (commandSurfaceAvailable)
            {
                context.AssetImportCommands.Import =
                    [](const Runtime::EditorFileImportCommand& command)
                    {
                        return Runtime::EditorFileImportResult{
                            .Status =
                                Runtime::EditorCommandStatus::Applied,
                            .PayloadKind = command.PayloadKind,
                        };
                    };
            }
            context.PendingAssetImportPath = path;
            context.PendingAssetImportPayloadKind = payloadKind;
            return Runtime::BuildEditorWorkspaceSnapshot(context).FileImport;
        };

    constexpr std::array allPayloadKinds{
        Assets::AssetPayloadKind::Unknown,
        Assets::AssetPayloadKind::Mesh,
        Assets::AssetPayloadKind::PointCloud,
        Assets::AssetPayloadKind::Graph,
        Assets::AssetPayloadKind::ModelScene,
        Assets::AssetPayloadKind::Texture2D,
    };

    Runtime::EditorFileImportModel model = buildModel(
        "assets/models/model.obj",
        Assets::AssetPayloadKind::Mesh,
        false);
    EXPECT_FALSE(model.Enabled);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::Unknown);
    EXPECT_EQ(
        model.PayloadHintDisabledReason,
        "Asset import requires an available runtime import command surface.");
    EXPECT_EQ(model.ImportDisabledReason, model.PayloadHintDisabledReason);
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array<Assets::AssetPayloadKind, 0>{});
    for (std::size_t i = 0u; i < model.PayloadOptions.size(); ++i)
    {
        EXPECT_EQ(model.PayloadOptions[i].Kind, allPayloadKinds[i]);
        EXPECT_EQ(model.PayloadOptions[i].DisabledReason,
                  model.ImportDisabledReason);
    }

    Intrinsic::Tests::EditorFeatureTestContext flagOnlyContext =
        MakeContext(registry, selection);
    flagOnlyContext.AssetImportCommandsAvailable = true;
    flagOnlyContext.PendingAssetImportPath = "assets/models/model.obj";
    flagOnlyContext.PendingAssetImportPayloadKind =
        Assets::AssetPayloadKind::Mesh;
    model = Runtime::BuildEditorWorkspaceSnapshot(flagOnlyContext).FileImport;
    EXPECT_TRUE(model.Enabled);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(
        model.ImportDisabledReason,
        "Asset import requires an available runtime import command surface.");
    EXPECT_TRUE(HasDiagnostic(
        model.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportUnavailable));

    model = buildModel({}, Assets::AssetPayloadKind::Graph, false);
    EXPECT_FALSE(model.Enabled);
    EXPECT_EQ(
        model.ImportDisabledReason,
        "Asset import requires an available runtime import command surface.");

    model = buildModel({}, Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(model.Enabled);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(
        model.ImportDisabledReason,
        "Enter an asset path before choosing a payload or importing.");
    EXPECT_EQ(model.StatusText, model.ImportDisabledReason);

    model = buildModel("assets/models/model",
                       Assets::AssetPayloadKind::Unknown);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(
        model.ImportDisabledReason,
        "Add a supported file extension to the asset path before importing.");

    model = buildModel("assets/models/model",
                       Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(
        model.ImportDisabledReason,
        "Add a supported file extension to the asset path before importing.");

    model = buildModel("assets/models/Duck0.bin",
                       Assets::AssetPayloadKind::Unknown);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(model.ImportDisabledReason,
              "Asset extension '.bin' is unsupported; choose a path with a "
              "supported asset file extension.");

    model = buildModel("assets/textures/albedo.ktx",
                       Assets::AssetPayloadKind::Unknown);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_EQ(model.ImportDisabledReason,
              "KTX import is unavailable because no promoted Texture2D importer "
              "supports this format; choose a supported asset format.");
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array<Assets::AssetPayloadKind, 0>{});

    model = buildModel("assets/textures/albedo.ktx2",
                       Assets::AssetPayloadKind::Graph);
    EXPECT_FALSE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::Unknown);
    EXPECT_EQ(model.ImportDisabledReason,
              "KTX import is unavailable because no promoted Texture2D importer "
              "supports this format; choose a supported asset format.");
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array<Assets::AssetPayloadKind, 0>{});

    model = buildModel("assets/models/model.obj",
                       Assets::AssetPayloadKind::Graph);
    EXPECT_TRUE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(model.PayloadHintDisabledReason.empty());
    EXPECT_EQ(
        model.ImportDisabledReason,
        "OBJ import requires the Mesh payload; Graph is incompatible.");
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array{
            Assets::AssetPayloadKind::Unknown,
            Assets::AssetPayloadKind::Mesh,
        });

    model = buildModel("assets/models/model.ply",
                       Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(model.CanChoosePayloadHint);
    EXPECT_FALSE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::Unknown);
    EXPECT_EQ(
        model.ImportDisabledReason,
        "PLY import requires an explicit Mesh or PointCloud payload.");
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array{
            Assets::AssetPayloadKind::Mesh,
            Assets::AssetPayloadKind::PointCloud,
        });

    const Runtime::EditorFileImportPayloadOption* unknownOption =
        FindFileImportPayloadOption(
            model,
            Assets::AssetPayloadKind::Unknown);
    ASSERT_NE(unknownOption, nullptr);
    EXPECT_EQ(unknownOption->DisabledReason, model.ImportDisabledReason);

    for (const Assets::AssetPayloadKind explicitPlyKind :
         {Assets::AssetPayloadKind::Mesh,
          Assets::AssetPayloadKind::PointCloud})
    {
        model = buildModel("assets/models/model.ply", explicitPlyKind);
        EXPECT_TRUE(model.CanChoosePayloadHint);
        EXPECT_TRUE(model.CanImport);
        EXPECT_EQ(model.ResolvedPayloadKind, explicitPlyKind);
        EXPECT_TRUE(model.ImportDisabledReason.empty());
        ExpectEnabledFileImportPayloadOptions(
            model,
            std::array{
                Assets::AssetPayloadKind::Mesh,
                Assets::AssetPayloadKind::PointCloud,
            });
    }

    model = buildModel("assets/models/cloud.xyz",
                       Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(model.CanChoosePayloadHint);
    EXPECT_TRUE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::PointCloud);
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array{
            Assets::AssetPayloadKind::Unknown,
            Assets::AssetPayloadKind::PointCloud,
        });

    model = buildModel("assets/models/Duck.glb",
                       Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(model.CanChoosePayloadHint);
    EXPECT_TRUE(model.CanImport);
    EXPECT_EQ(model.ResolvedPayloadKind, Assets::AssetPayloadKind::ModelScene);
    ExpectEnabledFileImportPayloadOptions(
        model,
        std::array{
            Assets::AssetPayloadKind::Unknown,
            Assets::AssetPayloadKind::ModelScene,
        });
}
TEST(SandboxEditorUi, FileImportDispatchRejectsInvalidPrerequisitesBeforeCallback)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    std::size_t callbackCalls = 0u;
    Runtime::EditorFileImportCommand observedCommand{};
    context.AssetImportCommands.Import =
        [&](const Runtime::EditorFileImportCommand& command)
        {
            ++callbackCalls;
            observedCommand = command;
            return Runtime::EditorFileImportResult{
                .Status = Runtime::EditorCommandStatus::Applied,
                .PayloadKind = command.PayloadKind,
            };
        };

    struct InvalidCase
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string_view Reason{};
    };
    constexpr std::array invalidCases{
        InvalidCase{
            .Path   = {},
            .Error  = Core::ErrorCode::InvalidPath,
            .Reason = "Enter an asset path before choosing a payload or importing.",
        },
        InvalidCase{
            .Path   = "assets/models/model",
            .Error  = Core::ErrorCode::InvalidPath,
            .Reason = "Add a supported file extension to the asset path before "
                      "importing.",
        },
        InvalidCase{
            .Path   = "assets/models/Duck0.bin",
            .Error  = Core::ErrorCode::AssetUnsupportedFormat,
            .Reason = "Asset extension '.bin' is unsupported; choose a path with "
                      "a supported asset file extension.",
        },
        InvalidCase{
            .Path   = "assets/textures/albedo.ktx",
            .Error  = Core::ErrorCode::AssetUnsupportedFormat,
            .Reason = "KTX import is unavailable because no promoted Texture2D "
                      "importer supports this format; choose a supported asset format.",
        },
        InvalidCase{
            .Path        = "assets/textures/albedo.ktx2",
            .PayloadKind = Assets::AssetPayloadKind::Graph,
            .Error       = Core::ErrorCode::AssetUnsupportedFormat,
            .Reason      = "KTX import is unavailable because no promoted Texture2D "
                           "importer supports this format; choose a supported asset format.",
        },
        InvalidCase{
            .Path   = "assets/models/model.ply",
            .Error  = Core::ErrorCode::InvalidArgument,
            .Reason = "PLY import requires an explicit Mesh or PointCloud payload.",
        },
        InvalidCase{
            .Path        = "assets/models/model.obj",
            .PayloadKind = Assets::AssetPayloadKind::Graph,
            .Error       = Core::ErrorCode::InvalidArgument,
            .Reason      = "OBJ import requires the Mesh payload; Graph is incompatible.",
        },
    };

    for (const InvalidCase& invalid : invalidCases)
    {
        SCOPED_TRACE(invalid.Path);
        const Runtime::EditorFileImportResult result =
            Runtime::ApplyEditorFileImportCommand(
                context,
                Runtime::EditorFileImportCommand{
                    .Path = std::string{invalid.Path},
                    .PayloadKind = invalid.PayloadKind,
                });
        EXPECT_EQ(result.Status,
                  Runtime::EditorCommandStatus::AssetImportFailed);
        EXPECT_EQ(result.Error, invalid.Error);
        EXPECT_EQ(result.Message, invalid.Reason);
        EXPECT_EQ(callbackCalls, 0u);
    }

    const Runtime::EditorFileImportResult valid =
        Runtime::ApplyEditorFileImportCommand(
            context,
            Runtime::EditorFileImportCommand{
                .Path = "assets/models/cloud.xyz",
                .PayloadKind = Assets::AssetPayloadKind::Unknown,
            });
    EXPECT_TRUE(valid.Succeeded());
    EXPECT_EQ(callbackCalls, 1u);
    EXPECT_EQ(observedCommand.Path, "assets/models/cloud.xyz");
    EXPECT_EQ(observedCommand.PayloadKind,
              Assets::AssetPayloadKind::PointCloud);
}
TEST(SandboxEditorUi, FileImportCommandRoutesThroughRuntimeOwnedSurface)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    bool commandObserved = false;
    Runtime::EditorFileImportCommand observedCommand{};
    context.AssetImportCommands =
        Runtime::EditorAssetImportCommandSurface{
            .Import =
                [&](const Runtime::EditorFileImportCommand& command)
                {
                    commandObserved = true;
                    observedCommand = command;
                    return Runtime::EditorFileImportResult{
                        .Status = Runtime::EditorCommandStatus::Applied,
                        .Asset = Assets::AssetId{7u, 2u},
                        .PayloadKind = Assets::AssetPayloadKind::ModelScene,
                        .PrimitiveEntitiesCreated = 1u,
                        .EmbeddedTextureAssetsCreated = 2u,
                        .TextureUploadRequests = 3u,
                        .MaterializedModelScene = true,
                        .Message = "Imported fake model.",
                    };
                },
        };
    context.PendingAssetImportPath = "assets/models/Duck.gltf";
    context.PendingAssetImportPayloadKind = Assets::AssetPayloadKind::Graph;

    Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    EXPECT_TRUE(frame.FileImport.Enabled);
    EXPECT_TRUE(frame.FileImport.CanChoosePayloadHint);
    EXPECT_FALSE(frame.FileImport.CanImport);
    EXPECT_EQ(frame.FileImport.PendingPath, "assets/models/Duck.gltf");
    EXPECT_EQ(frame.FileImport.PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(frame.FileImport.ResolvedPayloadKind,
              Assets::AssetPayloadKind::Unknown);
    EXPECT_EQ(frame.FileImport.ImportDisabledReason,
              "GLTF import requires the ModelScene payload; Graph is incompatible.");
    ExpectEnabledFileImportPayloadOptions(
        frame.FileImport,
        std::array{
            Assets::AssetPayloadKind::Unknown,
            Assets::AssetPayloadKind::ModelScene,
        });
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportUnavailable));

    const Runtime::EditorFileImportResult incompatible =
        Runtime::ApplyEditorFileImportCommand(
            context,
            Runtime::EditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
                .PayloadKind = Assets::AssetPayloadKind::Graph,
            });
    EXPECT_FALSE(commandObserved);
    EXPECT_EQ(incompatible.Status,
              Runtime::EditorCommandStatus::AssetImportFailed);
    EXPECT_EQ(incompatible.Error, Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(incompatible.Message, frame.FileImport.ImportDisabledReason);

    const Runtime::EditorFileImportResult result =
        Runtime::ApplyEditorFileImportCommand(
            context,
            Runtime::EditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
                .PayloadKind = Assets::AssetPayloadKind::Unknown,
            });

    EXPECT_TRUE(commandObserved);
    EXPECT_EQ(observedCommand.Path, "assets/models/Duck.gltf");
    EXPECT_EQ(observedCommand.PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Asset, (Assets::AssetId{7u, 2u}));
    EXPECT_EQ(result.PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(result.PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(result.EmbeddedTextureAssetsCreated, 2u);
    EXPECT_EQ(result.TextureUploadRequests, 3u);
    EXPECT_TRUE(result.MaterializedModelScene);
    EXPECT_STREQ(Runtime::DebugNameForEditorCommandStatus(result.Status),
                 "Applied");

    context.PendingAssetImportPayloadKind = Assets::AssetPayloadKind::Unknown;
    context.LastAssetImportResult = &result;
    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.FileImport.LastResult.has_value());
    EXPECT_EQ(frame.FileImport.StatusText, "Imported fake model.");
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportFailed));

    Intrinsic::Tests::EditorFeatureTestContext missingSurface =
        MakeContext(registry, selection);
    const Runtime::EditorFileImportResult missing =
        Runtime::ApplyEditorFileImportCommand(
            missingSurface,
            Runtime::EditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
            });
    EXPECT_EQ(missing.Status,
              Runtime::EditorCommandStatus::MissingAssetImportCommands);
    EXPECT_EQ(missing.Error, Core::ErrorCode::InvalidState);
    EXPECT_EQ(
        missing.Message,
        "Asset import requires an available runtime import command surface.");
    EXPECT_STREQ(Runtime::DebugNameForEditorCommandStatus(
                     missing.Status),
                 "MissingAssetImportCommands");

    const Runtime::EditorFileImportResult empty =
        Runtime::ApplyEditorFileImportCommand(
            context,
            Runtime::EditorFileImportCommand{});
    EXPECT_EQ(empty.Status,
              Runtime::EditorCommandStatus::AssetImportFailed);
    EXPECT_EQ(empty.Error, Core::ErrorCode::InvalidPath);
    EXPECT_EQ(
        empty.Message,
        "Enter an asset path before choosing a payload or importing.");
    EXPECT_STREQ(Runtime::DebugNameForEditorDiagnosticCode(
                     Runtime::EditorDiagnosticCode::AssetImportFailed),
                 "AssetImportFailed");

    context.LastAssetImportResult = &empty;
    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    EXPECT_TRUE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportFailed));
}
TEST(SandboxEditorUi, FileImportCommandTreatsAsyncPendingAsNonFailure)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::RuntimeAssetIngestHandle queuedHandle{42u, 7u};
    context.AssetImportCommands =
        Runtime::EditorAssetImportCommandSurface{
            .Import =
                [queuedHandle](const Runtime::EditorFileImportCommand&)
                {
                    return Runtime::EditorFileImportResult{
                        .Status = Runtime::EditorCommandStatus::Pending,
                        .Operation = queuedHandle,
                        .PayloadKind = Assets::AssetPayloadKind::Texture2D,
                    };
                },
        };

    const Runtime::EditorFileImportResult result =
        Runtime::ApplyEditorFileImportCommand(
            context,
            Runtime::EditorFileImportCommand{
                .Path = "assets/textures/albedo.png",
                .PayloadKind = Assets::AssetPayloadKind::Unknown,
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Operation, queuedHandle);
    EXPECT_EQ(result.Error, Core::ErrorCode::Success);
    EXPECT_EQ(result.PayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_NE(result.Message.find("Queued"), std::string::npos);

    context.PendingAssetImportPath = "assets/textures/albedo.png";
    context.PendingAssetImportPayloadKind = Assets::AssetPayloadKind::Unknown;
    context.LastAssetImportResult = &result;
    Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.FileImport.LastResult.has_value());
    EXPECT_EQ(frame.FileImport.LastResult->Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(frame.FileImport.StatusText, result.Message);
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportFailed));
}
TEST(SandboxEditorUi, SceneFileCommandRoutesThroughRuntimeOwnedSurface)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    bool saveObserved = false;
    bool loadObserved = false;
    bool newObserved = false;
    bool closeObserved = false;
    Runtime::EditorSceneFileCommand observedSave{};
    Runtime::EditorSceneFileCommand observedLoad{};
    context.SceneFileCommands = Runtime::EditorSceneFileCommandSurface{
        .New =
            [&]()
            {
                newObserved = true;
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Applied,
                    .Operation = Runtime::EditorSceneFileOperation::New,
                    .Message = "Created fake scene.",
                };
            },
        .Save =
            [&](const Runtime::EditorSceneFileCommand& command)
            {
                saveObserved = true;
                observedSave = command;
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Applied,
                    .Operation = Runtime::EditorSceneFileOperation::Save,
                    .Stats = Runtime::SceneSerializationStats{
                        .Entities = 3u,
                        .MeshEntities = 1u,
                        .GraphEntities = 1u,
                        .PointCloudEntities = 1u,
                    },
                    .Message = "Saved fake scene.",
                };
            },
        .Load =
            [&](const Runtime::EditorSceneFileCommand& command)
            {
                loadObserved = true;
                observedLoad = command;
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Applied,
                    .Operation = Runtime::EditorSceneFileOperation::Load,
                    .Stats = Runtime::SceneSerializationStats{.Entities = 2u},
                    .Message = "Loaded fake scene.",
                };
            },
        .Close =
            [&]()
            {
                closeObserved = true;
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Applied,
                    .Operation = Runtime::EditorSceneFileOperation::Close,
                    .Message = "Closed fake scene.",
                };
            },
    };
    context.PendingSceneFilePath = "scene.extrinsic.json";

    Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    EXPECT_TRUE(frame.SceneFile.Enabled);
    EXPECT_TRUE(frame.SceneFile.LifecycleEnabled);
    EXPECT_TRUE(frame.SceneFile.CanNew);
    EXPECT_TRUE(frame.SceneFile.CanClose);
    EXPECT_TRUE(frame.SceneFile.CanSave);
    EXPECT_TRUE(frame.SceneFile.CanOpen);
    EXPECT_TRUE(frame.SceneFile.PathEntryEnabled);
    EXPECT_FALSE(frame.SceneFile.NativeDialogsAvailable);
    EXPECT_TRUE(frame.SceneFile.FileDialogBoundaryText.find("Native file dialogs") !=
                std::string::npos);
    EXPECT_EQ(frame.SceneFile.PendingPath, "scene.extrinsic.json");
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::EditorDiagnosticCode::SceneFileUnavailable));

    const Runtime::EditorSceneFileResult save =
        Runtime::ApplyEditorSceneSaveCommand(
            context,
            Runtime::EditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_TRUE(saveObserved);
    EXPECT_EQ(observedSave.Path, "scene.extrinsic.json");
    EXPECT_TRUE(save.Succeeded());
    EXPECT_EQ(save.Stats.Entities, 3u);
    EXPECT_STREQ(Runtime::DebugNameForEditorCommandStatus(save.Status),
                 "Applied");

    const Runtime::EditorSceneFileResult created =
        Runtime::ApplyEditorNewSceneCommand(context);
    EXPECT_TRUE(newObserved);
    EXPECT_TRUE(created.Succeeded());
    EXPECT_EQ(created.Operation, Runtime::EditorSceneFileOperation::New);
    EXPECT_STREQ(Runtime::DebugNameForEditorCommandStatus(
                     Runtime::EditorCommandStatus::SceneNewFailed),
                 "SceneNewFailed");

    const Runtime::EditorSceneFileResult load =
        Runtime::ApplyEditorSceneLoadCommand(
            context,
            Runtime::EditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_TRUE(loadObserved);
    EXPECT_EQ(observedLoad.Path, "scene.extrinsic.json");
    EXPECT_TRUE(load.Succeeded());
    EXPECT_EQ(load.Stats.Entities, 2u);

    const Runtime::EditorSceneFileResult closed =
        Runtime::ApplyEditorCloseSceneCommand(context);
    EXPECT_TRUE(closeObserved);
    EXPECT_TRUE(closed.Succeeded());
    EXPECT_EQ(closed.Operation, Runtime::EditorSceneFileOperation::Close);
    EXPECT_STREQ(Runtime::DebugNameForEditorCommandStatus(
                     Runtime::EditorCommandStatus::SceneCloseFailed),
                 "SceneCloseFailed");

    context.LastSceneFileResult = &load;
    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.StatusText, "Loaded fake scene.");
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::EditorDiagnosticCode::SceneFileFailed));

    Intrinsic::Tests::EditorFeatureTestContext missingSurface =
        MakeContext(registry, selection);
    const Runtime::EditorSceneFileResult missing =
        Runtime::ApplyEditorSceneSaveCommand(
            missingSurface,
            Runtime::EditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_EQ(missing.Status,
              Runtime::EditorCommandStatus::MissingSceneFileCommands);
    EXPECT_EQ(missing.Error, Core::ErrorCode::InvalidState);
    EXPECT_STREQ(Runtime::DebugNameForEditorCommandStatus(
                     missing.Status),
                 "MissingSceneFileCommands");

    const Runtime::EditorSceneFileResult emptySave =
        Runtime::ApplyEditorSceneSaveCommand(
            context,
            Runtime::EditorSceneFileCommand{});
    EXPECT_EQ(emptySave.Status,
              Runtime::EditorCommandStatus::SceneSaveFailed);
    EXPECT_EQ(emptySave.Error, Core::ErrorCode::InvalidPath);
    EXPECT_STREQ(Runtime::DebugNameForEditorDiagnosticCode(
                     Runtime::EditorDiagnosticCode::SceneFileFailed),
                 "SceneFileFailed");

    context.LastSceneFileResult = &emptySave;
    frame = Runtime::BuildEditorWorkspaceSnapshot(context);
    EXPECT_TRUE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::EditorDiagnosticCode::SceneFileFailed));
}
TEST(SandboxEditorUi, SceneLoadCommandTreatsAsyncPendingAsNonFailure)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::JobToken queuedTask{42u, 7u};
    context.SceneFileCommands = Runtime::EditorSceneFileCommandSurface{
        .Save =
            [](const Runtime::EditorSceneFileCommand&)
            {
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Applied,
                    .Operation = Runtime::EditorSceneFileOperation::Save,
                };
            },
        .Load =
            [queuedTask](const Runtime::EditorSceneFileCommand&)
            {
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Pending,
                    .Operation = Runtime::EditorSceneFileOperation::Load,
                    .Task = queuedTask,
                };
            },
    };

    const Runtime::EditorSceneFileResult result =
        Runtime::ApplyEditorSceneLoadCommand(
            context,
            Runtime::EditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Operation, Runtime::EditorSceneFileOperation::Load);
    EXPECT_EQ(result.Task, queuedTask);
    EXPECT_EQ(result.Error, Core::ErrorCode::Success);
    EXPECT_NE(result.Message.find("Queued scene open"), std::string::npos);

    context.LastSceneFileResult = &result;
    Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.LastResult->Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(frame.SceneFile.StatusText, result.Message);
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::EditorDiagnosticCode::SceneFileFailed));
}
TEST(SandboxEditorUi, SceneSaveCommandTreatsAsyncPendingAsNonFailure)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Intrinsic::Tests::EditorFeatureTestContext context = MakeContext(registry, selection);

    const Runtime::JobToken queuedTask{43u, 7u};
    context.SceneFileCommands = Runtime::EditorSceneFileCommandSurface{
        .Save =
            [queuedTask](const Runtime::EditorSceneFileCommand&)
            {
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Pending,
                    .Operation = Runtime::EditorSceneFileOperation::Save,
                    .Task = queuedTask,
                };
            },
        .Load =
            [](const Runtime::EditorSceneFileCommand&)
            {
                return Runtime::EditorSceneFileResult{
                    .Status = Runtime::EditorCommandStatus::Applied,
                    .Operation = Runtime::EditorSceneFileOperation::Load,
                };
            },
    };

    const Runtime::EditorSceneFileResult result =
        Runtime::ApplyEditorSceneSaveCommand(
            context,
            Runtime::EditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });

    EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::Pending);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Operation, Runtime::EditorSceneFileOperation::Save);
    EXPECT_EQ(result.Task, queuedTask);
    EXPECT_EQ(result.Error, Core::ErrorCode::Success);
    EXPECT_NE(result.Message.find("Queued scene save"), std::string::npos);

    context.LastSceneFileResult = &result;
    Runtime::EditorWorkspaceSnapshot frame =
        Runtime::BuildEditorWorkspaceSnapshot(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.LastResult->Status,
              Runtime::EditorCommandStatus::Pending);
    EXPECT_EQ(frame.SceneFile.StatusText, result.Message);
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::EditorDiagnosticCode::SceneFileFailed));
}
TEST(SandboxEditorUi, AssetWorkflowReportsMissingFile)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.Initialize();

    auto imported = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = "/tmp/intrinsicengine-ui-001-missing.gltf",
        });
    EXPECT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error(), Core::ErrorCode::FileNotFound);

    engine.Shutdown();
}
TEST(SandboxEditorUi, AssetWorkflowMaterializesStandaloneGeometryDomains)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n");
    TmpFile graphFile(
        "runtime_dragdrop_import_graph.tgf",
        "1 0 0 0 first\n"
        "2 1 0 0 second\n"
        "#\n"
        "1 2 1.0 edge\n");
    TmpFile cloudFile(
        "runtime_dragdrop_import_cloud.xyz",
        "0 0 0\n"
        "1 2 3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    auto mesh = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    auto graph = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = graphFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Graph,
        });
    ASSERT_TRUE(graph.has_value()) << static_cast<int>(graph.error());
    EXPECT_TRUE(graph->Asset.IsValid());
    EXPECT_EQ(graph->PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(graph->PrimitiveEntitiesCreated, 1u);

    auto cloud = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = cloudFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::PointCloud,
        });
    ASSERT_TRUE(cloud.has_value()) << static_cast<int>(cloud.error());
    EXPECT_TRUE(cloud->Asset.IsValid());
    EXPECT_EQ(cloud->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_EQ(cloud->PrimitiveEntitiesCreated, 1u);

    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Graph), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::PointCloud), 1u);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderSurface>(*meshEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*meshEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*meshEntity)));
    const auto& meshWorld =
        raw.get<ECSC::Culling::World::Bounds>(*meshEntity);
    EXPECT_NEAR(meshWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(meshWorld.WorldBoundingSphere.Center.y, 0.5f, 1.0e-5f);
    EXPECT_GT(meshWorld.WorldBoundingSphere.Radius, 0.5f);

    const std::optional<ECS::EntityHandle> graphEntity =
        FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Graph);
    ASSERT_TRUE(graphEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderEdges>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*graphEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*graphEntity)));
    const auto& graphWorld =
        raw.get<ECSC::Culling::World::Bounds>(*graphEntity);
    EXPECT_NEAR(graphWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(graphWorld.WorldBoundingSphere.Center.y, 0.0f, 1.0e-5f);
    EXPECT_GT(graphWorld.WorldBoundingSphere.Radius, 0.4f);

    const std::optional<ECS::EntityHandle> cloudEntity =
        FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::PointCloud);
    ASSERT_TRUE(cloudEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*cloudEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*cloudEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*cloudEntity)));
    const auto& cloudWorld =
        raw.get<ECSC::Culling::World::Bounds>(*cloudEntity);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.z, 1.5f, 1.0e-5f);
    EXPECT_GT(cloudWorld.WorldBoundingSphere.Radius, 1.8f);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_NE(lastEvent->Path.find("runtime_dragdrop_import_cloud.xyz"),
              std::string::npos);

    engine.Shutdown();
}
TEST(SandboxEditorUi, AssetWorkflowMaterializesNonManifoldObjAsRenderableMesh)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_nonmanifold.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 0 -1 0\n"
        "v 0.5 0 1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vt 0 -1\n"
        "vt 0.5 0.5\n"
        "f 1/1 2/2 3/3\n"
        "f 2/2 1/1 4/4\n"
        "f 1/1 2/2 5/5\n");

    std::optional<ECS::EntityHandle> meshEntity{};
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForConditionApplication>(
                              [&meshEntity](Runtime::Engine& runningEngine)
                              {
                                  return meshEntity.has_value() &&
                                         DirectMeshPostProcessReady(runningEngine, *meshEntity);
                              }));
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    auto mesh = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    meshEntity = FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE((raw.all_of<ECSC::MetaData,
                            ECSC::Hierarchy::Component,
                            ECSC::Transform::Component,
                            ECSC::Transform::WorldMatrix,
                            Sel::SelectableTag,
                            G::RenderSurface,
                            G::VisualizationConfig,
                            GS::Vertices,
                            GS::Edges,
                            GS::Halfedges,
                            GS::Faces>(*meshEntity)));
    EXPECT_EQ(raw.get<G::RenderSurface>(*meshEntity).Domain,
              G::RenderSurface::SourceDomain::Vertex);

    engine.Run();
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));

    Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(*engine.Worlds().Get(engine.ActiveWorld()),
                                                   engine.GetRenderer(),
                                                   &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_GE(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
TEST(SandboxEditorUi, AssetWorkflowMaterializesObjWithoutAuthoredTexcoordsAsRenderableMesh)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_missing_uv.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    std::optional<ECS::EntityHandle> meshEntity{};
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForConditionApplication>(
                              [&meshEntity](Runtime::Engine& runningEngine)
                              {
                                  return meshEntity.has_value() &&
                                         DirectMeshPostProcessReady(runningEngine, *meshEntity);
                              }));
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    auto mesh = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    meshEntity = FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());

    engine.Run();
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    ASSERT_TRUE(raw.all_of<G::RenderSurface>(*meshEntity));
    const GS::ConstSourceView view = GS::BuildConstView(raw, *meshEntity);
    ASSERT_TRUE(view.Valid());
    ASSERT_NE(view.VertexSource, nullptr);
    const auto texcoords = view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    ASSERT_EQ(texcoords.Vector().size(), 3u);

    bool sawNonZeroTexcoord = false;
    for (const glm::vec2 uv : texcoords.Vector())
    {
        EXPECT_TRUE(std::isfinite(uv.x));
        EXPECT_TRUE(std::isfinite(uv.y));
        sawNonZeroTexcoord = sawNonZeroTexcoord ||
            std::abs(uv.x) > 1.0e-6f ||
            std::abs(uv.y) > 1.0e-6f;
    }
    EXPECT_TRUE(sawNonZeroTexcoord);

    Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(*engine.Worlds().Get(engine.ActiveWorld()),
                                                   engine.GetRenderer(),
                                                   &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 0u);
    EXPECT_GE(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(SandboxEditorUi, QueuedManualGeometryImportsRemainResponsiveAndApplyOnce)
{
    TmpFile meshFile(
        "runtime_manual_queue_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    TmpFile graphFile(
        "runtime_manual_queue_graph.tgf",
        "1 0 0 0 first\n"
        "2 1 0 0 second\n"
        "#\n"
        "1 2 1.0 edge\n");
    TmpFile cloudFile(
        "runtime_manual_queue_cloud.xyz",
        "0 0 0\n"
        "1 2 3\n");

    struct ImportCase
    {
        std::filesystem::path Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        GS::Domain Domain{GS::Domain::None};
    };
    const std::array cases{
        ImportCase{meshFile.Path, Assets::AssetPayloadKind::Mesh, GS::Domain::Mesh},
        ImportCase{graphFile.Path, Assets::AssetPayloadKind::Graph, GS::Domain::Graph},
        ImportCase{
            cloudFile.Path,
            Assets::AssetPayloadKind::PointCloud,
            GS::Domain::PointCloud},
    };

    for (const ImportCase& importCase : cases)
    {
        SCOPED_TRACE(Assets::DebugNameForAssetPayloadKind(importCase.PayloadKind));
        auto decodeState = std::make_shared<BlockingGeometryDecodeState>();
        auto application =
            std::make_unique<DriveBlockedGeometryImportApplication>(decodeState);

        Core::Config::EngineConfig config = HeadlessConfig();
        config.Camera.Enabled = true;
        Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(application));
        ComposeAsyncWorkAndInitialize(engine);
        InstallSandboxDefaultRuntimePolicies(engine);
        decodeState->BaselineLiveAssetCount =
            RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount();

        auto recordingController =
            std::make_unique<RecordingImportCameraController>();
        RecordingImportCameraController* recorder = recordingController.get();
        engine.Services()
            .Find<Runtime::CameraControllerRegistry>()
            ->Replace(
            Runtime::CameraControllerSlot::Main,
            std::move(recordingController));

        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
            .SetQueuedGeometryImportBeforeDecodeHookForTest(
                [decodeState](const Runtime::RuntimeAssetImportRequest& request)
                {
                    decodeState->ObservedPayloadKind.store(
                        static_cast<std::uint32_t>(request.PayloadKind),
                        std::memory_order_release);
                    decodeState->Started.store(true, std::memory_order_release);
                    while (!decodeState->Release.load(std::memory_order_acquire))
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1));
                    }
                });

        Runtime::EditorWorkspaceAttachment attachment;
        attachment.Attach(engine.Worlds(), engine.Services());
        ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                        attachment,
                        {},
                        importCase.Path.string(),
                        importCase.PayloadKind)
                        .has_value());

        std::optional<Runtime::EditorFileImportResult> commandResult{};
        const Runtime::EditorSceneEditingPreparedFrame prepared =
            Runtime::PrepareEditorSceneEditingFrame(attachment);
        commandResult = Runtime::ApplyEditorFileImportCommand(
            prepared.Commands,
            Runtime::EditorFileImportCommand{
                .Path = importCase.Path.string(),
                .PayloadKind = importCase.PayloadKind,
            });
        ASSERT_TRUE(commandResult.has_value());
        EXPECT_EQ(commandResult->Status, Runtime::EditorCommandStatus::Pending);
        EXPECT_TRUE(commandResult->Operation.IsValid());
        EXPECT_EQ(commandResult->PayloadKind, importCase.PayloadKind);
        decodeState->Operation = commandResult->Operation;
        EXPECT_FALSE(engine.Services().Find<Runtime::EditorCommandHistory>()->IsDirty());
        EXPECT_TRUE(Selection(engine).SelectedStableIds().empty());
        EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), importCase.Domain), 0u);

        Runtime::RuntimeAssetImportQueueSnapshot queue =
            RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
        ASSERT_EQ(queue.Entries.size(), 1u);
        EXPECT_EQ(queue.ActiveCount, 1u);
        EXPECT_EQ(queue.Entries[0].Source,
                  Runtime::RuntimeAssetIngestSource::ManualImport);
        EXPECT_EQ(queue.Entries[0].Operation, commandResult->Operation);

        engine.Run();

        EXPECT_TRUE(decodeState->Started.load(std::memory_order_acquire));
        EXPECT_GE(
            decodeState->FramesWhileBlocked.load(std::memory_order_acquire),
            3u);
        EXPECT_TRUE(decodeState->ImGuiFrameBaselineCaptured);
        EXPECT_GT(
            decodeState->ImGuiFramesProducedWhileBlocked,
            decodeState->ImGuiFramesProducedAtBlockStart);
        EXPECT_FALSE(decodeState->MutationObservedWhileBlocked.load(
            std::memory_order_acquire));
        EXPECT_EQ(
            decodeState->ObservedPayloadKind.load(std::memory_order_acquire),
            static_cast<std::uint32_t>(importCase.PayloadKind));

        queue = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
        ASSERT_EQ(queue.Entries.size(), 1u);
        EXPECT_EQ(queue.ActiveCount, 0u);
        EXPECT_EQ(queue.TerminalCount, 1u);
        EXPECT_EQ(queue.Entries[0].TerminalStatus,
                  Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);
        EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), importCase.Domain), 1u);
        EXPECT_TRUE(engine.Services().Find<Runtime::EditorCommandHistory>()->IsDirty());
        EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Revision, 1u);
        EXPECT_EQ(Selection(engine).SelectedStableIds().size(), 1u);
        EXPECT_EQ(recorder->FocusCalls, 1u);
        EXPECT_TRUE(recorder->LastFocus.has_value());

        const auto importedEntity =
            FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), importCase.Domain);
        ASSERT_TRUE(importedEntity.has_value());
        EXPECT_EQ(
            Selection(engine).SelectedStableIds().front(),
            Runtime::SelectionController::ToStableEntityId(*importedEntity));

        const std::vector<Runtime::RuntimeAssetIngestRecord> records =
            RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetIngestRecordsForTest();
        ASSERT_EQ(records.size(), 1u);
        EXPECT_EQ(records[0].Request.Source,
                  Runtime::RuntimeAssetIngestSource::ManualImport);
        EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
        ASSERT_TRUE(records[0].Result.has_value());
        EXPECT_EQ(records[0].Result->PayloadKind, importCase.PayloadKind);
        EXPECT_EQ(records[0].Result->PrimitiveEntitiesCreated, 1u);

        const auto& event =
            RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
        ASSERT_TRUE(event.has_value());
        EXPECT_TRUE(event->Succeeded());
        EXPECT_EQ(event->Sequence, 1u);
        ASSERT_TRUE(event->Result.has_value());
        EXPECT_EQ(event->Result->PayloadKind, importCase.PayloadKind);
        EXPECT_TRUE(event->Result->Asset.IsValid());
        EXPECT_TRUE(RequiredEngineService<Extrinsic::Assets::AssetService>(engine).IsAlive(event->Result->Asset));

        attachment.Detach();
        engine.Shutdown();
    }
}

TEST(SandboxEditorUi, QueuedManualGeometryCancellationPreventsApply)
{
    TmpFile meshFile(
        "runtime_manual_queue_cancel_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    auto decodeState = std::make_shared<BlockingGeometryDecodeState>();
    auto application = std::make_unique<DriveBlockedGeometryImportApplication>(
        decodeState,
        BlockedGeometryImportAction::Cancel);

    Core::Config::EngineConfig config = HeadlessConfig();
    config.Camera.Enabled = true;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(application));
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);
    decodeState->BaselineLiveAssetCount =
        RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount();

    auto recordingController = std::make_unique<RecordingImportCameraController>();
    RecordingImportCameraController* recorder = recordingController.get();
    engine.Services()
        .Find<Runtime::CameraControllerRegistry>()
        ->Replace(
        Runtime::CameraControllerSlot::Main,
        std::move(recordingController));

    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
        .SetQueuedGeometryImportBeforeDecodeHookForTest(
            [decodeState](const Runtime::RuntimeAssetImportRequest& request)
            {
                decodeState->ObservedPayloadKind.store(
                    static_cast<std::uint32_t>(request.PayloadKind),
                    std::memory_order_release);
                decodeState->Started.store(true, std::memory_order_release);
                while (!decodeState->Release.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });

    Runtime::EditorWorkspaceAttachment attachment;
    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                    attachment,
                    {},
                    meshFile.Path.string(),
                    Assets::AssetPayloadKind::Mesh)
                    .has_value());

    std::optional<Runtime::EditorFileImportResult> commandResult{};
    const Runtime::EditorSceneEditingPreparedFrame prepared =
        Runtime::PrepareEditorSceneEditingFrame(attachment);
    commandResult = Runtime::ApplyEditorFileImportCommand(
        prepared.Commands,
        Runtime::EditorFileImportCommand{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(commandResult.has_value());
    ASSERT_EQ(commandResult->Status, Runtime::EditorCommandStatus::Pending);
    ASSERT_TRUE(commandResult->Operation.IsValid());
    decodeState->Operation = commandResult->Operation;

    engine.Run();

    EXPECT_TRUE(decodeState->Started.load(std::memory_order_acquire));
    EXPECT_GE(
        decodeState->FramesWhileBlocked.load(std::memory_order_acquire),
        3u);
    EXPECT_FALSE(decodeState->MutationObservedWhileBlocked.load(
        std::memory_order_acquire));
    EXPECT_TRUE(decodeState->CancelAttempted);
    EXPECT_TRUE(decodeState->CancelSucceeded);
    EXPECT_EQ(
        decodeState->ObservedPayloadKind.load(std::memory_order_acquire),
        static_cast<std::uint32_t>(Assets::AssetPayloadKind::Mesh));

    const Runtime::RuntimeAssetImportQueueSnapshot queue =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 1u);
    EXPECT_EQ(queue.Entries[0].Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Cancelled);
    EXPECT_EQ(records[0].Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::Cancelled);
    EXPECT_FALSE(records[0].Result.has_value());

    const auto& event =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_FALSE(event->Succeeded());
    EXPECT_EQ(event->Sequence, 1u);
    EXPECT_EQ(event->RequestedPayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(event->Error, Core::ErrorCode::InvalidState);
    EXPECT_EQ(event->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::Cancelled);

    EXPECT_EQ(RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount(),
              decodeState->BaselineLiveAssetCount);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 0u);
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Revision, 0u);
    EXPECT_TRUE(Selection(engine).SelectedStableIds().empty());
    EXPECT_EQ(recorder->FocusCalls, 0u);

    attachment.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, ShutdownCancelsBlockedManualGeometryBeforeSessionTeardown)
{
    TmpFile meshFile(
        "runtime_manual_queue_shutdown_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    auto shutdownState =
        std::make_shared<ShutdownBlockedGeometryImportState>();
    auto application =
        std::make_unique<ShutdownWhileGeometryImportBlockedApplication>(
            shutdownState);
    ShutdownWhileGeometryImportBlockedApplication* applicationPtr =
        application.get();

    Core::Config::EngineConfig config = HeadlessConfig();
    config.Camera.Enabled = true;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(application));
    ComposeAsyncWorkAndInitialize(engine);
    ASSERT_EQ(shutdownState->InitializeCalls, 1u);
    ASSERT_TRUE(shutdownState->RuntimeReady);
    shutdownState->BaselineLiveAssetCount =
        RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount();
    Runtime::AssetWorkflowModule* const pipeline =
        &RequiredEngineService<
            Extrinsic::Runtime::AssetWorkflowModule>(engine);

    pipeline->SetQueuedGeometryImportBeforeDecodeHookForTest(
            [shutdownState](const Runtime::RuntimeAssetImportRequest&)
            {
                shutdownState->Started.store(true, std::memory_order_release);
                while (!shutdownState->Release.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });

    Runtime::EditorWorkspaceAttachment& attachment = applicationPtr->Editor();
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                    attachment,
                    {},
                    meshFile.Path.string(),
                    Assets::AssetPayloadKind::Mesh)
                    .has_value());

    std::optional<Runtime::EditorFileImportResult> commandResult{};
    const Runtime::EditorSceneEditingPreparedFrame prepared =
        Runtime::PrepareEditorSceneEditingFrame(attachment);
    commandResult = Runtime::ApplyEditorFileImportCommand(
        prepared.Commands,
        Runtime::EditorFileImportCommand{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(commandResult.has_value());
    ASSERT_EQ(commandResult->Status, Runtime::EditorCommandStatus::Pending);
    ASSERT_TRUE(commandResult->Operation.IsValid());
    shutdownState->Operation = commandResult->Operation;

    engine.Run();

    ASSERT_TRUE(shutdownState->Started.load(std::memory_order_acquire));
    EXPECT_TRUE(shutdownState->ExitRequestedWhileWorkerBlocked);
    EXPECT_FALSE(shutdownState->Release.load(std::memory_order_acquire));
    EXPECT_EQ(RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount(),
              shutdownState->BaselineLiveAssetCount);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 0u);
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Revision, 0u);
    EXPECT_TRUE(Selection(engine).SelectedStableIds().empty());

    const Runtime::RuntimeAssetImportQueueSnapshot activeQueue =
        pipeline->GetAssetImportQueueSnapshot();
    ASSERT_EQ(activeQueue.Entries.size(), 1u);
    EXPECT_EQ(activeQueue.ActiveCount, 1u);
    EXPECT_EQ(activeQueue.TerminalCount, 0u);
    EXPECT_EQ(activeQueue.Entries[0].Operation, shutdownState->Operation);
    EXPECT_EQ(activeQueue.Entries[0].Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);

    engine.Shutdown();

    EXPECT_EQ(shutdownState->ShutdownCalls, 1u);
    EXPECT_TRUE(shutdownState->SessionDetachedBeforeWorkerRelease);
    EXPECT_TRUE(shutdownState->WorkerReleasedFromOnShutdown);
    EXPECT_TRUE(shutdownState->Release.load(std::memory_order_acquire));

    const Runtime::RuntimeAssetImportQueueSnapshot cancelledQueue =
        pipeline->GetAssetImportQueueSnapshot();
    ASSERT_EQ(cancelledQueue.Entries.size(), 1u);
    EXPECT_EQ(cancelledQueue.ActiveCount, 0u);
    EXPECT_EQ(cancelledQueue.TerminalCount, 1u);
    EXPECT_EQ(cancelledQueue.Entries[0].Operation, shutdownState->Operation);
    EXPECT_EQ(cancelledQueue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        pipeline->GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Handle, shutdownState->Operation);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Cancelled);
    EXPECT_EQ(records[0].Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::Cancelled);
    EXPECT_FALSE(records[0].Result.has_value());

    const auto& event =
        pipeline->GetLastAssetImportEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_FALSE(event->Succeeded());
    EXPECT_EQ(event->Sequence, 1u);
    EXPECT_EQ(event->Error, Core::ErrorCode::InvalidState);
    EXPECT_EQ(event->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::Cancelled);
    EXPECT_FALSE(event->Result.has_value());

    engine.Initialize();

    EXPECT_EQ(shutdownState->InitializeCalls, 2u);
    EXPECT_EQ(
        engine.Services().Find<Runtime::AssetWorkflowModule>(),
        pipeline);
    EXPECT_EQ(RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount(), 0u);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 0u);
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Revision, 0u);
    EXPECT_FALSE(engine.Services().Find<Runtime::EditorCommandHistory>()->IsDirty());
    EXPECT_TRUE(Selection(engine).SelectedStableIds().empty());

    const Runtime::RuntimeAssetImportQueueSnapshot reinitializedQueue =
        pipeline->GetAssetImportQueueSnapshot();
    ASSERT_EQ(reinitializedQueue.Entries.size(), 1u);
    EXPECT_EQ(reinitializedQueue.ActiveCount, 0u);
    EXPECT_EQ(reinitializedQueue.TerminalCount, 1u);
    EXPECT_EQ(reinitializedQueue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled);

    pipeline->SetQueuedGeometryImportBeforeDecodeHookForTest({});
    engine.Shutdown();
    EXPECT_EQ(shutdownState->ShutdownCalls, 2u);
}

TEST(SandboxEditorUi, QueuedManualGeometryDecodeFailureIsFailClosed)
{
    TmpFile malformedMeshFile(
        "runtime_manual_queue_malformed_mesh.obj",
        "v nan 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Core::Config::EngineConfig config = HeadlessConfig();
    config.Camera.Enabled = true;
    Intrinsic::Tests::RuntimeTestKernel engine(
        config, std::make_unique<WaitForAssetImportEventApplication>());
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);
    const std::size_t baselineLiveAssetCount =
        RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount();

    auto recordingController = std::make_unique<RecordingImportCameraController>();
    RecordingImportCameraController* recorder = recordingController.get();
    engine.Services()
        .Find<Runtime::CameraControllerRegistry>()
        ->Replace(
        Runtime::CameraControllerSlot::Main,
        std::move(recordingController));

    Runtime::EditorWorkspaceAttachment attachment;
    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(Runtime::PrepareEditorWorkspaceSnapshotFrame(
                    attachment,
                    {},
                    malformedMeshFile.Path.string(),
                    Assets::AssetPayloadKind::Mesh)
                    .has_value());

    std::optional<Runtime::EditorFileImportResult> commandResult{};
    const Runtime::EditorSceneEditingPreparedFrame prepared =
        Runtime::PrepareEditorSceneEditingFrame(attachment);
    commandResult = Runtime::ApplyEditorFileImportCommand(
        prepared.Commands,
        Runtime::EditorFileImportCommand{
            .Path = malformedMeshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(commandResult.has_value());
    ASSERT_EQ(commandResult->Status, Runtime::EditorCommandStatus::Pending);
    ASSERT_TRUE(commandResult->Operation.IsValid());

    EXPECT_EQ(RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount(), baselineLiveAssetCount);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 0u);
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Revision, 0u);
    EXPECT_TRUE(Selection(engine).SelectedStableIds().empty());

    engine.Run();

    const Runtime::RuntimeAssetImportQueueSnapshot queue =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 1u);
    EXPECT_EQ(queue.Entries[0].Source,
              Runtime::RuntimeAssetIngestSource::ManualImport);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Failed);
    EXPECT_FALSE(queue.Entries[0].DiagnosticText.empty());

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Failed);
    EXPECT_EQ(records[0].Diagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::DecodeFailed);
    EXPECT_EQ(records[0].Error, Core::ErrorCode::InvalidFormat);
    EXPECT_FALSE(records[0].Result.has_value());

    const auto& event =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_FALSE(event->Succeeded());
    EXPECT_EQ(event->Sequence, 1u);
    EXPECT_EQ(event->RequestedPayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(event->Error, Core::ErrorCode::InvalidFormat);
    EXPECT_EQ(event->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::DecodeFailed);
    EXPECT_FALSE(event->Result.has_value());

    EXPECT_EQ(RequiredEngineService<Extrinsic::Assets::AssetService>(engine).LiveAssetCount(), baselineLiveAssetCount);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 0u);
    EXPECT_EQ(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Revision, 0u);
    EXPECT_TRUE(Selection(engine).SelectedStableIds().empty());
    EXPECT_EQ(recorder->FocusCalls, 0u);

    attachment.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, DuplicateDroppedGeometryImportUsesSingleIngestRecord)
{
    TmpFile meshFile(
        "runtime_duplicate_drop_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<FixedFrameApplication>(128u));
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    const std::vector<std::string> droppedPaths{
        meshFile.Path.string(),
        meshFile.Path.string(),
    };
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportDroppedFilePaths(droppedPaths);

    std::vector<Runtime::RuntimeAssetIngestRecord> records =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(records[0].Request.Path, meshFile.Path.string());
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Decoding);

    const std::optional<Runtime::RuntimeAssetImportEvent>& duplicateEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(duplicateEvent.has_value());
    EXPECT_FALSE(duplicateEvent->Succeeded());
    EXPECT_EQ(duplicateEvent->Error, Core::ErrorCode::ResourceBusy);
    EXPECT_EQ(duplicateEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::DuplicateActiveRequest);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);
    records = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[0].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(records[0].Result.has_value());
    EXPECT_EQ(records[0].Result->PrimitiveEntitiesCreated, 1u);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);

    engine.Shutdown();
}
TEST(SandboxEditorUi, DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted)
{
    TmpFile meshFile(
        "runtime_queue_drop_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    const std::filesystem::path missingFile =
        std::filesystem::temp_directory_path() / "runtime_queue_missing.obj";
    std::filesystem::remove(missingFile);

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<FixedFrameApplication>(128u));
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    const std::vector<std::string> droppedPaths{
        meshFile.Path.string(),
        missingFile.string(),
    };
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportDroppedFilePaths(droppedPaths);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 2u);
    EXPECT_EQ(queue.Entries[0].SourcePath, meshFile.Path.string());
    EXPECT_EQ(queue.Entries[1].SourcePath, missingFile.string());
    EXPECT_EQ(queue.Entries[0].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_TRUE(queue.Entries[0].CanCancel);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";
    engine.Run();

    queue = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 2u);
    EXPECT_TRUE(queue.CanClearCompleted);
    EXPECT_EQ(queue.Entries[0].SourcePath, meshFile.Path.string());
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);
    EXPECT_EQ(queue.Entries[1].SourcePath, missingFile.string());
    EXPECT_EQ(queue.Entries[1].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Failed);
    EXPECT_FALSE(queue.Entries[1].DiagnosticText.empty());
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);

    EXPECT_EQ(RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ClearCompletedAssetImports(), 2u);
    queue = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    EXPECT_TRUE(queue.Entries.empty());

    engine.Shutdown();
}
TEST(SandboxEditorUi, DroppedGeometryQueueCancellationPreventsMainThreadApply)
{
    TmpFile meshFile(
        "runtime_queue_cancel_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<FixedFrameApplication>(16u));
    ComposeAsyncWorkAndInitialize(engine);

    const std::vector<std::string> droppedPaths{meshFile.Path.string()};
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportDroppedFilePaths(droppedPaths);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_TRUE(queue.Entries[0].CanCancel);
    EXPECT_TRUE(RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).CancelAssetImport(queue.Entries[0].Operation).has_value());

    queue = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled);
    EXPECT_FALSE(queue.Entries[0].CanCancel);
    EXPECT_NE(queue.Entries[0].DiagnosticText.find("Cancelled"),
              std::string::npos);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";
    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 0u);

    engine.Shutdown();
}
TEST(SandboxEditorUi, DroppedGeometryAssetReimportWaitReportsDeadlineAndCancellationPhase)
{
    TmpFile meshFile(
        "runtime_drop_reimport_wait_timeout.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    auto waitDiagnostics =
        std::make_shared<AssetImportEventWaitDiagnostics>();
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForAssetImportEventApplication>(
                              std::chrono::milliseconds(250), waitDiagnostics, true));
    ComposeAsyncWorkAndInitialize(engine);

    waitDiagnostics->Arm();
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
        .SetQueuedGeometryImportBeforeDecodeHookForTest(
            [waitDiagnostics](const Runtime::RuntimeAssetImportRequest&)
            {
                waitDiagnostics->WorkerStartedAtMicros.store(
                    waitDiagnostics->ElapsedMicros(),
                    std::memory_order_release);
                waitDiagnostics->WorkerStarted.store(
                    true,
                    std::memory_order_release);
                while (!waitDiagnostics->ReleaseWorker.load(
                    std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                waitDiagnostics->WorkerGateReleasedAtMicros.store(
                    waitDiagnostics->ElapsedMicros(),
                    std::memory_order_release);
            });

    const std::vector<std::string> droppedPaths{meshFile.Path.string()};
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportDroppedFilePaths(droppedPaths);
    const Runtime::RuntimeAssetImportQueueSnapshot queuedImport =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    if (queuedImport.Entries.size() != 1u)
        waitDiagnostics->ReleaseWorker.store(true, std::memory_order_release);
    ASSERT_EQ(queuedImport.Entries.size(), 1u);
    waitDiagnostics->Operation = queuedImport.Entries.front().Operation;

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";
    engine.Run();

    EXPECT_EQ(
        waitDiagnostics->ExitReason,
        AssetImportEventWaitExitReason::DeadlineExceeded)
        << waitDiagnostics->Describe();
    EXPECT_FALSE(
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent().has_value())
        << waitDiagnostics->Describe();
    EXPECT_TRUE(
        waitDiagnostics->WorkerStarted.load(std::memory_order_acquire))
        << waitDiagnostics->Describe();
    EXPECT_EQ(
        waitDiagnostics->LastStage,
        Runtime::RuntimeAssetImportQueueStage::Decoding)
        << waitDiagnostics->Describe();
    EXPECT_GE(
        waitDiagnostics->ElapsedMicros() -
            waitDiagnostics->WorkerStartedAtMicros.load(
                std::memory_order_acquire),
        250'000)
        << waitDiagnostics->Describe();

    engine.Shutdown();

    EXPECT_EQ(
        waitDiagnostics->LastStage,
        Runtime::RuntimeAssetImportQueueStage::Cancelled)
        << waitDiagnostics->Describe();
    EXPECT_EQ(
        waitDiagnostics->LastTerminalStatus,
        Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled)
        << waitDiagnostics->Describe();
}
TEST(SandboxEditorUi, DroppedGeometryAssetReimportReloadsSameAssetWithoutDuplicateEntity)
{
    TmpFile meshFile(
        "runtime_drop_reimport_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    auto waitDiagnostics =
        std::make_shared<AssetImportEventWaitDiagnostics>();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<WaitForAssetImportEventApplication>(
                                                   std::chrono::seconds(10), waitDiagnostics));
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    constexpr std::uint32_t formerFrameBudget = 128u;
    waitDiagnostics->Arm();
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
        .SetQueuedGeometryImportBeforeDecodeHookForTest(
            [waitDiagnostics](const Runtime::RuntimeAssetImportRequest&)
            {
                waitDiagnostics->WorkerStartedAtMicros.store(
                    waitDiagnostics->ElapsedMicros(),
                    std::memory_order_release);
                waitDiagnostics->WorkerStarted.store(
                    true,
                    std::memory_order_release);
                while (
                    waitDiagnostics->ObservedFrames.load(
                        std::memory_order_acquire) <= formerFrameBudget &&
                    !waitDiagnostics->ReleaseWorker.load(
                        std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (waitDiagnostics->ObservedFrames.load(
                        std::memory_order_acquire) > formerFrameBudget)
                {
                    waitDiagnostics->FormerFrameBudgetCrossed.store(
                        true,
                        std::memory_order_release);
                }
                waitDiagnostics->WorkerGateReleasedAtMicros.store(
                    waitDiagnostics->ElapsedMicros(),
                    std::memory_order_release);
            });

    const std::vector<std::string> droppedPaths{meshFile.Path.string()};
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportDroppedFilePaths(droppedPaths);
    const Runtime::RuntimeAssetImportQueueSnapshot queuedImport =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetImportQueueSnapshot();
    if (queuedImport.Entries.size() != 1u)
        waitDiagnostics->ReleaseWorker.store(true, std::memory_order_release);
    ASSERT_EQ(queuedImport.Entries.size(), 1u);
    waitDiagnostics->Operation = queuedImport.Entries.front().Operation;

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();
    waitDiagnostics->ReleaseWorker.store(true, std::memory_order_release);
    RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine)
        .SetQueuedGeometryImportBeforeDecodeHookForTest({});

    const std::optional<Runtime::RuntimeAssetImportEvent>& droppedEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(droppedEvent.has_value()) << waitDiagnostics->Describe();
    EXPECT_EQ(
        waitDiagnostics->ExitReason,
        AssetImportEventWaitExitReason::EventObserved)
        << waitDiagnostics->Describe();
    EXPECT_GT(
        waitDiagnostics->ObservedFrames.load(std::memory_order_acquire),
        formerFrameBudget)
        << "the regression must cross the former frame-only exit";
    EXPECT_TRUE(
        waitDiagnostics->FormerFrameBudgetCrossed.load(
            std::memory_order_acquire));
    EXPECT_TRUE(
        waitDiagnostics->WorkerStarted.load(std::memory_order_acquire));
    const std::int64_t workerGateReleasedAt =
        waitDiagnostics->WorkerGateReleasedAtMicros.load(
            std::memory_order_acquire);
    const std::int64_t mainThreadApplyAt =
        waitDiagnostics->MainThreadApplyAtMicros.load(
            std::memory_order_acquire);
    const std::int64_t eventObservedAt =
        waitDiagnostics->EventObservedAtMicros.load(
            std::memory_order_acquire);
    EXPECT_GE(workerGateReleasedAt, 0) << waitDiagnostics->Describe();
    EXPECT_GE(mainThreadApplyAt, workerGateReleasedAt)
        << waitDiagnostics->Describe();
    EXPECT_GE(eventObservedAt, mainThreadApplyAt)
        << waitDiagnostics->Describe();
    EXPECT_LT(eventObservedAt, std::chrono::seconds(10).count() * 1'000'000)
        << waitDiagnostics->Describe();
    ASSERT_TRUE(droppedEvent->Succeeded());
    ASSERT_TRUE(droppedEvent->Result.has_value());
    const Assets::AssetId droppedAsset = droppedEvent->Result->Asset;
    ASSERT_TRUE(droppedAsset.IsValid());
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);
    const auto firstTicket =
        RequiredEngineService<Extrinsic::Assets::AssetService>(engine).GetPayloadTicket(droppedAsset);
    ASSERT_TRUE(firstTicket.has_value());

    {
        std::ofstream out(meshFile.Path, std::ios::binary | std::ios::trunc);
        out << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "v 0 0 1\n"
               "f 1 2 3\n"
               "f 1 3 4\n";
    }

    auto reimported = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ReimportAsset(Runtime::RuntimeAssetReimportRequest{
        .Asset = droppedAsset,
    });
    ASSERT_TRUE(reimported.has_value()) << static_cast<int>(reimported.error());
    EXPECT_EQ(reimported->Asset, droppedAsset);
    EXPECT_EQ(reimported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(reimported->PrimitiveEntitiesCreated, 0u);
    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);

    const auto secondTicket =
        RequiredEngineService<Extrinsic::Assets::AssetService>(engine).GetPayloadTicket(droppedAsset);
    ASSERT_TRUE(secondTicket.has_value());
    EXPECT_EQ(secondTicket->slot, firstTicket->slot);
    EXPECT_GT(secondTicket->generation, firstTicket->generation);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[1].Request.Source,
              Runtime::RuntimeAssetIngestSource::Reimport);
    EXPECT_EQ(records[1].Request.ExistingAsset, droppedAsset);
    EXPECT_EQ(records[1].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[1].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->Asset, droppedAsset);

    engine.Shutdown();
}
TEST(SandboxEditorUi, PlatformDropEventImportsObjMeshSelectsItAndEnablesRenderComponents)
{
    TmpFile meshFile(
        "runtime_platform_drop_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForAssetImportEventApplication>());
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Mesh);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(*meshEntity);
    const auto selectedIds = Selection(engine).SelectedStableIds();
    ASSERT_EQ(selectedIds.size(), 1u);
    EXPECT_EQ(selectedIds[0], stableId);

    Intrinsic::Tests::EditorFeatureTestContext commandContext =
        MakeContext(
            *engine.Worlds().Get(engine.ActiveWorld()),
            Selection(engine));

    EXPECT_EQ(Runtime::ApplyEditorPrimitiveViewCommand(
                  commandContext,
                  Runtime::EditorPrimitiveViewCommand{
                      .StableEntityId = stableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                      .SetVertexView = true,
                      .EnableVertexView = true,
                      .SetVertexRenderMode = true,
                      .VertexRenderMode =
                          Runtime::MeshVertexViewRenderMode::ImpostorSphere,
                      .SetVertexPointRadius = true,
                      .VertexPointRadiusPx = 10.0f,
                  }),
              Runtime::EditorCommandStatus::Applied);

    auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
    ASSERT_TRUE(raw.all_of<G::RenderEdges>(*meshEntity));
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(*meshEntity));
    const G::RenderPoints& points = raw.get<G::RenderPoints>(*meshEntity);
    EXPECT_EQ(points.Type, G::RenderPoints::RenderType::Sphere);
    ASSERT_NE(std::get_if<float>(&points.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&points.SizeSource), 10.0f);

    Runtime::RenderExtractionCache extraction;
    const Runtime::RuntimeRenderExtractionStats stats =
        extraction.ExtractAndSubmit(*engine.Worlds().Get(engine.ActiveWorld()),
                                    engine.GetRenderer(),
                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);
    const auto sidecar = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(sidecar.has_value());
    const auto config = engine.GetRenderer()
                            .GetGpuWorld()
                            .GetEntityConfigForTest(sidecar->MeshVertexViewInstance);
    EXPECT_EQ(config.Point.PointMode, 1u);
    EXPECT_FLOAT_EQ(config.Point.PointSize, 10.0f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
TEST(SandboxEditorUi, PlatformDropNoUvObjUploadsRawSurfaceBeforeDeferredPostProcess)
{
    TmpFile meshFile(
        "runtime_platform_drop_no_uv_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForAssetImportEventApplication>());
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(lastEvent->Result->PrimitiveEntitiesCreated, 1u);

    const Runtime::RuntimeRenderExtractionStats& stats =
        RequiredEngineService<Runtime::RenderExtractionCache>(engine).GetLastStats();
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 1u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_GE(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    engine.Shutdown();
}
TEST(SandboxEditorUi, DroppedFileImportFailureLogsDiagnostics)
{
    const std::filesystem::path missingMeshPath =
        std::filesystem::temp_directory_path() /
        "runtime_platform_drop_missing_mesh.obj";
    std::error_code ec;
    std::filesystem::remove(missingMeshPath, ec);

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForAssetImportEventApplication>());
    ComposeAsyncWorkAndInitialize(engine);

    Core::Log::ClearEntries();

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {missingMeshPath.string()},
    });

    const Core::Log::LogSnapshot queuedLogs = Core::Log::TakeSnapshot();
    EXPECT_TRUE(LogSnapshotContains(queuedLogs, "File drop received"))
        << "The platform drop boundary must log receipt before deferred import "
           "work completes.";
    EXPECT_TRUE(LogSnapshotContains(queuedLogs, "Queued dropped geometry import"))
        << "Dropped geometry imports must log that they were queued off the "
           "platform polling path.";
    EXPECT_FALSE(RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_FALSE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->RequestedPayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(lastEvent->Error, Core::ErrorCode::FileNotFound);

    const Core::Log::LogSnapshot completedLogs = Core::Log::TakeSnapshot();
    EXPECT_TRUE(LogSnapshotContains(completedLogs, "Asset import failed"));
    EXPECT_TRUE(LogSnapshotContains(completedLogs, "FileNotFound"));
    EXPECT_TRUE(LogSnapshotContains(completedLogs, "Mesh"));
    EXPECT_TRUE(LogSnapshotContains(completedLogs,
                                    missingMeshPath.filename().string()));

    engine.Shutdown();
}
TEST(SandboxEditorUi, PlatformDropEventImportsOffMesh)
{
    TmpFile meshFile(
        "runtime_platform_drop_mesh.off",
        "OFF\n"
        "3 1 3\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n");

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<WaitForAssetImportEventApplication>());
    ComposeAsyncWorkAndInitialize(engine);
    InstallSandboxDefaultRuntimePolicies(engine);

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Mesh);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(*engine.Worlds().Get(engine.ActiveWorld()), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const auto selectedIds = Selection(engine).SelectedStableIds();
    ASSERT_EQ(selectedIds.size(), 1u);
    EXPECT_EQ(selectedIds[0],
              Runtime::SelectionController::ToStableEntityId(*meshEntity));

    engine.Shutdown();
}
TEST(SandboxEditorUi, ConfiguredBackendBornClosedLogsZeroFrameRunDiagnostic)
{
    const std::string engineSource =
        ReadRepositoryTextFile("src/runtime/Kernel/Runtime.Engine.cpp");
    ASSERT_FALSE(engineSource.empty());
    EXPECT_NE(engineSource.find(
                  "m_Impl->m_Window && m_Impl->m_Window->ShouldClose()"),
              std::string::npos);
    EXPECT_NE(engineSource.find("Platform window initialized closed"),
              std::string::npos);
    EXPECT_NE(engineSource.find("Engine::Run() will execute zero frames"),
              std::string::npos);
}
TEST(SandboxEditorUi, PlatformCloseEventStopsEngineRunState)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig());
    engine.Initialize();

    ASSERT_TRUE(engine.IsRunning());
    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    Core::Log::ClearEntries();
    engine.DispatchPlatformEventForTest(Plat::WindowCloseEvent{});

    const Core::Log::LogSnapshot closeLogs = Core::Log::TakeSnapshot();
    EXPECT_TRUE(LogSnapshotContains(closeLogs,
                                    Core::Log::Level::Info,
                                    "Window close requested"))
        << "Sandbox window close must leave an [INFO] close breadcrumb.";

    engine.Run();

    EXPECT_FALSE(engine.IsRunning());

    engine.Shutdown();
}
TEST(SandboxEditorUi, RenderRecipeEditorDraftValidationPreviewActivationAndCancel)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::EditorRenderRecipeEditorState editorState{};
    Intrinsic::Tests::EditorFeatureTestContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState);
    const std::string validDocument = ValidSandboxRenderRecipeConfig();

    Runtime::EditorRenderRecipeCommandResult result =
        Runtime::ApplyEditorRenderRecipeCommand(
            context,
            Runtime::EditorRenderRecipeCommand{
                .Kind = Runtime::EditorRenderRecipeCommandKind::UpdateDraft,
                .Document = validDocument,
                .SourceId = "valid-preview.json",
                .Debounced = true,
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::Debounced);
    EXPECT_EQ(editorState.DraftState,
              Runtime::EditorRenderRecipeDraftState::Debounced);
    EXPECT_EQ(editorState.DraftRevision, 1u);

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind = Runtime::EditorRenderRecipeCommandKind::ValidateDraft,
            .Document = "{not valid json",
            .SourceId = "invalid-preview.json",
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::ValidationFailed);
    EXPECT_EQ(editorState.DraftState,
              Runtime::EditorRenderRecipeDraftState::Rejected);
    EXPECT_FALSE(result.RecipeDiagnostics.empty());

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind = Runtime::EditorRenderRecipeCommandKind::PreviewDraft,
            .Document = std::string{R"json({
  "schema": ")json"} + std::string{Graphics::kRenderRecipeConfigSchemaId} +
                        R"json(",
  "version": 1,
  "rendererId": ")json" +
                        std::string{Graphics::kCurrentRendererContractId} +
                        R"json(",
  "recipe": {"slots": [{"name": "ray-traced-gi"}]}
})json",
            .SourceId = "unsupported-preview.json",
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::PreviewFailed);
    EXPECT_EQ(editorState.DraftState,
              Runtime::EditorRenderRecipeDraftState::Rejected);

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind = Runtime::EditorRenderRecipeCommandKind::PreviewDraft,
            .Document = validDocument,
            .SourceId = "valid-preview.json",
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::Previewed);
    EXPECT_EQ(editorState.DraftState,
              Runtime::EditorRenderRecipeDraftState::Previewed);
    ASSERT_TRUE(editorState.HasLastPreview);
    EXPECT_TRUE(Graphics::IsConfigUsable(editorState.LastPreview));

    Runtime::EditorRenderRecipeEditorModel model =
        Runtime::BuildEditorRenderRecipeEditorModel(context);
    EXPECT_TRUE(model.CanActivate);
    EXPECT_EQ(model.DraftRecipeId, "current-renderer.user-preview");

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind =
                Runtime::EditorRenderRecipeCommandKind::ActivatePreview,
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::Activated);
    EXPECT_EQ(editorState.DraftState,
              Runtime::EditorRenderRecipeDraftState::Activated);
    EXPECT_TRUE(editorState.HasActiveOverride);
    EXPECT_EQ(editorState.ActiveRevision, 1u);

    model = Runtime::BuildEditorRenderRecipeEditorModel(context);
    EXPECT_EQ(model.ActiveRecipeId, "current-renderer.user-preview");

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind = Runtime::EditorRenderRecipeCommandKind::CancelDraft,
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::Canceled);
    EXPECT_EQ(editorState.DraftState,
              Runtime::EditorRenderRecipeDraftState::Canceled);
    EXPECT_TRUE(editorState.DraftDocument.empty());
    EXPECT_FALSE(editorState.HasLastPreview);

    model = Runtime::BuildEditorRenderRecipeEditorModel(context);
    EXPECT_FALSE(model.CanCancel);
}
TEST(SandboxEditorUi, RenderRecipeEditorUnchangedDraftIsNoOp)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::EditorRenderRecipeEditorState editorState{};
    Intrinsic::Tests::EditorFeatureTestContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState);
    const std::string validDocument = ValidSandboxRenderRecipeConfig();

    Runtime::EditorRenderRecipeCommandResult result =
        Runtime::ApplyEditorRenderRecipeCommand(
            context,
            Runtime::EditorRenderRecipeCommand{
                .Kind = Runtime::EditorRenderRecipeCommandKind::UpdateDraft,
                .Document = validDocument,
                .SourceId = "stable-draft.json",
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::DraftUpdated);
    EXPECT_EQ(editorState.DraftRevision, 1u);

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind = Runtime::EditorRenderRecipeCommandKind::UpdateDraft,
            .Document = validDocument,
            .SourceId = "stable-draft.json",
        });
    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::NoChange);
    EXPECT_EQ(editorState.DraftRevision, 1u);
}
TEST(SandboxEditorUi, RenderRecipeEditorArtifactPublishAndApplyUseRegistry)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::EditorRenderRecipeEditorState editorState{};
    Runtime::RenderArtifactRegistry artifacts;
    ASSERT_TRUE(artifacts.RegisterArtifact(
                            MakeSandboxRenderArtifact("sandbox-candidate"))
                    .Succeeded());
    Intrinsic::Tests::EditorFeatureTestContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState,
        &artifacts);

    Runtime::EditorRenderRecipeCommandResult result =
        Runtime::ApplyEditorRenderRecipeCommand(
            context,
            Runtime::EditorRenderRecipeCommand{
                .Kind =
                    Runtime::EditorRenderRecipeCommandKind::PublishArtifact,
                .ArtifactId = "sandbox-candidate",
                .Provenance = "sandbox editor test",
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::Published);
    EXPECT_EQ(result.ArtifactState,
              Runtime::RenderArtifactPublicationState::Published);

    Runtime::EditorRenderRecipeEditorModel model =
        Runtime::BuildEditorRenderRecipeEditorModel(context);
    const Runtime::EditorRenderArtifactRow* artifact =
        FindRenderArtifactRow(model, "sandbox-candidate");
    ASSERT_NE(artifact, nullptr);
    EXPECT_FALSE(artifact->CanPublish);
    EXPECT_TRUE(artifact->CanApply);

    result = Runtime::ApplyEditorRenderRecipeCommand(
        context,
        Runtime::EditorRenderRecipeCommand{
            .Kind =
                Runtime::EditorRenderRecipeCommandKind::ApplyArtifact,
            .ArtifactId = "sandbox-candidate",
            .Provenance = "sandbox editor test",
            .ProjectTarget = "scene.preview.accepted",
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::Applied);
    EXPECT_TRUE(result.ProjectMutationAuthorized);
    EXPECT_EQ(result.ArtifactState,
              Runtime::RenderArtifactPublicationState::Applied);

    Intrinsic::Tests::EditorFeatureTestContext missingRegistry =
        MakeRenderRecipeEditorContext(recipeContext, editorState, nullptr);
    result = Runtime::ApplyEditorRenderRecipeCommand(
        missingRegistry,
        Runtime::EditorRenderRecipeCommand{
            .Kind =
                Runtime::EditorRenderRecipeCommandKind::PublishArtifact,
            .ArtifactId = "sandbox-candidate",
            .Provenance = "sandbox editor test",
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::EditorRenderRecipeCommandStatus::MissingArtifactRegistry);
}
