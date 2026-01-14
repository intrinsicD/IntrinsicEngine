#include <gtest/gtest.h>
#include <memory>

#include "RHI.Vulkan.hpp"

import RHI;
import Core;

namespace {

class DescriptorAllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        RHI::ContextConfig config{
            .AppName = "DescriptorAllocatorTest",
            .EnableValidation = true,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(config);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);

        m_Layout = std::make_unique<RHI::DescriptorLayout>(m_Device);
        ASSERT_TRUE(m_Layout->IsValid());

        m_Allocator = std::make_unique<RHI::DescriptorAllocator>(m_Device);
        ASSERT_TRUE(m_Allocator->IsValid());
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;

    std::unique_ptr<RHI::DescriptorLayout> m_Layout;
    std::unique_ptr<RHI::DescriptorAllocator> m_Allocator;
};

TEST_F(DescriptorAllocatorTest, GrowsPoolsAndAllocatesManySets)
{
    // Force pool growth by allocating more sets than a single pool's maxSets.
    // The allocator should transparently create additional pools when exhausted.
    constexpr uint32_t kAllocCount = 10'000;

    for (uint32_t i = 0; i < kAllocCount; ++i)
    {
        VkDescriptorSet set = m_Allocator->Allocate(m_Layout->GetHandle());
        ASSERT_NE(set, VK_NULL_HANDLE) << "Allocation failed at i=" << i;
    }
}

TEST_F(DescriptorAllocatorTest, ResetRecyclesPoolsAndAllocationsStillSucceed)
{
    constexpr uint32_t kAllocCount = 6'000;

    for (uint32_t i = 0; i < kAllocCount; ++i)
    {
        VkDescriptorSet set = m_Allocator->Allocate(m_Layout->GetHandle());
        ASSERT_NE(set, VK_NULL_HANDLE) << "Pre-reset allocation failed at i=" << i;
    }

    // Reset at frame start: pools must be reset and reused.
    m_Allocator->Reset();

    for (uint32_t i = 0; i < kAllocCount; ++i)
    {
        VkDescriptorSet set = m_Allocator->Allocate(m_Layout->GetHandle());
        ASSERT_NE(set, VK_NULL_HANDLE) << "Post-reset allocation failed at i=" << i;
    }
}

} // namespace

