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

TEST(RuntimeEnginePrivateGlue, ImGuiEditorBridgeIsEnginePrivateImplementation)
{
    const auto root = RepoRoot();
    const auto engineInterface = ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl = ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto privateHeader = ReadFile(
        root / "src/runtime/Runtime.ImGuiEditorBridge.Internal.hpp");
    const auto bridgeImpl = ReadFile(
        root / "src/runtime/Runtime.ImGuiEditorBridge.cpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto moduleInventory = ReadFile(
        root / "docs/api/generated/module_inventory.md");
    constexpr std::string_view includeDirective =
        "#include \"Runtime.ImGuiEditorBridge.Internal.hpp\"";

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
        root / "src/runtime/Runtime.ImGuiEditorBridge.cppm"));
    ASSERT_EQ(includeOwners.size(), 1u);
    EXPECT_EQ(includeOwners.front().filename(), "Runtime.Engine.cppm");
    EXPECT_NE(engineInterface.find(includeDirective), std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ImGuiEditorBridge"),
              std::string::npos);
    EXPECT_FALSE(ContainsModuleDirective(privateHeader));
    EXPECT_NE(privateHeader.find("class ImGuiEditorBridge"), std::string::npos);
    EXPECT_NE(privateHeader.find("Graphics::ImGuiOverlaySystem m_Overlay"),
              std::string::npos);
    EXPECT_NE(privateHeader.find("std::unique_ptr<ImGuiAdapter> m_Adapter"),
              std::string::npos);
    EXPECT_NE(engineInterface.find(
                  "ImGuiEditorBridge                    m_ImGuiEditorBridge"),
              std::string::npos);

    EXPECT_NE(bridgeImpl.find("module Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_EQ(bridgeImpl.find("module Extrinsic.Runtime.ImGuiEditorBridge;"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ImGuiEditorBridge.Initialize("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ImGuiEditorBridge.Shutdown("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ImGuiEditorBridge.BeginFrame("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ImGuiEditorBridge.EndFrame("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ImGuiEditorBridge.CaptureSnapshot()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ImGuiEditorBridge.Diagnostics()"),
              std::string::npos);

    EXPECT_NE(bridgeImpl.find(
                  "std::make_unique<ImGuiAdapter>(window, m_Overlay)"),
              std::string::npos);
    EXPECT_NE(bridgeImpl.find("renderer.SetImGuiOverlaySystem(&m_Overlay)"),
              std::string::npos);
    EXPECT_NE(bridgeImpl.find("renderer->SetImGuiOverlaySystem(nullptr)"),
              std::string::npos);
    EXPECT_NE(bridgeImpl.find("m_Adapter->BeginFrame(deltaSeconds)"),
              std::string::npos);
    EXPECT_NE(bridgeImpl.find("m_Adapter->EndFrame()"), std::string::npos);
    EXPECT_NE(bridgeImpl.find("m_Adapter->GetDiagnostics()"),
              std::string::npos);

    EXPECT_EQ(runtimeCMake.find("Runtime.ImGuiEditorBridge.cppm"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("Extrinsic.Runtime.ImGuiEditorBridge"),
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
