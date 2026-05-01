#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Types;

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

    std::vector<std::filesystem::path> FilesUnder(const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;

            const auto ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".cppm" || ext == ".hpp" || ext == ".h")
                files.push_back(entry.path());
        }
        return files;
    }
}

TEST(RendererRhiBoundary, RendererPublicSurfaceCompilesThroughRhiOnly)
{
    static_assert(std::is_polymorphic_v<Extrinsic::Graphics::IRenderer>);
    static_assert(std::is_polymorphic_v<Extrinsic::RHI::IDevice>);
    static_assert(std::is_polymorphic_v<Extrinsic::RHI::ICommandContext>);

    Extrinsic::RHI::FrameHandle frame{};
    frame.FrameIndex = 1;
    frame.SwapchainImageIndex = 2;

    EXPECT_EQ(frame.FrameIndex, 1u);
    EXPECT_EQ(frame.SwapchainImageIndex, 2u);
}

TEST(RendererRhiBoundary, RendererDoesNotImportVulkanBackend)
{
    const auto rendererRoot = RepoRoot() / "src/graphics/renderer";

    for (const auto& path : FilesUnder(rendererRoot))
    {
        const auto content = ReadFile(path);
        EXPECT_EQ(content.find("import Extrinsic.Backends.Vulkan"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("import :Device"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("Vulkan.hpp"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("VkDevice"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("VkCommandBuffer"), std::string::npos) << path.string();
    }
}

TEST(RendererRhiBoundary, RhiLayerDoesNotImportVulkan)
{
    const auto rhiRoot = RepoRoot() / "src/graphics/rhi";

    for (const auto& path : FilesUnder(rhiRoot))
    {
        const auto content = ReadFile(path);
        EXPECT_EQ(content.find("import Extrinsic.Backends.Vulkan"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("Vulkan.hpp"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("#include <vulkan"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("VkDevice"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("VkCommandBuffer"), std::string::npos) << path.string();
    }
}



