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

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Sandbox.Editor.Controller;
import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;

namespace Core = Extrinsic::Core;
namespace Plat = Extrinsic::Platform;
namespace Runtime = Extrinsic::Runtime;
namespace SandboxEditor = Extrinsic::Sandbox::Editor;

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

    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}
    };

    class TooltipDelayApplication final : public Runtime::IApplication
    {
    public:
        static constexpr std::uint32_t MaxFrames = 64u;

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            ++m_Frames;
            if (m_Frames >= MaxFrames)
                engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::uint32_t m_Frames{0u};
    };

    class WaitForAssetImportEventApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForAssetImportEventApplication(
            const std::chrono::milliseconds timeout = std::chrono::seconds(10))
            : m_Timeout(timeout)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            const auto now = std::chrono::steady_clock::now();
            if (!m_Started)
            {
                m_Started = true;
                m_StartedAt = now;
                m_Deadline = now + m_Timeout;
            }
            ++m_ObservedFrames;
            if (RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).GetLastAssetImportEvent().has_value())
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
        void OnShutdown(Runtime::Engine&) override {}

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

    class ToggleEditorVisibilityApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_Frames;
            if (m_Frames == 1u)
            {
                ImGui::SetNextFrameWantCaptureKeyboard(true);
                return;
            }
            const Plat::IWindow& window = engine.GetWindow();
            auto& input = const_cast<Plat::Input::Context&>(window.GetInput());
            input.SetKeyState(Plat::Input::Key::G, true);
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}

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
        const std::vector<Runtime::SandboxEditorDiagnostic>& diagnostics,
        const Runtime::SandboxEditorDiagnosticCode code)
    {
        return std::ranges::any_of(
            diagnostics,
            [code](const Runtime::SandboxEditorDiagnostic& diagnostic)
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
        SandboxEditor::EditorShell& shell,
        SandboxEditor::MethodPanels& methodPanels,
        SandboxEditor::MeshProcessingPanels& meshProcessingPanels,
        SandboxEditor::DomainPanels& domainPanels)
    {
        methodPanels.Register(shell);
        meshProcessingPanels.Register(shell);
        domainPanels.Register(shell);
    }
}

TEST(SandboxEditorPresentation, DefaultDrawStartsWithOnlyMenuBarVisible)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);

    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    SandboxEditor::MethodPanels methodPanels;
    SandboxEditor::MeshProcessingPanels meshProcessingPanels;
    SandboxEditor::DomainPanels domainPanels;
    RegisterAllAppPanels(shell, methodPanels, meshProcessingPanels, domainPanels);

    engine.Run();

    EXPECT_TRUE(ImGuiWindowExists("##MainMenuBar"));
    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), 35u);
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
    const std::array<ExpectedWindow, 25> expected{{
        {"pointcloud.appearance", {"PointCloud"}},
        {"pointcloud.properties", {"PointCloud"}},
        {"pointcloud.selection", {"PointCloud"}},
        {"pointcloud.processing.remove_outliers", {"PointCloud", "Processing"}},
        {"graph.appearance", {"Graph"}},
        {"graph.properties", {"Graph"}},
        {"graph.selection", {"Graph"}},
        {"mesh.appearance", {"Mesh"}},
        {"mesh.properties", {"Mesh"}},
        {"mesh.selection", {"Mesh"}},
        {"pointcloud.processing.kmeans", {"PointCloud", "Processing"}},
        {"graph.processing.kmeans", {"Graph", "Processing"}},
        {"mesh.processing.kmeans", {"Mesh", "Processing"}},
        {"pointcloud.processing.progressive_poisson", {"PointCloud", "Processing"}},
        {"mesh.processing.progressive_poisson", {"Mesh", "Processing"}},
        {"mesh.processing.parameterize_uv", {"Mesh", "Processing"}},
        {"mesh.processing.denoise", {"Mesh", "Processing"}},
        {"mesh.processing.curvature", {"Mesh", "Processing"}},
        {"mesh.processing.remesh", {"Mesh", "Processing"}},
        {"mesh.processing.subdivide", {"Mesh", "Processing"}},
        {"mesh.processing.simplify", {"Mesh", "Processing"}},
        {"mesh.processing.vertices.normals", {"Mesh", "Processing", "Vertices"}},
        {"graph.processing.vertices.normals", {"Graph", "Processing", "Vertices"}},
        {"pointcloud.processing.vertices.normals", {"PointCloud", "Processing", "Vertices"}},
        {"view.registration", {"View"}},
    }};

    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    SandboxEditor::MethodPanels methodPanels;
    SandboxEditor::MeshProcessingPanels meshProcessingPanels;
    SandboxEditor::DomainPanels domainPanels;
    RegisterAllAppPanels(shell, methodPanels, meshProcessingPanels, domainPanels);

    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), expected.size() + 10u);
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
          "ApplySandboxEditorPointCloudOutlierRemovalCommand",
          "ApplySandboxEditorUvRegenerationCommand",
          "context.LastUvRegenerationResult",
          "LastPointCloudOutlierRemovalResult.reset();",
          "LastUvRegenerationResult.reset();",
          "MeshPropertyPlotState.SelectedProperty.clear();",
          "ImGuiCond_FirstUseEver"})
    {
        EXPECT_NE(source.find(required), std::string::npos) << required;
    }
}

TEST(SandboxEditorPresentation, MeshProcessingPanelsPreserveLifetimeAndResultPublication)
{
    const std::string source = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp");
    ASSERT_FALSE(source.empty());

    constexpr std::array<std::string_view, 9> commands{{
        "ApplySandboxEditorMeshDenoiseCommand",
        "ApplySandboxEditorMeshCurvatureCommand",
        "ApplySandboxEditorMeshRemeshCommand",
        "ApplySandboxEditorMeshSubdivideCommand",
        "ApplySandboxEditorMeshSimplifyCommand",
        "ApplySandboxEditorMeshVertexNormalsCommand",
        "ApplySandboxEditorGraphVertexNormalsCommand",
        "ApplySandboxEditorPointCloudVertexNormalsCommand",
        "ApplySandboxEditorRegistrationCommand",
    }};
    constexpr std::array<std::string_view, 9> sinks{{
        "context.MethodResultSinks.MeshDenoise",
        "context.MethodResultSinks.MeshCurvature",
        "context.MethodResultSinks.MeshRemesh",
        "context.MethodResultSinks.MeshSubdivide",
        "context.MethodResultSinks.MeshSimplify",
        "context.MethodResultSinks.MeshVertexNormals",
        "context.MethodResultSinks.GraphVertexNormals",
        "context.MethodResultSinks.PointCloudVertexNormals",
        "context.MethodResultSinks.Registration",
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

TEST(SandboxEditorPresentation, ExtrinsicSandboxAppStaysRuntimeOnly)
{
    constexpr std::array<std::string_view, 13> paths{{
        "src/app/Sandbox/Sandbox.cppm",
        "src/app/Sandbox/Sandbox.cpp",
        "src/app/Sandbox/main.cpp",
        "src/app/Sandbox/Editor/Sandbox.EditorController.cppm",
        "src/app/Sandbox/Editor/Sandbox.EditorController.cpp",
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
              "import Extrinsic.Backends",
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
    EXPECT_EQ(controller.find("import Extrinsic.Runtime.SandboxEditorFacades;"),
              std::string::npos);
    EXPECT_NE(shell.find("import Extrinsic.Runtime.SandboxEditorFacades;"),
              std::string::npos);
    EXPECT_NE(cmake.find("target_link_libraries(ExtrinsicSandboxEditor"),
              std::string::npos);
    EXPECT_NE(cmake.find("ExtrinsicRuntime"), std::string::npos);
    EXPECT_EQ(cmake.find("ExtrinsicGraphics"), std::string::npos);
    EXPECT_EQ(cmake.find("ExtrinsicPlatform"), std::string::npos);
    EXPECT_EQ(cmake.find("ExtrinsicRHI"), std::string::npos);
}

TEST(SandboxEditorPresentation, RuntimeFacadesCompileSeparatelyFromEditorShell)
{
    const std::string appShell = ReadRepositoryTextFile(
        "src/app/Sandbox/Editor/Sandbox.EditorShell.cpp");
    const std::string runtimeFacade = ReadRepositoryTextFile(
        "src/runtime/Runtime.SandboxEditorFacades.cpp");
    const std::string methodFacade = ReadRepositoryTextFile(
        "src/runtime/Runtime.SandboxMethodFacade.cpp");
    const std::string renderRecipeFacade = ReadRepositoryTextFile(
        "src/runtime/Runtime.SandboxEditorRenderRecipeFacade.cpp");
    const std::string runtimeCMake =
        ReadRepositoryTextFile("src/runtime/CMakeLists.txt");
    ASSERT_FALSE(appShell.empty());
    ASSERT_FALSE(runtimeFacade.empty());
    ASSERT_FALSE(methodFacade.empty());
    ASSERT_FALSE(renderRecipeFacade.empty());

    constexpr std::string_view kMeansDefinition =
        "SandboxEditorKMeansResult ApplySandboxEditorKMeansCommand(";
    constexpr std::string_view poissonDefinition =
        "ApplySandboxEditorProgressivePoissonCommand(";
    constexpr std::string_view renderRecipeDefinition =
        "SandboxEditorRenderRecipeEditorModel\n"
        "    BuildSandboxEditorRenderRecipeEditorModel(";
    constexpr std::string_view renderRecipeCommand =
        "SandboxEditorRenderRecipeCommandResult\n"
        "    ApplySandboxEditorRenderRecipeCommand(";
    EXPECT_NE(appShell.find("#include <imgui.h>"), std::string::npos);
    EXPECT_NE(appShell.find("DrawMainMenuBar("), std::string::npos);
    EXPECT_EQ(runtimeFacade.find("#include <imgui.h>"), std::string::npos);
    EXPECT_EQ(runtimeFacade.find("ImGui::"), std::string::npos);
    EXPECT_EQ(runtimeFacade.find(kMeansDefinition), std::string::npos);
    EXPECT_EQ(runtimeFacade.find(renderRecipeDefinition), std::string::npos);
    EXPECT_EQ(runtimeFacade.find(renderRecipeCommand), std::string::npos);
    EXPECT_NE(methodFacade.find(kMeansDefinition), std::string::npos);
    EXPECT_NE(methodFacade.find(poissonDefinition), std::string::npos);
    EXPECT_NE(renderRecipeFacade.find(renderRecipeDefinition), std::string::npos);
    EXPECT_NE(renderRecipeFacade.find(renderRecipeCommand), std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.SandboxMethodFacade.cpp"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.SandboxEditorRenderRecipeFacade.cpp"),
              std::string::npos);
    EXPECT_NE(methodFacade.find("epoch->load"), std::string::npos);
    EXPECT_NE(methodFacade.find("HasInFlightJob()"), std::string::npos);
    EXPECT_NE(methodFacade.find("m_KMeansGpuJobs.reset()"),
              std::string::npos);
}

TEST(SandboxEditorPresentation, GeometryProcessingMenusExposeDomainElementSubmenus)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    const auto mesh = Runtime::GetSandboxEditorGeometryProcessingMenuItems(
        Runtime::SandboxEditorDomainWindowKind::Mesh);
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

    const auto graph = Runtime::GetSandboxEditorGeometryProcessingMenuItems(
        Runtime::SandboxEditorDomainWindowKind::Graph);
    ASSERT_EQ(graph.size(), 3u);
    EXPECT_EQ(graph[0].Domain, Domain::GraphVertices);
    EXPECT_STREQ(graph[0].Label, "Vertices");
    EXPECT_TRUE(graph[0].HasNormalsMethod);
    EXPECT_EQ(graph[1].Domain, Domain::GraphEdges);
    EXPECT_STREQ(graph[1].Label, "Edges");
    EXPECT_EQ(graph[2].Domain, Domain::GraphHalfedges);
    EXPECT_STREQ(graph[2].Label, "Halfedges");

    const auto cloud = Runtime::GetSandboxEditorGeometryProcessingMenuItems(
        Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_EQ(cloud.size(), 1u);
    EXPECT_EQ(cloud[0].Domain, Domain::PointCloudPoints);
    EXPECT_STREQ(cloud[0].Label, "Vertices");
    EXPECT_TRUE(cloud[0].HasNormalsMethod);
}

TEST(SandboxEditorPresentation, AdapterCallbackDrawsDeterministicMenuOnlyFrame)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    SandboxEditor::EditorShell shell;
    shell.Attach(engine);

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
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<TooltipDelayApplication>());
    ComposeEditorUiAndInitialize(engine);

    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    ASSERT_TRUE(shell.SetEditorWindowOpen("file.import", true));

    std::uint32_t observerFrames = 0u;
    bool importButtonHovered = false;
    bool tooltipVisible = false;
    ImVec2 queuedMousePosition{};
    ImVec2 observedMousePosition{};
    std::string hoveredWindowName{};
    const Runtime::EditorWindowHandle observer = shell.RegisterEditorWindow(
        SandboxEditor::EditorWindowDescriptor{
            .Id = "test.file_import_hover_observer",
            .MenuPath = {"View"},
            .Title = "File import hover observer",
            .OpenByDefault = true,
            .Draw =
                [&](bool&, const Runtime::SandboxEditorContext&)
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

    for (const std::string_view required :
         {"ImGuiHoveredFlags_ForTooltip |",
          "ImGuiHoveredFlags_AllowWhenDisabled",
          "option.DisabledReason",
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
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    ComposeEditorUiAndInitialize(engine);
    SandboxEditor::EditorShell shell;
    shell.Attach(engine);

    const auto imported = RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = "/tmp/intrinsic-arch-006-missing.ply",
            .PayloadKind =
                Runtime::SandboxEditorAssetPayloadKind::PointCloud,
        });
    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error(), Core::ErrorCode::FileNotFound);

    engine.Run();

    ASSERT_TRUE(shell.GetLastFrame().FileImport.LastResult.has_value());
    EXPECT_FALSE(shell.GetLastFrame().FileImport.LastResult->Succeeded());
    EXPECT_EQ(shell.GetLastFrame().FileImport.LastResult->PayloadKind,
              Runtime::SandboxEditorAssetPayloadKind::PointCloud);
    EXPECT_TRUE(HasDiagnostic(
        shell.GetLastFrame().FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade)
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
    Runtime::Engine engine(
        HeadlessConfig(),
        std::move(waitForImport));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    ComposeEditorUiAndInitialize(engine);
    (void)Runtime::RegisterSandboxDefaultRuntimePolicies(
        engine, nullptr);

    SandboxEditor::EditorShell shell;
    shell.Attach(engine);

    const std::vector<std::string> droppedPaths{cloudFile.Path.string()};
    RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).ImportDroppedFilePaths(droppedPaths);
    EXPECT_FALSE(RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).GetLastAssetImportEvent().has_value());
    ASSERT_FALSE(engine.GetWindow().ShouldClose());

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        RequiredEngineService<Extrinsic::Runtime::AssetImportPipeline>(engine).GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value()) << waitDiagnostics->Describe();
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind,
              Runtime::SandboxEditorAssetPayloadKind::PointCloud);
    EXPECT_EQ(lastEvent->Result->PrimitiveEntitiesCreated, 1u);

    ASSERT_TRUE(shell.GetLastFrame().FileImport.LastResult.has_value());
    EXPECT_TRUE(shell.GetLastFrame().FileImport.LastResult->Succeeded());
    EXPECT_EQ(shell.GetLastFrame().FileImport.LastResult->PayloadKind,
              Runtime::SandboxEditorAssetPayloadKind::PointCloud);
    EXPECT_FALSE(HasDiagnostic(
        shell.GetLastFrame().FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, EngineAttachmentRegistersEditorCallback)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    EXPECT_TRUE(shell.IsAttached());

    engine.Run();

    const Runtime::EditorUiHost* editorUi =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(editorUi, nullptr);
    EXPECT_GE(editorUi->GetDiagnostics().EditorCallbackInvocations, 1u);
    EXPECT_TRUE(shell.GetLastFrame().FileImport.Enabled);
    EXPECT_FALSE(HasDiagnostic(
        shell.GetLastFrame().FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, ControllerReattachPinsPanelAttachmentResetPolicy)
{
    SandboxEditor::SandboxEditorController controller;

    Runtime::Engine firstEngine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(firstEngine);
    controller.Attach(firstEngine);
    ASSERT_TRUE(controller.IsAttached());
    firstEngine.Run();
    controller.Detach();
    EXPECT_FALSE(controller.IsAttached());
    firstEngine.Shutdown();

    Runtime::Engine secondEngine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(secondEngine);
    controller.Attach(secondEngine);
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
    EXPECT_NE(methodPanels.find("KMeans.LastResult.reset();"),
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
    EXPECT_NE(meshPanels.find("MeshNormals.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("GraphNormals.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("Registration.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(meshPanels.find("PointNormals.LastResult.reset();"),
              std::string::npos);
    EXPECT_NE(domainPanels.find(
                  "LastPointCloudOutlierRemovalResult.reset();"),
              std::string::npos);
    EXPECT_NE(domainPanels.find("LastUvRegenerationResult.reset();"),
              std::string::npos);
    EXPECT_NE(domainPanels.find(
                  "MeshPropertyPlotState.SelectedProperty.clear();"),
              std::string::npos);
}

TEST(SandboxEditorPresentation, EditorShellStartsWithOnlyBuiltinWindows)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), 10u);
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
          "view.geometry_visualization"})
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
    EXPECT_NE(
        compactPanel.find(
            "ApplySandboxEditorGpuProfilingConfigCommand(*context,gpuProfilingEnabled)"),
        std::string::npos);
    EXPECT_NE(frameGraphPanel.find("Queue envelopes:"),
              std::string_view::npos);
    EXPECT_NE(frameGraphPanel.find("Pass samples:"),
              std::string_view::npos);
}

TEST(SandboxEditorPresentation, ExternalWindowContributionNeedsNoLegacySwitchEntry)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);
    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    int drawCalls = 0;
    const Runtime::EditorWindowHandle handle = shell.RegisterEditorWindow(
        SandboxEditor::EditorWindowDescriptor{
            .Id = "graph.analysis.curvature",
            .MenuPath = {"Graph", "Analysis"},
            .Title = "Curvature",
            .OpenByDefault = false,
            .Draw =
                [&drawCalls](bool&, const Runtime::SandboxEditorContext&)
                {
                    ++drawCalls;
                },
        });
    ASSERT_TRUE(handle.IsValid());

    const auto menu = shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), 11u);
    const Runtime::EditorWindowMenuEntry* contributed =
        FindWindow(menu, "graph.analysis.curvature");
    ASSERT_NE(contributed, nullptr);
    EXPECT_EQ(contributed->MenuPath,
              (std::vector<std::string>{"Graph", "Analysis"}));
    EXPECT_EQ(drawCalls, 0);

    EXPECT_TRUE(shell.UnregisterEditorWindow(handle));
    EXPECT_EQ(shell.BuildEditorWindowMenuModel().size(), 10u);
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, ContextWindowContributionReceivesRuntimeFacade)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    ComposeEditorUiAndInitialize(engine);

    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
    int drawCalls = 0;
    bool receivedScene = false;
    const Runtime::EditorWindowHandle handle = shell.RegisterEditorWindow(
        SandboxEditor::EditorWindowDescriptor{
            .Id = "test.context_window",
            .MenuPath = {"View"},
            .Title = "Context Window",
            .OpenByDefault = true,
            .Draw =
                [&drawCalls, &receivedScene](
                    bool&,
                    const Runtime::SandboxEditorContext& context)
                {
                    ++drawCalls;
                    receivedScene = context.Scene != nullptr;
                },
        });
    ASSERT_TRUE(handle.IsValid());

    engine.Run();

    EXPECT_EQ(drawCalls, 1);
    EXPECT_TRUE(receivedScene);
    EXPECT_TRUE(shell.UnregisterEditorWindow(handle));
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorPresentation, GlobalVisibilityHotkeyUsesTheVisibilityCommandPath)
{
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<ToggleEditorVisibilityApplication>());
    ComposeEditorUiAndInitialize(engine);

    SandboxEditor::EditorShell shell;
    shell.Attach(engine);
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
