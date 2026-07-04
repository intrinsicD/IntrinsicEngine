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
import Extrinsic.RHI.Descriptors;
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

TEST(RendererRhiBoundary, RendererDoesNotImportLiveEcsOwnership)
{
    const auto rendererRoot = RepoRoot() / "src/graphics/renderer";

    for (const auto& path : FilesUnder(rendererRoot))
    {
        const auto content = ReadFile(path);
        EXPECT_EQ(content.find("#include <entt"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("entt::registry"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("entt::entity"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("import Extrinsic.ECS"), std::string::npos) << path.string();
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

TEST(RendererRhiBoundary, PromotedGraphicsAndRuntimeDoNotImportCuda)
{
    const std::vector<std::filesystem::path> roots{
        RepoRoot() / "src/graphics/rhi",
        RepoRoot() / "src/graphics/vulkan",
        RepoRoot() / "src/graphics/renderer",
        RepoRoot() / "src/runtime",
    };
    const std::vector<std::string> forbiddenTokens{
        "CudaDevice",
        "CudaError",
        "INTRINSIC_HAS_CUDA",
        "#include <cuda",
        "#include <cuda.h>",
        "CUdevice",
        "CUcontext",
        "CUstream",
    };

    for (const auto& root : roots)
    {
        for (const auto& path : FilesUnder(root))
        {
            const auto content = ReadFile(path);
            for (const auto& token : forbiddenTokens)
                EXPECT_EQ(content.find(token), std::string::npos) << path.string() << " contains " << token;
        }
    }
}

TEST(RendererRhiBoundary, SamplerBorderColorStaysBackendNeutralAndMapsInVulkanBackend)
{
    const Extrinsic::RHI::SamplerDesc defaultDesc{};
    EXPECT_EQ(defaultDesc.BorderColor, Extrinsic::RHI::SamplerBorderColor::OpaqueBlackFloat);

    const auto rhiDescriptors = ReadFile(RepoRoot() / "src/graphics/rhi/RHI.Descriptors.cppm");
    EXPECT_NE(rhiDescriptors.find("enum class SamplerBorderColor"), std::string::npos);
    EXPECT_EQ(rhiDescriptors.find("VkBorderColor"), std::string::npos);
    EXPECT_EQ(rhiDescriptors.find("VK_BORDER_COLOR"), std::string::npos);

    const auto vulkanMappings = ReadFile(RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Mappings.cpp");
    EXPECT_NE(vulkanMappings.find("VkBorderColor ToVkBorderColor"), std::string::npos);
    EXPECT_NE(vulkanMappings.find("VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK"), std::string::npos);
    EXPECT_NE(vulkanMappings.find("VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE"), std::string::npos);
    EXPECT_NE(vulkanMappings.find("VK_BORDER_COLOR_INT_TRANSPARENT_BLACK"), std::string::npos);

    const auto vulkanDevice = ReadFile(RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp");
    EXPECT_NE(vulkanDevice.find("samplerInfo.borderColor = ToVkBorderColor(desc.BorderColor);"),
              std::string::npos);
}

TEST(RendererRhiBoundary, VulkanBackendDefinesPromotedSymbols)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto content = ReadFile(deviceSource);

    // Linkage guard only: behavioral fail-closed coverage lives in
    // Test.VulkanFailClosedContract.cpp when the Vulkan backend target exists.
    const std::vector<std::string> requiredSymbols{
        "VulkanDevice::Initialize(",
        "VulkanDevice::Shutdown()",
        "VulkanDevice::WaitIdle()",
        "VulkanDevice::BeginFrame(",
        "VulkanDevice::EndFrame(",
        "VulkanDevice::Present(",
        "VulkanDevice::Resize(",
        "VulkanDevice::GetBackbufferExtent()",
        "VulkanDevice::SetPresentMode(",
        "VulkanDevice::GetBackbufferHandle(",
        "VulkanDevice::GetGraphicsContext(",
        "VulkanDevice::CreateBuffer(",
        "VulkanDevice::DestroyBuffer(",
        "VulkanDevice::WriteBuffer(",
        "VulkanDevice::GetBufferDeviceAddress(",
        "VulkanDevice::GetBufferMemoryRequirements(",
        "VulkanDevice::CreateMemoryBlock(",
        "VulkanDevice::DestroyMemoryBlock(",
        "VulkanDevice::GetMemoryBlockInfo(",
        "VulkanDevice::CreatePlacedBuffer(",
        "VulkanDevice::GetBufferMemoryPlacement(",
        "VulkanDevice::CreateTexture(",
        "VulkanDevice::DestroyTexture(",
        "VulkanDevice::WriteTexture(",
        "VulkanDevice::GetTextureMemoryRequirements(",
        "VulkanDevice::CreatePlacedTexture(",
        "VulkanDevice::GetTextureMemoryPlacement(",
        "VulkanDevice::CreateSampler(",
        "VulkanDevice::DestroySampler(",
        "VulkanDevice::CreatePipeline(",
        "VulkanDevice::DestroyPipeline(",
        "VulkanDevice::BeginOneShot()",
        "VulkanDevice::EndOneShot(",
        "VulkanDevice::DeferDelete(",
        "VulkanDevice::FlushDeletionQueue(",
        "GetFallbackBindlessAllocationAttemptCount()",
        "GetFallbackPresentAttemptCount()",
        "GetFallbackResizeAttemptCount()",
    };

    for (const auto& symbol : requiredSymbols)
        EXPECT_NE(content.find(symbol), std::string::npos) << symbol;
}

