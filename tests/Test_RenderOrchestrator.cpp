#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <algorithm>

#include "RHI.Vulkan.hpp"

import Runtime.RenderOrchestrator;
import RHI;
import Graphics;
import Core;

// ---------------------------------------------------------------------------
// Compile-time API contract tests
// ---------------------------------------------------------------------------

TEST(RenderOrchestrator, NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::RenderOrchestrator>);
    static_assert(!std::is_copy_assignable_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderOrchestrator, NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::RenderOrchestrator>);
    static_assert(!std::is_move_assignable_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderOrchestrator, NotDefaultConstructible)
{
    static_assert(!std::is_default_constructible_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Headless integration tests (real Vulkan, no window surface)
// ---------------------------------------------------------------------------

class RenderOrchestratorHeadlessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Minimal headless Vulkan setup to verify subsystem wiring.
        RHI::ContextConfig ctxConfig{
            .AppName = "RenderOrchestratorTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        m_Bindless = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
        m_TextureSystem = std::make_unique<RHI::TextureSystem>(*m_Device, *m_Bindless);
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);
    }

    void TearDown() override
    {
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_TextureSystem.reset();
        m_Bindless.reset();
        m_Device->FlushAllDeletionQueues();
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<RHI::BindlessDescriptorSystem> m_Bindless;
    std::unique_ptr<RHI::TextureSystem> m_TextureSystem;
    std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
    std::unique_ptr<RHI::DescriptorAllocator> m_DescriptorPool;
};

TEST_F(RenderOrchestratorHeadlessTest, ShaderRegistryPopulated)
{
    // The ShaderRegistry should have entries after construction.
    // We can't fully construct RenderOrchestrator without a swapchain/renderer,
    // but we can verify the ShaderRegistry type is usable.
    Graphics::ShaderRegistry reg;
    reg.Register(Core::Hash::StringID{42}, "test.spv");
    EXPECT_TRUE(reg.Contains(Core::Hash::StringID{42}));
    auto path = reg.Get(Core::Hash::StringID{42});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "test.spv");
}

TEST_F(RenderOrchestratorHeadlessTest, DescriptorSubsystemsReady)
{
    // Verify the descriptor subsystems that RenderOrchestrator depends on
    // (provided by GraphicsBackend) can be created headlessly.
    EXPECT_TRUE(m_DescriptorLayout->IsValid());
    EXPECT_TRUE(m_DescriptorPool->IsValid());
}

TEST_F(RenderOrchestratorHeadlessTest, MaterialSystemCreatable)
{
    // MaterialSystem is one of the first things RenderOrchestrator creates.
    // Verify it can be constructed with headless Vulkan infrastructure.
    Core::Assets::AssetManager assetManager;
    auto matSys = std::make_unique<Graphics::MaterialSystem>(*m_TextureSystem, assetManager);
    EXPECT_NE(matSys, nullptr);

    // Create and destroy a material to exercise the pool.
    Graphics::MaterialData data{};
    data.RoughnessFactor = 0.5f;
    auto handle = matSys->Create(data);
    EXPECT_NE(handle, Graphics::MaterialHandle{});

    matSys->Destroy(handle);
    matSys->ProcessDeletions(1);
}

TEST_F(RenderOrchestratorHeadlessTest, GeometryPoolInitializable)
{
    // GeometryPool is a Core::ResourcePool; it doesn't require explicit initialization.
    Graphics::GeometryPool pool;
    pool.Clear();
    SUCCEED();
}

TEST_F(RenderOrchestratorHeadlessTest, RenderGraphDebugMetadataTracksImportedAttachmentContracts)
{
    Core::Memory::LinearArena arena(1024 * 64);
    Core::Memory::ScopeStack scope(1024 * 64);
    Graphics::RenderGraph graph(m_Device, arena, scope);

    RHI::VulkanImage color(*m_Device,
                           4, 4, 1,
                           VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);
    RHI::VulkanImage depth(*m_Device,
                           4, 4, 1,
                           VK_FORMAT_D32_SFLOAT,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT);

    struct GBufferPassData
    {
        Graphics::RGResourceHandle Color{};
        Graphics::RGResourceHandle Depth{};
    };
    graph.AddPass<GBufferPassData>("GBuffer",
                                   [&](GBufferPassData& data, Graphics::RGBuilder& builder)
                                   {
                                       const auto colorHandle = builder.ImportTexture(Core::Hash::StringID{"MainColor"},
                                                                                      color.GetHandle(),
                                                                                      color.GetView(),
                                                                                      color.GetFormat(),
                                                                                      {4u, 4u},
                                                                                      VK_IMAGE_LAYOUT_UNDEFINED);
                                       const auto depthHandle = builder.ImportTexture(Core::Hash::StringID{"MainDepth"},
                                                                                      depth.GetHandle(),
                                                                                      depth.GetView(),
                                                                                      depth.GetFormat(),
                                                                                      {4u, 4u},
                                                                                      VK_IMAGE_LAYOUT_UNDEFINED);

                                       Graphics::RGAttachmentInfo colorInfo{};
                                       colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                       colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                       Graphics::RGAttachmentInfo depthInfo{};
                                       depthInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                       depthInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                       depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                       data.Color = builder.WriteColor(colorHandle, colorInfo);
                                       data.Depth = builder.WriteDepth(depthHandle, depthInfo);
                                   },
                                   [](const GBufferPassData&, const Graphics::RGRegistry&, VkCommandBuffer) {});

    struct SamplePassData
    {
        Graphics::RGResourceHandle Color{};
    };
    graph.AddPass<SamplePassData>("DebugSample",
                                  [&](SamplePassData& data, Graphics::RGBuilder& builder)
                                  {
                                      const auto colorHandle = builder.ImportTexture(Core::Hash::StringID{"MainColor"},
                                                                                     color.GetHandle(),
                                                                                     color.GetView(),
                                                                                     color.GetFormat(),
                                                                                     {4u, 4u},
                                                                                     VK_IMAGE_LAYOUT_UNDEFINED);
                                      data.Color = builder.Read(colorHandle,
                                                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                                  },
                                  [](const SamplePassData&, const Graphics::RGRegistry&, VkCommandBuffer) {});

    graph.Compile(0);

    const auto passes = graph.BuildDebugPassList();
    const auto images = graph.BuildDebugImageList();

    ASSERT_EQ(passes.size(), 2u);
    EXPECT_STREQ(passes[0].Name, "GBuffer");
    ASSERT_EQ(passes[0].Attachments.size(), 2u);
    EXPECT_EQ(passes[0].Attachments[0].Format, VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(passes[0].Attachments[0].LoadOp, VK_ATTACHMENT_LOAD_OP_CLEAR);
    EXPECT_FALSE(passes[0].Attachments[0].IsDepth);
    EXPECT_TRUE(passes[0].Attachments[0].IsImported);
    EXPECT_TRUE(passes[0].Attachments[1].IsDepth);
    EXPECT_TRUE(passes[0].Attachments[1].IsImported);

    const auto colorIt = std::find_if(images.begin(), images.end(), [](const Graphics::RenderGraphDebugImage& img)
    {
        return img.Name == Core::Hash::StringID{"MainColor"};
    });
    ASSERT_NE(colorIt, images.end());
    EXPECT_TRUE(colorIt->IsImported);
    EXPECT_EQ(colorIt->Format, VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(colorIt->FirstWritePass, 0u);
    EXPECT_EQ(colorIt->LastWritePass, 0u);
    EXPECT_EQ(colorIt->FirstReadPass, 1u);
    EXPECT_EQ(colorIt->LastReadPass, 1u);

    const auto depthIt = std::find_if(images.begin(), images.end(), [](const Graphics::RenderGraphDebugImage& img)
    {
        return img.Name == Core::Hash::StringID{"MainDepth"};
    });
    ASSERT_NE(depthIt, images.end());
    EXPECT_TRUE(depthIt->IsImported);
    EXPECT_EQ(depthIt->FirstWritePass, 0u);
    EXPECT_EQ(depthIt->LastWritePass, 0u);
    EXPECT_EQ(depthIt->FirstReadPass, ~0u);
}
