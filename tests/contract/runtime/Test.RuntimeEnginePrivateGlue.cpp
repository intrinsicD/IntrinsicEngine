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

    std::string WithoutWhitespace(const std::string_view content)
    {
        std::string compact;
        compact.reserve(content.size());
        for (const char value : content)
        {
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                compact.push_back(value);
        }
        return compact;
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

    EXPECT_EQ(engineImpl.find("PopulateMainCameraForFrame"),
              std::string::npos);
    EXPECT_EQ(privateHeader.find("PopulateMainCameraForFrame"),
              std::string::npos);

    const auto uiEndCapture =
        engineImpl.find("FramePhase::UiEndCapture");
    const auto renderInputInitialization = engineImpl.find(
        "frameContext.RenderInput = Graphics::RenderFrameInput{");
    const auto viewportDispatch = engineImpl.find(
        "for (const Impl::ViewportInputHookRecord& record");
    const auto transformFlush = engineImpl.find(
        "ECS::Systems::TransformHierarchy::OnUpdate(");
    const auto inputActions = engineImpl.find(
        "m_Impl->m_InputActions.DispatchForFrame(");
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
        "DriveGizmoInteractionForFrame(", interactionViewport);
    const auto interactionExtraction =
        interactionImpl.find("void RunBeforeExtraction(");
    const auto picking = interactionImpl.find(
        "Selection.ConsumePendingPick()",
        interactionExtraction);
    const auto gizmoPackets = interactionImpl.find(
        "GizmoPacketBuilder.Build(", interactionExtraction);
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
        engineImpl.find(
            "return lhs.ModuleName < rhs.ModuleName;"),
        std::string::npos);

    std::size_t registrarWiringCount = 0u;
    std::size_t cursor = 0u;
    constexpr std::string_view registrarCall =
        "m_Impl->m_ViewportInputHooks.push_back(";
    while ((cursor = engineImpl.find(registrarCall, cursor)) !=
           std::string::npos)
    {
        ++registrarWiringCount;
        cursor += registrarCall.size();
    }
    EXPECT_EQ(registrarWiringCount, 1u);

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
                  "m_Impl->m_ServiceRegistry.Provide<Platform::IWindow>("),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_Impl->m_ServiceRegistry.Provide<Graphics::IRenderer>("),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_Impl->m_ServiceRegistry.Provide<RuntimeInputActionRegistry>("),
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
    const std::string compactEngineImpl = WithoutWhitespace(engineImpl);
    const auto uiBegin = compactEngineImpl.find("FramePhase::UiBegin");
    const auto uiBuild = compactEngineImpl.find("FramePhase::UiBuild");
    const auto uiEndCapture =
        compactEngineImpl.find("FramePhase::UiEndCapture");
    const auto completedCapture = compactEngineImpl.find(
        "EditorInputCaptureSnapshot&editorCapture=");
    ASSERT_NE(uiBegin, std::string::npos);
    ASSERT_NE(uiBuild, std::string::npos);
    ASSERT_NE(uiEndCapture, std::string::npos);
    ASSERT_NE(completedCapture, std::string::npos);
    EXPECT_LT(uiBegin, uiBuild);
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
    ASSERT_EQ(includeOwners.size(), 2u);
    EXPECT_NE(engineImpl.find(includeDirective), std::string::npos);
    EXPECT_NE(serviceImpl.find(includeDirective), std::string::npos);
    EXPECT_EQ(engineInterface.find(includeDirective), std::string::npos);
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
    EXPECT_EQ(privateHeader.find("RuntimeRenderExtractionStats m_LastStats"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("std::uint64_t m_FrameIndex"),
              std::string::npos);
    EXPECT_NE(WithoutWhitespace(engineImpl).find(
                  "RenderExtractionServicem_RenderExtractionService"),
              std::string::npos);
    EXPECT_NE(WithoutWhitespace(engineInterface).find(
                  "structImpl;std::unique_ptr<Impl>m_Impl;"),
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

    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Runtime.RenderExtraction;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
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
    EXPECT_NE(engineImpl.find("m_Impl->m_RenderExtractionService.ConfigurePool("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RenderExtractionService.Cache()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RenderExtractionService.Pool()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RenderExtractionService.ConsumeFrameIndex()"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_Impl->m_RenderExtractionService.PublishLastStats("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RenderExtractionService.ReleaseFrontSlot("),
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
    EXPECT_EQ(serviceImpl.find("m_Cache.SetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetLastRenderExtractionStats"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetRenderWorldPool"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetVisualizationAdapterBinding"),
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

TEST(RuntimeEnginePrivateGlue, EngineInterfaceCarriesOnlyOpaqueImplementationState)
{
    const auto root = RepoRoot();
    const std::string engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const std::string engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const std::string moduleInterface =
        ReadFile(root / "src/runtime/Runtime.Module.cppm");
    const std::string compactInterface = WithoutWhitespace(engineInterface);

    EXPECT_NE(compactInterface.find(
                  "structImpl;std::unique_ptr<Impl>m_Impl;"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("struct Engine::Impl"), std::string::npos);
    EXPECT_NE(engineImpl.find("Core::FrameClock m_FrameClock"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("std::vector<FrameHookRecord> m_FrameHooks"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("RenderExtractionService m_RenderExtractionService"),
              std::string::npos);

    constexpr std::string_view implementationOnlyInterfaceTokens[] = {
        "import Extrinsic.Core.Error;",
        "import Extrinsic.Core.FrameClock;",
        "import Extrinsic.Core.FrameGraph;",
        "import Extrinsic.Core.Geometry2D;",
        "import Extrinsic.ECS.Scene.Registry;",
        "import Extrinsic.Runtime.InputActions;",
        "import Extrinsic.Runtime.RenderExtraction;",
        "import Extrinsic.Runtime.RenderWorldPool;",
        "m_Config",
        "m_RuntimeModules",
        "m_RenderExtractionService",
        "m_FrameHooks",
        "m_ViewportInputHooks",
        "m_FrameClock",
        "m_ServiceRegistry",
    };
    for (const std::string_view token : implementationOnlyInterfaceTokens)
        EXPECT_EQ(engineInterface.find(token), std::string::npos) << token;

    EXPECT_EQ(moduleInterface.find("Engine&"), std::string::npos);
    EXPECT_EQ(moduleInterface.find("Engine *"), std::string::npos);
}

TEST(RuntimeEnginePrivateGlue,
     AssetWorkflowAndTextureBakeCompositionAreModuleOwned)
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
    const auto textureBakeInterface = ReadFile(
        root / "src/runtime/Runtime.TextureBakeModule.cppm");
    const auto textureBakeImpl = ReadFile(
        root / "src/runtime/Runtime.TextureBakeModule.cpp");
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
                "module Extrinsic.Runtime.AssetWorkflowModule") !=
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
        foundInterface |=
            owner ==
            root /
                "src/runtime/Runtime.AssetWorkflowModule.cppm";
        foundImplementation |=
            owner ==
            root /
                "src/runtime/Runtime.AssetWorkflowModule.cpp";
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
        "m_AssetWorkflowModule",
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
        "AssetWorkflowImportExecutor ImportExecutor{}",
        "Provide<Assets::AssetService>",
        "Provide<AssetWorkflowModule>",
        "Provide<Graphics::GpuAssetCache>",
        "Provide<Core::IAssetFrameHooks>",
        "std::make_unique<AssetWorkflowTextureResidency>",
        "std::make_unique<AssetWorkflowModelMaterializer>",
    };
    for (const auto token : ownedCompositionTokens)
    {
        EXPECT_NE(workflowImpl.find(token), std::string::npos)
            << token;
        EXPECT_EQ(engineImpl.find(token), std::string::npos)
            << token;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(
                 root / "src/runtime"))
        {
            if (!entry.is_regular_file())
                continue;
            const auto extension = entry.path().extension();
            if (extension != ".cpp" &&
                extension != ".cppm" &&
                extension != ".hpp" &&
                extension != ".h")
            {
                continue;
            }
            if (entry.path() ==
                root /
                    "src/runtime/Runtime.AssetWorkflowModule.cpp")
            {
                continue;
            }
            EXPECT_EQ(
                ReadFile(entry.path()).find(token),
                std::string::npos)
                << token << " in " << entry.path();
        }
    }
    EXPECT_NE(
        workflowImpl.find(
            "InitializeRuntimeGpuAssetFallbackTexture("),
        std::string::npos);
    EXPECT_NE(workflowImpl.find(
                  "import Extrinsic.Runtime.TextureBakeModule;"),
              std::string::npos);
    EXPECT_EQ(workflowImpl.find("TextureBake->ProducerContext()"),
              std::string::npos);
    EXPECT_NE(workflowImpl.find(
                  "Find<TextureBakeService>()"),
              std::string::npos);
    EXPECT_NE(textureBakeInterface.find(
                  "export module Extrinsic.Runtime.TextureBakeModule;"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find(
                  "ObjectSpaceNormalBakeService Bake{}"),
              std::string::npos);
    EXPECT_NE(textureBakeImpl.find("Provide<TextureBakeService>"),
              std::string::npos);
    EXPECT_EQ(workflowImpl.find(
                  "ObjectSpaceNormalBakeService Bake{}"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "ObjectSpaceNormalBakeService Bake{}"),
              std::string::npos);

    EXPECT_NE(workflowImpl.find(
                  "setup.Services().Find<SelectionController>()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_Impl->m_ServiceRegistry.Find<Graphics::GpuAssetCache>()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "Core::IAssetFrameHooks>()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_Impl->m_ServiceRegistry.Find<AssetWorkflowModule>()"),
              std::string::npos);
    EXPECT_NE(frameLoop.find("if (AssetWorkflow != nullptr)"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("pipeline != nullptr"),
              std::string::npos);

    EXPECT_NE(runtimeCMake.find("Runtime.AssetWorkflowModule.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.AssetWorkflowModule.cpp"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.TextureBakeModule.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.TextureBakeModule.cpp"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.AssetResidencyService"),
              std::string::npos);
}

TEST(RuntimeEnginePrivateGlue,
     AssetWorkflowImplementationModulesStayPrivateAndRetiredNamesStayDeleted)
{
    const auto root = RepoRoot();
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto privateBoundary = runtimeCMake.find("        PRIVATE\n");
    ASSERT_NE(privateBoundary, std::string::npos);
    const std::string publicSources = runtimeCMake.substr(0u, privateBoundary);

    constexpr std::string_view privateModules[] = {
        "Runtime.AssetWorkflowImportExecutor.cppm",
        "Runtime.AssetWorkflowGeometryMaterialization.cppm",
        "Runtime.AssetWorkflowModelMaterialization.cppm",
        "Runtime.AssetWorkflowModelTextureDecode.cppm",
        "Runtime.AssetWorkflowRecipePolicies.cppm",
        "Runtime.AssetWorkflowTextureResidency.cppm",
    };
    for (const auto module : privateModules)
    {
        EXPECT_EQ(publicSources.find(module), std::string::npos) << module;
        EXPECT_NE(runtimeCMake.find(module), std::string::npos) << module;
    }

    constexpr std::string_view retiredStems[] = {
        "AssetImportPipeline",
        "AssetImportPolicies",
        "AssetGeometryIO",
        "AssetMeshNormals",
        "AssetModelSceneHandoff",
        "AssetModelTextureHandoff",
        "AssetModelTextureIO",
    };
    for (const auto stem : retiredStems)
    {
        EXPECT_EQ(runtimeCMake.find(stem), std::string::npos) << stem;
        EXPECT_FALSE(std::filesystem::exists(
            root / "src/runtime" /
            (std::string{"Runtime."} + std::string{stem} + ".cppm")))
            << stem;
        EXPECT_FALSE(std::filesystem::exists(
            root / "src/runtime" /
            (std::string{"Runtime."} + std::string{stem} + ".cpp")))
            << stem;
    }
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/assets/Asset.GeometryIOBridge.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/assets/Asset.ModelTextureIOBridge.cppm"));
}

TEST(RuntimeEnginePrivateGlue, ClusteringServiceIsTheSoleKMeansRuntimeRoute)
{
    const auto root = RepoRoot();
    const auto facadeInterface = ReadFile(
        root / "src/runtime/internal/Runtime.EditorFeatures.Detail.cppm");
    const auto panelImpl = ReadFile(
        root / "src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp");
    const auto operationImpl = ReadFile(
        root / "src/runtime/Runtime.GeometryProcessingOperations.Public.cpp");
    const auto moduleInterface = ReadFile(
        root / "src/runtime/Modules/Clustering/Runtime.ClusteringModule.cppm");
    const auto gpuPartition = ReadFile(
        root / "src/runtime/Modules/Clustering/Runtime.ClusteringGpuBackend.cppm");
    const auto gpuState = ReadFile(
        root / "src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");

    EXPECT_NE(moduleInterface.find("class ClusteringService"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find("RunKMeans(RunKMeans command)"),
              std::string::npos);
    EXPECT_NE(facadeInterface.find("ClusteringService* Clustering"),
              std::string::npos);
    EXPECT_NE(operationImpl.find("context.Clustering->RunKMeans("),
              std::string::npos);
    EXPECT_EQ(panelImpl.find("Clustering->RunKMeans("), std::string::npos);

    EXPECT_NE(gpuPartition.find(
                  "module Extrinsic.Runtime.ClusteringModule:GpuBackend;"),
              std::string::npos);
    EXPECT_EQ(gpuPartition.find("export module"), std::string::npos);
    EXPECT_NE(gpuState.find("import :GpuBackend;"), std::string::npos);
    EXPECT_NE(runtimeCMake.find(
                  "Modules/Clustering/Runtime.ClusteringGpuBackend.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find(
                  "Modules/Clustering/Runtime.ClusteringGpuBackend.cpp"),
              std::string::npos);

    const std::vector<std::string_view> forbiddenNames{
        "Extrinsic.Runtime.KMeansBackend",
        "Extrinsic.Runtime.KMeansGpuBackend",
        "RuntimeKMeansGpuJobQueue",
        "RuntimeKMeansGpuJobRequest",
        "EditorKMeansBackend",
        "EditorKMeansCommand",
        "EditorKMeansResult",
        "KMeansGpuCommands",
        "ApplyEditorKMeansCommand",
    };
    for (const std::string_view forbidden : forbiddenNames)
    {
        EXPECT_EQ(facadeInterface.find(forbidden), std::string::npos)
            << forbidden;
        EXPECT_EQ(panelImpl.find(forbidden), std::string::npos) << forbidden;
        EXPECT_EQ(operationImpl.find(forbidden), std::string::npos)
            << forbidden;
        EXPECT_EQ(runtimeCMake.find(forbidden), std::string::npos)
            << forbidden;
        EXPECT_EQ(moduleInventory.find(forbidden), std::string::npos)
            << forbidden;
    }

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.KMeansBackend.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.KMeansGpuBackend.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.KMeansGpuJobQueue.Internal.hpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.KMeansGpuJobQueue.cpp"));
}
