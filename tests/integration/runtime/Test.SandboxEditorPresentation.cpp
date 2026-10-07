#include <unordered_map>
#include <cstddef>
#include <functional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
// ARCH-006 Slice 5 app-owned editor presentation and composition coverage.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "RuntimeTestModule.hpp"

#include "EditorFeatureTestContext.hpp"
#include "ImGuiItemProbe.hpp"
#include "TestImGuiFrameScope.hpp"

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

#include "../../../src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp"

namespace Core = Extrinsic::Core;
namespace Plat = Extrinsic::Platform;
namespace Runtime = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;

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

    struct ProcessingButtonGui
    {
        ImGuiContext* Previous{ImGui::GetCurrentContext()};
        ImGuiContext* Context{ImGui::CreateContext()};
        ImVec2 ButtonCenter{};

        ProcessingButtonGui()
        {
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {600, 400};
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
            // Control delay only here; the helper must itself allow disabled hover.
            ImGui::GetStyle().HoverFlagsForTooltipMouse = ImGuiHoveredFlags_None;
        }
        ~ProcessingButtonGui()
        {
            if (Context->WithinFrameScope)
                ImGui::EndFrame();
            ImGui::DestroyContext(Context);
            ImGui::SetCurrentContext(Previous);
        }
        bool Draw(const Runtime::ActionReadiness& readiness)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowSize({500, 200});
            ImGui::Begin("Processing action test", nullptr,
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
            Context->LogBuffer.clear();
            ImGui::LogToBuffer();
            const bool clicked = Editor::DrawProcessingActionButton("Run method", readiness);
            const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
            ButtonCenter = {(minimum.x + maximum.x) / 2, (minimum.y + maximum.y) / 2};
            return clicked;
        }
        void End()
        {
            ImGui::LogFinish();
            ImGui::End();
            ImGui::Render();
        }
    };

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

    class TooltipDelayApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        static constexpr std::uint32_t MaxFrames = 64u;

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            ++m_Frames;
            if (m_Frames >= MaxFrames)
                engine.RequestExit();
        }
        void Shutdown() override {}

    private:
        std::uint32_t m_Frames{0u};
    };

    class WaitForAssetImportEventApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit WaitForAssetImportEventApplication(
            const std::chrono::milliseconds timeout = std::chrono::seconds(10))
            : m_Timeout(timeout)
        {
        }

        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine   = Kernel();
            const auto now = std::chrono::steady_clock::now();
            if (!m_Started)
            {
                m_Started = true;
                m_StartedAt = now;
                m_Deadline = now + m_Timeout;
            }
            ++m_ObservedFrames;
            if (RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).GetLastAssetImportEvent().has_value())
            {
                m_EventObserved = true;
                m_Elapsed = now - m_StartedAt;
                engine.RequestExit();
                return;
            }
            if (now >= m_Deadline)
            {
                m_TimedOut = true;
                m_Elapsed = now - m_StartedAt;
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void Shutdown() override {}

        [[nodiscard]] std::string Describe() const
        {
            return "event_observed=" +
                std::string{m_EventObserved ? "true" : "false"} +
                ", timed_out=" +
                std::string{m_TimedOut ? "true" : "false"} +
                ", frames=" + std::to_string(m_ObservedFrames) +
                ", elapsed_ms=" +
                std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        m_Elapsed)
                        .count());
        }

    private:
        std::chrono::milliseconds m_Timeout{std::chrono::seconds(10)};
        std::chrono::steady_clock::time_point m_StartedAt{};
        std::chrono::steady_clock::time_point m_Deadline{};
        std::chrono::steady_clock::duration m_Elapsed{};
        std::uint32_t m_ObservedFrames{0u};
        bool m_EventObserved{false};
        bool m_TimedOut{false};
        bool m_Started{false};
    };

    struct TmpFile
    {
        std::filesystem::path Path;

        TmpFile(const std::string_view name, const std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string{name})
        {
            std::ofstream output{Path};
            output << contents;
        }

        ~TmpFile()
        {
            std::error_code error;
            std::filesystem::remove(Path, error);
        }
    };

    class ToggleEditorVisibilityApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++m_Frames;
            if (m_Frames == 1u)
            {
                // G toggles at the next UiBegin, ImGui keyboard capture
                // notwithstanding.
                ImGui::SetNextFrameWantCaptureKeyboard(true);
                static_cast<Plat::Backends::Null::NullWindow&>(engine.GetWindow())
                    .QueueKey(Plat::Input::Key::G, true);
                return;
            }
            engine.RequestExit();
        }
        void Shutdown() override {}

    private:
        std::uint32_t m_Frames{0u};
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    void ComposeEditorUiAndInitialize(Runtime::Engine& engine)
    {
        engine.EmplaceModule<Runtime::EditorUiModule>();
        engine.Initialize();
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

    [[nodiscard]] bool ImGuiWindowExists(const std::string_view name)
    {
        const ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr)
            return false;

        for (const ImGuiWindow* window : context->Windows)
        {
            if (window != nullptr && std::string_view{window->Name} == name)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool ActiveImGuiTooltipExists()
    {
        const ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr)
            return false;

        return std::ranges::any_of(
            context->Windows,
            [](const ImGuiWindow* window)
            {
                return window != nullptr &&
                       window->Active &&
                       (window->Flags & ImGuiWindowFlags_Tooltip) != 0;
            });
    }

    [[nodiscard]] std::string WithoutAsciiWhitespace(
        const std::string_view text)
    {
        std::string compact{};
        compact.reserve(text.size());
        for (const char character : text)
        {
            if (!std::isspace(static_cast<unsigned char>(character)))
                compact.push_back(character);
        }
        return compact;
    }

    [[nodiscard]] bool HasDiagnostic(
        const std::vector<Runtime::EditorDiagnostic>& diagnostics,
        const Runtime::EditorDiagnosticCode code)
    {
        return std::ranges::any_of(
            diagnostics,
            [code](const Runtime::EditorDiagnostic& diagnostic)
            {
                return diagnostic.Code == code;
            });
    }

    [[nodiscard]] const Runtime::EditorWindowMenuEntry* FindWindow(
        const std::vector<Runtime::EditorWindowMenuEntry>& menu,
        const std::string_view id)
    {
        const auto found = std::ranges::find_if(
            menu,
            [id](const Runtime::EditorWindowMenuEntry& entry)
            {
                return entry.Id == id;
            });
        return found == menu.end() ? nullptr : &*found;
    }

    void RegisterAllAppPanels(
        Editor::EditorShell& shell,
        Editor::MethodPanels& methodPanels,
        Editor::MeshProcessingPanels& meshProcessingPanels,
        Editor::DomainPanels& domainPanels)
    {
        methodPanels.Register(shell);
        meshProcessingPanels.Register(shell);
        domainPanels.Register(shell);
    }
} // namespace

TEST(SandboxEditorPresentation, DefaultDrawStartsWithOnlyMenuBarVisible)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MethodPanels methodPanels;
    Editor::MeshProcessingPanels meshProcessingPanels;
    Editor::DomainPanels domainPanels;
    RegisterAllAppPanels(shell, methodPanels, meshProcessingPanels, domainPanels);

    engine.Run();

    EXPECT_TRUE(ImGuiWindowExists("##MainMenuBar"));
    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), 98u);
    for (const Runtime::EditorWindowMenuEntry& entry : menu)
    {
        EXPECT_FALSE(entry.Open) << entry.Id;
        EXPECT_FALSE(ImGuiWindowExists(entry.Title)) << entry.Id;
    }

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, DomainMenusUseAppearanceAndFocusedProcessingWindows)
{
    struct ExpectedWindow
    {
        std::string_view Id;
        std::vector<std::string> MenuPath;
    };
    const std::array<ExpectedWindow, 86> expected{{
        {"view.property_inspector", {"View"}},
        {"pointcloud.properties", {"PointCloud"}},
        {"pointcloud.selection", {"PointCloud"}},
        {"pointcloud.processing.remove_outliers", {"PointCloud", "Processing"}},
        {"graph.properties", {"Graph"}},
        {"graph.selection", {"Graph"}},
        {"scene.appearance", {"View"}},
        {"mesh.properties", {"Mesh"}},
        {"mesh.selection", {"Mesh"}},
        {"pointcloud.processing.kmeans", {"PointCloud", "Processing"}},
        {"graph.processing.kmeans", {"Graph", "Processing"}},
        {"mesh.processing.kmeans", {"Mesh", "Processing"}},
        {"mesh.processing.segmentation", {"Mesh", "Processing"}},
        {"mesh.processing.scalar_ridges", {"Mesh", "Processing"}},
        {"pointcloud.processing.progressive_poisson", {"PointCloud", "Processing"}},
        {"graph.processing.progressive_poisson", {"Graph", "Processing"}},
        {"pointcloud.processing.consolidation", {"PointCloud", "Processing"}},
        {"graph.processing.consolidation", {"Graph", "Processing"}},
        {"mesh.processing.consolidation", {"Mesh", "Processing"}},
        {"mesh.processing.progressive_poisson", {"Mesh", "Processing"}},
        {"mesh.processing.parameterize_uv", {"Mesh", "Processing"}},
        {"mesh.uv_atlas_workspace", {"Mesh"}},
        {"mesh.processing.denoise", {"Mesh", "Processing"}},
        {"mesh.processing.geodesics", {"Mesh", "Geodesics"}},
        {"mesh.processing.curvature", {"Mesh", "Processing"}},
        {"mesh.processing.remesh", {"Mesh", "Processing"}},
        {"mesh.processing.subdivide", {"Mesh", "Processing"}},
        {"mesh.processing.simplify", {"Mesh", "Processing"}},
        {"mesh.processing.vertices.normals", {"Mesh", "Processing", "Vertices"}},
        {"mesh.processing.faces.normals", {"Mesh", "Processing", "Faces"}},
        {"mesh.processing.faces.scalar_gradient", {"Mesh", "Processing", "Faces"}},
        {"view.property_smoothing", {"View"}},
        {"mesh.processing.property_smoothing", {"Mesh", "Processing"}},
        {"graph.processing.property_smoothing", {"Graph", "Processing"}},
        {"pointcloud.processing.property_smoothing", {"PointCloud", "Processing"}},
        {"view.harmonic_field", {"View"}},
        {"mesh.processing.harmonic_field", {"Mesh", "Processing"}},
        {"graph.processing.harmonic_field", {"Graph", "Processing"}},
        {"pointcloud.processing.harmonic_field", {"PointCloud", "Processing"}},
        {"view.laplacian_eigenbasis", {"View"}},
        {"mesh.processing.laplacian_eigenbasis", {"Mesh", "Processing"}},
        {"graph.processing.laplacian_eigenbasis", {"Graph", "Processing"}},
        {"pointcloud.processing.laplacian_eigenbasis", {"PointCloud", "Processing"}},
        {"graph.processing.vertices.normals", {"Graph", "Processing", "Vertices"}},
        {"pointcloud.processing.vertices.normals", {"PointCloud", "Processing", "Vertices"}},
        {"view.normal_estimation", {"View"}},
        {"view.kernel_density", {"View"}},
        {"view.point_spacing", {"View"}},
        {"view.bilateral_filter", {"View"}},
        {"view.keypoint_analysis", {"View"}},
        {"view.descriptor_analysis", {"View"}},
        {"view.density_weights", {"View"}},
        {"view.point_construction", {"View"}},
        {"mesh.processing.kernel_density", {"Mesh", "Processing"}},
        {"mesh.processing.point_spacing", {"Mesh", "Processing"}},
        {"mesh.processing.bilateral_filter", {"Mesh", "Processing"}},
        {"mesh.processing.keypoints", {"Mesh", "Processing"}},
        {"mesh.processing.descriptors", {"Mesh", "Processing"}},
        {"mesh.processing.density_weights", {"Mesh", "Processing"}},
        {"mesh.processing.point_construction", {"Mesh", "Processing"}},
        {"graph.processing.kernel_density", {"Graph", "Processing"}},
        {"graph.processing.point_spacing", {"Graph", "Processing"}},
        {"graph.processing.bilateral_filter", {"Graph", "Processing"}},
        {"graph.processing.keypoints", {"Graph", "Processing"}},
        {"graph.processing.descriptors", {"Graph", "Processing"}},
        {"graph.processing.density_weights", {"Graph", "Processing"}},
        {"graph.processing.point_construction", {"Graph", "Processing"}},
        {"pointcloud.processing.kernel_density", {"PointCloud", "Processing"}},
        {"pointcloud.processing.point_spacing", {"PointCloud", "Processing"}},
        {"pointcloud.processing.bilateral_filter", {"PointCloud", "Processing"}},
        {"pointcloud.processing.keypoints", {"PointCloud", "Processing"}},
        {"pointcloud.processing.descriptors", {"PointCloud", "Processing"}},
        {"pointcloud.processing.density_weights", {"PointCloud", "Processing"}},
        {"pointcloud.processing.point_construction", {"PointCloud", "Processing"}},
        {"view.outlier_analysis", {"View"}},
        {"mesh.processing.outliers", {"Mesh", "Processing"}},
        {"graph.processing.outliers", {"Graph", "Processing"}},
        {"view.registration", {"View"}},
        {"mesh.processing.registration", {"Mesh", "Processing"}},
        {"graph.processing.registration", {"Graph", "Processing"}},
        {"pointcloud.processing.registration", {"PointCloud", "Processing"}},
        {"view.coherent_point_drift", {"View"}},
        {"view.point_sampling", {"View"}},
        {"mesh.processing.coherent_point_drift", {"Mesh", "Processing"}},
        {"graph.processing.coherent_point_drift", {"Graph", "Processing"}},
        {"pointcloud.processing.coherent_point_drift", {"PointCloud", "Processing"}},
    }};

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MethodPanels methodPanels;
    Editor::MeshProcessingPanels meshProcessingPanels;
    Editor::DomainPanels domainPanels;
    RegisterAllAppPanels(shell, methodPanels, meshProcessingPanels, domainPanels);

    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), expected.size() + 12u);
    for (const ExpectedWindow& expectedWindow : expected)
    {
        const Runtime::EditorWindowMenuEntry* entry =
            FindWindow(menu, expectedWindow.Id);
        ASSERT_NE(entry, nullptr) << expectedWindow.Id;
        EXPECT_EQ(entry->MenuPath, expectedWindow.MenuPath)
            << expectedWindow.Id;
        EXPECT_FALSE(entry->Open) << expectedWindow.Id;
    }
    domainPanels.Unregister();
    meshProcessingPanels.Unregister();
    methodPanels.Unregister();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorGeodesics, RegistersAndDrawsWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    panels.Register(shell);

    const auto menu = shell.BuildEditorWindowMenuModel();
    const auto* entry = FindWindow(menu, "mesh.processing.geodesics");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Title, "Virtual Source Propagation");
    EXPECT_EQ(entry->MenuPath, (std::vector<std::string>{"Mesh", "Geodesics"}));
    EXPECT_EQ(std::ranges::count_if(menu, [](const auto& row) {
        return row.Id == "mesh.processing.geodesics";
    }), 1);
    ASSERT_TRUE(shell.SetEditorWindowOpen("mesh.processing.geodesics", true));
    engine.Run();
    EXPECT_TRUE(ImGuiWindowExists("Mesh / Geodesics / Virtual Source Propagation"));

    panels.Unregister();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorProgressivePoisson,
     RegistersAndDrawsAllVertexDomainWindows)
{
    struct ExpectedWindow
    {
        std::string_view Id;
        std::vector<std::string> MenuPath;
        std::string_view WindowTitle;
    };
    const std::array<ExpectedWindow, 3u> expected{{
        {"pointcloud.processing.progressive_poisson",
         {"PointCloud", "Processing"},
         "PointCloud / Processing / Progressive Poisson"},
        {"graph.processing.progressive_poisson",
         {"Graph", "Processing"},
         "Graph / Processing / Progressive Poisson"},
        {"mesh.processing.progressive_poisson",
         {"Mesh", "Processing"},
         "Mesh / Processing / Progressive Poisson"},
    }};

    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MethodPanels methodPanels;
    methodPanels.Register(shell);

    const auto menu = shell.BuildEditorWindowMenuModel();
    for (const ExpectedWindow& expectedWindow : expected)
    {
        const Runtime::EditorWindowMenuEntry* entry =
            FindWindow(menu, expectedWindow.Id);
        ASSERT_NE(entry, nullptr) << expectedWindow.Id;
        EXPECT_EQ(entry->MenuPath, expectedWindow.MenuPath)
            << expectedWindow.Id;
        EXPECT_EQ(entry->Title, "Progressive Poisson")
            << expectedWindow.Id;
        ASSERT_TRUE(shell.SetEditorWindowOpen(expectedWindow.Id, true));
    }

    engine.Run();

    for (const ExpectedWindow& expectedWindow : expected)
        EXPECT_TRUE(ImGuiWindowExists(expectedWindow.WindowTitle))
            << expectedWindow.Id;

    methodPanels.Unregister();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, DomainPanelsPreserveLifetimeCacheAndResultPublication)
{
    const std::string source = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp");
    ASSERT_FALSE(source.empty());

    for (const std::string_view required :
         {"DomainPanels::~DomainPanels()",
          "m_Impl->Unregister();",
          "Shell->UnregisterEditorWindow(handle)",
          "Handles.clear();",
          "Shell = nullptr;",
          "CachedModelFrame != frame",
          "CachedDomainModels",
          "DomainWindowModelCacheHits",
        "DrawTextureBakeControls",
        "context.Parameterization.Results.LastUvRegenerationResult",
        "std::int32_t TextureBakeWidth{1024};",
        "LastUvRegenerationResult.reset();",
        "LastUvExtentAdoption.reset();",
        "MeshPropertyPlotState.SelectedProperty.clear();",
        "ImGuiCond_FirstUseEver"})
    {
        EXPECT_NE(source.find(required), std::string::npos) << required;
    }
}

// Both panels route through shared bake controls so queued UV results retain
// their terminal callback, dismissal, and extent-adoption path.
TEST(SandboxEditorPresentation, UvRegenerationHasOneImplementationBothPanelsDriveIt)
{
    const std::string shared = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.PanelSupport.cpp");
    ASSERT_FALSE(shared.empty());

    for (const std::string_view required :
         {"void DrawTextureBakeControls(",
          "SandboxUvRegenerationControls{",
          ".LastResult = lastUvRegenerationResult",
          "ImGui::InputInt(\"Bake padding\"",
          ".PaddingTexels = paddingSupported",
          "void DrawSandboxUvRegenerationControls(",
          "ApplyEditorUvRegenerationCommand(",
          "context->Parameterization.Commands",
          "context->Parameterization.ResultSinks.UvRegeneration",
          "DismissUvRegenerationResult()",
          "DrawProcessingActionButton(\"Regenerate UVs\", readiness)",
          "lastExtentAdoption = *lastResult;"})
    {
        EXPECT_NE(shared.find(required), std::string::npos) << required;
    }

    const auto submit = shared.find("if (DrawProcessingActionButton(\"Regenerate UVs\", readiness)");
    ASSERT_NE(submit, std::string::npos);
    EXPECT_LT(shared.find("DismissUvRegenerationResult();", submit),
              shared.find("ApplyEditorUvRegenerationCommand(", submit))
        << "A new pending run must not restore the previous session result.";

    constexpr std::array<std::string_view, 2> panels{{
        "src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp",
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp",
    }};
    for (const std::string_view panel : panels)
    {
        const std::string source = ReadRepositoryTextFile(std::string{panel});
        ASSERT_FALSE(source.empty()) << panel;
        EXPECT_NE(source.find("DrawTextureBakeControls("),
                  std::string::npos)
            << panel;
        EXPECT_EQ(source.find("ApplyEditorTextureBakeCommand("),
                  std::string::npos)
            << panel << " must not own a second bake submission path";
        EXPECT_EQ(source.find("ApplyEditorUvRegenerationCommand("),
                  std::string::npos)
            << panel << " must not own a second UV submission path";
    }
}

TEST(SandboxEditorPresentation, BoundRenderRowsPreserveDiagnosticPriorityAndEmptyState)
{
    TestSupport::ImGuiFrameScope gui;
    ImGui::GetIO().DisplaySize = {1600, 1200};
    gui.NextFrame();
    const auto draw = [&](const Runtime::EditorBoundRenderStateModel& model)
    {
        ImGui::SetNextWindowSize({1500, 1000});
        ImGui::Begin("Bound render rows", nullptr, ImGuiWindowFlags_NoSavedSettings);
        gui.Context->LogBuffer.clear();
        ImGui::LogToBuffer();
        Editor::DrawBoundRenderStateRows(model);
        const std::string text{gui.Context->LogBuffer.c_str()};
        ImGui::LogFinish();
        ImGui::End();
        return text;
    };

    Runtime::EditorBoundRenderStateModel model{};
    model.RecipeGeneration = 42u;
    model.Rows.push_back({
        .Label = "Scalar preview",
        .Property = {.Name = "v:temperature"},
        .HasCatalogMatch = true,
        .SourceDescription = "Explicit source label",
        .DisabledReason = "Hidden lower-priority reason",
        .Diagnostic = "Primary row diagnostic",
    });
    model.Rows.push_back({
        .Kind = Runtime::EditorBoundRenderStateRowKind::DerivedJob,
        .Label = "Pending bake job",
        .JobProgress = 0.25f,
        .DisabledReason = "Wait for the bake job",
    });
    model.Diagnostics.push_back({.Message = "Model-level diagnostic"});
    const auto populated = draw(model);
    for (const auto* expected : {"Rows: 2 generation=42", "Scalar preview",
             "Explicit source label", "v:temperature catalog", "Primary row diagnostic",
             "Pending bake job", "25%", "Wait for the bake job", "Model-level diagnostic"})
        EXPECT_NE(populated.find(expected), std::string::npos) << expected << '\n' << populated;
    EXPECT_EQ(populated.find("Hidden lower-priority reason"), std::string::npos);

    gui.NextFrame();
    model.Rows.clear();
    const auto empty = draw(model);
    EXPECT_NE(empty.find("No bound render state rows."), std::string::npos);
    EXPECT_NE(empty.find("Model-level diagnostic"), std::string::npos);
}

TEST(SandboxEditorPresentation, TextureBakeControlsKeepCallerMutationStateAcrossFrames)
{
    TestSupport::ImGuiFrameScope gui;
    ImGui::GetIO().DisplaySize = {1600, 1600};
    gui.NextFrame();
    Runtime::EditorTextureBakeControlsModel model{};
    const std::string outputName(150u, 'x');
    model.BakedTextures.push_back({.OutputName = outputName});
    Editor::TextureBakeMutationUiState inspector{}, appearance{};
    inspector.MutationDiagnostic = "Inspector mutation diagnostic";
    Editor::SandboxEditorContext context{};
    const auto draw = [&](const char* title, Editor::TextureBakeMutationUiState& mutation,
                          const bool rename)
    {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1400, 1500});
        ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (rename)
        {
            ImGui::PushID(outputName.c_str());
            ImGui::ActivateItemByID(ImGui::GetID("Rename"));
            ImGui::PopID();
        }
        gui.Context->LogBuffer.clear();
        ImGui::LogToBuffer();
        Editor::DrawTextureBakeControls(model, &context, nullptr, mutation);
        const std::string text{gui.Context->LogBuffer.c_str()};
        ImGui::LogFinish();
        ImGui::End();
        return text;
    };

    (void)draw("Inspector bake controls", inspector, true);
    gui.NextFrame();
    const auto renamed = draw("Inspector bake controls", inspector, false);
    EXPECT_EQ(inspector.RenameTarget, outputName);
    EXPECT_EQ(std::string{inspector.RenameBuffer.data()}, outputName.substr(0u, 127u));
    EXPECT_EQ(inspector.RenameBuffer.back(), '\0');
    EXPECT_NE(renamed.find(inspector.MutationDiagnostic), std::string::npos);

    gui.NextFrame();
    const auto separate = draw("Appearance bake controls", appearance, false);
    EXPECT_TRUE(appearance.RenameTarget.empty());
    EXPECT_EQ(appearance.RenameBuffer.front(), '\0');
    EXPECT_TRUE(appearance.MutationDiagnostic.empty());
    EXPECT_EQ(separate.find(inspector.MutationDiagnostic), std::string::npos);
    gui.NextFrame();
    const auto persisted = draw("Inspector bake controls", inspector, false);
    EXPECT_EQ(inspector.RenameTarget, outputName);
    EXPECT_NE(persisted.find(inspector.MutationDiagnostic), std::string::npos);
}

TEST(SandboxEditorPresentation, TextureBakeControlsClampToBakeableSourcesAndAllowEmptyFallbacks)
{
    TestSupport::ImGuiFrameScope gui;
    ImGui::GetIO().DisplaySize = {1600, 1600};
    gui.NextFrame();
    Runtime::EditorTextureBakeControlsModel model{};
    model.Sources = {
        {.Name = "Blocked source", .Bakeable = false},
        {.Name = "First source", .Bakeable = true},
        {.Name = "Second source", .Bakeable = true},
    };
    Editor::TextureBakeMutationUiState mutation{};
    std::int32_t selected = 99;
    Editor::TextureBakeUiState state{.SourceIndex = &selected};
    const auto draw = [&](Editor::TextureBakeUiState* storage)
    {
        ImGui::SetNextWindowSize({1400, 1500});
        ImGui::Begin("Bake sources", nullptr, ImGuiWindowFlags_NoSavedSettings);
        gui.Context->LogBuffer.clear();
        ImGui::LogToBuffer();
        Editor::DrawTextureBakeControls(model, nullptr, storage, mutation);
        const std::string text{gui.Context->LogBuffer.c_str()};
        ImGui::LogFinish();
        ImGui::End();
        return text;
    };
    (void)draw(&state);
    EXPECT_EQ(selected, 1);
    gui.NextFrame();
    model.Sources.clear();
    (void)draw(&state);
    EXPECT_EQ(selected, 0);
    gui.NextFrame();
    EXPECT_NE(draw(nullptr).find("No baked property textures on this entity."), std::string::npos);
}

// The shell rebuilds `SandboxEditorContext` every frame and drops it again at
// end of frame, so UV panel state has to live on the shell, not in that copy.
TEST(SandboxEditorPresentation, EditorShellHoldsUvResultStateForPanelLifetime)
{
    const std::string source = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp");
    ASSERT_FALSE(source.empty());

    for (const std::string_view required :
         {"LastUvRegenerationResult{};",
          ".LastUvRegenerationResult = &LastUvRegenerationResult,",
          "LastUvRegenerationResult.reset();",
          "LastUvExtentAdoption.reset();"})
    {
        EXPECT_NE(source.find(required), std::string::npos) << required;
    }
    EXPECT_EQ(
        source.find("&ActiveContext->Parameterization.Results"
                    ".LastUvRegenerationResult"),
        std::string::npos)
        << "panel state must not point into the per-frame context copy";
}

TEST(SandboxEditorPresentation, MeshProcessingPanelsPreserveLifetimeAndResultPublication)
{
    const std::string source = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp");
    ASSERT_FALSE(source.empty());

    constexpr std::array<std::string_view, 10> commands{{
      "ApplyEditorMeshDenoiseCommand",
      "ApplyEditorMeshCurvatureCommand",
      "ApplyEditorMeshRemeshCommand",
      "ApplyEditorMeshSubdivideCommand",
      "ApplyEditorMeshSimplifyCommand",
      "ApplyEditorConfiguredNormalEstimation",
      "ApplyEditorConfiguredOutlierAnalysis",
      "ApplyEditorConfiguredKernelDensity",
      "ApplyEditorConfiguredPointSpacing",
      "ApplyEditorConfiguredRegistrationCommand",
    }};
    constexpr std::array<std::string_view, 10> sinks{{
        "context.MeshTopology.ResultSinks.MeshDenoise",
        "context.MeshFields.ResultSinks.MeshCurvature",
        "context.MeshTopology.ResultSinks.MeshRemesh",
        "context.MeshTopology.ResultSinks.MeshSubdivide",
        "context.MeshTopology.ResultSinks.MeshSimplify",
        "context.Normals.ResultSinks.NormalEstimation",
        "context.PointAnalysis.ResultSinks.OutlierAnalysis",
        "context.PointFields.ResultSinks.KernelDensity",
        "context.PointFields.ResultSinks.PointSpacing",
        "context.Registration.ResultSinks.Registration",
    }};
    for (const std::string_view required : commands)
        EXPECT_NE(source.find(required), std::string::npos) << required;
    for (const std::string_view required : sinks)
        EXPECT_NE(source.find(required), std::string::npos) << required;

    EXPECT_NE(source.find("MeshProcessingPanels::~MeshProcessingPanels()"),
              std::string::npos);
    EXPECT_NE(source.find("m_Impl->Unregister()"), std::string::npos);
    EXPECT_NE(source.find("Shell->UnregisterEditorWindow(handle)"),
              std::string::npos);
    EXPECT_NE(source.find("Handles.clear()"), std::string::npos);
    EXPECT_NE(source.find("Shell = nullptr"), std::string::npos);
}

TEST(SandboxEditorProgressivePoisson,
     ProgressivePoissonPanelUsesOneNonDestructiveConfigAndRunPath)
{
    const std::string source = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp");
    ASSERT_FALSE(source.empty());

    for (const std::string_view required :
         {"PreviewEditorProgressivePoissonCommand",
          "DrawProcessingActionButton(\"Run Progressive Poisson##ProgressivePoisson\",",
          "ProgressivePoissonPlaygroundChannel::Rank",
          "ProgressivePoissonPlaygroundChannel::Level",
          "ProgressivePoissonPlaygroundChannel::SplatRadius",
          "ProgressivePoissonPlaygroundChannel::PrefixVisible",
          "const auto runSampler = [&]()",
          "ApplyEditorProgressivePoissonConfig",
          "ApplyEditorProgressivePoissonCommand",
          "source topology and cardinality are preserved",
          "Published source-cardinality level, rank",
          "result.BackendFallbackReason"})
    {
        EXPECT_NE(source.find(required), std::string::npos) << required;
    }

    const std::size_t manualRun = source.find("runSampler();");
    ASSERT_NE(manualRun, std::string::npos);
    EXPECT_NE(source.find("runSampler();", manualRun + 1u),
              std::string::npos)
        << "manual Run and debounced auto-run must share one command path";

    for (const std::string_view forbidden :
         {"MeshSurfaceSampleCount",
          "MeshSurfaceSampleSeed",
          "MeshSurfaceMinTriangleArea",
          "MeshSurfaceInterpolateNormals",
          "Surface input",
          "p:poisson_phase"})
    {
        EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(SandboxEditorPresentation, ExtrinsicSandboxAppStaysRuntimeOnly)
{
    constexpr std::array<std::string_view, 15> paths{{
        "src/app/Sandbox/Sandbox.cppm",
        "src/app/Sandbox/Sandbox.cpp",
        "src/app/Sandbox/main.cpp",
        "src/app/Sandbox/Editor/Sandbox.EditorController.cppm",
        "src/app/Sandbox/Editor/Sandbox.EditorController.cpp",
        "src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp",
        "src/app/Sandbox/Editor/Sandbox.PanelSupport.cpp",
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cppm",
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp",
        "src/app/Sandbox/Editor/Sandbox.MethodPanels.cppm",
        "src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp",
        "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cppm",
        "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp",
        "src/app/Sandbox/Editor/Sandbox.DomainPanels.cppm",
        "src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp",
    }};
    for (const std::string_view path : paths)
    {
        const std::string source = ReadRepositoryTextFile(path);
        ASSERT_FALSE(source.empty()) << path;
        for (const std::string_view forbidden :
             {"import Extrinsic.Asset",
              "import Extrinsic.Core",
              "import Extrinsic.ECS",
              "import Extrinsic.Graphics",
              "import Extrinsic.Platform",
              "import Extrinsic.RHI",
              "import Extrinsic.Backends", "import Extrinsic.Runtime.Private.",
              "import Geometry."})
        {
            EXPECT_EQ(source.find(forbidden), std::string::npos)
                << path << ": " << forbidden;
        }
    }

    const std::string controller = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.EditorController.cpp");
    const std::string shell = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp");
    const std::string cmake =
        ReadRepositoryTextFile("src/app/Sandbox/CMakeLists.txt");
    EXPECT_NE(controller.find("import Extrinsic.Sandbox.Editor.Shell;"),
              std::string::npos);
  constexpr std::array<std::string_view, 6> featureImports{{
      "import Extrinsic.Runtime.EditorWorkspaceSnapshots;",
      "import Extrinsic.Runtime.EditorJobProjection;",
      "import Extrinsic.Runtime.SceneEditingOperations;",
      "import Extrinsic.Runtime.GeometryProcessingOperations;",
      "import Extrinsic.Runtime.VisualizationEditingOperations;",
      "import Extrinsic.Runtime.RenderRecipeEditingOperations;",
  }};
  for (const std::string_view featureImport : featureImports) {
    EXPECT_EQ(controller.find(featureImport), std::string::npos)
        << featureImport;
    EXPECT_NE(shell.find(featureImport), std::string::npos) << featureImport;
  }
  EXPECT_NE(cmake.find("target_link_libraries(ExtrinsicSandboxEditor"),
              std::string::npos);
    EXPECT_NE(cmake.find("ExtrinsicRuntime"), std::string::npos);
    EXPECT_EQ(cmake.find("ExtrinsicGraphics"), std::string::npos);
    EXPECT_EQ(cmake.find("ExtrinsicPlatform"), std::string::npos);
    EXPECT_EQ(cmake.find("ExtrinsicRHI"), std::string::npos);
}

TEST(SandboxEditorPresentation,
     RuntimeFeatureModulesCompileSeparatelyFromEditorShell)
{
    const std::string appShell =
        ReadRepositoryTextFile("src/app/Sandbox/Editor/Sandbox.EditorShell.cpp");
    const std::string appShellContract =
        ReadRepositoryTextFile("src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp");
    const std::string detailContract =
        ReadRepositoryTextFile("src/runtime/Editor/internal/Runtime.EditorFeatures.Internal.hpp");
    const std::string workspaceSession =
        ReadRepositoryTextFile("src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp");
    const std::string workspaceSessionContract =
        ReadRepositoryTextFile("src/runtime/Editor/internal/Runtime.EditorWorkspaceAttachment.Detail.cppm");
    const std::string workspaceModels =
        ReadRepositoryTextFile("src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp");
    const std::string sceneActions =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp");
    const std::string pointSetOperations =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cpp");
    const std::string pointCloudServiceOperations = ReadRepositoryTextFile(
        "src/runtime/Editor/Operations/Runtime.PointCloudServiceOperations.cpp");
    const std::string geometryMeshOperations =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp");
    const std::string meshTopologyOperations =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.MeshTopologyOperations.Topology.cpp");
    const std::string meshFieldOperations =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Curvature.cpp");
    const std::string visualizationActions =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp");
    const std::string renderRecipeOperations =
        ReadRepositoryTextFile("src/runtime/Editor/Operations/Runtime.RenderRecipeEditingOperations.cpp");
    const std::string runtimeCMake = ReadRepositoryTextFile("src/runtime/CMakeLists.txt");
    ASSERT_FALSE(appShell.empty());
    ASSERT_FALSE(appShellContract.empty());
    ASSERT_FALSE(detailContract.empty());
    ASSERT_FALSE(workspaceSession.empty());
    ASSERT_FALSE(workspaceSessionContract.empty());
    ASSERT_FALSE(workspaceModels.empty());
    ASSERT_FALSE(sceneActions.empty());
    ASSERT_FALSE(pointSetOperations.empty());
    ASSERT_FALSE(pointCloudServiceOperations.empty());
    ASSERT_FALSE(geometryMeshOperations.empty());
    ASSERT_FALSE(meshTopologyOperations.empty());
    ASSERT_FALSE(meshFieldOperations.empty());
    ASSERT_FALSE(visualizationActions.empty());
    ASSERT_FALSE(renderRecipeOperations.empty());

    constexpr std::string_view clusteringConfigDefinition =
        "ApplyEditorClusteringConfig(";
    constexpr std::string_view poissonDefinition =
        "ApplyEditorProgressivePoissonCommand(";
    constexpr std::string_view meshDenoiseDefinition =
        "ApplyEditorMeshDenoiseCommand(";
    constexpr std::string_view renderRecipeDefinition =
        "EditorRenderRecipeEditorModel\n"
        "    BuildEditorRenderRecipeEditorModel(";
    constexpr std::string_view renderRecipeCommand = "EditorRenderRecipeCommandResult\n"
                                                     "    ApplyEditorRenderRecipeCommand(";
    EXPECT_NE(appShell.find("#include <imgui.h>"), std::string::npos);
    EXPECT_NE(appShell.find("DrawMainMenuBar("), std::string::npos);
    EXPECT_NE(appShellContract.find("struct SandboxEditorContext final"), std::string::npos);
    for (const std::string_view capability :
         {"Runtime::EditorSceneEditingCommands SceneCommands{};",
          "Runtime::EditorProcessingCommands Processing{};",
          "Runtime::EditorVisualizationEditingCommands VisualizationCommands{};",
          "Runtime::EditorRenderRecipeEditingCommands RenderRecipeCommands{};",
          "Runtime::EditorWorkspaceSnapshotQueries SnapshotQueries{};"})
    {
        EXPECT_NE(appShellContract.find(capability), std::string::npos) << capability;
    }
    EXPECT_EQ(appShellContract.find("EditorFeatureTestContext"), std::string::npos);
    EXPECT_EQ(appShellContract.find("EditorFeatureBindings"), std::string::npos);
    EXPECT_NE(
        appShellContract.find("struct SandboxEditorFrame final : Runtime::EditorWorkspaceSnapshot"),
        std::string::npos);
    EXPECT_NE(appShell.find(
                  "LastFrame = SandboxEditorFrame{prepared.Workspace.Frame}"),
              std::string::npos);
    EXPECT_EQ(detailContract.find("#include <imgui.h>"), std::string::npos);
    EXPECT_EQ(workspaceSession.find("ImGui::"), std::string::npos);
    for (const std::string_view forbidden :
         {"import Geometry.",
          "import Extrinsic.RHI.CommandContext;",
          "import Extrinsic.RHI.Device;",
          "import Extrinsic.Runtime.TextureBakeModule;"})
    {
        EXPECT_EQ(workspaceSessionContract.find(forbidden), std::string::npos)
            << forbidden;
    }
    for (const std::string_view privateOperationPrefix :
         {"ApplyEditor", "BuildEditor", "GetEditor", "SubmitEditor",
          "ResolveEditor", "RenameEditor", "RemoveEditor",
          "DebugNameForEditor"})
    {
        EXPECT_EQ(detailContract.find(privateOperationPrefix), std::string::npos)
            << privateOperationPrefix;
    }
    // EditorCompilationLocality.WorkspaceAttachment checks compiler dependencies.
    EXPECT_NE(workspaceSessionContract.find("std::unique_ptr<Impl> m_Impl;"),
              std::string::npos);
    EXPECT_EQ(workspaceSessionContract.find("EditorFeatureBindings m_Context"),
              std::string::npos);
    EXPECT_NE(workspaceSession.find("EditorFeatureBindings m_Context"),
              std::string::npos);
    EXPECT_NE(workspaceModels.find("BuildEditorWorkspaceSnapshotFromBindings("),
              std::string::npos);
    EXPECT_NE(sceneActions.find("ApplyEditorFileImportCommand("),
              std::string::npos);
    EXPECT_NE(pointCloudServiceOperations.find(clusteringConfigDefinition), std::string::npos);
    EXPECT_NE(pointSetOperations.find(poissonDefinition), std::string::npos);
    // Mesh topology editing and mesh field publication own their commands; the
    // broad mesh unit keeps only the cross-cutting domain/menu catalogue.
    EXPECT_NE(meshTopologyOperations.find(meshDenoiseDefinition),
              std::string::npos);
    EXPECT_NE(meshFieldOperations.find("ApplyEditorMeshCurvatureCommand("),
              std::string::npos);
    EXPECT_EQ(geometryMeshOperations.find(meshDenoiseDefinition),
              std::string::npos);
    EXPECT_NE(geometryMeshOperations.find("ResolveEditorGeometryProcessingEntries("),
              std::string::npos);
    EXPECT_NE(visualizationActions.find("ApplyEditorTextureBakeCommand("),
              std::string::npos);
    EXPECT_NE(renderRecipeOperations.find(renderRecipeDefinition), std::string::npos);
    EXPECT_NE(renderRecipeOperations.find(renderRecipeCommand), std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.SandboxMethodFacade.cpp"), std::string::npos);
    EXPECT_NE(runtimeCMake.find("Modules/Clustering/Runtime.ClusteringModule.cpp"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.RenderRecipeEditingOperations.cpp"), std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.EditorWorkspaceSnapshots.Models.cpp"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("internal/Runtime.EditorWorkspaceSession.cpp"),
              std::string::npos);
    const auto firstPrivateSourceSet = runtimeCMake.find("\n        PRIVATE\n");
    ASSERT_NE(firstPrivateSourceSet, std::string::npos);
    const std::string_view publicRuntimeSources{runtimeCMake.data(), firstPrivateSourceSet};
    for (const std::string_view privateModule : {"internal/Runtime.EditorWorkspaceAttachment.Detail.cppm"})
    {
        EXPECT_EQ(publicRuntimeSources.find(privateModule), std::string_view::npos)
            << privateModule;
        EXPECT_NE(runtimeCMake.find(privateModule), std::string::npos) << privateModule;
    }
    EXPECT_EQ(runtimeCMake.find("FILE_SET feature_config_impl"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.FeatureConfigCodecs.Detail.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Config/internal/Runtime.FeatureConfigCodecs.Detail.cpp"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("PRIVATE\n        FILE_SET editor_feature_impl TYPE CXX_MODULES"),
              std::string::npos);
    EXPECT_NE(workspaceSession.find("SubscribeRunCompleted("), std::string::npos);
    EXPECT_NE(workspaceSession.find("AttachmentEpochIsActive(epoch)"), std::string::npos);
    EXPECT_NE(workspaceSession.find("EditorPointCloudServiceResultsSnapshot m_PointCloudServiceResults{};"),
              std::string::npos);
    EXPECT_NE(workspaceSession.find("m_PointCloudServiceResults = {};"), std::string::npos);
    EXPECT_EQ(pointSetOperations.find("HasInFlightJob()"), std::string::npos);
    EXPECT_EQ(pointCloudServiceOperations.find("m_KMeansGpuJobs"), std::string::npos);

    constexpr std::array<std::string_view, 16> retiredFiles{{
        "src/runtime/Runtime.SandboxEditorFacades.cppm",
        "src/runtime/Runtime.SandboxEditorFacades.cpp",
        "src/runtime/Runtime.SandboxEditorFacades.Internal.hpp",
        "src/runtime/Runtime.SandboxMethodFacade.cpp",
        "src/runtime/Runtime.SandboxParameterizationFacade.cpp",
        "src/runtime/Runtime.SandboxEditorRenderRecipeFacade.cpp",
        "src/runtime/Runtime.SandboxConfigSections.cppm",
        "src/runtime/Runtime.SandboxConfigSections.cpp",
        "src/runtime/Runtime.SandboxDefaultPolicies.cppm",
        "src/runtime/Runtime.SandboxDefaultPolicies.cpp",
        "src/runtime/Runtime.RegistrationAlignment.cppm",
        "src/runtime/Runtime.RegistrationAlignment.cpp",
        "src/runtime/internal/Runtime.EditorWorkspaceSnapshots.cpp",
        "src/runtime/internal/Runtime.EditorFeatures.Detail.hpp",
        "src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Public.cpp",
        "src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp",
    }};
    for (const std::string_view retiredFile : retiredFiles)
    {
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{ENGINE_ROOT_DIR} / retiredFile))
            << retiredFile;
        const std::string retiredCMakeEntry =
            "\n        " + std::filesystem::path{retiredFile}.filename().string() + "\n";
        EXPECT_EQ(runtimeCMake.find(retiredCMakeEntry), std::string::npos)
            << retiredFile;
    }
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{ENGINE_ROOT_DIR} /
                                         "tests/unit/runtime/Test.RegistrationAlignment.cpp"));

    constexpr std::array<std::string_view, 24> allowedPrivateImporters{{
        "Runtime.EditorWorkspaceSession.cpp",
        "Runtime.EditorCommon.Public.cpp",
        "Runtime.EditorJobProjection.Public.cpp",
        "Runtime.EditorWorkspaceSnapshots.Public.cpp",
        "Runtime.EditorWorkspaceSnapshots.Models.cpp",
        "Runtime.EditorFeatureContextAdapters.cpp",
        "Runtime.SceneEditingOperations.Public.cpp",
        "Runtime.SceneEditingOperations.Actions.cpp",
        "Runtime.PointFieldOperations.Frame.cpp",
        "Runtime.PointAnalysisOperations.Frame.cpp",
        "Runtime.PointSetOperations.Frame.cpp",
        "Runtime.PointConstructionOperations.Frame.cpp",
        "Runtime.PointCloudServiceOperations.Frame.cpp",
        "Runtime.NormalOperations.Frame.cpp",
        "Runtime.RegistrationOperations.Frame.cpp",
        "Runtime.ParameterizationOperations.Frame.cpp",
        "Runtime.MeshFieldOperations.Frame.cpp",
        "Runtime.MeshTopologyOperations.Frame.cpp",
        "Runtime.GeometryProcessingOperations.Frame.cpp",
        "Runtime.VisualizationEditingOperations.Public.cpp",
        "Runtime.VisualizationEditingOperations.Actions.cpp",
        "Runtime.RenderRecipeEditingOperations.Public.cpp",
        "Runtime.EditorWorkspaceAttachment.Detail.cppm",
        "Runtime.EditorWorkspaceAttachment.cpp",
    }};
    const std::filesystem::path runtimeRoot =
        std::filesystem::path{ENGINE_ROOT_DIR} / "src/runtime";
    for (const auto& entry : std::filesystem::recursive_directory_iterator{runtimeRoot})
    {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        if (extension != ".cpp" && extension != ".cppm" && extension != ".hpp" && extension != ".h")
            continue;

        const std::string source = ReadRepositoryTextFile(
            std::filesystem::relative(entry.path(), ENGINE_ROOT_DIR).string());
        for (const std::string_view retiredPrefix :
             {"SandboxEditor", "SandboxMethod", "SandboxConfig", "SandboxDefault",
              "SandboxParameterization"})
        {
            EXPECT_EQ(source.find(retiredPrefix), std::string::npos)
                << entry.path() << ": " << retiredPrefix;
        }

        if (source.find("import Extrinsic.Runtime.Private.") == std::string::npos)
            continue;
        EXPECT_NE(std::ranges::find(allowedPrivateImporters, entry.path().filename().string()),
                  allowedPrivateImporters.end())
            << entry.path();
    }
}

TEST(SandboxEditorPresentation, GeometryProcessingMenusExposeDomainElementSubmenus)
{
    using Domain = Runtime::EditorGeometryProcessingDomain;

    const auto mesh = Runtime::GetEditorGeometryProcessingMenuItems(
        Runtime::EditorDomainWindowKind::Mesh);
    ASSERT_EQ(mesh.size(), 3u);
    EXPECT_EQ(mesh[0].Domain, Domain::MeshVertices);
    EXPECT_STREQ(mesh[0].Label, "Vertices");
    EXPECT_TRUE(mesh[0].HasNormalsMethod);
    EXPECT_TRUE(mesh[0].HasDenoiseMethod);
    EXPECT_TRUE(mesh[0].HasCurvatureMethod);
    EXPECT_TRUE(mesh[0].HasRemeshMethod);
    EXPECT_TRUE(mesh[0].HasSubdivideMethod);
    EXPECT_TRUE(mesh[0].HasSimplifyMethod);
    EXPECT_EQ(mesh[1].Domain, Domain::MeshEdges);
    EXPECT_STREQ(mesh[1].Label, "Edges");
    EXPECT_FALSE(mesh[1].HasNormalsMethod);
    EXPECT_EQ(mesh[2].Domain, Domain::MeshFaces);
    EXPECT_STREQ(mesh[2].Label, "Faces");
    EXPECT_FALSE(mesh[2].HasNormalsMethod);

    const auto graph = Runtime::GetEditorGeometryProcessingMenuItems(
        Runtime::EditorDomainWindowKind::Graph);
    ASSERT_EQ(graph.size(), 3u);
    EXPECT_EQ(graph[0].Domain, Domain::GraphVertices);
    EXPECT_STREQ(graph[0].Label, "Vertices");
    EXPECT_TRUE(graph[0].HasNormalsMethod);
    EXPECT_EQ(graph[1].Domain, Domain::GraphEdges);
    EXPECT_STREQ(graph[1].Label, "Edges");
    EXPECT_EQ(graph[2].Domain, Domain::GraphHalfedges);
    EXPECT_STREQ(graph[2].Label, "Halfedges");

    const auto cloud = Runtime::GetEditorGeometryProcessingMenuItems(
        Runtime::EditorDomainWindowKind::PointCloud);
    ASSERT_EQ(cloud.size(), 1u);
    EXPECT_EQ(cloud[0].Domain, Domain::PointCloudPoints);
    EXPECT_STREQ(cloud[0].Label, "Vertices");
    EXPECT_TRUE(cloud[0].HasNormalsMethod);
}

TEST(SandboxEditorPresentation, AdapterCallbackDrawsDeterministicMenuOnlyFrame)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());

    engine.Run();

    const Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    const auto& diagnostics = editorUi->GetDiagnostics();
    EXPECT_GE(diagnostics.EditorCallbackInvocations, 1u);
    EXPECT_GE(diagnostics.FramesProduced, 1u);
    EXPECT_GE(diagnostics.LastDrawListCount, 1u);
    EXPECT_TRUE(ImGuiWindowExists("##MainMenuBar"));
    EXPECT_TRUE(shell.GetLastFrame().FileImport.Enabled);

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation,
     FileImportDisabledReasonRendersThroughRealHoveredControl)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<TooltipDelayApplication>());
    ComposeEditorUiAndInitialize(engine);

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(shell.SetEditorWindowOpen("file.import", true));

    std::uint32_t observerFrames = 0u;
    bool importButtonHovered = false;
    bool tooltipVisible = false;
    ImVec2 queuedMousePosition{};
    ImVec2 observedMousePosition{};
    std::string hoveredWindowName{};
    const Runtime::EditorWindowHandle observer = shell.RegisterEditorWindow(
        Editor::EditorWindowDescriptor{
            .Id = "test.file_import_hover_observer",
            .MenuPath = {"View"},
            .Title = "File import hover observer",
            .OpenByDefault = true,
            .Draw =
                [&](bool&, const Editor::SandboxEditorContext&)
                {
                    ++observerFrames;
                    ImGuiWindow* importWindow =
                        ImGui::FindWindowByName("File / Import");
                    if (importWindow == nullptr)
                        return;

                    if (observerFrames == 1u)
                    {
                        // Keep the observer window from occluding the real
                        // built-in window on the hover frame.
                        ImGui::SetWindowPos(
                            ImVec2(8.0f, 500.0f),
                            ImGuiCond_Always);
                        const ImGuiStyle& style = ImGui::GetStyle();
                        const float frameHeight = ImGui::GetFrameHeight();
                        const ImVec2 labelSize =
                            ImGui::CalcTextSize("Import asset");
                        const ImVec2 importButtonCenter{
                            importWindow->DC.CursorStartPos.x +
                                (labelSize.x + 2.0f * style.FramePadding.x) /
                                    2.0f,
                            importWindow->DC.CursorStartPos.y +
                                2.0f * (frameHeight + style.ItemSpacing.y) +
                                frameHeight / 2.0f,
                        };
                        queuedMousePosition = importButtonCenter;
                        static_cast<
                            Plat::Backends::Null::NullWindow&>(
                                engine.GetWindow())
                            .QueueCursor(
                                importButtonCenter.x,
                                importButtonCenter.y);
                        return;
                    }

                    if (observerFrames == 2u)
                        return;

                    const ImGuiID importButtonId =
                        importWindow->GetID("Import asset");
                    observedMousePosition = ImGui::GetIO().MousePos;
                    if (ImGui::GetCurrentContext()->HoveredWindow != nullptr)
                    {
                        hoveredWindowName =
                            ImGui::GetCurrentContext()->HoveredWindow->Name;
                    }
                    importButtonHovered =
                        ImGui::GetCurrentContext()->HoveredId == importButtonId ||
                        (ImGui::GetCurrentContext()->HoveredId == 0u &&
                         ImGui::GetCurrentContext()->HoveredIdIsDisabled);
                    tooltipVisible = ActiveImGuiTooltipExists();
                    if (tooltipVisible)
                        engine.RequestExit();
                },
        });
    ASSERT_TRUE(observer.IsValid());

    const ImGuiHoveredFlags productionTooltipHoverFlags =
        ImGui::GetStyle().HoverFlagsForTooltipMouse;
    ASSERT_TRUE(
        (productionTooltipHoverFlags & ImGuiHoveredFlags_Stationary) != 0);
    ASSERT_TRUE(
        (productionTooltipHoverFlags & ImGuiHoveredFlags_DelayShort) != 0);
    engine.Run();

    ASSERT_GE(observerFrames, 3u);
    ASSERT_LT(observerFrames, TooltipDelayApplication::MaxFrames);
    ASSERT_FALSE(shell.GetLastFrame().FileImport.CanImport);
    ASSERT_FALSE(shell.GetLastFrame().FileImport.ImportDisabledReason.empty());
    EXPECT_TRUE(importButtonHovered)
        << "queued=(" << queuedMousePosition.x << ", "
        << queuedMousePosition.y << ") observed=("
        << observedMousePosition.x << ", " << observedMousePosition.y
        << ") hoveredWindow=" << hoveredWindowName;
    EXPECT_TRUE(tooltipVisible)
        << "queued=(" << queuedMousePosition.x << ", "
        << queuedMousePosition.y << ") observed=("
        << observedMousePosition.x << ", " << observedMousePosition.y
        << ") hoveredWindow=" << hoveredWindowName;

    EXPECT_TRUE(shell.UnregisterEditorWindow(observer));
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation,
     DisabledReasonTooltipConventionIsSharedByImportAndQueueControls)
{
    const std::string shell = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp");
    ASSERT_FALSE(shell.empty());
    const std::string compactShell = WithoutAsciiWhitespace(shell);
    EXPECT_NE(
        compactShell.find(
            "DrawDisabledReasonTooltip(model.ClearCompletedDisabledReason);"),
        std::string::npos);
    EXPECT_NE(
        compactShell.find(
            "DrawDisabledReasonTooltip(row.CancelDisabledReason);"),
        std::string::npos);
    EXPECT_NE(
        compactShell.find(
            "DrawDisabledReasonTooltip(frame.FileImport.ImportDisabledReason);"),
        std::string::npos);

    const auto support = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.PanelSupport.cpp");
    ASSERT_FALSE(support.empty());
    for (const auto flag : {"ImGuiHoveredFlags_ForTooltip |", "ImGuiHoveredFlags_AllowWhenDisabled"})
        EXPECT_NE(support.find(flag), std::string::npos) << flag;

    for (const std::string_view required :
         {"option.DisabledReason",
          "frame.FileImport.PayloadHintDisabledReason",
          "frame.FileImport.ImportDisabledReason",
          "frame.FileImport.CanChoosePayloadHint",
          "frame.FileImport.CanImport"})
    {
        EXPECT_NE(shell.find(required), std::string::npos) << required;
    }
}

TEST(SandboxEditorPresentation, RuntimeImportEventIsReflectedByAppFilePanel)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());

    const auto imported = RequiredEngineService<Extrinsic::Runtime::AssetWorkflowModule>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = "/tmp/intrinsic-arch-006-missing.ply",
            .PayloadKind =
                Runtime::EditorAssetPayloadKind::PointCloud,
        });
    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error(), Core::ErrorCode::FileNotFound);

    engine.Run();

    ASSERT_TRUE(shell.GetLastFrame().FileImport.LastResult.has_value());
    EXPECT_FALSE(shell.GetLastFrame().FileImport.LastResult->Succeeded());
    EXPECT_EQ(shell.GetLastFrame().FileImport.LastResult->PayloadKind,
              Runtime::EditorAssetPayloadKind::PointCloud);
    EXPECT_TRUE(HasDiagnostic(
        shell.GetLastFrame().FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportFailed));

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedFilePathsRouteAmbiguousPlyThroughAssetWorkflow)
{
    TmpFile cloudFile(
        "runtime_dragdrop_event_cloud.ply",
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "2 0 0\n");

    auto waitForImport =
        std::make_unique<WaitForAssetImportEventApplication>();
    WaitForAssetImportEventApplication* waitDiagnostics =
        waitForImport.get();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(waitForImport));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    ComposeEditorUiAndInitialize(engine);
    auto& pipeline =
        RequiredEngineService<Runtime::AssetWorkflowModule>(engine);

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());

    const std::vector<std::string> droppedPaths{cloudFile.Path.string()};
    pipeline.ImportDroppedFilePaths(droppedPaths);
    EXPECT_FALSE(pipeline.GetLastAssetImportEvent().has_value());
    ASSERT_FALSE(engine.GetWindow().ShouldClose());

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        pipeline.GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value()) << waitDiagnostics->Describe();
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind,
              Runtime::EditorAssetPayloadKind::PointCloud);
    EXPECT_EQ(lastEvent->Result->PrimitiveEntitiesCreated, 1u);

    ASSERT_TRUE(shell.GetLastFrame().FileImport.LastResult.has_value());
    EXPECT_TRUE(shell.GetLastFrame().FileImport.LastResult->Succeeded());
    EXPECT_EQ(shell.GetLastFrame().FileImport.LastResult->PayloadKind,
              Runtime::EditorAssetPayloadKind::PointCloud);
    EXPECT_FALSE(HasDiagnostic(
        shell.GetLastFrame().FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportFailed));

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, EngineAttachmentRegistersEditorCallback)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    EXPECT_TRUE(shell.IsAttached());

    engine.Run();

    const Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    EXPECT_GE(editorUi->GetDiagnostics().EditorCallbackInvocations, 1u);
    EXPECT_TRUE(shell.GetLastFrame().FileImport.Enabled);
    EXPECT_FALSE(HasDiagnostic(
        shell.GetLastFrame().FileImport.Diagnostics,
        Runtime::EditorDiagnosticCode::AssetImportUnavailable));

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, ControllerReattachPinsPanelAttachmentResetPolicy)
{
    Editor::SandboxEditorController controller;

    Intrinsic::Tests::RuntimeTestKernel firstEngine(HeadlessConfig(),
                                                    std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(firstEngine);
    controller.Attach(firstEngine.Worlds(), firstEngine.Services());
    ASSERT_TRUE(controller.IsAttached());
    firstEngine.Run();
    controller.Detach();
    EXPECT_FALSE(controller.IsAttached());
    firstEngine.Shutdown();

    Intrinsic::Tests::RuntimeTestKernel secondEngine(HeadlessConfig(),
                                                     std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(secondEngine);
    controller.Attach(secondEngine.Worlds(), secondEngine.Services());
    ASSERT_TRUE(controller.IsAttached());
    secondEngine.Run();
    controller.Detach();
    EXPECT_FALSE(controller.IsAttached());
    secondEngine.Shutdown();

    const std::string methodPanels = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp");
    const std::string meshPanels = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp");
    const std::string domainPanels = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp");
    EXPECT_NE(methodPanels.find("KMeans = KMeansState{};"),
              std::string::npos);
    EXPECT_NE(methodPanels.find("ProgressivePoisson.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(methodPanels.find("ProgressivePoisson.LastConfigResult.reset();"),
              std::string::npos);
    EXPECT_NE(methodPanels.find("ProgressivePoisson.AutoRunPending = false;"),
              std::string::npos);
    EXPECT_NE(methodPanels.find("ProgressivePoisson.LastEditTime = 0.0;"),
              std::string::npos);
    EXPECT_NE(methodPanels.find("PendingStableEntityId = 0u;"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Denoise.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Curvature.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Remesh.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Subdivide.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Simplify.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Normals = {};"), std::string::npos);
    EXPECT_NE(meshPanels.find("Registration = {};"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Outliers = {};"), std::string::npos);
    EXPECT_NE(domainPanels.find("LastUvRegenerationResult.reset();"),
              std::string::npos);
    EXPECT_NE(domainPanels.find("LastUvExtentAdoption.reset();"),
              std::string::npos);
    EXPECT_NE(domainPanels.find(
                  "MeshPropertyPlotState.SelectedProperty.clear();"),
              std::string::npos);
}

TEST(SandboxEditorPresentation, EditorShellStartsWithOnlyBuiltinWindows)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), 12u);
    for (const std::string_view id :
         {"sandbox.shell",
          "scene.hierarchy",
          "scene.inspector",
          "scene.selection",
          "file.scene",
          "file.import",
          "view.frame_graph",
          "view.render_recipes",
          "view.camera_render",
          "view.geometry_visualization",
          "view.jobs",
          "view.diagnostics"})
    {
        EXPECT_NE(FindWindow(menu, id), nullptr) << id;
    }
    EXPECT_EQ(FindWindow(menu, "mesh.appearance"), nullptr);
    EXPECT_EQ(FindWindow(menu, "pointcloud.processing.kmeans"), nullptr);
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation,
     GpuProfilingControlsReuseTheExistingFrameGraphWindow)
{
    const std::string shell = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp");
    ASSERT_FALSE(shell.empty());

    const std::size_t frameGraphBegin =
        shell.find("windowId == \"view.frame_graph\"");
    const std::size_t frameGraphEnd =
        shell.find("windowId == \"view.render_recipes\"", frameGraphBegin);
    ASSERT_NE(frameGraphBegin, std::string::npos);
    ASSERT_NE(frameGraphEnd, std::string::npos);
    ASSERT_LT(frameGraphBegin, frameGraphEnd);

    const std::string_view frameGraphPanel{
        shell.data() + frameGraphBegin,
        frameGraphEnd - frameGraphBegin};
    const std::string compactPanel =
        WithoutAsciiWhitespace(frameGraphPanel);
    EXPECT_NE(
        frameGraphPanel.find("Enable GPU profiling"),
        std::string_view::npos);
    EXPECT_NE(frameGraphPanel.find("\"GPU Profile\""),
              std::string_view::npos);
    EXPECT_NE(compactPanel.find("ApplyEditorGpuProfilingConfigCommand("
                              "context->RenderRecipeCommands,"
                              "gpuProfilingEnabled)"),
              std::string::npos);
    EXPECT_NE(frameGraphPanel.find("Queue envelopes:"),
              std::string_view::npos);
    EXPECT_NE(frameGraphPanel.find("Pass samples:"),
              std::string_view::npos);
}

TEST(SandboxEditorPresentation, ExternalWindowContributionNeedsNoLegacySwitchEntry)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    int drawCalls = 0;
    const Runtime::EditorWindowHandle handle = shell.RegisterEditorWindow(
        Editor::EditorWindowDescriptor{
            .Id = "graph.analysis.curvature",
            .MenuPath = {"Graph", "Analysis"},
            .Title = "Curvature",
            .OpenByDefault = false,
            .Draw =
                [&drawCalls](bool&, const Editor::SandboxEditorContext&)
                {
                    ++drawCalls;
                },
        });
    ASSERT_TRUE(handle.IsValid());

    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), 13u);
    const Runtime::EditorWindowMenuEntry* contributed =
        FindWindow(menu, "graph.analysis.curvature");
    ASSERT_NE(contributed, nullptr);
    EXPECT_EQ(contributed->MenuPath,
              (std::vector<std::string>{"Graph", "Analysis"}));
    EXPECT_EQ(drawCalls, 0);

    EXPECT_TRUE(shell.UnregisterEditorWindow(handle));
    EXPECT_EQ(shell.BuildEditorWindowMenuModel().size(), 12u);
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation,
     ContextWindowContributionReceivesAppOwnedContext)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    int drawCalls = 0;
    bool receivedScene = false;
    std::optional<Runtime::EditorPointCloudServicePreparedFrame> retained;
    const Runtime::EditorWindowHandle handle = shell.RegisterEditorWindow(
        Editor::EditorWindowDescriptor{
            .Id = "test.context_window",
            .MenuPath = {"View"},
            .Title = "Context Window",
            .OpenByDefault = true,
            .Draw =
                [&drawCalls, &receivedScene, &retained](
                    bool&,
                    const Editor::SandboxEditorContext& context)
                {
                    ++drawCalls;
                    receivedScene = context.SceneAvailable;
                    ASSERT_NE(context.PointCloudService, nullptr);
                    EXPECT_TRUE(context.PointCloudService->Commands.IsBound());
                    retained = *context.PointCloudService;
                },
        });
    ASSERT_TRUE(handle.IsValid());

    engine.Run();

    EXPECT_EQ(drawCalls, 1);
    EXPECT_TRUE(receivedScene);
    ASSERT_TRUE(retained.has_value());
    EXPECT_TRUE(retained->Commands.IsBound());
    EXPECT_TRUE(shell.UnregisterEditorWindow(handle));
    shell.Detach();
    EXPECT_FALSE(retained->Commands.IsBound());
    EXPECT_FALSE(Runtime::IsEditorClusteringAvailable(
        retained->Commands, retained->Clustering));
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, GlobalVisibilityHotkeyUsesTheVisibilityCommandPath)
{
    Intrinsic::Tests::RuntimeTestKernel engine(
        HeadlessConfig(), std::make_unique<ToggleEditorVisibilityApplication>());
    ComposeEditorUiAndInitialize(engine);

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(shell.IsEditorVisible());

    engine.Run();

    EXPECT_FALSE(shell.IsEditorVisible());
    const Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    EXPECT_FALSE(editorUi->GetDiagnostics().CapturesViewportInput);
    const Runtime::EditorUiVisibilityCommandResult restored =
        shell.ApplyEditorUiVisibilityCommand(
            Runtime::EditorUiVisibilityCommand{
                Runtime::EditorUiVisibilityCommandKind::Show});
    EXPECT_FALSE(restored.WasVisible);
    EXPECT_TRUE(restored.IsVisible);
    EXPECT_TRUE(restored.Changed);
    EXPECT_TRUE(shell.IsEditorVisible());

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, RegistrationDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.registration", "graph.processing.registration", "pointcloud.processing.registration"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.registration"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.registration")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.registration", false));
    }
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, NormalDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.vertices.normals", "graph.processing.vertices.normals", "pointcloud.processing.vertices.normals", "mesh.processing.faces.normals"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.normal_estimation"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.normal_estimation")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.normal_estimation", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.normal_estimation", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, OutlierDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.outliers", "graph.processing.outliers", "pointcloud.processing.remove_outliers"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.outlier_analysis"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.outlier_analysis")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.outlier_analysis", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.outlier_analysis", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, DensityDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.kernel_density", "graph.processing.kernel_density", "pointcloud.processing.kernel_density"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.kernel_density"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.kernel_density")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.kernel_density", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.kernel_density", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, SpacingDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.point_spacing", "graph.processing.point_spacing", "pointcloud.processing.point_spacing"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.point_spacing"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.point_spacing")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.point_spacing", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.point_spacing", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, BilateralDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.bilateral_filter", "graph.processing.bilateral_filter", "pointcloud.processing.bilateral_filter"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.bilateral_filter"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.bilateral_filter")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.bilateral_filter", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.bilateral_filter", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, KeypointDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.keypoints", "graph.processing.keypoints", "pointcloud.processing.keypoints"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.keypoint_analysis"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.keypoint_analysis")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.keypoint_analysis", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.keypoint_analysis", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}
TEST(SandboxEditorPresentation, DescriptorDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.descriptors", "graph.processing.descriptors", "pointcloud.processing.descriptors"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.descriptor_analysis"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.descriptor_analysis")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.descriptor_analysis", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.descriptor_analysis", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, DensityWeightDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.density_weights", "graph.processing.density_weights", "pointcloud.processing.density_weights"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.density_weights"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.density_weights")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.density_weights", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.density_weights", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}


TEST(SandboxEditorPresentation, PointConstructionDomainMenusOpenOneSharedWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    for (const auto* id : {"mesh.processing.point_construction", "graph.processing.point_construction", "pointcloud.processing.point_construction"})
    {
        ASSERT_TRUE(shell.SetEditorWindowOpen(id, true));
        const auto menu = shell.BuildEditorWindowMenuModel();
        ASSERT_NE(FindWindow(menu, "view.point_construction"), nullptr);
        EXPECT_TRUE(FindWindow(menu, "view.point_construction")->Open);
        ASSERT_NE(FindWindow(menu, id), nullptr);
        EXPECT_FALSE(FindWindow(menu, id)->Open);
        ASSERT_TRUE(shell.SetEditorWindowOpen("view.point_construction", false));
    }
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.point_construction", true));
    engine.Run();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, RegistrationInterfacesExcludeProcessingDependencies)
{
    for (const auto path : {
             "src/app/Sandbox/Editor/Sandbox.DomainPanels.cppm",
             "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cppm",
             "src/app/Sandbox/Editor/Sandbox.MethodPanels.cppm",
             "src/app/Sandbox/Editor/Sandbox.EditorShell.cppm"})
    {
        const auto source = ReadRepositoryTextFile(path);
        ASSERT_FALSE(source.empty()) << path;
        for (const auto dependency : {"EditorWorkspaceSnapshots", "GeometryProcessingOperations",
                                     "VisualizationEditingOperations", "Editor.PanelSupport"})
            EXPECT_EQ(source.find(dependency), std::string::npos) << path << ": " << dependency;
    }
    for (const auto path : {"src/app/Sandbox/Editor/Sandbox.EditorShell.cppm",
                            "src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp"})
    {
        const auto source = ReadRepositoryTextFile(path);
        // A definition may be exported only if its first declaration is exported.
        EXPECT_EQ(source.find("#include \"Sandbox.EditorFwd.hpp\""), std::string::npos) << path;
    }
    const auto helpers = ReadRepositoryTextFile(
        "src/runtime/Editor/internal/Runtime.EditorFeatures.Internal.hpp");
    EXPECT_EQ(helpers.find("class EditorWorkspaceSession"), std::string::npos);
    EXPECT_EQ(helpers.find("#include <entt/entity/registry.hpp>"), std::string::npos);
}

TEST(SandboxEditorPresentation, DisabledActionReasonTooltipAppearsAfterTwoFrames)
{
    ProcessingButtonGui gui;
    const auto readiness = Runtime::ResolveEditorProcessingActionReadiness({}, {true, {}});
    ASSERT_FALSE(readiness.Enabled);
    ASSERT_FALSE(readiness.DisabledReason.empty());
    EXPECT_FALSE(gui.Draw(readiness));
    const auto button = gui.ButtonCenter;
    EXPECT_FALSE(ActiveImGuiTooltipExists());
    EXPECT_EQ(std::string{gui.Context->LogBuffer.c_str()}.find(readiness.DisabledReason), std::string::npos);
    gui.End();

    ImGui::GetIO().AddMousePosEvent(button.x, button.y);
    EXPECT_FALSE(gui.Draw(readiness));
    EXPECT_TRUE(ActiveImGuiTooltipExists());
    EXPECT_NE(std::string{gui.Context->LogBuffer.c_str()}.find(readiness.DisabledReason), std::string::npos);
    gui.End();
}

TEST(SandboxEditorPresentation, ShowPropertyButtonTakesAnOptionalLabelAndReportsTheAppliedStatus)
{
    ProcessingButtonGui gui;
    const Editor::SandboxEditorContext context{};
    const Runtime::GeometryPropertyRef property{.Name = "outlier_mask"};
    std::string diagnostic;
    std::optional<Runtime::EditorCommandStatus> status;
    const auto frame = [&](const char* label, const bool force) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({500, 200});
        ImGui::Begin("Show button test", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
        gui.Context->LogBuffer.clear();
        ImGui::LogToBuffer();
        status = Editor::DrawProcessingPropertyShowButton(context, 1u, property, diagnostic, label, false, force);
        Editor::DrawProcessingDisplayDiagnostic(diagnostic);
        const std::string drawn = gui.Context->LogBuffer.c_str();
        gui.End();
        return drawn;
    };
    auto drawn = frame(nullptr, false);
    EXPECT_NE(drawn.find("Show outlier_mask"), std::string::npos);
    EXPECT_FALSE(status.has_value());
    EXPECT_TRUE(diagnostic.empty());
    EXPECT_EQ(drawn.find("Display:"), std::string::npos);
    drawn = frame("Show mask", true);
    EXPECT_NE(drawn.find("Show mask"), std::string::npos);
    EXPECT_EQ(drawn.find("Show outlier_mask"), std::string::npos);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(diagnostic, Runtime::DebugNameForEditorCommandStatus(*status));
    EXPECT_NE(drawn.find("Display: " + diagnostic), std::string::npos);
}

TEST(SandboxEditorPresentation, ProcessingActionButtonBlocksDisabledClicksAndEmitsEnabledConfigCommand)
{
    namespace Config = Extrinsic::Core::Config;
    Config::EngineConfigSectionRegistry sections;
    ASSERT_TRUE(sections.Register(Runtime::MakeNormalEstimationConfigSectionRegistration()));
    Runtime::RuntimeEngineConfigControlState state;
    Config::PopulateEngineConfigSectionDefaults(state.ActiveConfig, sections);
    unsigned applied = 0;
    Runtime::EditorProcessingContext context;
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    context.PreviewEngineConfigDocument = [&](const auto& document, const auto& origin) {
        return Config::PreviewEngineConfig(document, state.ActiveConfig, {origin, &sections});
    };
    context.ApplyEngineConfigHotSubset = [&](const auto& preview) {
        ++applied;
        state.ActiveConfig = preview.Preview.Config;
        return Runtime::RuntimeEngineConfigApplyResult{.Status = Runtime::RuntimeEngineConfigApplyStatus::Applied};
    };
    const auto commands = Runtime::BindEditorProcessingCommands(context);
    Runtime::NormalEstimationConfig request;
    request.KNeighbors = 12;
    ProcessingButtonGui gui;
    const auto disabled = Runtime::ResolveEditorProcessingActionReadiness({}, {true, {}});
    const auto enabled = Runtime::ResolveEditorProcessingActionReadiness(commands, {true, {}});
    ASSERT_TRUE(enabled.Enabled);
    EXPECT_FALSE(gui.Draw(disabled));
    const auto button = gui.ButtonCenter;
    gui.End();
    ImGui::GetIO().AddMousePosEvent(button.x, button.y);
    const auto draw = [&](const auto& readiness) {
        if (gui.Draw(readiness))
            EXPECT_TRUE(Runtime::ApplyEditorNormalEstimationConfig(commands, request).Succeeded());
        if (readiness.Enabled) EXPECT_FALSE(ActiveImGuiTooltipExists());
        gui.End();
    };
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw(disabled);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw(disabled);
    EXPECT_EQ(applied, 0u);
    draw(enabled);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw(enabled);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw(enabled);
    EXPECT_EQ(applied, 1u);
    ASSERT_TRUE(Runtime::GetEditorNormalEstimationConfig(commands));
    EXPECT_EQ(Runtime::GetEditorNormalEstimationConfig(commands)->KNeighbors, 12u);
}

// UI-071: no panel hand-writes a GPU transaction's Stop/Accept/Discard row; they all go through
// `DrawGpuTransactionControls`. Coherent Point Drift's Discard belongs to its own stepped run (it is
// not a GPU two-phase transaction), so that one literal is the only exception.
TEST(SandboxEditorPresentation, GpuTransactionRowsAreDrawnByTheSharedHelper)
{
    std::size_t helperUses = 0;
    for (const char* file : {"Sandbox.MeshProcessingPanels.cpp", "Sandbox.MethodPanels.cpp", "Sandbox.DomainPanels.cpp",
                             "Sandbox.EditorShell.cpp"})
    {
        std::string source = ReadRepositoryTextFile(std::filesystem::path{"src/app/Sandbox/Editor"} / file);
        ASSERT_FALSE(source.empty()) << file;
        for (std::size_t at = source.find("DrawGpuTransactionControls("); at != std::string::npos;
             at = source.find("DrawGpuTransactionControls(", at + 1))
            ++helperUses;
        for (const std::string_view label : {"Accept", "Discard", "Stop"})
        {
            const std::string needle = "Button(\"" + std::string{label};
            std::size_t at = 0;
            while ((at = source.find(needle, at)) != std::string::npos)
            {
                const auto end = source.find('"', at + needle.size());
                const std::string literal = source.substr(at, end - at + 1);
                EXPECT_EQ(literal, "Button(\"Discard##CPD\"") << file << " hand-writes " << literal;
                at += needle.size();
            }
        }
    }
    EXPECT_GE(helperUses, 6u) << "scalar, Outliers, Normals, Smoothing, consolidation and K-Means draw the shared row";
}

// UI-058: an action button is never left in a bare `BeginDisabled`: a disabled button goes through
// `DrawProcessingActionButton` (the runtime's or the panel's reasons as a tooltip), or each button in the disabled
// group is followed by `DrawDisabledReasonTooltip`. Source scan over the panel files; nested groups count once.
TEST(SandboxEditorPresentation, ActionButtonsNeverSitInABareBeginDisabled)
{
    for (const char* file : {"Sandbox.MeshProcessingPanels.cpp", "Sandbox.MethodPanels.cpp",
                             "Sandbox.EditorShell.cpp", "Sandbox.PanelSupport.cpp"})
    {
        const std::string source = ReadRepositoryTextFile(std::filesystem::path{"src/app/Sandbox/Editor"} / file);
        ASSERT_FALSE(source.empty()) << file;
        const std::string_view begin = "ImGui::BeginDisabled(", end = "ImGui::EndDisabled()";
        const auto count = [](const std::string_view text, const std::initializer_list<std::string_view> needles) {
            std::size_t n = 0;
            for (const auto needle : needles)
                for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) ++n;
            return n;
        };
        for (std::size_t at = source.find(begin); at != std::string::npos; at = source.find(begin, at + 1))
        {
            // The matching EndDisabled: nested Begin/End pairs inside the group are skipped.
            std::size_t close = at + begin.size();
            for (int depth = 1; depth > 0;)
            {
                const std::size_t nextEnd = source.find(end, close), nextBegin = source.find(begin, close);
                ASSERT_NE(nextEnd, std::string::npos) << file;
                if (nextBegin != std::string::npos && nextBegin < nextEnd) { ++depth; close = nextBegin + begin.size(); }
                else { --depth; close = nextEnd + end.size(); }
            }
            const std::size_t nextGroup = source.find(begin, close);
            const std::size_t tailEnd = std::min({source.size(), close + 700, nextGroup == std::string::npos ? source.size() : nextGroup});
            const std::string_view block{source.data() + at, close - at};
            const std::string_view withTail{source.data() + at, tailEnd - at};
            const auto buttons = count(block, {"ImGui::Button(", "ImGui::SmallButton(", "DrawProcessingPropertyShowButton("});
            const auto reasons = count(withTail, {"DrawDisabledReasonTooltip(", "DrawReadinessReasonsTooltip("});
            const auto line = std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(at), '\n') + 1;
            EXPECT_GE(reasons, buttons)
                << file << ":" << line << " draws a button in a bare BeginDisabled; use DrawProcessingActionButton";
        }
    }
}

// --- UI-078: the ImGuizmo frontend in the production shell ------------------

namespace
{
    namespace Tf = Extrinsic::ECS::Components::Transform;
    namespace CameraKind = Extrinsic::Core::Config;
    using NullWindow = Plat::Backends::Null::NullWindow;

    // Runs one scripted step per frame at the variable tick, i.e. after that
    // frame's event poll and before its UI; a step returning false runs again
    // next frame. Exits when the script is done.
    class GizmoScriptApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::vector<std::function<bool()>> Steps{};

        void Frame(double, double) override
        {
            if (m_Next >= Steps.size())
                Kernel().RequestExit();
            else if (Steps[m_Next]())
                ++m_Next;
        }

    private:
        std::size_t m_Next{0u};
    };

    // The production shell over the Sandbox's editor/camera/document/config
    // modules, driven by real Null-window events. Events a step queues reach
    // the next frame's UI; `Then` observes that UI's result one step later.
    struct GizmoFixture
    {
        GizmoScriptApplication* Script{};
        std::unique_ptr<Intrinsic::Tests::RuntimeTestKernel> Engine{};
        Editor::EditorShell Shell{};
        Runtime::SceneInteractionModule* Interaction{};
        Runtime::EditorUiHost* Host{};
        Runtime::EditorCommandHistory* History{};
        Runtime::EngineConfigControl* Config{};
        Runtime::GizmoOrientation Orientation{Runtime::GizmoOrientation::Global};
        Runtime::GizmoPivotMode Pivot{Runtime::GizmoPivotMode::WorldOrigins};

        explicit GizmoFixture(
            const Core::Config::CameraControllerKind camera = Core::Config::CameraControllerKind::Orbit)
        {
            auto sections = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
            Core::Config::EngineConfig config = Runtime::CreateReferenceEngineConfig(sections);
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = true;
            config.Camera.Controller = camera;
            config.Window.Backend = Core::Config::WindowBackend::Null;
            config.Window.Width = 1280;
            config.Window.Height = 720;
            config.Render.EnablePromotedVulkanDevice = false;
            config.Render.DefaultRecipeConfigPath.clear();
            auto script = std::make_unique<GizmoScriptApplication>();
            Script = script.get();
            Engine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(std::move(config), std::move(script));
            Engine->EmplaceModule<Runtime::EngineConfigControl>(std::move(sections));
            Engine->EmplaceModule<Runtime::CameraModule>();
            Engine->EmplaceModule<Runtime::EditorUiModule>();
            Engine->EmplaceModule<Runtime::SceneDocumentModule>();
            Engine->EmplaceModule<Runtime::SceneInteractionModule>();
            Engine->Initialize();
            Shell.Attach(Engine->Worlds(), Engine->Services());
            Interaction = Engine->Services().Find<Runtime::SceneInteractionModule>();
            Host = Engine->Services().Find<Runtime::EditorUiHost>();
            History = Engine->Services().Find<Runtime::EditorCommandHistory>();
            Config = Engine->Services().Find<Runtime::EngineConfigControl>();
            EXPECT_TRUE(Shell.IsAttached());
        }

        ~GizmoFixture()
        {
            Shell.Detach();
            Engine->Shutdown();
        }

        GizmoFixture(const GizmoFixture&) = delete;
        GizmoFixture& operator=(const GizmoFixture&) = delete;

        [[nodiscard]] NullWindow& Window() { return static_cast<NullWindow&>(Engine->GetWindow()); }
        [[nodiscard]] Extrinsic::ECS::Scene::Registry& Scene() { return *Engine->Worlds().Get(Engine->ActiveWorld()); }
        [[nodiscard]] Tf::Component& TransformOf(const Extrinsic::ECS::EntityHandle entity)
        {
            return Scene().Raw().get<Tf::Component>(entity);
        }
        [[nodiscard]] bool Claimed() const { return Host->GetDiagnostics().CapturesViewportInput; }
        [[nodiscard]] bool Dragging() { return Interaction->Interaction().IsDragging(); }
        [[nodiscard]] glm::mat4 CameraView()
        {
            return Interaction->PrepareGizmo(Orientation, Pivot).View;
        }

        Extrinsic::ECS::EntityHandle Select(const glm::vec3 position, const bool add = false)
        {
            auto& scene = Scene();
            const Extrinsic::ECS::EntityHandle entity = Extrinsic::ECS::Scene::CreateDefault(scene, "Gizmo target");
            TransformOf(entity).Position = position;
            scene.Raw().emplace_or_replace<Extrinsic::ECS::Components::Selection::SelectableTag>(entity);
            auto& selection = *Engine->Services().Find<Runtime::SelectionController>();
            if (!add)
            {
                EXPECT_TRUE(selection.SetSelectedEntity(scene, entity));
                return entity;
            }
            selection.RequestClickPick(0u, 0u, Runtime::SelectionPickMode::Add);
            (void)selection.ConsumePendingPick();
            selection.ConsumeHit(scene, Runtime::SelectionController::ToStableEntityId(entity));
            return entity;
        }

        // Where ImGuizmo draws a world point: the frame's camera mapped into
        // its scene rectangle (the Null display origin is 0,0). No Y flip:
        // NDC is Y-up, ImGui Y-down.
        [[nodiscard]] static glm::vec2 ToScreen(const Runtime::GizmoUiFrame& frame, const glm::vec3 point)
        {
            const glm::vec4 clip = frame.Projection * frame.View * glm::vec4{point, 1.0f};
            const glm::vec2 ndc = glm::vec2{clip} / clip.w;
            return {frame.SceneRect.X + (ndc.x * 0.5f + 0.5f) * frame.SceneRect.Width,
                    frame.SceneRect.Y + (0.5f - ndc.y * 0.5f) * frame.SceneRect.Height};
        }
        [[nodiscard]] glm::vec2 Screen(const glm::vec3 point)
        {
            return ToScreen(Interaction->PrepareGizmo(Orientation, Pivot), point);
        }
        [[nodiscard]] glm::vec2 PivotScreen()
        {
            const Runtime::GizmoUiFrame frame = Interaction->PrepareGizmo(Orientation, Pivot);
            return ToScreen(frame, frame.Frame.Pivot);
        }

        void Do(std::function<void()> step)
        {
            Script->Steps.push_back([step = std::move(step)] { step(); return true; });
        }
        void Wait(const int frames = 1)
        {
            for (int i = 0; i < frames; ++i)
                Do([] {});
        }
        void Then(std::function<void()> check)
        {
            Wait();
            Do(std::move(check));
        }
        void MoveTo(std::function<glm::vec2()> where)
        {
            Do([this, where = std::move(where)]
            {
                const glm::vec2 p = where();
                Window().QueueCursor(p.x, p.y);
            });
        }
        void MoveTo(const glm::vec2 p) { MoveTo([p] { return p; }); }
        void Mouse(const bool down) { Do([this, down] { Window().QueueMouseButton(0, down); }); }
        void Key(const int key, const bool down) { Do([this, key, down] { Window().QueueKey(key, down); }); }
        void Tap(const int key)
        {
            Key(key, true);
            Key(key, false);
        }
        // Press at `from`, move in `steps` frames to `to`, release.
        void Drag(std::function<glm::vec2()> from, std::function<glm::vec2(glm::vec2)> to, const int steps = 6)
        {
            auto start = std::make_shared<glm::vec2>();
            MoveTo([start, from = std::move(from)] { return *start = from(); });
            Mouse(true);
            for (int i = 1; i <= steps; ++i)
            {
                MoveTo([start, to, i, steps]
                       { return *start + (to(*start) - *start) * (static_cast<float>(i) / steps); });
            }
            Mouse(false);
        }

        // A TestSupport::ImGuiCursorProbe over the Null window, one step per
        // frame; `found` ends as its `Found`.
        void Probe(std::function<bool()> hit, std::function<glm::vec2(int)> at,
                   const std::shared_ptr<glm::vec2>& found, const int count)
        {
            auto probe = std::make_shared<TestSupport::ImGuiCursorProbe>();
            Script->Steps.push_back([this, hit, at, found, probe, count]
            {
                const bool done =
                    probe->Step(hit, at, count, [this](const glm::vec2 p) { Window().QueueCursor(p.x, p.y); });
                *found = probe->Found;
                return done;
            });
        }
        // Clicks item `label` of ImGui window `window` on its actual rectangle,
        // found by scanning along `line`.
        void ClickItem(const char* window, const char* label, const bool menuBar,
                       std::function<glm::vec2(const ImGuiWindow&, int)> line, const int count)
        {
            auto found = std::make_shared<glm::vec2>(0.0f);
            Probe([window, label, menuBar] { return TestSupport::ImGuiItemHoveredPreviousFrame(window, label, menuBar); },
                  [window, line](const int k) { return TestSupport::ImGuiWindowScan(window, line, k); }, found, count);
            Do([found, label] { EXPECT_NE(*found, glm::vec2{0.0f}) << label << " not found"; });
            Mouse(true);
            Mouse(false);
        }
        void OpenGizmoMenu()
        {
            Wait(); // the menu bar needs one drawn frame
            ClickItem("##MainMenuBar", "Gizmo", true, TestSupport::MenuBarScan, 200);
            Wait();
        }
        void ClickMenuItem(const char* label)
        {
            ClickItem("###Menu_00", label, false, TestSupport::WindowColumnScan, 150);
        }
        void EnableGizmo()
        {
            OpenGizmoMenu();
            ClickMenuItem("Enabled");
            Wait();
        }
        // An ImGui panel over the scene; returns its center.
        glm::vec2 AddPanel()
        {
            EXPECT_TRUE(Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
                .Id = "test.gizmo_panel", .MenuPath = {"View"}, .Title = "Gizmo panel",
                .Draw = [](bool& open, const Editor::SandboxEditorContext&)
                {
                    ImGui::SetNextWindowPos(ImVec2{900.0f, 480.0f});
                    ImGui::SetNextWindowSize(ImVec2{300.0f, 160.0f});
                    if (ImGui::Begin("Gizmo panel", &open, ImGuiWindowFlags_NoSavedSettings))
                        ImGui::TextUnformatted("panel");
                    ImGui::End();
                }}).IsValid());
            EXPECT_TRUE(Shell.SetEditorWindowOpen("test.gizmo_panel", true));
            return {1050.0f, 560.0f};
        }
        // A point on ImGuizmo's handle for frame axis `axis` at `fraction` of
        // its length (0.1 clip units of the camera-right vector), on the side
        // it draws (the clearly longer projection, else the positive one).
        [[nodiscard]] glm::vec2 AxisHandle(const int axis, const float fraction = 0.6f)
        {
            const Runtime::GizmoUiFrame frame = Interaction->PrepareGizmo(Orientation, Pivot);
            const glm::vec3 pivot = frame.Frame.Pivot;
            const glm::vec3 right = glm::vec3{glm::inverse(frame.View)[0]};
            const glm::mat4 viewProjection = frame.Projection * frame.View;
            const auto ndc = [&](const glm::vec3 p)
            {
                const glm::vec4 clip = viewProjection * glm::vec4{p, 1.0f};
                return glm::vec2{clip} / clip.w;
            };
            glm::vec2 d = ndc(pivot + right) - ndc(pivot);
            d.y /= frame.SceneRect.Width / frame.SceneRect.Height;
            const glm::vec3 direction = frame.Frame.Basis[axis] * (0.1f * fraction / glm::length(d));
            const glm::vec2 center = ToScreen(frame, pivot);
            const glm::vec2 plus = ToScreen(frame, pivot + direction);
            const glm::vec2 minus = ToScreen(frame, pivot - direction);
            return glm::length(minus - center) > glm::length(plus - center) + 0.5f ? minus : plus;
        }
        // A screen-space drag from the pivot (translate: camera-plane move,
        // scale: uniform; rotate has no center handle).
        void CenterDrag(const glm::vec2 offset)
        {
            Drag([this] { return PivotScreen(); }, [offset](const glm::vec2 p) { return p + offset; });
        }
        // Finds a rotation ring along the screen diagonal from the pivot (the
        // edge-on rings of a top-down view lie on the screen axes): the first
        // point outwards whose hover the gizmo claims.
        void FindRing(const std::shared_ptr<glm::vec2>& found)
        {
            Probe([this] { return Claimed(); },
                  [this](const int k)
                  { return PivotScreen() + glm::vec2{20.0f + 2.0f * k} / std::sqrt(2.0f); },
                  found, 64);
        }
        // Moves the held cursor along the circle through `ring` around `center`
        // from `fromDegrees` to `toDegrees`.
        void Sweep(const std::shared_ptr<glm::vec2>& ring, const std::shared_ptr<glm::vec2>& center,
                   const float fromDegrees, const float toDegrees)
        {
            constexpr int kSteps = 6;
            for (int i = 1; i <= kSteps; ++i)
            {
                MoveTo([ring, center, fromDegrees, toDegrees, i]
                {
                    const float t = glm::radians(fromDegrees + (toDegrees - fromDegrees) * i / kSteps);
                    const glm::vec2 v = *ring - *center;
                    return *center + glm::vec2{v.x * std::cos(t) - v.y * std::sin(t),
                                               v.x * std::sin(t) + v.y * std::cos(t)};
                });
            }
        }
    };

    [[nodiscard]] bool GizmoStatusShown()
    {
        const ImGuiWindow* window = ImGui::FindWindowByName("##GizmoStatus");
        return window != nullptr && window->WasActive;
    }

    // Rotation angle in degrees of a unit quaternion.
    [[nodiscard]] float AngleDegrees(const glm::quat q)
    {
        return glm::degrees(2.0f * std::acos(std::min(1.0f, std::abs(q.w))));
    }
}

TEST(SandboxEditorGizmo, OffByDefaultEnabledOnlyFromTheMenuAndInactiveWhileHidden)
{
    GizmoFixture f;
    const auto entity = f.Select(glm::vec3{0.0f});
    const auto drawsGizmo = []
    {
        // Steps run after this frame's NewFrame: the last frame's activity.
        const ImGuiWindow* window = ImGui::FindWindowByName("gizmo");
        return window != nullptr && window->WasActive && !window->DrawList->VtxBuffer.empty();
    };
    // Off: hovering, W and a drag over the pivot do nothing gizmo-related.
    f.MoveTo([&f] { return f.PivotScreen(); });
    f.Tap(Plat::Input::Key::W);
    f.Then([&] { EXPECT_FALSE(f.Claimed()); EXPECT_FALSE(drawsGizmo()); });
    f.Drag([&f] { return f.PivotScreen(); }, [](glm::vec2 p) { return p + glm::vec2{40.0f, 0.0f}; });
    f.Then([&]
    {
        EXPECT_FALSE(f.Dragging());
        EXPECT_EQ(f.TransformOf(entity).Position, glm::vec3{0.0f});
        EXPECT_EQ(f.History->UndoCount(), 0u);
    });

    f.EnableGizmo();
    f.MoveTo([&f] { return f.PivotScreen(); });
    f.Then([&] { EXPECT_TRUE(f.Claimed()); EXPECT_TRUE(drawsGizmo()); });

    // Hidden: neither drawn nor claimed nor draggable; the choice survives.
    f.Do([&] { (void)f.Shell.ApplyEditorUiVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Hide}); });
    f.Then([&] { EXPECT_FALSE(f.Claimed()); EXPECT_FALSE(drawsGizmo()); });
    f.Drag([&f] { return f.PivotScreen(); }, [](glm::vec2 p) { return p + glm::vec2{40.0f, 0.0f}; });
    f.Then([&]
    {
        EXPECT_FALSE(f.Dragging());
        EXPECT_EQ(f.TransformOf(entity).Position, glm::vec3{0.0f});
    });
    f.Do([&] { (void)f.Shell.ApplyEditorUiVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Show}); });
    f.MoveTo([&f] { return f.PivotScreen(); });
    f.Then([&] { EXPECT_TRUE(f.Claimed()); EXPECT_TRUE(drawsGizmo()); });
    f.Engine->Run();
    EXPECT_EQ(f.History->UndoCount(), 0u);
}

TEST(SandboxEditorGizmo, GroupDragClaimsBlocksCameraAndPickAndCommitsOneUndo)
{
    GizmoFixture f;
    const auto first = f.Select(glm::vec3{-1.0f, 0.0f, 0.0f});
    const auto second = f.Select(glm::vec3{1.0f, 0.5f, 0.0f}, true);
    auto& selection = *f.Engine->Services().Find<Runtime::SelectionController>();
    f.EnableGizmo();
    glm::mat4 view{};
    glm::vec2 start{};
    glm::vec2 target{};
    std::uint64_t picks = 0u;
    // Each step queues one input and records the state; a record shows the
    // frame that processed the input queued two steps earlier.
    struct Seen
    {
        bool Claimed{};
        bool Dragging{};
        std::size_t Undo{};
    };
    std::vector<Seen> seen{};
    const auto step = [&](std::function<void()> input)
    {
        f.Do([&f, &seen, input = std::move(input)]
        {
            input();
            seen.push_back({f.Claimed(), f.Dragging(), f.History->UndoCount()});
        });
    };
    f.Do([&]
    {
        view = f.CameraView();
        start = f.PivotScreen();
        target = start + glm::vec2{60.0f, -30.0f};
        picks = selection.GetDiagnostics().ClickRequestsSubmitted;
    });
    step([&] { f.Window().QueueCursor(start.x, start.y); });
    step([&] { f.Window().QueueMouseButton(0, true); });                  // record 1
    constexpr int kMoves = 10;
    for (int i = 1; i <= kMoves; ++i)
    {
        step([&, i]
        {
            const glm::vec2 p = start + (target - start) * (static_cast<float>(i) / kMoves);
            f.Window().QueueCursor(p.x, p.y);
        });
    }
    step([&] { f.Window().QueueMouseButton(0, false); });                 // record 12
    step([&] { f.Window().QueueCursor(60.0f, 650.0f); });                 // away from the gizmo
    step([] {});
    step([] {});
    step([] {});
    f.Engine->Run();

    ASSERT_EQ(seen.size(), 17u);
    // Press through the last drag frame: dragging, claimed, nothing recorded.
    for (std::size_t i = 3u; i <= 13u; ++i)
    {
        EXPECT_TRUE(seen[i].Dragging) << i;
        EXPECT_TRUE(seen[i].Claimed) << i;
        EXPECT_EQ(seen[i].Undo, 0u) << i;
    }
    // The release frame claims and commits exactly once; off the gizmo the
    // claim ends (after ImGuizmo's one-frame hover capture request).
    EXPECT_FALSE(seen[14].Dragging);
    EXPECT_TRUE(seen[14].Claimed);
    EXPECT_EQ(seen[14].Undo, 1u);
    EXPECT_FALSE(seen[16].Claimed);
    EXPECT_EQ(seen[16].Undo, 1u);
    EXPECT_EQ(f.CameraView(), view) << "a claimed drag must not move the camera";
    EXPECT_EQ(selection.GetDiagnostics().ClickRequestsSubmitted, picks);
    ASSERT_EQ(f.History->UndoCount(), 1u);
    const glm::vec3 delta = f.TransformOf(first).Position - glm::vec3{-1.0f, 0.0f, 0.0f};
    EXPECT_GT(glm::length(delta), 0.01f);
    EXPECT_EQ(f.TransformOf(second).Position - glm::vec3(1.0f, 0.5f, 0.0f), delta);
    // The pivot followed the cursor: no Y flip, no rectangle or scale error.
    const glm::vec2 pivot = f.PivotScreen();
    EXPECT_NEAR(pivot.x, target.x, 1.0f);
    EXPECT_NEAR(pivot.y, target.y, 1.0f);

    ASSERT_TRUE(f.History->Undo().Succeeded());
    EXPECT_EQ(f.TransformOf(first).Position, glm::vec3(-1.0f, 0.0f, 0.0f));
    EXPECT_EQ(f.TransformOf(second).Position, glm::vec3(1.0f, 0.5f, 0.0f));
}
TEST(SandboxEditorGizmo, NoOpAndEscapeLeaveNoUndoAndEscapeBlocksARestartUntilRelease)
{
    GizmoFixture f;
    const auto entity = f.Select(glm::vec3{0.0f});
    f.EnableGizmo();
    // Press and release without motion: a no-op drag.
    f.MoveTo([&f] { return f.PivotScreen(); });
    f.Mouse(true);
    f.Then([&] { EXPECT_TRUE(f.Dragging()); });
    f.Mouse(false);
    f.Then([&] { EXPECT_FALSE(f.Dragging()); EXPECT_EQ(f.History->UndoCount(), 0u); });

    // Escape discards a moved drag; the still-held mouse cannot start another.
    auto start = std::make_shared<glm::vec2>();
    f.MoveTo([&f, start] { return *start = f.PivotScreen(); });
    f.Mouse(true);
    f.MoveTo([start] { return *start + glm::vec2{50.0f, 0.0f}; });
    f.Then([&] { EXPECT_TRUE(f.Dragging()); EXPECT_NE(f.TransformOf(entity).Position, glm::vec3{0.0f}); });
    f.Key(Plat::Input::Key::Escape, true);
    f.Then([&]
    {
        EXPECT_FALSE(f.Dragging());
        EXPECT_EQ(f.TransformOf(entity).Position, glm::vec3{0.0f});
        EXPECT_TRUE(f.Claimed()) << "the cancel frame keeps the camera off";
    });
    f.Key(Plat::Input::Key::Escape, false);
    f.MoveTo([start] { return *start; });
    f.MoveTo([start] { return *start + glm::vec2{30.0f, 0.0f}; });
    f.Then([&]
    {
        EXPECT_FALSE(f.Dragging()) << "no restart while the mouse is held";
        EXPECT_EQ(f.TransformOf(entity).Position, glm::vec3{0.0f});
        EXPECT_TRUE(f.Claimed());
    });
    f.Mouse(false);
    f.Then([&] { EXPECT_FALSE(f.Dragging()); EXPECT_EQ(f.History->UndoCount(), 0u); });
    // After the release the gizmo works again.
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_EQ(f.History->UndoCount(), 1u); });
    f.Engine->Run();
    EXPECT_NE(f.TransformOf(entity).Position, glm::vec3{0.0f});
}

namespace
{
    // A drag whose session ends elsewhere is never committed by the later
    // release, and the held mouse does not restart it (UI-078 restart lock).
    void ExpectLifecycleCancelNeverCommits(const std::function<void(GizmoFixture&)>& end,
                                           const std::function<void(GizmoFixture&)>& resume)
    {
        GizmoFixture f;
        Extrinsic::ECS::Scene::Registry* const registry = &f.Scene();
        const auto entity = f.Select(glm::vec3{0.0f});
        f.EnableGizmo();
        auto start = std::make_shared<glm::vec2>();
        f.MoveTo([&f, start] { return *start = f.PivotScreen(); });
        f.Mouse(true);
        f.MoveTo([start] { return *start + glm::vec2{50.0f, 0.0f}; });
        f.Then([&] { EXPECT_TRUE(f.Dragging()); });
        f.Do([&] { end(f); });
        f.Then([&] { EXPECT_FALSE(f.Dragging()); });
        f.Do([&] { resume(f); });
        f.MoveTo([start] { return *start; });
        f.MoveTo([start] { return *start + glm::vec2{80.0f, 0.0f}; });
        f.Then([&] { EXPECT_FALSE(f.Dragging()) << "no restart while the mouse is held"; });
        f.Mouse(false);
        f.Wait(2);
        f.Engine->Run();
        EXPECT_FALSE(f.Dragging());
        EXPECT_EQ(f.History->UndoCount(), 0u);
        if (registry->IsValid(entity))
            EXPECT_EQ(registry->Raw().get<Tf::Component>(entity).Position, glm::vec3{0.0f});
    }
}

TEST(SandboxEditorGizmo, HideFocusLossWorldAndDocumentChangesPreventTheReleaseCommit)
{
    const auto nothing = [](GizmoFixture&) {};
    {
        SCOPED_TRACE("hide, shown again while held");
        ExpectLifecycleCancelNeverCommits(
            [](GizmoFixture& f) { (void)f.Shell.ApplyEditorUiVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Hide}); },
            [](GizmoFixture& f) { (void)f.Shell.ApplyEditorUiVisibilityCommand({Runtime::EditorUiVisibilityCommandKind::Show}); });
    }
    {
        SCOPED_TRACE("focus loss and regain");
        ExpectLifecycleCancelNeverCommits(
            [](GizmoFixture& f) { f.Window().QueueEvent(Plat::WindowFocusEvent{.Focused = false}); },
            [](GizmoFixture& f) { f.Window().QueueEvent(Plat::WindowFocusEvent{.Focused = true}); });
    }
    {
        SCOPED_TRACE("world switch");
        ExpectLifecycleCancelNeverCommits(
            [](GizmoFixture& f)
            {
                const Runtime::WorldHandle other = f.Engine->Worlds().CreateWorld("Gizmo other world");
                ASSERT_TRUE(f.Engine->Worlds().RequestSetActiveWorld(other).has_value());
            },
            nothing);
    }
    {
        SCOPED_TRACE("document replacement");
        ExpectLifecycleCancelNeverCommits(
            [](GizmoFixture& f)
            { ASSERT_TRUE(f.Engine->Services().Find<Runtime::SceneDocumentModule>()->NewSceneDocument().has_value()); },
            nothing);
    }
}

TEST(SandboxEditorGizmo, WerSwitchModesOnlyWhenEnabledAndNotWhileTypingOrCaptured)
{
    GizmoFixture f;
    const auto entity = f.Select(glm::vec3{0.0f});
    char text[16] = {};
    bool captureKeyboard = false;
    const Runtime::EditorUiFrameContributionHandle capture = f.Host->RegisterFrameContribution([&]
    {
        if (captureKeyboard)
            ImGui::SetNextFrameWantCaptureKeyboard(true);
    });
    ASSERT_TRUE(f.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.gizmo_text", .MenuPath = {"View"}, .Title = "Gizmo text probe",
        .Draw = [&text](bool& open, const Editor::SandboxEditorContext&)
        {
            ImGui::SetNextWindowPos(ImVec2{900.0f, 500.0f});
            ImGui::SetNextWindowSize(ImVec2{300.0f, 120.0f});
            if (ImGui::Begin("Gizmo text probe", &open, ImGuiWindowFlags_NoSavedSettings))
                ImGui::InputText("##text", text, sizeof(text));
            ImGui::End();
        }}).IsValid());
    ASSERT_TRUE(f.Shell.SetEditorWindowOpen("test.gizmo_text", true));
    const auto moved = [&] { return f.TransformOf(entity).Position != glm::vec3{0.0f}; };
    const auto scaled = [&] { return f.TransformOf(entity).Scale != glm::vec3{1.0f}; };
    const auto undoAll = [&] { while (f.History->UndoCount() > 0u) (void)f.History->Undo(); };

    // Disabled: R switches nothing (the later default drag still translates).
    f.Tap(Plat::Input::Key::R);
    f.Then([&] { EXPECT_FALSE(f.Claimed()); });
    f.EnableGizmo();
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_TRUE(moved()); EXPECT_FALSE(scaled()); undoAll(); });

    // Enabled: R selects scale and the consumed key claims the viewport.
    f.Key(Plat::Input::Key::R, true);
    f.Then([&] { EXPECT_TRUE(f.Claimed()); });
    f.Key(Plat::Input::Key::R, false);
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_FALSE(moved()); EXPECT_TRUE(scaled()); undoAll(); });

    // E selects rotate (no center handle), then W translate; a held W never
    // reaches the camera.
    f.Tap(Plat::Input::Key::E);
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_FALSE(moved()); EXPECT_FALSE(scaled()); EXPECT_EQ(f.History->UndoCount(), 0u); });
    glm::mat4 view{};
    f.MoveTo(glm::vec2{60.0f, 400.0f});
    f.Do([&] { view = f.CameraView(); });
    f.Key(Plat::Input::Key::W, true);
    f.Wait(8);
    f.Key(Plat::Input::Key::W, false);
    f.Then([&] { EXPECT_EQ(f.CameraView(), view) << "W leaked into the camera"; });
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_TRUE(moved()); undoAll(); });

    // Typing an R into a text field does not switch the mode.
    f.MoveTo([] {
        const ImGuiWindow* window = ImGui::FindWindowByName("Gizmo text probe");
        return window != nullptr ? glm::vec2{window->DC.CursorStartPos.x + 20.0f,
                                             window->DC.CursorStartPos.y + ImGui::GetFrameHeight() * 0.5f}
                                 : glm::vec2{};
    });
    f.Mouse(true);
    f.Mouse(false);
    f.Wait();
    f.Key(Plat::Input::Key::R, true);
    f.Do([&] { f.Window().QueueEvent(Plat::CharEvent{.Character = 'r'}); });
    f.Key(Plat::Input::Key::R, false);
    f.Then([&] { EXPECT_STREQ(text, "r"); });
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_TRUE(moved()); EXPECT_FALSE(scaled()); undoAll(); });

    // Nor does an R while ImGui captures the keyboard.
    f.Do([&] { captureKeyboard = true; });
    f.Wait();
    f.Tap(Plat::Input::Key::R);
    f.Then([&] { captureKeyboard = false; });
    f.Wait();
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&] { EXPECT_TRUE(moved()); EXPECT_FALSE(scaled()); });
    f.Engine->Run();
    (void)f.Host->UnregisterFrameContribution(capture);
}

TEST(SandboxEditorGizmo, PivotAndLocalAxesFromTheMenuIncludingTheGroupFallback)
{
    GizmoFixture f;
    // Bounds centered at local (0.5,0,0), rotated 90 degrees about Z: world (0,0.5,0).
    const auto entity = f.Select(glm::vec3{0.0f});
    f.TransformOf(entity).Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    f.Scene().Raw().emplace_or_replace<Extrinsic::ECS::Components::Culling::Local::Bounds>(
        entity, Extrinsic::ECS::Components::Culling::Local::Bounds{
                    .LocalBoundingAABB = {.Min = {0.0f, -0.5f, -0.5f}, .Max = {1.0f, 0.5f, 0.5f}}});
    f.EnableGizmo();

    // Bounds-center pivot: the gizmo sits at (0,0.5,0), not at the origin.
    f.OpenGizmoMenu();
    f.ClickMenuItem("Pivot at bounds centers");
    f.Do([&] { f.Pivot = Runtime::GizmoPivotMode::BoundsCenters; });
    f.Wait();
    f.CenterDrag({40.0f, 0.0f});
    f.Then([&]
    {
        ASSERT_EQ(f.History->UndoCount(), 1u);
        EXPECT_NE(f.TransformOf(entity).Position, glm::vec3{0.0f});
        (void)f.History->Undo();
    });

    // Local axes: the X handle is the entity's local X, i.e. world Y.
    f.OpenGizmoMenu();
    f.ClickMenuItem("Local axes");
    f.Do([&] { f.Orientation = Runtime::GizmoOrientation::Local; f.Pivot = Runtime::GizmoPivotMode::WorldOrigins; });
    f.OpenGizmoMenu();
    f.ClickMenuItem("Pivot at bounds centers");
    f.Wait();
    auto handle = std::make_shared<glm::vec2>();
    f.Do([&f, handle] { *handle = f.AxisHandle(0); });
    f.Drag([handle] { return *handle; }, [](const glm::vec2 p) { return p + glm::vec2{25.0f, 25.0f}; });
    f.Then([&]
    {
        ASSERT_EQ(f.History->UndoCount(), 1u);
        const glm::vec3 position = f.TransformOf(entity).Position;
        EXPECT_GT(std::abs(position.y), 0.01f);
        EXPECT_NEAR(position.x, 0.0f, 1.0e-4f);
        EXPECT_NEAR(position.z, 0.0f, 1.0e-4f);
    });

    // A group whose rotations average to nothing falls back to world axes and says so.
    f.Do([&] {
        const auto other = f.Select(glm::vec3{2.0f, 0.0f, 0.0f}, true);
        f.TransformOf(other).Rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3{1.0f, 0.0f, 0.0f});
        f.TransformOf(entity).Rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    });
    f.Then([&]
    {
        EXPECT_NE(f.Interaction->PrepareGizmo(f.Orientation, f.Pivot).Frame.BasisFallback,
                  Runtime::GizmoBasisFallback::None);
        EXPECT_TRUE(GizmoStatusShown());
    });
    f.Engine->Run();
}

TEST(SandboxEditorGizmo, SnapStepsPreviewValidateApplyAndShiftSnapsEachMode)
{
    GizmoFixture f{Core::Config::CameraControllerKind::TopDown};
    const auto entity = f.Select(glm::vec3{0.0f});
    const auto active = [&] { return Runtime::GetGizmoSnapConfig(f.Config->GetEngineConfigControlState().ActiveConfig); };
    const auto type = [&f](const std::string& value)
    {
        for (const char c : value)
            f.Do([&f, c] { f.Window().QueueEvent(Plat::CharEvent{.Character = static_cast<unsigned int>(c)}); });
    };
    f.EnableGizmo();

    // An invalid draft shows its reason and cannot be applied.
    f.OpenGizmoMenu();
    f.ClickMenuItem("Translate step");
    f.Wait();
    type("0");
    f.ClickMenuItem("Apply snap steps");
    f.Then([&]
    {
        ASSERT_TRUE(active().has_value());
        EXPECT_EQ(active()->TranslateStep, 0.25f);
        EXPECT_FALSE(Runtime::PreviewGizmoSnapConfig(*f.Config, {.TranslateStep = 0.0f}).empty());
    });
    // A valid draft is applied through the config lane and round-trips.
    f.ClickMenuItem("Translate step");
    f.Wait();
    type("0.5");
    f.ClickMenuItem("Rotate step (degrees)");
    f.Wait();
    type("30");
    f.ClickMenuItem("Scale step");
    f.Wait();
    type("0.5");
    f.Then([&] { EXPECT_EQ(active()->TranslateStep, 0.25f) << "editing a draft applies nothing"; });
    f.ClickMenuItem("Apply snap steps");
    f.Then([&]
    {
        ASSERT_TRUE(active().has_value());
        EXPECT_EQ(*active(), (Runtime::GizmoSnapConfig{.TranslateStep = 0.5f, .RotateStepDegrees = 30.0f,
                                                       .ScaleStep = 0.5f}));
        const auto roundTrip = f.Config->PreviewEngineConfigControlDocument(
            Core::Config::SerializeEngineConfig(f.Config->GetEngineConfigControlState().ActiveConfig));
        EXPECT_EQ(Runtime::GetGizmoSnapConfig(roundTrip.Preview.Config), active());
    });
    f.MoveTo(glm::vec2{60.0f, 400.0f});
    f.Mouse(true); // closes the menu
    f.Mouse(false);
    f.Wait();

    const auto multipleOf = [](const float value, const float step)
    { return std::abs(value / step - std::round(value / step)) < 1.0e-3f; };
    // Translate with Shift: every world component is a multiple of 0.5.
    f.Key(Plat::Input::Key::LeftShift, true);
    f.CenterDrag({37.0f, -23.0f});
    f.Key(Plat::Input::Key::LeftShift, false);
    f.Then([&]
    {
        const glm::vec3 p = f.TransformOf(entity).Position;
        EXPECT_GT(glm::length(p), 0.1f);
        for (int i = 0; i < 3; ++i)
            EXPECT_TRUE(multipleOf(p[i], 0.5f)) << p[i];
        while (f.History->UndoCount() > 0u) (void)f.History->Undo();
    });
    // Scale with Shift: the uniform factor is a multiple of 0.5.
    f.Tap(Plat::Input::Key::R);
    f.Key(Plat::Input::Key::LeftShift, true);
    f.CenterDrag({37.0f, 0.0f});
    f.Key(Plat::Input::Key::LeftShift, false);
    f.Then([&]
    {
        const glm::vec3 scale = f.TransformOf(entity).Scale;
        EXPECT_NE(scale, glm::vec3{1.0f});
        for (int i = 0; i < 3; ++i)
            EXPECT_TRUE(multipleOf(scale[i], 0.5f)) << scale[i];
        while (f.History->UndoCount() > 0u) (void)f.History->Undo();
    });
    // Rotate with Shift: the angle is a multiple of 30 degrees.
    f.Tap(Plat::Input::Key::E);
    auto ring = std::make_shared<glm::vec2>(0.0f);
    auto center = std::make_shared<glm::vec2>();
    f.Do([&f, center] { *center = f.PivotScreen(); });
    f.FindRing(ring);
    f.Then([ring] { ASSERT_NE(*ring, glm::vec2{0.0f}) << "no rotation ring found"; });
    f.Key(Plat::Input::Key::LeftShift, true);
    f.Mouse(true);
    f.Sweep(ring, center, 0.0f, 50.0f);
    f.Mouse(false);
    f.Key(Plat::Input::Key::LeftShift, false);
    f.Then([&]
    {
        ASSERT_EQ(f.History->UndoCount(), 1u);
        const float angle = AngleDegrees(f.TransformOf(entity).Rotation);
        EXPECT_GT(angle, 1.0f);
        EXPECT_TRUE(multipleOf(angle, 30.0f)) << angle;
    });
    f.Engine->Run();
}

TEST(SandboxEditorGizmo, RejectedPreviewShowsReasonWritesNothingAndReleaseCommitsLastAccepted)
{
    GizmoFixture f;
    // Scaling world X skews the entity turned 45 degrees about Y: the whole
    // group is rejected, though the unrotated one alone would be valid.
    const auto plain = f.Select(glm::vec3{-1.0f, 0.0f, 0.0f});
    const auto turned = f.Select(glm::vec3{1.0f, 0.0f, 0.0f}, true);
    f.TransformOf(turned).Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const Tf::Component plainBefore = f.TransformOf(plain);
    const Tf::Component turnedBefore = f.TransformOf(turned);
    const auto unchanged = [&]
    {
        for (const auto& [entity, before] : {std::pair{plain, plainBefore}, std::pair{turned, turnedBefore}})
        {
            const Tf::Component& now = f.TransformOf(entity);
            for (int i = 0; i < 3; ++i)
            {
                EXPECT_NEAR(now.Position[i], before.Position[i], 1.0e-5f);
                EXPECT_NEAR(now.Scale[i], before.Scale[i], 1.0e-5f);
            }
        }
    };
    f.EnableGizmo();
    f.Tap(Plat::Input::Key::R);
    auto start = std::make_shared<glm::vec2>();
    f.MoveTo([&f, start] { return *start = f.AxisHandle(0); });
    f.Mouse(true);
    f.MoveTo([start] { return *start + glm::vec2{30.0f, 0.0f}; });
    f.Then([&]
    {
        EXPECT_TRUE(f.Dragging());
        EXPECT_TRUE(GizmoStatusShown()) << "the rejection reason is shown";
        unchanged(); // no partial write
    });
    // Back at the start pixel the candidate is valid again.
    f.MoveTo([start] { return *start; });
    f.Then([&] { EXPECT_TRUE(f.Dragging()); EXPECT_FALSE(GizmoStatusShown()); unchanged(); });
    // Release on a rejected tick commits the last accepted (here: start) state.
    f.MoveTo([start] { return *start + glm::vec2{40.0f, 0.0f}; });
    f.Then([&] { EXPECT_TRUE(GizmoStatusShown()); });
    f.Mouse(false);
    f.Then([&]
    {
        EXPECT_FALSE(f.Dragging());
        EXPECT_EQ(f.History->UndoCount(), 0u) << "the last accepted state was the start";
        unchanged();
        EXPECT_TRUE(GizmoStatusShown()) << "the error stays visible after the release";
    });
    f.Engine->Run();
}

TEST(SandboxEditorGizmo, OffsetSplitSceneRectInPerspectiveAndOrthographicViews)
{
    for (const auto camera : {Core::Config::CameraControllerKind::Orbit, Core::Config::CameraControllerKind::TopDown})
    {
        SCOPED_TRACE(static_cast<int>(camera));
        GizmoFixture f{camera};
        const auto entity = f.Select(glm::vec3{0.5f, 0.0f, 0.25f});
        ASSERT_TRUE(f.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
            .Id = "test.gizmo_split", .MenuPath = {"View"}, .Title = "Gizmo split",
            .Draw = [](bool&, const Editor::SandboxEditorContext& context)
            { context.ClaimSceneViewport(420.0f, 80.0f, 640.0f, 480.0f); }}).IsValid());
        ASSERT_TRUE(f.Shell.SetEditorWindowOpen("test.gizmo_split", true));
        f.EnableGizmo();
        glm::vec2 target{};
        f.Do([&]
        {
            const Runtime::GizmoUiFrame frame = f.Interaction->PrepareGizmo(f.Orientation, f.Pivot);
            EXPECT_EQ(frame.SceneRect.X, 420.0f);
            EXPECT_EQ(frame.SceneRect.Width, 640.0f);
            EXPECT_EQ(frame.Orthographic, camera == Core::Config::CameraControllerKind::TopDown);
            target = f.PivotScreen() + glm::vec2{-45.0f, 35.0f};
        });
        f.Drag([&f] { return f.PivotScreen(); }, [&](glm::vec2) { return target; });
        f.Then([&]
        {
            ASSERT_EQ(f.History->UndoCount(), 1u);
            const glm::vec2 pivot = f.Screen(f.TransformOf(entity).Position);
            EXPECT_NEAR(pivot.x, target.x, 1.0f);
            EXPECT_NEAR(pivot.y, target.y, 1.0f);
        });
        f.Engine->Run();
    }
}

TEST(SandboxEditorGizmo, ScaleAndRotateReleasedOverAPanelCommitOnce)
{
    // ImGuizmo 1.10 skips its scale/rotate release while another window is
    // hovered. ImGui reports that window during a drag when the press was
    // ImGui-owned, e.g. the click that also closes an open menu.
    for (const int key : {Plat::Input::Key::R, Plat::Input::Key::E})
    {
        SCOPED_TRACE(key);
        GizmoFixture f{Core::Config::CameraControllerKind::TopDown};
        const auto entity = f.Select(glm::vec3{0.0f});
        const glm::vec2 panel = f.AddPanel();
        f.EnableGizmo();
        f.Tap(key);
        auto start = std::make_shared<glm::vec2>(0.0f);
        if (key == Plat::Input::Key::E)
            f.FindRing(start);
        else
            f.Do([&f, start] { *start = f.PivotScreen(); });
        f.OpenGizmoMenu();
        f.MoveTo([start] { return *start; });
        f.Mouse(true);
        for (int i = 1; i <= 6; ++i)
            f.MoveTo([start, panel, i] { return *start + (panel - *start) * (i / 6.0f); });
        f.Then([&] { EXPECT_TRUE(f.Dragging()); });
        f.Mouse(false);
        Tf::Component released{};
        f.Then([&]
        {
            EXPECT_FALSE(f.Dragging()) << "the release over the panel ends the drag";
            EXPECT_EQ(f.History->UndoCount(), 1u);
            released = f.TransformOf(entity);
        });
        // Moving back over the gizmo without a button transforms nothing.
        f.MoveTo([start] { return *start; });
        f.MoveTo([start] { return *start + glm::vec2{-40.0f, 30.0f}; });
        f.Then([&]
        {
            EXPECT_FALSE(f.Dragging());
            EXPECT_EQ(f.History->UndoCount(), 1u);
            EXPECT_EQ(f.TransformOf(entity).Rotation, released.Rotation);
            EXPECT_EQ(f.TransformOf(entity).Scale, released.Scale);
        });
        f.Engine->Run();
        EXPECT_TRUE(released.Scale != glm::vec3(1.0f) || released.Rotation != glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
            << "the drag transformed before reaching the panel";
    }
}

TEST(SandboxEditorGizmo, OverlappingModeKeysKeepTheCameraOffUntilTheLastRelease)
{
    GizmoFixture f;
    (void)f.Select(glm::vec3{0.0f});
    f.EnableGizmo();
    glm::mat4 view{};
    f.MoveTo(glm::vec2{60.0f, 650.0f});
    f.Do([&] { view = f.CameraView(); });
    f.Key(Plat::Input::Key::W, true);
    f.Wait();
    f.Key(Plat::Input::Key::E, true);
    f.Key(Plat::Input::Key::E, false);
    f.Wait(6); // W still held
    f.Then([&] { EXPECT_EQ(f.CameraView(), view) << "the held W leaked into the camera"; });
    f.Key(Plat::Input::Key::W, false);
    f.Engine->Run();
}

TEST(SandboxEditorGizmo, ShiftSnappedRotationUnderANonUniformParentIsAccepted)
{
    GizmoFixture f{Core::Config::CameraControllerKind::TopDown};
    // Rotating about the view (Y) axis under a parent scaled (1,1,2) is TRS
    // only at multiples of 90 degrees; ImGuizmo's own snap drifts off them.
    const auto parent = f.Select(glm::vec3{0.0f});
    f.TransformOf(parent).Scale = {1.0f, 1.0f, 2.0f};
    const auto child = f.Select(glm::vec3{0.0f});
    f.Scene().Raw().emplace_or_replace<Extrinsic::ECS::Components::Hierarchy::Component>(
        child, Extrinsic::ECS::Components::Hierarchy::Component{.Parent = parent});
    const Tf::Component original = f.TransformOf(child);
    f.EnableGizmo();
    f.Tap(Plat::Input::Key::E);
    auto ring = std::make_shared<glm::vec2>(0.0f);
    auto center = std::make_shared<glm::vec2>();
    f.Do([&f, center] { *center = f.PivotScreen(); });
    f.FindRing(ring);
    f.Key(Plat::Input::Key::LeftShift, true);
    f.Mouse(true);
    f.Sweep(ring, center, 0.0f, 88.0f);
    f.Then([&]
    {
        EXPECT_FALSE(GizmoStatusShown()) << "the snapped 90 degrees are accepted";
        EXPECT_NEAR(AngleDegrees(f.TransformOf(child).Rotation), 90.0f, 0.01f);
    });
    f.Sweep(ring, center, 88.0f, 3.0f);
    f.Then([&]
    {
        EXPECT_FALSE(GizmoStatusShown());
        EXPECT_EQ(f.TransformOf(child).Rotation, original.Rotation) << "back at the exact start";
    });
    f.Sweep(ring, center, 3.0f, 88.0f);
    f.Mouse(false);
    f.Key(Plat::Input::Key::LeftShift, false);
    f.Then([&]
    {
        EXPECT_FALSE(f.Dragging());
        ASSERT_EQ(f.History->UndoCount(), 1u);
        EXPECT_NEAR(AngleDegrees(f.TransformOf(child).Rotation), 90.0f, 0.01f);
    });
    f.Engine->Run();
}

namespace
{
    // Drags the scale handle of world axis `axis` from 60% to 90% of its
    // length. Two unrotated entities symmetric to the pivot scale by 1.5 on
    // that axis only and move with it about the pivot; the drag is claimed,
    // leaves camera and pick alone, and its release records one undo.
    void ExpectAxisScaleDragAndUndo(const Core::Config::CameraControllerKind camera, const int axis)
    {
        SCOPED_TRACE(axis);
        GizmoFixture f{camera};
        glm::vec3 offset{0.0f};
        offset[axis] = 0.5f;
        offset[(axis + 1) % 3] = 0.25f;
        const auto plus = f.Select(offset);
        const auto minus = f.Select(-offset, true);
        auto& selection = *f.Engine->Services().Find<Runtime::SelectionController>();
        f.EnableGizmo();
        f.Tap(Plat::Input::Key::R);
        glm::mat4 view{};
        std::uint64_t picks = 0u;
        auto start = std::make_shared<glm::vec2>();
        auto end = std::make_shared<glm::vec2>();
        f.Do([&f, &view, &picks, &selection, start, end, axis]
        {
            view = f.CameraView();
            picks = selection.GetDiagnostics().ClickRequestsSubmitted;
            *start = f.AxisHandle(axis);
            *end = f.AxisHandle(axis, 0.9f);
            // A hand is never pixel-exact: stay 4 px beside the handle line.
            const glm::vec2 side = glm::normalize(glm::vec2{*start - *end}) * 4.0f;
            *start += glm::vec2{-side.y, side.x};
            *end += glm::vec2{-side.y, side.x};
        });
        f.MoveTo([start] { return *start; });
        f.Mouse(true);
        for (int i = 1; i <= 6; ++i)
            f.MoveTo([start, end, i] { return *start + (*end - *start) * (i / 6.0f); });
        f.Then([&]
        {
            EXPECT_TRUE(f.Dragging());
            EXPECT_TRUE(f.Claimed());
            EXPECT_EQ(f.History->UndoCount(), 0u);
        });
        f.Mouse(false);
        f.Then([&] { EXPECT_FALSE(f.Dragging()); });
        f.Engine->Run();

        EXPECT_EQ(f.CameraView(), view) << "a claimed drag must not move the camera";
        EXPECT_EQ(selection.GetDiagnostics().ClickRequestsSubmitted, picks);
        ASSERT_EQ(f.History->UndoCount(), 1u);
        for (const auto& [entity, sign] : {std::pair{plus, 1.0f}, std::pair{minus, -1.0f}})
        {
            const Tf::Component& now = f.TransformOf(entity);
            for (int i = 0; i < 3; ++i)
            {
                ASSERT_TRUE(std::isfinite(now.Position[i]) && std::isfinite(now.Scale[i])) << i;
                const float factor = i == axis ? now.Scale[axis] : 1.0f;
                EXPECT_NEAR(now.Scale[i], i == axis ? 1.5f : 1.0f, 0.03f) << i;
                EXPECT_NEAR(now.Position[i], sign * offset[i] * factor, 1.0e-4f) << i;
            }
        }
        ASSERT_TRUE(f.History->Undo().Succeeded());
        EXPECT_EQ(f.TransformOf(plus).Position, offset);
        EXPECT_EQ(f.TransformOf(minus).Position, -offset);
        EXPECT_EQ(f.TransformOf(plus).Scale, glm::vec3{1.0f});
        EXPECT_EQ(f.TransformOf(minus).Scale, glm::vec3{1.0f});
    }
}

TEST(SandboxEditorGizmo, TopDownScaleAxesDragAndUndo)
{
    for (const int axis : {0, 2})
        ExpectAxisScaleDragAndUndo(Core::Config::CameraControllerKind::TopDown, axis);
}

TEST(SandboxEditorGizmo, FrontalScaleAxesDragAndUndo)
{
    // The default orbit camera looks along -Z at the origin: the eye lies in
    // the pivot's X and Y axis planes.
    for (const int axis : {0, 1})
        ExpectAxisScaleDragAndUndo(Core::Config::CameraControllerKind::Orbit, axis);
}
