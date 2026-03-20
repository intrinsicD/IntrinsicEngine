// tests/Test_RenderGraphPackets.cpp
//
// Tests for RenderGraph GPU packet execution and plan caching.
// Validates packetization logic (merging of linear non-raster pass chains)
// and shape-key-based cache hit/miss behavior.
//
// Uses headless Vulkan — no window surface required.

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "RHI.Vulkan.hpp"

import Graphics;
import RHI;
import Core;

using namespace Graphics;
using namespace Core::Hash;

// ===========================================================================
// Compile-time contract tests (no Vulkan device needed)
// ===========================================================================

TEST(RenderGraphPackets, PacketStatsStructLayout)
{
    PacketStats stats{};
    EXPECT_EQ(stats.PacketCount, 0u);
    EXPECT_EQ(stats.PassCount, 0u);
    EXPECT_FALSE(stats.CacheHit);
}

// ===========================================================================
// Headless integration tests
// ===========================================================================

class RenderGraphPacketTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Core::Tasks::Scheduler::Initialize();

        RHI::ContextConfig ctxConfig{
            .AppName = "RenderGraphPacketTest",
            .EnableValidation = true,
            .Headless = true,
        };
        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        m_Arena = std::make_unique<Core::Memory::LinearArena>(256 * 1024);
        m_Scope = std::make_unique<Core::Memory::ScopeStack>(64 * 1024);
        m_TransientAlloc = std::make_unique<RHI::TransientAllocator>(*m_Device);

        m_Graph = std::make_unique<RenderGraph>(m_Device, *m_Arena, *m_Scope);
        m_Graph->SetTransientAllocator(*m_TransientAlloc);
    }

    void TearDown() override
    {
        m_Graph.reset();
        m_TransientAlloc.reset();
        m_Scope.reset();
        m_Arena.reset();
        if (m_Device)
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        m_Device.reset();
        m_Context.reset();
        Core::Tasks::Scheduler::Shutdown();
    }

    // Helper: register a compute-only pass that reads and/or writes a buffer.
    void AddComputePass(const std::string& name,
                        RGResourceHandle readBuf = {},
                        RGResourceHandle writeBuf = {})
    {
        struct PassData {};
        m_Graph->AddPass<PassData>(name,
            [readBuf, writeBuf](PassData&, RGBuilder& builder)
            {
                if (readBuf.IsValid())
                    builder.Read(readBuf,
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_READ_BIT);
                if (writeBuf.IsValid())
                    builder.Write(writeBuf,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {
                // No-op execution for testing.
            });
    }

    // Helper: register a raster pass writing a color attachment.
    void AddRasterPass(const std::string& name, RGResourceHandle colorTarget)
    {
        struct PassData {};
        m_Graph->AddPass<PassData>(name,
            [colorTarget](PassData&, RGBuilder& builder)
            {
                builder.WriteColor(colorTarget, RGAttachmentInfo{});
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {
                // No-op execution for testing.
            });
    }

    // Helper: register a raster pass writing color + depth attachments.
    void AddRasterPassColorDepth(const std::string& name,
                                 RGResourceHandle colorTarget,
                                 RGResourceHandle depthTarget,
                                 RGAttachmentInfo colorInfo = {},
                                 RGAttachmentInfo depthInfo = {})
    {
        struct PassData {};
        m_Graph->AddPass<PassData>(name,
            [colorTarget, depthTarget, colorInfo, depthInfo](PassData&, RGBuilder& builder)
            {
                builder.WriteColor(colorTarget, colorInfo);
                builder.WriteDepth(depthTarget, depthInfo);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
    std::unique_ptr<Core::Memory::LinearArena> m_Arena;
    std::unique_ptr<Core::Memory::ScopeStack> m_Scope;
    std::unique_ptr<RHI::TransientAllocator> m_TransientAlloc;
    std::unique_ptr<RenderGraph> m_Graph;
};

// ---------------------------------------------------------------------------
// Single pass: no merging, packet count == pass count
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, SinglePass_OnePacket)
{
    struct PassData {};
    RGResourceHandle buf;
    m_Graph->AddPass<PassData>("OnlyPass",
        [&buf](PassData&, RGBuilder& builder)
        {
            buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
            builder.Write(buf);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 1u);
    EXPECT_EQ(stats.PacketCount, 1u);
    EXPECT_FALSE(stats.CacheHit);
}

// ---------------------------------------------------------------------------
// Two raster passes targeting DIFFERENT attachments: no merging
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, TwoRasterPasses_DifferentAttachments_NoMerge)
{
    struct PassData {};

    RGResourceHandle colorA, colorB;

    m_Graph->AddPass<PassData>("RasterA",
        [&colorA](PassData&, RGBuilder& builder)
        {
            colorA = builder.CreateTexture("colorA"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            builder.WriteColor(colorA, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("RasterB",
        [&colorB](PassData&, RGBuilder& builder)
        {
            colorB = builder.CreateTexture("colorB"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            builder.WriteColor(colorB, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 2u);
    EXPECT_EQ(stats.PacketCount, 2u);  // Different attachments → no merging
}

// ---------------------------------------------------------------------------
// Cache hit: same topology on second compile
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, CacheHit_SameTopology)
{
    auto buildFrame = [this]()
    {
        struct PassData {};
        RGResourceHandle buf;
        m_Graph->AddPass<PassData>("PassA",
            [&buf](PassData&, RGBuilder& builder)
            {
                buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
                builder.Write(buf);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
    };

    // Frame 0: cache miss
    buildFrame();
    m_Graph->Compile(0);
    auto stats0 = m_Graph->GetLastPacketStats();
    EXPECT_FALSE(stats0.CacheHit);

    // Frame 1: same topology → cache hit
    m_Graph->Reset(1);
    buildFrame();
    m_Graph->Compile(1);
    auto stats1 = m_Graph->GetLastPacketStats();
    EXPECT_TRUE(stats1.CacheHit);
    EXPECT_EQ(stats1.PacketCount, stats0.PacketCount);
}

// ---------------------------------------------------------------------------
// Cache miss: changed topology
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, CacheMiss_ChangedTopology)
{
    // Frame 0: one pass
    {
        struct PassData {};
        RGResourceHandle buf;
        m_Graph->AddPass<PassData>("PassA",
            [&buf](PassData&, RGBuilder& builder)
            {
                buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
                builder.Write(buf);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
    }
    m_Graph->Compile(0);
    EXPECT_FALSE(m_Graph->GetLastPacketStats().CacheHit);

    // Frame 1: two passes (different topology)
    m_Graph->Reset(1);
    {
        struct PassData {};
        RGResourceHandle buf;
        m_Graph->AddPass<PassData>("PassA",
            [&buf](PassData&, RGBuilder& builder)
            {
                buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
                builder.Write(buf);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
        m_Graph->AddPass<PassData>("PassB",
            [&buf](PassData&, RGBuilder& builder)
            {
                builder.Read(buf,
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                             VK_ACCESS_2_SHADER_READ_BIT);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
    }
    m_Graph->Compile(1);
    EXPECT_FALSE(m_Graph->GetLastPacketStats().CacheHit);
}

// ---------------------------------------------------------------------------
// Trim invalidates cache
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, TrimInvalidatesCache)
{
    // Frame 0
    {
        struct PassData {};
        RGResourceHandle buf;
        m_Graph->AddPass<PassData>("PassA",
            [&buf](PassData&, RGBuilder& builder)
            {
                buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
                builder.Write(buf);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
    }
    m_Graph->Compile(0);
    EXPECT_FALSE(m_Graph->GetLastPacketStats().CacheHit);

    // Trim (simulates resize)
    vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    m_Graph->Trim();

    // Frame 1: same topology but cache was cleared
    m_Graph->Reset(1);
    {
        struct PassData {};
        RGResourceHandle buf;
        m_Graph->AddPass<PassData>("PassA",
            [&buf](PassData&, RGBuilder& builder)
            {
                buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
                builder.Write(buf);
            },
            [](const PassData&, const RGRegistry&, VkCommandBuffer) {});
    }
    m_Graph->Compile(1);
    EXPECT_FALSE(m_Graph->GetLastPacketStats().CacheHit);
}

// ---------------------------------------------------------------------------
// GetExecutionLayers still valid after packet compilation
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, ExecutionLayersStillValid)
{
    struct PassData {};
    RGResourceHandle buf;
    m_Graph->AddPass<PassData>("PassA",
        [&buf](PassData&, RGBuilder& builder)
        {
            buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
            builder.Write(buf);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    const auto& layers = m_Graph->GetExecutionLayers();
    EXPECT_FALSE(layers.empty());

    // Count total nodes across all layers
    uint32_t totalNodes = 0;
    for (const auto& layer : layers)
        totalNodes += static_cast<uint32_t>(layer.size());
    EXPECT_EQ(totalNodes, 1u);
}

// ---------------------------------------------------------------------------
// Layout-sensitive read chains may need explicit ordering even when both later
// passes are logically reads. The render graph uses MEMORY_WRITE as a scheduler-
// only ordering marker for this case; barrier generation must still sanitize it
// back to a valid Vulkan read access mask.
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, LayoutSensitiveReadChain_SerializesAcrossLayers)
{
    struct PassData {};
    RGResourceHandle color;

    m_Graph->AddPass<PassData>("WriteColor",
        [&color](PassData&, RGBuilder& builder)
        {
            color = builder.CreateTexture("layout_sensitive_color"_id,
                RGTextureDesc{
                    .Width = 64,
                    .Height = 64,
                    .Format = VK_FORMAT_R8G8B8A8_UNORM,
                    .Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    .Aspect = VK_IMAGE_ASPECT_COLOR_BIT});
            builder.WriteColor(color, RGAttachmentInfo{
                .LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .StoreOp = VK_ATTACHMENT_STORE_OP_STORE});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("ReadForBlit",
        [&color](PassData&, RGBuilder& builder)
        {
            builder.Read(color,
                         VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("ReadForSample",
        [&color](PassData&, RGBuilder& builder)
        {
            builder.Read(color,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    const auto& layers = m_Graph->GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(layers[2].size(), 1u);
}


// ---------------------------------------------------------------------------
// Invalid resource handles should be rejected across all RGBuilder accessors
// without creating phantom resources.
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, InvalidResourceHandles_AreRejectedAndDoNotCreateResources)
{
    struct PassData {};

    constexpr ResourceID kOutOfRangeId = 999999u;
    const RGResourceHandle invalid{kOutOfRangeId};

    m_Graph->AddPass<PassData>("InvalidHandles",
        [invalid](PassData&, RGBuilder& builder)
        {
            builder.Read(invalid,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_READ_BIT);
            builder.Write(invalid,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            builder.WriteColor(invalid, RGAttachmentInfo{});
            builder.WriteDepth(invalid, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    const auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 1u);
    EXPECT_EQ(stats.PacketCount, 1u);

    const auto images = m_Graph->BuildDebugImageList();
    EXPECT_TRUE(images.empty());
}

TEST_F(RenderGraphPacketTest, TransferWriteThenSampleRead_SanitizesFinalizeBarrierAccess)
{
    struct PassData
    {
        RGResourceHandle Texture{};
    };

    RHI::VulkanImage importTexture(*m_Device,
                                  8,
                                  8,
                                  1,
                                  VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT);

    RGResourceHandle imported;

    m_Graph->AddPass<PassData>("Upload",
        [&](PassData& data, RGBuilder& builder)
        {
            imported = builder.ImportTexture("preview"_id,
                                             importTexture.GetHandle(),
                                             importTexture.GetView(),
                                             importTexture.GetFormat(),
                                             {8u, 8u},
                                             VK_IMAGE_LAYOUT_UNDEFINED);
            data.Texture = builder.Write(imported,
                                         VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("Finalize",
        [&](PassData& data, RGBuilder& builder)
        {
            data.Texture = builder.Read(imported,
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    const auto passes = m_Graph->BuildDebugPassList();
    ASSERT_EQ(passes.size(), 2u);
    ASSERT_EQ(passes[1].ImageBarriers.size(), 1u);

    const auto& barrier = passes[1].ImageBarriers[0];
    EXPECT_EQ(barrier.OldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EXPECT_EQ(barrier.NewLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(barrier.SrcAccessMask, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    EXPECT_EQ(barrier.DstAccessMask, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    EXPECT_EQ(barrier.DstAccessMask & VK_ACCESS_2_MEMORY_WRITE_BIT, 0u);
}

TEST_F(RenderGraphPacketTest, AttachmentWritesAndReadsTrackTransientTextureLifetime)
{
    struct PassData
    {
        RGResourceHandle Color{};
    };

    RGResourceHandle color;

    m_Graph->AddPass<PassData>("ColorWrite",
        [&color](PassData& data, RGBuilder& builder)
        {
            color = builder.CreateTexture("trackedColor"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});

            RGAttachmentInfo info{};
            info.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            info.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            data.Color = builder.WriteColor(color, info);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("ColorRead",
        [&color](PassData& data, RGBuilder& builder)
        {
            data.Color = builder.Read(color,
                                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    const auto images = m_Graph->BuildDebugImageList();
    const auto it = std::find_if(images.begin(), images.end(), [](const RenderGraphDebugImage& img)
    {
        return img.Name == "trackedColor"_id;
    });

    ASSERT_NE(it, images.end());
    EXPECT_FALSE(it->IsImported);
    EXPECT_EQ(it->StartPass, 0u);
    EXPECT_EQ(it->EndPass, 1u);
    EXPECT_EQ(it->FirstWritePass, 0u);
    EXPECT_EQ(it->LastWritePass, 0u);
    EXPECT_EQ(it->FirstReadPass, 1u);
    EXPECT_EQ(it->LastReadPass, 1u);
}

// ===========================================================================
// Raster packet merging tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Two consecutive raster passes targeting the SAME color+depth attachments
// should merge into a single packet (shared vkCmdBeginRendering scope).
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, TwoRasterPasses_SameAttachments_Merge)
{
    struct PassData {};

    RGResourceHandle color, depth;

    // Pass A creates the targets and writes them.
    m_Graph->AddPass<PassData>("RasterA",
        [&color, &depth](PassData&, RGBuilder& builder)
        {
            color = builder.CreateTexture("color"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            depth = builder.CreateTexture("depth"_id,
                RGTextureDesc{.Width = 64, .Height = 64,
                              .Format = VK_FORMAT_D32_SFLOAT,
                              .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              .Aspect = VK_IMAGE_ASPECT_DEPTH_BIT});
            builder.WriteColor(color, RGAttachmentInfo{
                .LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .StoreOp = VK_ATTACHMENT_STORE_OP_STORE});
            builder.WriteDepth(depth, RGAttachmentInfo{
                .LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .StoreOp = VK_ATTACHMENT_STORE_OP_STORE});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    // Pass B writes the same targets (load existing content, store result).
    m_Graph->AddPass<PassData>("RasterB",
        [&color, &depth](PassData&, RGBuilder& builder)
        {
            builder.WriteColor(color, RGAttachmentInfo{
                .LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .StoreOp = VK_ATTACHMENT_STORE_OP_STORE});
            builder.WriteDepth(depth, RGAttachmentInfo{
                .LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .StoreOp = VK_ATTACHMENT_STORE_OP_STORE});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 2u);
    EXPECT_EQ(stats.PacketCount, 1u);  // Same attachments → merged into 1 packet
}

// ---------------------------------------------------------------------------
// Three consecutive raster passes on the same attachments: all merge.
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, ThreeRasterPasses_SameAttachments_MergeAll)
{
    struct PassData {};

    RGResourceHandle color, depth;

    m_Graph->AddPass<PassData>("RasterA",
        [&color, &depth](PassData&, RGBuilder& builder)
        {
            color = builder.CreateTexture("color"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            depth = builder.CreateTexture("depth"_id,
                RGTextureDesc{.Width = 64, .Height = 64,
                              .Format = VK_FORMAT_D32_SFLOAT,
                              .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              .Aspect = VK_IMAGE_ASPECT_DEPTH_BIT});
            builder.WriteColor(color, RGAttachmentInfo{});
            builder.WriteDepth(depth, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("RasterB",
        [&color, &depth](PassData&, RGBuilder& builder)
        {
            builder.WriteColor(color, RGAttachmentInfo{.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD});
            builder.WriteDepth(depth, RGAttachmentInfo{.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("RasterC",
        [&color, &depth](PassData&, RGBuilder& builder)
        {
            builder.WriteColor(color, RGAttachmentInfo{.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD});
            builder.WriteDepth(depth, RGAttachmentInfo{.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 3u);
    EXPECT_EQ(stats.PacketCount, 1u);  // All three merge
}

// ---------------------------------------------------------------------------
// Raster passes with same color but different depth: no merging.
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, RasterPasses_SameColorDifferentDepth_NoMerge)
{
    struct PassData {};

    RGResourceHandle color, depthA, depthB;

    m_Graph->AddPass<PassData>("RasterA",
        [&color, &depthA](PassData&, RGBuilder& builder)
        {
            color = builder.CreateTexture("color"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            depthA = builder.CreateTexture("depthA"_id,
                RGTextureDesc{.Width = 64, .Height = 64,
                              .Format = VK_FORMAT_D32_SFLOAT,
                              .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              .Aspect = VK_IMAGE_ASPECT_DEPTH_BIT});
            builder.WriteColor(color, RGAttachmentInfo{});
            builder.WriteDepth(depthA, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("RasterB",
        [&color, &depthB](PassData&, RGBuilder& builder)
        {
            depthB = builder.CreateTexture("depthB"_id,
                RGTextureDesc{.Width = 64, .Height = 64,
                              .Format = VK_FORMAT_D32_SFLOAT,
                              .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              .Aspect = VK_IMAGE_ASPECT_DEPTH_BIT});
            builder.WriteColor(color, RGAttachmentInfo{.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD});
            builder.WriteDepth(depthB, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 2u);
    EXPECT_EQ(stats.PacketCount, 2u);  // Different depth → no merging
}

// ---------------------------------------------------------------------------
// Raster pass followed by a compute pass: no merging across types.
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, RasterThenCompute_NoMerge)
{
    struct PassData {};

    RGResourceHandle color, buf;

    m_Graph->AddPass<PassData>("Raster",
        [&color](PassData&, RGBuilder& builder)
        {
            color = builder.CreateTexture("color"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            builder.WriteColor(color, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("Compute",
        [&color, &buf](PassData&, RGBuilder& builder)
        {
            buf = builder.CreateBuffer("buf"_id, RGBufferDesc{.Size = 256});
            builder.Read(color,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_READ_BIT);
            builder.Write(buf);
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 2u);
    EXPECT_EQ(stats.PacketCount, 2u);  // Raster + compute → never merge
}

// ---------------------------------------------------------------------------
// Color-only raster passes on the same target merge.
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, TwoRasterPasses_ColorOnly_SameTarget_Merge)
{
    struct PassData {};

    RGResourceHandle color;

    m_Graph->AddPass<PassData>("RasterA",
        [&color](PassData&, RGBuilder& builder)
        {
            color = builder.CreateTexture("color"_id,
                RGTextureDesc{.Width = 64, .Height = 64, .Format = VK_FORMAT_R8G8B8A8_UNORM});
            builder.WriteColor(color, RGAttachmentInfo{});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->AddPass<PassData>("RasterB",
        [&color](PassData&, RGBuilder& builder)
        {
            builder.WriteColor(color, RGAttachmentInfo{.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD});
        },
        [](const PassData&, const RGRegistry&, VkCommandBuffer) {});

    m_Graph->Compile(0);

    auto stats = m_Graph->GetLastPacketStats();
    EXPECT_EQ(stats.PassCount, 2u);
    EXPECT_EQ(stats.PacketCount, 1u);  // Same color target → merged
}
