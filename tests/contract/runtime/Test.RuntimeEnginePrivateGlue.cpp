#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::filesystem::path RepoRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        EXPECT_TRUE(in.good()) << "Unable to open: " << path.string();
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    bool ContainsModuleDirective(const std::string& content)
    {
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line))
        {
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos)
                continue;

            const std::string_view trimmed(line.data() + first, line.size() - first);
            if (trimmed.starts_with("module;") ||
                trimmed.starts_with("module ") ||
                trimmed.starts_with("export module ") ||
                trimmed.starts_with("import ") ||
                trimmed.starts_with("export import "))
                return true;
        }
        return false;
    }
}

TEST(RuntimeEnginePrivateGlue, FrameLoopHelpersArePrivateTextualGlue)
{
    const auto root = RepoRoot();
    const auto engineImpl = ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto privateHeader = ReadFile(
        root / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");
    constexpr std::string_view includeDirective =
        "#include \"Runtime.Engine.FrameLoop.Internal.hpp\"";

    std::vector<std::filesystem::path> includeOwners;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root / "src/runtime"))
    {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        if (extension != ".cpp" && extension != ".cppm" &&
            extension != ".hpp" && extension != ".h")
            continue;
        if (ReadFile(entry.path()).find(includeDirective) != std::string::npos)
            includeOwners.push_back(entry.path());
    }

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.Engine.FrameLoop.cppm"));
    ASSERT_EQ(includeOwners.size(), 1u);
    EXPECT_EQ(includeOwners.front().filename(), "Runtime.Engine.cpp");
    EXPECT_NE(engineImpl.find(includeDirective), std::string::npos);
    EXPECT_EQ(engineImpl.find("import :FrameLoop"), std::string::npos);
    EXPECT_FALSE(ContainsModuleDirective(privateHeader));
    EXPECT_EQ(runtimeCMake.find("Runtime.Engine.FrameLoop.cppm"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("Extrinsic.Runtime.Engine:FrameLoop"),
              std::string::npos);
}

TEST(RuntimeEnginePrivateGlue,
     TypedViewportInputDispatchKeepsCameraOutOfGenericFrameContext)
{
    const auto root = RepoRoot();
    const std::string engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const std::string privateHeader = ReadFile(
        root / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const std::string moduleInterface =
        ReadFile(root / "src/runtime/Runtime.Module.cppm");
    const std::string interactionImpl = ReadFile(
        root /
        "src/runtime/Scene/Runtime.SceneInteractionModule.cpp");
    const std::string cameraImpl = ReadFile(
        root /
        "src/runtime/Cameras/Runtime.CameraModule.cpp");
    const std::string scheduleImpl = ReadFile(
        root / "src/runtime/Runtime.ModuleSchedule.cpp");

    EXPECT_EQ(engineImpl.find("PopulateMainCameraForFrame"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find("PopulateMainCameraForFrame"),
              std::string::npos);

    const auto uiEndCapture =
        engineImpl.find("FramePhase::UiEndCapture");
    const auto renderInputInitialization = engineImpl.find(
        "frameContext.RenderInput = Graphics::RenderFrameInput{");
    const auto viewportDispatch = engineImpl.find(
        "m_RuntimeModuleSchedule.RunViewportInputHooks(");
    const auto transformFlush = engineImpl.find(
        "FlushPreRenderTransformState(*m_Scene)");
    const auto inputActions = engineImpl.find(
        "m_InputActions.DispatchForFrame(");
    const auto beforeExtraction = engineImpl.find(
        "FramePhase::BeforeExtraction");
    ASSERT_NE(uiEndCapture, std::string::npos);
    ASSERT_NE(renderInputInitialization, std::string::npos);
    ASSERT_NE(viewportDispatch, std::string::npos);
    ASSERT_NE(transformFlush, std::string::npos);
    ASSERT_NE(inputActions, std::string::npos);
    ASSERT_NE(beforeExtraction, std::string::npos);
    EXPECT_LT(uiEndCapture, renderInputInitialization);
    EXPECT_LT(renderInputInitialization, viewportDispatch);
    EXPECT_LT(viewportDispatch, transformFlush);
    EXPECT_LT(transformFlush, inputActions);
    EXPECT_LT(viewportDispatch, inputActions);
    EXPECT_LT(inputActions, beforeExtraction);
    EXPECT_EQ(
        engineImpl.find(
            "m_GizmoFrameService.DriveInputForFrame("),
        std::string::npos);
    EXPECT_EQ(
        engineImpl.find(
            "m_SelectionReadback.DrainPendingPickForFrame("),
        std::string::npos);

    const auto interactionViewport =
        interactionImpl.find("void RunViewportInput(");
    const auto gizmoInput = interactionImpl.find(
        "Gizmo.DriveInputForFrame(", interactionViewport);
    const auto interactionExtraction =
        interactionImpl.find("void RunBeforeExtraction(");
    const auto picking = interactionImpl.find(
        "Readback.DrainPendingPickForFrame(",
        interactionExtraction);
    const auto gizmoPackets = interactionImpl.find(
        "Gizmo.BuildRenderPackets(", interactionExtraction);
    ASSERT_NE(interactionViewport, std::string::npos);
    ASSERT_NE(gizmoInput, std::string::npos);
    ASSERT_NE(interactionExtraction, std::string::npos);
    ASSERT_NE(picking, std::string::npos);
    ASSERT_NE(gizmoPackets, std::string::npos);
    EXPECT_LT(interactionViewport, gizmoInput);
    EXPECT_LT(gizmoInput, interactionExtraction);
    EXPECT_LT(picking, gizmoPackets);
    EXPECT_NE(
        interactionImpl.find(
            ".CapturesViewportInput()"),
        std::string::npos);

    EXPECT_NE(
        cameraImpl.find("return \"Runtime.CameraModule\";"),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "return \"Runtime.SceneInteractionModule\";"),
        std::string::npos);
    EXPECT_NE(
        scheduleImpl.find(
            "return lhs.ModuleName < rhs.ModuleName;"),
        std::string::npos);

    std::size_t registrarWiringCount = 0u;
    std::size_t cursor = 0u;
    constexpr std::string_view registrarCall =
        "m_RuntimeModuleSchedule.RegisterViewportInputHook(";
    while ((cursor = engineImpl.find(registrarCall, cursor)) !=
           std::string::npos)
    {
        ++registrarWiringCount;
        cursor += registrarCall.size();
    }
    EXPECT_EQ(registrarWiringCount, 2u);

    const auto frameContextBegin = moduleInterface.find(
        "export struct RuntimeFrameHookContext");
    const auto frameContextEnd = moduleInterface.find(
        "};", frameContextBegin);
    ASSERT_NE(frameContextBegin, std::string::npos);
    ASSERT_NE(frameContextEnd, std::string::npos);
    const std::string frameContext = moduleInterface.substr(
        frameContextBegin,
        frameContextEnd - frameContextBegin);
    EXPECT_EQ(frameContext.find("Viewport"), std::string::npos);
    EXPECT_EQ(frameContext.find("RenderInput"), std::string::npos);
    EXPECT_EQ(frameContext.find("const Core::Config::EngineConfig&"),
              std::string::npos);

    EXPECT_NE(moduleInterface.find(
                  "export struct RuntimeViewportInputHookContext"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find(
                  "using RuntimeViewportInputHook ="),
              std::string::npos);
}

TEST(RuntimeEnginePrivateGlue, EditorUiModuleOwnsOptionalEditorUiComposition)
{
    const auto root = RepoRoot();
    const auto engineInterface = ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl = ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto moduleInterface = ReadFile(
        root / "src/runtime/Editor/Runtime.EditorUiModule.cppm");
    const auto moduleImpl = ReadFile(
        root / "src/runtime/Editor/Runtime.EditorUiModule.cpp");
    const auto hostInterface = ReadFile(
        root / "src/runtime/Editor/Runtime.EditorUiHost.cppm");
    const auto hostImpl = ReadFile(
        root / "src/runtime/Editor/Runtime.EditorUiHost.cpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ImGuiEditorBridge.Internal.hpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ImGuiEditorBridge.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ImGuiEditorBridge.cpp"));
    EXPECT_EQ(engineInterface.find("ImGuiEditorBridge"), std::string::npos);
    EXPECT_EQ(engineImpl.find("ImGuiEditorBridge"), std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ImGuiEditorBridge"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ImGuiAdapter"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.ImGuiAdapter"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("EditorUiHost"), std::string::npos);
    EXPECT_EQ(engineImpl.find("EditorUiHost"), std::string::npos);
    EXPECT_EQ(engineInterface.find("EditorUiModule"), std::string::npos);
    EXPECT_EQ(engineImpl.find("EditorUiModule"), std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Graphics.ImGuiOverlaySystem;"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "import Extrinsic.Graphics.ImGuiOverlaySystem;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("SetImGuiEditorCallback"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("SetImGuiEditorVisible"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetImGuiAdapter"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("SetImGuiEditorCallback"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("SetImGuiEditorVisible"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("GetImGuiAdapter"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_ImGui"), std::string::npos);

    EXPECT_NE(moduleInterface.find(
                  "export module Extrinsic.Runtime.EditorUiModule;"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find(
                  "export import Extrinsic.Runtime.EditorUiHost;"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find(
                  "class EditorUiModule final : public IRuntimeModule"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find("struct Impl;"), std::string::npos);
    EXPECT_NE(moduleInterface.find("std::unique_ptr<Impl> m_Impl"),
              std::string::npos);
    EXPECT_EQ(moduleInterface.find("ImGuiAdapter"), std::string::npos);
    EXPECT_EQ(moduleInterface.find("ImGuiOverlaySystem"), std::string::npos);

    EXPECT_NE(moduleImpl.find(
                  "import Extrinsic.Graphics.ImGuiOverlaySystem;"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("import Extrinsic.Runtime.ImGuiAdapter;"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("struct EditorUiModule::Impl"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("Graphics::ImGuiOverlaySystem Overlay{}"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("EditorUiHost Host{}"), std::string::npos);
    EXPECT_NE(moduleImpl.find("EditorUiHostOwnerControl HostOwner"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("Host.ClaimOwnerControl()"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("std::unique_ptr<ImGuiAdapter> Adapter{}"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "setup.Services().Provide<EditorUiHost>("),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "setup.Services().Require<Platform::IWindow>(Name())"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "setup.Services().Require<Graphics::IRenderer>(Name())"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "setup.Services().Require<RuntimeInputActionRegistry>(Name())"),
              std::string::npos);
    EXPECT_EQ(moduleImpl.find("Require<ECS::"), std::string::npos);
    EXPECT_EQ(moduleImpl.find("Require<Assets::"), std::string::npos);

    EXPECT_NE(hostInterface.find(
                  "export module Extrinsic.Runtime.EditorUiHost;"),
              std::string::npos);
    EXPECT_NE(hostInterface.find("struct EditorUiDiagnostics"),
              std::string::npos);
    EXPECT_NE(hostInterface.find("RegisterFrameContribution("),
              std::string::npos);
    EXPECT_NE(hostInterface.find("class EditorUiHostOwnerControl final"),
              std::string::npos);
    EXPECT_NE(hostInterface.find("ClaimOwnerControl()"),
              std::string::npos);
    EXPECT_NE(hostInterface.find("RegisterWindow("), std::string::npos);
    EXPECT_NE(hostInterface.find("ApplyVisibilityCommand("),
              std::string::npos);
    EXPECT_NE(hostInterface.find("struct Impl;"), std::string::npos);
    EXPECT_NE(hostInterface.find("std::unique_ptr<Impl> m_Impl"),
              std::string::npos);
    EXPECT_EQ(hostInterface.find("Engine&"), std::string::npos);
    EXPECT_EQ(hostInterface.find("import Extrinsic.Runtime.Engine"),
              std::string::npos);
    EXPECT_EQ(hostInterface.find("ImGuiAdapter"), std::string::npos);
    EXPECT_EQ(hostInterface.find("EditorInputCaptureSnapshot"),
              std::string::npos);
    EXPECT_EQ(hostImpl.find("Engine&"), std::string::npos);

    EXPECT_NE(engineImpl.find(
                  "m_ServiceRegistry.Provide<Platform::IWindow>("),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_ServiceRegistry.Provide<Graphics::IRenderer>("),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_ServiceRegistry.Provide<RuntimeInputActionRegistry>("),
              std::string::npos);

    EXPECT_NE(moduleImpl.find("FramePhase::UiBegin"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("FramePhase::UiBuild"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("FramePhase::UiEndCapture"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "m_Impl->Adapter->BeginFrame(context.FrameDeltaSeconds)"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("m_Impl->Adapter->BuildEditorFrame()"),
              std::string::npos);
    const auto uiBegin = engineImpl.find("FramePhase::UiBegin");
    const auto variableTick = engineImpl.find(
        "m_Application->OnVariableTick(*this, alpha, frameDt);");
    const auto uiBuild = engineImpl.find("FramePhase::UiBuild");
    const auto uiEndCapture =
        engineImpl.find("FramePhase::UiEndCapture");
    const auto completedCapture = engineImpl.find(
        "const EditorInputCaptureSnapshot& editorCapture =");
    ASSERT_NE(uiBegin, std::string::npos);
    ASSERT_NE(variableTick, std::string::npos);
    ASSERT_NE(uiBuild, std::string::npos);
    ASSERT_NE(uiEndCapture, std::string::npos);
    ASSERT_NE(completedCapture, std::string::npos);
    EXPECT_LT(uiBegin, variableTick);
    EXPECT_LT(variableTick, uiBuild);
    EXPECT_LT(uiBuild, uiEndCapture);
    EXPECT_LT(uiEndCapture, completedCapture);
    const auto adapterEndFrame =
        moduleImpl.find("m_Impl->Adapter->EndFrame();");
    const auto captureWrite = moduleImpl.find(
        "context.EditorCapture = m_Impl->Adapter->CaptureSnapshot();");
    ASSERT_NE(adapterEndFrame, std::string::npos);
    ASSERT_NE(captureWrite, std::string::npos);
    EXPECT_LT(adapterEndFrame, captureWrite);

    EXPECT_NE(moduleImpl.find(
                  ".KeyCode = Platform::Input::Key::G"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  ".SuppressWhenImGuiCapturesKeyboard = false"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "m_Impl->InputActions->Unregister("),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "m_Impl->Renderer->SetImGuiOverlaySystem(nullptr)"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "services->Withdraw<EditorUiHost>(m_Impl->Host)"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("m_Impl.reset();"), std::string::npos);

    EXPECT_NE(runtimeCMake.find("Editor/Runtime.EditorUiModule.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Editor/Runtime.EditorUiModule.cpp"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("Extrinsic.Runtime.ImGuiEditorBridge"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find("Extrinsic.Runtime.EditorUiModule"),
              std::string::npos);
}

TEST(RuntimeEnginePrivateGlue, RenderExtractionServiceIsEnginePrivateImplementation)
{
    const auto root = RepoRoot();
    const auto engineInterface = ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl = ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto privateHeader = ReadFile(
        root / "src/runtime/Runtime.RenderExtractionService.Internal.hpp");
    const auto serviceImpl = ReadFile(
        root / "src/runtime/Runtime.RenderExtractionService.cpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");
    constexpr std::string_view includeDirective =
        "#include \"Runtime.RenderExtractionService.Internal.hpp\"";

    std::vector<std::filesystem::path> includeOwners;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root / "src/runtime"))
    {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        if (extension != ".cpp" && extension != ".cppm" &&
            extension != ".hpp" && extension != ".h")
            continue;
        if (ReadFile(entry.path()).find(includeDirective) != std::string::npos)
            includeOwners.push_back(entry.path());
    }

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.RenderExtractionService.cppm"));
    ASSERT_EQ(includeOwners.size(), 1u);
    EXPECT_EQ(includeOwners.front().filename(), "Runtime.Engine.cppm");
    EXPECT_NE(engineInterface.find(includeDirective), std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Runtime.RenderExtractionService"),
              std::string::npos);
    EXPECT_FALSE(ContainsModuleDirective(privateHeader));
    EXPECT_NE(privateHeader.find("class RenderExtractionService"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("RenderExtractionCache m_Cache"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("std::unique_ptr<RenderWorldPool> m_Pool"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("RuntimeRenderExtractionStats m_LastStats"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("std::uint64_t m_FrameIndex"),
              std::string::npos);
    EXPECT_NE(engineInterface.find(
                  "RenderExtractionService               m_RenderExtractionService"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "RenderExtractionCache                 m_RenderExtraction"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "std::unique_ptr<RenderWorldPool>      m_RenderWorldPool"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "RuntimeRenderExtractionStats          m_LastExtractionStats"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "std::uint64_t                         m_FrameIndex"),
              std::string::npos);

    EXPECT_NE(engineInterface.find(
                  "import Extrinsic.Runtime.RenderExtraction;"),
              std::string::npos);
    EXPECT_NE(engineInterface.find(
                  "import Extrinsic.Runtime.RenderWorldPool;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("FindSurfaceGpuGeometry"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetMaterialTextureAssetBindings"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("Engine::FindSurfaceGpuGeometry"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("Engine::GetMaterialTextureAssetBindings"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find(
                  "GetMaterialTextureAssetBindingsForTest"),
              std::string::npos);
    EXPECT_EQ(serviceImpl.find(
                  "GetMaterialTextureAssetBindingsForTest"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "export import Extrinsic.Runtime.RenderExtraction;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "export import Extrinsic.Runtime.RenderWorldPool;"),
              std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(
        root / "src/runtime/Runtime.RenderExtraction.cppm"));
    EXPECT_TRUE(std::filesystem::exists(
        root / "src/runtime/Runtime.RenderWorldPool.cppm"));

    EXPECT_NE(serviceImpl.find("module Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_EQ(serviceImpl.find(
                  "module Extrinsic.Runtime.RenderExtractionService;"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.ConfigurePool("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.Cache()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.Pool()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.ConsumeFrameIndex()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.PublishLastStats("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.ReleaseFrontSlot("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderExtraction.Shutdown("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "m_RenderExtraction.GetMaterialTextureAssetBindings("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "m_RenderExtraction.SetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "m_RenderExtraction.ClearVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "m_RenderExtraction.GetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderWorldPool->ReleaseFront("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_LastExtractionStats ="),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_FrameIndex++"), std::string::npos);
    EXPECT_NE(serviceImpl.find("m_Cache.Shutdown(renderer)"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_Cache.SetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("RenderWorldPool::kDefaultBuffers"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_Pool->ReleaseFront(slot)"),
              std::string::npos);

    EXPECT_EQ(runtimeCMake.find("Runtime.RenderExtractionService.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.RenderExtraction.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.RenderWorldPool.cppm"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find(
                  "Extrinsic.Runtime.RenderExtractionService"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find("Extrinsic.Runtime.RenderExtraction`"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find("Extrinsic.Runtime.RenderWorldPool`"),
              std::string::npos);
}

TEST(RuntimeEnginePrivateGlue, AssetWorkflowCompositionIsModuleOwned)
{
    const auto root = RepoRoot();
    const auto engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop = ReadFile(
        root / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto workflowInterface = ReadFile(
        root / "src/runtime/Runtime.AssetWorkflowModule.cppm");
    const auto workflowImpl = ReadFile(
        root / "src/runtime/Runtime.AssetWorkflowModule.cpp");
    const auto runtimeCMake =
        ReadFile(root / "src/runtime/CMakeLists.txt");

    std::vector<std::filesystem::path> workflowModuleUnits;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root / "src/runtime"))
    {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        if (extension != ".cpp" && extension != ".cppm" &&
            extension != ".hpp" && extension != ".h")
        {
            continue;
        }
        if (ReadFile(entry.path()).find(
                "module Extrinsic.Runtime.AssetWorkflowModule;") !=
            std::string::npos)
        {
            workflowModuleUnits.push_back(entry.path());
        }
    }

    ASSERT_EQ(workflowModuleUnits.size(), 2u);
    bool foundInterface = false;
    bool foundImplementation = false;
    for (const auto& owner : workflowModuleUnits)
    {
        foundInterface |= owner.filename() ==
                          "Runtime.AssetWorkflowModule.cppm";
        foundImplementation |= owner.filename() ==
                               "Runtime.AssetWorkflowModule.cpp";
    }
    EXPECT_TRUE(foundInterface);
    EXPECT_TRUE(foundImplementation);
    EXPECT_FALSE(std::filesystem::exists(
        root /
        "src/runtime/Runtime.AssetResidencyService.Internal.hpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.AssetResidencyService.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.AssetResidencyService.cpp"));

    const std::vector<std::string> removedEngineTokens = {
        "AssetResidencyService",
        std::string{"GetAsset"} + "Service(",
        std::string{"GetGpuAsset"} + "Cache(",
        std::string{"GetAssetImport"} + "Pipeline(",
        std::string{"GetObjectSpaceNormalBakeQueue"} +
            "DiagnosticsForTest(",
        std::string{"GetPendingObjectSpaceNormalBake"} +
            "CountForTest(",
        "m_AssetService",
        "m_AssetImportPipeline",
        "m_AssetResidencyService",
        "m_ObjectSpaceNormalBakeService",
        "BindActiveSceneAssetHandoffs",
        "RegisterSceneReplacementParticipants",
    };
    for (const auto& token : removedEngineTokens)
    {
        EXPECT_EQ(engineInterface.find(token), std::string::npos)
            << token;
        EXPECT_EQ(engineImpl.find(token), std::string::npos)
            << token;
    }

    EXPECT_NE(workflowInterface.find(
                  "export module Extrinsic.Runtime.AssetWorkflowModule;"),
              std::string::npos);
    EXPECT_NE(workflowInterface.find(
                  "public Core::IAssetFrameHooks"),
              std::string::npos);
    constexpr std::string_view ownedCompositionTokens[] = {
        "std::unique_ptr<Assets::AssetService> Assets{}",
        "std::unique_ptr<Graphics::GpuAssetCache> Cache{}",
        "AssetImportPipeline Pipeline{}",
        "ObjectSpaceNormalBakeService Bake{}",
        "Provide<Assets::AssetService>",
        "Provide<AssetImportPipeline>",
        "Provide<Graphics::GpuAssetCache>",
        "Provide<Core::IAssetFrameHooks>",
        "std::make_unique<AssetModelTextureHandoff>",
        "std::make_unique<AssetModelSceneHandoff>",
        "InitializeRuntimeGpuAssetFallbackTexture(",
    };
    for (const auto token : ownedCompositionTokens)
    {
        EXPECT_NE(workflowImpl.find(token), std::string::npos)
            << token;
        EXPECT_EQ(engineImpl.find(token), std::string::npos)
            << token;
    }

    EXPECT_NE(workflowImpl.find(
                  "setup.Services().Find<StreamingExecutor>()"),
              std::string::npos);
    EXPECT_NE(workflowImpl.find(
                  "setup.Services().Find<SelectionController>()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_ServiceRegistry.Find<Graphics::GpuAssetCache>()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "Core::IAssetFrameHooks>()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_ServiceRegistry.Find<AssetImportPipeline>()"),
              std::string::npos);
    EXPECT_NE(frameLoop.find("if (AssetWorkflow != nullptr)"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("pipeline != nullptr"),
              std::string::npos);

    EXPECT_NE(runtimeCMake.find("Runtime.AssetWorkflowModule.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.AssetWorkflowModule.cpp"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.AssetResidencyService"),
              std::string::npos);
}

TEST(RuntimeEnginePrivateGlue, KMeansGpuJobQueueIsSandboxFacadePrivateImplementation)
{
    const auto root = RepoRoot();
    const auto facadeInterface = ReadFile(
        root / "src/runtime/Runtime.SandboxEditorFacades.cppm");
    const auto methodImpl = ReadFile(
        root / "src/runtime/Runtime.SandboxMethodFacade.cpp");
    const auto privateHeader = ReadFile(
        root / "src/runtime/Runtime.KMeansGpuJobQueue.Internal.hpp");
    const auto queueImpl = ReadFile(
        root / "src/runtime/Runtime.KMeansGpuJobQueue.cpp");
    const auto engineInterface = ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl = ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");
    constexpr std::string_view includeDirective =
        "#include \"Runtime.KMeansGpuJobQueue.Internal.hpp\"";

    std::vector<std::filesystem::path> includeOwners;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root / "src/runtime"))
    {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        if (extension != ".cpp" && extension != ".cppm" &&
            extension != ".hpp" && extension != ".h")
            continue;
        if (ReadFile(entry.path()).find(includeDirective) != std::string::npos)
            includeOwners.push_back(entry.path());
    }

    const std::string oldImport =
        std::string{"import Extrinsic.Runtime."} + "KMeansGpuJobQueue;";
    std::vector<std::filesystem::path> oldImportOwners;
    for (const auto& sourceRoot : {root / "src", root / "tests"})
    {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(sourceRoot))
        {
            if (!entry.is_regular_file())
                continue;
            const auto extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".cppm" &&
                extension != ".hpp" && extension != ".h")
                continue;
            if (ReadFile(entry.path()).find(oldImport) != std::string::npos)
                oldImportOwners.push_back(entry.path());
        }
    }

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.KMeansGpuJobQueue.cppm"));
    ASSERT_EQ(includeOwners.size(), 1u);
    EXPECT_EQ(includeOwners.front().filename(),
              "Runtime.SandboxEditorFacades.cppm");
    EXPECT_NE(facadeInterface.find(includeDirective), std::string::npos);
    EXPECT_FALSE(ContainsModuleDirective(privateHeader));
    EXPECT_EQ(privateHeader.find("export "), std::string::npos);
    EXPECT_NE(privateHeader.find("class RuntimeKMeansGpuJobQueue"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find("enum class RuntimeKMeansGpuJobStatus"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find("struct RuntimeKMeansGpuJobRequest"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find("struct RuntimeKMeansGpuJobSubmission"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find("struct RuntimeKMeansGpuJobResult"),
              std::string::npos);
    EXPECT_TRUE(oldImportOwners.empty());

    const auto publicStatus = facadeInterface.find(
        "enum class RuntimeKMeansGpuJobStatus");
    const auto publicRequest = facadeInterface.find(
        "struct RuntimeKMeansGpuJobRequest");
    const auto publicSubmission = facadeInterface.find(
        "struct RuntimeKMeansGpuJobSubmission");
    const auto publicResult = facadeInterface.find(
        "struct RuntimeKMeansGpuJobResult");
    const auto privateInclude = facadeInterface.find(includeDirective);
    const auto publicSession = facadeInterface.find(
        "class SandboxEditorSession");
    ASSERT_NE(publicStatus, std::string::npos);
    ASSERT_NE(publicRequest, std::string::npos);
    ASSERT_NE(publicSubmission, std::string::npos);
    ASSERT_NE(publicResult, std::string::npos);
    ASSERT_NE(privateInclude, std::string::npos);
    ASSERT_NE(publicSession, std::string::npos);
    EXPECT_LT(publicStatus, publicRequest);
    EXPECT_LT(publicRequest, publicSubmission);
    EXPECT_LT(publicSubmission, publicResult);
    EXPECT_LT(publicResult, privateInclude);
    EXPECT_LT(privateInclude, publicSession);
    EXPECT_NE(facadeInterface.find(
                  "return Status == RuntimeKMeansGpuJobStatus::Accepted;"),
              std::string::npos);
    EXPECT_NE(facadeInterface.find(
                  "return Status == RuntimeKMeansGpuJobStatus::Completed;"),
              std::string::npos);
    constexpr std::string_view directImports[] = {
        "import Extrinsic.RHI.BufferManager;",
        "import Extrinsic.RHI.CommandContext;",
        "import Extrinsic.RHI.TransferQueue;",
        "import Extrinsic.Runtime.KMeansGpuBackend;",
        "import Geometry.KMeans;",
    };
    for (const auto directImport : directImports)
    {
        EXPECT_NE(facadeInterface.find(directImport), std::string::npos);
        EXPECT_EQ(facadeInterface.find(std::string{"export "} +
                                       std::string{directImport}),
                  std::string::npos);
    }

    EXPECT_NE(queueImpl.find("module Extrinsic.Runtime.SandboxEditorFacades;"),
              std::string::npos);
    EXPECT_EQ(queueImpl.find(
                  "module Extrinsic.Runtime.KMeansGpuJobQueue;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("KMeansGpuJobQueue"), std::string::npos);
    EXPECT_EQ(engineImpl.find("KMeansGpuJobQueue"), std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.KMeansGpuJobQueue.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.KMeansGpuJobQueue.cpp"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find(
                  "Extrinsic.Runtime.KMeansGpuJobQueue`"),
              std::string::npos);

    const auto queueMember = facadeInterface.find(
        "std::unique_ptr<RuntimeKMeansGpuJobQueue> m_KMeansGpuJobs");
    const auto participantMember = facadeInterface.find(
        "GpuQueueParticipantHandle m_KMeansGpuParticipant");
    ASSERT_NE(queueMember, std::string::npos);
    ASSERT_NE(participantMember, std::string::npos);
    EXPECT_LT(queueMember, participantMember);

    const auto detach = methodImpl.find(
        "void SandboxEditorSession::DetachKMeansGpuQueue(");
    const auto unregister = methodImpl.find(
        "UnregisterGpuQueueParticipant(", detach);
    const auto waitIdle = methodImpl.find(
        "engine->GetDevice().WaitIdle();", unregister);
    const auto clearParticipant = methodImpl.find(
        "m_KMeansGpuParticipant = {};", waitIdle);
    const auto destroyQueue = methodImpl.find(
        "m_KMeansGpuJobs.reset();", clearParticipant);
    ASSERT_NE(detach, std::string::npos);
    ASSERT_NE(unregister, std::string::npos);
    ASSERT_NE(waitIdle, std::string::npos);
    ASSERT_NE(clearParticipant, std::string::npos);
    ASSERT_NE(destroyQueue, std::string::npos);
    EXPECT_LT(unregister, waitIdle);
    EXPECT_LT(waitIdle, clearParticipant);
    EXPECT_LT(clearParticipant, destroyQueue);
}
