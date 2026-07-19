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

TEST(RuntimeEnginePrivateGlue, AssetResidencyServiceIsEnginePrivateImplementation)
{
    const auto root = RepoRoot();
    const auto engineInterface = ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl = ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop = ReadFile(
        root / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto privateHeader = ReadFile(
        root / "src/runtime/Runtime.AssetResidencyService.Internal.hpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");
    constexpr std::string_view includeDirective =
        "#include \"Runtime.AssetResidencyService.Internal.hpp\"";

    std::vector<std::filesystem::path> includeOwners;
    std::vector<std::filesystem::path> implementationOwners;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root / "src/runtime"))
    {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        if (extension != ".cpp" && extension != ".cppm" &&
            extension != ".hpp" && extension != ".h")
            continue;
        const auto content = ReadFile(entry.path());
        if (content.find(includeDirective) != std::string::npos)
            includeOwners.push_back(entry.path());
        if (content.find("AssetResidencyService::") != std::string::npos)
            implementationOwners.push_back(entry.path());
    }

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.AssetResidencyService.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.AssetResidencyService.cpp"));
    ASSERT_EQ(includeOwners.size(), 1u);
    EXPECT_EQ(includeOwners.front().filename(), "Runtime.Engine.cpp");
    ASSERT_EQ(implementationOwners.size(), 1u);
    EXPECT_EQ(implementationOwners.front().filename(), "Runtime.Engine.cpp");
    EXPECT_EQ(engineInterface.find(includeDirective), std::string::npos);
    EXPECT_NE(engineImpl.find(includeDirective), std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Runtime.AssetResidencyService"),
              std::string::npos);
    EXPECT_FALSE(ContainsModuleDirective(privateHeader));
    EXPECT_NE(privateHeader.find(
                  "Runtime.Engine.cpp is the sole include"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("class AssetResidencyService;"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("struct AssetResidencySceneHandoffOptions"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("class AssetResidencyService"),
              std::string::npos);
    EXPECT_NE(privateHeader.find(
                  "std::unique_ptr<Graphics::GpuAssetCache> m_GpuAssetCache"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("m_GpuAssetCacheListener"),
              std::string::npos);
    EXPECT_NE(privateHeader.find(
                  "std::unique_ptr<AssetModelTextureHandoff> m_AssetModelTextureHandoff"),
              std::string::npos);
    EXPECT_NE(privateHeader.find(
                  "std::unique_ptr<AssetModelSceneHandoff> m_AssetModelSceneHandoff"),
              std::string::npos);
    EXPECT_NE(engineInterface.find(
                  "std::unique_ptr<AssetResidencyService>   m_AssetResidencyService"),
              std::string::npos);
    const auto assetServiceMember = engineInterface.find(
        "std::unique_ptr<Assets::AssetService>  m_AssetService");
    const auto residencyMember = engineInterface.find(
        "std::unique_ptr<AssetResidencyService>   m_AssetResidencyService");
    const auto normalBakeMember = engineInterface.find(
        "ObjectSpaceNormalBakeService             m_ObjectSpaceNormalBakeService");
    ASSERT_NE(assetServiceMember, std::string::npos);
    ASSERT_NE(residencyMember, std::string::npos);
    ASSERT_NE(normalBakeMember, std::string::npos);
    EXPECT_LT(assetServiceMember, residencyMember);
    EXPECT_LT(residencyMember, normalBakeMember);
    EXPECT_EQ(engineInterface.find("m_GpuAssetCache"), std::string::npos);
    EXPECT_EQ(engineInterface.find("m_GpuAssetCacheListener"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_AssetModelTextureHandoff"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_AssetModelSceneHandoff"),
              std::string::npos);

    constexpr std::string_view implementationOnlyImports[] = {
        "import Extrinsic.Asset.EventBus;",
        "import Extrinsic.Runtime.AssetModelSceneHandoff;",
        "import Extrinsic.Runtime.AssetModelTextureHandoff;",
        "import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;",
    };
    for (const auto implementationOnlyImport : implementationOnlyImports)
    {
        EXPECT_EQ(engineInterface.find(implementationOnlyImport),
                  std::string::npos);
        EXPECT_NE(engineImpl.find(implementationOnlyImport),
                  std::string::npos);
    }
    EXPECT_NE(engineInterface.find(
                  "import Extrinsic.Graphics.GpuAssetCache;"),
              std::string::npos);
    EXPECT_NE(engineInterface.find(
                  "import Extrinsic.Runtime.ObjectSpaceNormalBakeService;"),
              std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(
        root / "src/graphics/assets/Graphics.GpuAssetCache.cppm"));
    EXPECT_TRUE(std::filesystem::exists(
        root / "src/runtime/Runtime.AssetModelSceneHandoff.cppm"));
    EXPECT_TRUE(std::filesystem::exists(
        root / "src/runtime/Runtime.AssetModelTextureHandoff.cppm"));
    EXPECT_TRUE(std::filesystem::exists(
        root / "src/runtime/Runtime.ObjectSpaceNormalBakeQueue.cppm"));

    EXPECT_NE(engineImpl.find("module Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "module Extrinsic.Runtime.AssetResidencyService;"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_AssetResidencyService(std::make_unique<AssetResidencyService>())"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService->InitializeGpuCache("),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_AssetResidencyService->InitializeSceneHandoffs("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService->CachePtr()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_AssetResidencyService->ModelTextureHandoff()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_AssetResidencyService->ModelSceneHandoff()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService->Cache()"),
              std::string::npos);
    EXPECT_NE(frameLoop.find("AssetResidency.TickAssets(AssetService"),
              std::string::npos);

    EXPECT_NE(engineImpl.find(
                  "import Extrinsic.Runtime.AssetModelSceneHandoff"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "import Extrinsic.Runtime.AssetModelTextureHandoff"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Asset.EventBus"),
              std::string::npos);
    constexpr std::string_view methodDefinitions[] = {
        "AssetResidencyService::~AssetResidencyService()",
        "void AssetResidencyService::InitializeGpuCache(",
        "void AssetResidencyService::InitializeSceneHandoffs(",
        "Graphics::GpuAssetCache& AssetResidencyService::Cache() noexcept",
        "AssetResidencyService::Cache() const noexcept",
        "Graphics::GpuAssetCache* AssetResidencyService::CachePtr() noexcept",
        "AssetResidencyService::CachePtr() const noexcept",
        "AssetResidencyService::ModelTextureHandoff() noexcept",
        "AssetResidencyService::ModelTextureHandoff() const noexcept",
        "AssetResidencyService::ModelSceneHandoff() noexcept",
        "AssetResidencyService::ModelSceneHandoff() const noexcept",
        "void AssetResidencyService::TickAssets(",
        "void AssetResidencyService::DestroySceneBorrowers()",
        "void AssetResidencyService::DestroyAssets(",
    };
    for (const auto methodDefinition : methodDefinitions)
        EXPECT_NE(engineImpl.find(methodDefinition), std::string::npos);

    EXPECT_NE(engineImpl.find("std::make_unique<Graphics::GpuAssetCache>"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("std::make_unique<AssetModelTextureHandoff>"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("std::make_unique<AssetModelSceneHandoff>"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("SubscribeAll("), std::string::npos);
    EXPECT_NE(engineImpl.find("NotifyFailed(id)"), std::string::npos);
    EXPECT_NE(engineImpl.find("InitializeRuntimeGpuAssetFallbackTexture("),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("AssetModelSceneHandoff*"), std::string::npos);

    EXPECT_NE(engineImpl.find(
                  "InitializeRuntimeGpuAssetFallbackTexture(*m_GpuAssetCache"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("assets.SubscribeAll("), std::string::npos);
    EXPECT_NE(engineImpl.find("cache->NotifyFailed(id)"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("std::make_unique<AssetModelTextureHandoff>"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("std::make_unique<AssetModelSceneHandoff>"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("ResolvePendingMaterialTextureBindings()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "assets->UnsubscribeAll(m_GpuAssetCacheListener)"),
              std::string::npos);

    const auto destroyAssets = engineImpl.find(
        "void AssetResidencyService::DestroyAssets(");
    ASSERT_NE(destroyAssets, std::string::npos);
    const auto destroyScene = engineImpl.find(
        "DestroySceneBorrowers();", destroyAssets);
    const auto destroyTextureHandoff = engineImpl.find(
        "m_AssetModelTextureHandoff.reset();", destroyAssets);
    const auto unsubscribe = engineImpl.find(
        "assets->UnsubscribeAll(m_GpuAssetCacheListener)", destroyAssets);
    const auto destroyCache = engineImpl.find(
        "m_GpuAssetCache.reset();", destroyAssets);
    ASSERT_NE(destroyScene, std::string::npos);
    ASSERT_NE(destroyTextureHandoff, std::string::npos);
    ASSERT_NE(unsubscribe, std::string::npos);
    ASSERT_NE(destroyCache, std::string::npos);
    EXPECT_LT(destroyScene, destroyTextureHandoff);
    EXPECT_LT(destroyTextureHandoff, unsubscribe);
    EXPECT_LT(unsubscribe, destroyCache);

    EXPECT_EQ(runtimeCMake.find("Runtime.AssetResidencyService.cpp"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.AssetResidencyService.cppm"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find(
                  "Extrinsic.Runtime.AssetResidencyService"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find("Extrinsic.Graphics.GpuAssetCache`"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find(
                  "Extrinsic.Runtime.AssetModelSceneHandoff`"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find(
                  "Extrinsic.Runtime.AssetModelTextureHandoff`"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find(
                  "Extrinsic.Runtime.ObjectSpaceNormalBakeQueue`"),
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
