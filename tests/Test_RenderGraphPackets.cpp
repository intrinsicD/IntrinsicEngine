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
// Two independent raster passes: no merging (raster passes don't merge)
// ---------------------------------------------------------------------------
TEST_F(RenderGraphPacketTest, TwoRasterPasses_NoMerge)
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
    EXPECT_EQ(stats.PacketCount, 2u);  // No merging for raster passes
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
