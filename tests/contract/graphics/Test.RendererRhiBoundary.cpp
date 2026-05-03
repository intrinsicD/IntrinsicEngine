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

TEST(RendererRhiBoundary, VulkanBackendDefinesPromotedDeviceLifecycle)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto content = ReadFile(deviceSource);

    const std::vector<std::string> requiredDefinitions{
        "void VulkanDevice::Initialize(",
        "void VulkanDevice::Shutdown()",
        "void VulkanDevice::WaitIdle()",
        "bool VulkanDevice::BeginFrame(",
        "void VulkanDevice::EndFrame(",
        "void VulkanDevice::Present(",
        "void VulkanDevice::Resize(",
        "Platform::Extent2D VulkanDevice::GetBackbufferExtent() const",
        "void VulkanDevice::SetPresentMode(",
        "RHI::TextureHandle VulkanDevice::GetBackbufferHandle(",
        "RHI::ICommandContext& VulkanDevice::GetGraphicsContext(",
    };

    for (const auto& definition : requiredDefinitions)
        EXPECT_NE(content.find(definition), std::string::npos) << definition;
}

TEST(RendererRhiBoundary, VulkanBackendDefinesPromotedResourceSurface)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto content = ReadFile(deviceSource);

    const std::vector<std::string> requiredDefinitions{
        "RHI::BufferHandle VulkanDevice::CreateBuffer(",
        "void VulkanDevice::DestroyBuffer(",
        "void VulkanDevice::WriteBuffer(",
        "uint64_t VulkanDevice::GetBufferDeviceAddress(",
        "RHI::TextureHandle VulkanDevice::CreateTexture(",
        "void VulkanDevice::DestroyTexture(",
        "void VulkanDevice::WriteTexture(",
        "RHI::SamplerHandle VulkanDevice::CreateSampler(",
        "void VulkanDevice::DestroySampler(",
        "RHI::PipelineHandle VulkanDevice::CreatePipeline(",
        "void VulkanDevice::DestroyPipeline(",
        "VkCommandBuffer VulkanDevice::BeginOneShot()",
        "void VulkanDevice::EndOneShot(",
        "void VulkanDevice::DeferDelete(",
        "void VulkanDevice::FlushDeletionQueue(",
    };

    for (const auto& definition : requiredDefinitions)
        EXPECT_NE(content.find(definition), std::string::npos) << definition;

    EXPECT_NE(content.find("if (!m_Operational || m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE"),
              std::string::npos);
}

TEST(RendererRhiBoundary, VulkanBackendCreatesSamplersWhenOperational)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto deviceInterface = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cppm";
    const auto sourceContent = ReadFile(deviceSource);
    const auto interfaceContent = ReadFile(deviceInterface);

    const std::vector<std::string> requiredSourceSnippets{
        "RHI::SamplerHandle VulkanDevice::CreateSampler(",
        "vkCreateSampler(m_Device, &samplerInfo, nullptr, &sampler.Sampler)",
        "return m_Samplers.Add(std::move(sampler));",
        "samplerInfo.magFilter = ToVkFilter(desc.MagFilter);",
        "samplerInfo.mipmapMode = ToVkMipmapMode(desc.MipFilter);",
        "samplerInfo.addressModeU = ToVkAddressMode(desc.AddressU);",
        "samplerInfo.compareOp = ToVkCompareOp(desc.Compare);",
        "m_SamplerAnisotropySupported && desc.MaxAnisotropy > 1.0f",
        "vkSetDebugUtilsObjectNameEXT(m_Device, &nameInfo);",
    };

    for (const auto& snippet : requiredSourceSnippets)
        EXPECT_NE(sourceContent.find(snippet), std::string::npos) << snippet;

    EXPECT_NE(interfaceContent.find("bool             m_SamplerAnisotropySupported = false;"),
              std::string::npos);
}

TEST(RendererRhiBoundary, VulkanBackendCreatesTexturesWhenOperational)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto content = ReadFile(deviceSource);

    const std::vector<std::string> requiredSnippets{
        "RHI::TextureHandle VulkanDevice::CreateTexture(",
        "VkImageType ToVkImageType(",
        "VkImageViewType ToVkImageViewType(",
        "VkSampleCountFlagBits ToVkSampleCount(",
        "const VkFormat format = ToVkFormat(desc.Fmt);",
        "const VkImageUsageFlags usage = ToVkTextureUsage(desc.Usage);",
        "desc.Dimension == RHI::TextureDimension::TexCube && desc.DepthOrArrayLayers != 6",
        "vmaCreateImage(m_Vma, &imageInfo, &allocationInfo, &image.Image, &image.Allocation, nullptr)",
        "viewInfo.subresourceRange.aspectMask = AspectFromFormat(format);",
        "vkCreateImageView(m_Device, &viewInfo, nullptr, &image.View)",
        "return m_Images.Add(std::move(image));",
    };

    for (const auto& snippet : requiredSnippets)
        EXPECT_NE(content.find(snippet), std::string::npos) << snippet;
}

TEST(RendererRhiBoundary, VulkanBackendUploadsTexturesWhenOperational)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto memoryInterface = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Memory.cppm";
    const auto sourceContent = ReadFile(deviceSource);
    const auto memoryContent = ReadFile(memoryInterface);

    const std::vector<std::string> requiredSourceSnippets{
        "void VulkanDevice::WriteTexture(",
        "std::uint64_t RequiredUploadBytes(",
        "std::uint32_t FormatBlockByteSize(",
        "bool IsBlockCompressedFormat(",
        "void ImageBarrier(",
        "if (mipLevel >= image->MipLevels || arrayLayer >= image->ArrayLayers)",
        "if (requiredBytes == 0 || dataSizeBytes < requiredBytes)",
        "vmaCreateBuffer(m_Vma,",
        "VkCommandBuffer cmd = BeginOneShot();",
        "vkCmdCopyBufferToImage(cmd,",
        "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL",
        "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL",
        "image->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;",
        "vmaDestroyBuffer(m_Vma, stagingBuffer, stagingAllocation);",
    };

    for (const auto& snippet : requiredSourceSnippets)
        EXPECT_NE(sourceContent.find(snippet), std::string::npos) << snippet;

    EXPECT_NE(memoryContent.find("uint32_t      Depth       = 1;"), std::string::npos);
    EXPECT_NE(memoryContent.find("VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;"),
              std::string::npos);
}

TEST(RendererRhiBoundary, VulkanBackendShutdownDrainsLiveResourcePools)
{
    const auto deviceSource = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cpp";
    const auto content = ReadFile(deviceSource);

    const std::vector<std::string> requiredSnippets{
        "m_Pipelines.ForEach([device](RHI::PipelineHandle, VulkanPipeline& pipeline)",
        "vkDestroyPipeline(device, pipeline.Pipeline, nullptr);",
        "m_Pipelines.Clear();",
        "m_Samplers.ForEach([device](RHI::SamplerHandle, VulkanSampler& sampler)",
        "vkDestroySampler(device, sampler.Sampler, nullptr);",
        "m_Samplers.Clear();",
        "m_Images.ForEach([device, vma](RHI::TextureHandle, VulkanImage& image)",
        "vmaDestroyImage(vma, image.Image, image.Allocation);",
        "m_Images.Clear();",
        "m_Buffers.ForEach([vma](RHI::BufferHandle, VulkanBuffer& buffer)",
        "vmaDestroyBuffer(vma, buffer.Buffer, buffer.Allocation);",
        "m_Buffers.Clear();",
    };

    for (const auto& snippet : requiredSnippets)
        EXPECT_NE(content.find(snippet), std::string::npos) << snippet;
}

TEST(RendererRhiBoundary, VulkanBackendProvidesFailClosedServiceFallbacks)
{
    const auto deviceInterface = RepoRoot() / "src/graphics/vulkan/Backends.Vulkan.Device.cppm";
    const auto content = ReadFile(deviceInterface);

    EXPECT_NE(content.find("class FallbackBindlessHeap final : public RHI::IBindlessHeap"), std::string::npos);
    EXPECT_NE(content.find("class FallbackTransferQueue final : public RHI::ITransferQueue"), std::string::npos);
    EXPECT_NE(content.find("return m_FallbackTransferQueue;"), std::string::npos);
    EXPECT_NE(content.find("return m_FallbackBindlessHeap;"), std::string::npos);
    EXPECT_NE(content.find("return RHI::kInvalidBindlessIndex;"), std::string::npos);
    EXPECT_NE(content.find("return !token.IsValid();"), std::string::npos);
}



