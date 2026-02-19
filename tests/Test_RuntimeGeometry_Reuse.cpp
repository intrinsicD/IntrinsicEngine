module;

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include "RHI.Vulkan.hpp"

import Graphics;
import RHI;

namespace
{
    class GeometryReuseTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            RHI::ContextConfig ctxConfig{
                .AppName = "GeometryReuseTest",
                .EnableValidation = true,
                .Headless = true,
            };

            m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
            m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
            m_TransferManager = std::make_unique<RHI::TransferManager>(*m_Device);

            m_Pool.Initialize(64);
        }

        void TearDown() override
        {
            m_TransferManager.reset();
            if (m_Device)
                m_Device->FlushAllDeletionQueues();
            m_Device.reset();
            m_Context.reset();
        }

        Graphics::GeometryPool m_Pool;
        std::unique_ptr<RHI::VulkanContext> m_Context;
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        std::unique_ptr<RHI::TransferManager> m_TransferManager;
    };
}

TEST_F(GeometryReuseTest, ReuseSharesVertexBufferAndCreatesUniqueIndexBuffer)
{
    std::vector<glm::vec3> positions = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
    std::vector<glm::vec3> normals = { {0, 0, 1}, {0, 0, 1}, {0, 0, 1} };
    std::vector<glm::vec4> aux = { {0, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0} };
    std::vector<uint32_t> tri = { 0, 1, 2 };

    // Base geometry (allocates and uploads vertex + index buffers)
    Graphics::GeometryUploadRequest req1;
    req1.Positions = positions;
    req1.Normals = normals;
    req1.Aux = aux;
    req1.Indices = tri;
    req1.Topology = Graphics::PrimitiveTopology::Triangles;
    req1.UploadMode = Graphics::GeometryUploadMode::Staged;

    auto [gpu1, t1] = Graphics::GeometryGpuData::CreateAsync(m_Device, *m_TransferManager, req1, &m_Pool);
    ASSERT_NE(gpu1, nullptr);
    ASSERT_NE(gpu1->GetVertexBuffer(), nullptr);
    ASSERT_NE(gpu1->GetIndexBuffer(), nullptr);

    const VkBuffer vb1 = gpu1->GetVertexBuffer()->GetHandle();
    const VkBuffer ib1 = gpu1->GetIndexBuffer()->GetHandle();

    const auto h1 = m_Pool.Add(std::move(gpu1));
    ASSERT_TRUE(h1.IsValid());

    // Derived view: reuse vertices, different topology + indices.
    std::vector<uint32_t> line = { 0, 1 };

    Graphics::GeometryUploadRequest req2;
    req2.ReuseVertexBuffersFrom = h1;
    req2.Indices = line;
    req2.Topology = Graphics::PrimitiveTopology::Lines;
    req2.UploadMode = Graphics::GeometryUploadMode::Staged;

    auto [gpu2, t2] = Graphics::GeometryGpuData::CreateAsync(m_Device, *m_TransferManager, req2, &m_Pool);
    ASSERT_NE(gpu2, nullptr);
    ASSERT_NE(gpu2->GetVertexBuffer(), nullptr);
    ASSERT_NE(gpu2->GetIndexBuffer(), nullptr);

    const VkBuffer vb2 = gpu2->GetVertexBuffer()->GetHandle();
    const VkBuffer ib2 = gpu2->GetIndexBuffer()->GetHandle();

    // The heavy vertex memory is shared.
    EXPECT_EQ(vb1, vb2);

    // Indices are unique per view.
    EXPECT_NE(ib1, ib2);

    // Layout is inherited (except topology, which is view-specific).
    const auto* source = m_Pool.GetUnchecked(h1);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(gpu2->GetLayout().PositionsOffset, source->GetLayout().PositionsOffset);
    EXPECT_EQ(gpu2->GetLayout().PositionsSize, source->GetLayout().PositionsSize);
    EXPECT_EQ(gpu2->GetTopology(), Graphics::PrimitiveTopology::Lines);
}
