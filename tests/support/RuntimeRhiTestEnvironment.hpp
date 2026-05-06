#pragma once

#include <gtest/gtest.h>

#include <memory>

namespace Intrinsic::Tests
{
    class RuntimeRhiTestEnvironment
    {
    public:
        RuntimeRhiTestEnvironment(const RuntimeRhiTestEnvironment&) = delete;
        RuntimeRhiTestEnvironment& operator=(const RuntimeRhiTestEnvironment&) = delete;

        static RuntimeRhiTestEnvironment& Get()
        {
            static RuntimeRhiTestEnvironment environment{};
            return environment;
        }

        [[nodiscard]] ::testing::AssertionResult CheckAvailable()
        {
            EnsureContext();
            if (!m_Context || m_Context->GetInstance() == VK_NULL_HANDLE)
            {
                return ::testing::AssertionFailure() << "No Vulkan instance available (headless environment)";
            }

            EnsureDevice();
            if (!m_Device || !m_Device->IsValid())
            {
                return ::testing::AssertionFailure() << "No suitable GPU found";
            }

            return ::testing::AssertionSuccess();
        }

        [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> Device()
        {
            EnsureDevice();
            return m_Device;
        }

    private:
        RuntimeRhiTestEnvironment() = default;

        ~RuntimeRhiTestEnvironment()
        {
            if (m_Device && m_Device->IsValid() && m_Device->GetLogicalDevice() != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_Device->GetLogicalDevice());
            }
            m_Device.reset();
            m_Context.reset();
        }

        void EnsureContext()
        {
            if (m_Context)
            {
                return;
            }

            RHI::ContextConfig config{
                .AppName = "RuntimeRhiTestEnvironment",
                .EnableValidation = true,
                .Headless = true,
            };
            m_Context = std::make_unique<RHI::VulkanContext>(config);
        }

        void EnsureDevice()
        {
            EnsureContext();
            if (m_Device || !m_Context || m_Context->GetInstance() == VK_NULL_HANDLE)
            {
                return;
            }

            m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
        }

        std::unique_ptr<RHI::VulkanContext> m_Context{};
        std::shared_ptr<RHI::VulkanDevice> m_Device{};
    };
}
