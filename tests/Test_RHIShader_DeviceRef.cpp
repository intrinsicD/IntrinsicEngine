#include <gtest/gtest.h>

import RHI;

TEST(RHIShader, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::ShaderModule, RHI::VulkanDevice&, const std::string&, RHI::ShaderStage>);
    static_assert(!std::is_constructible_v<RHI::ShaderModule, std::shared_ptr<RHI::VulkanDevice>, const std::string&, RHI::ShaderStage>);
    SUCCEED();
}

