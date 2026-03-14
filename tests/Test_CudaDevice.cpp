#include <gtest/gtest.h>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <type_traits>

import RHI;

// =============================================================================
// CudaError — always-available domain error type (no CUDA runtime needed)
// =============================================================================

TEST(CudaError, ErrorCodeToStringCoversAllValues)
{
    // Verify that all defined error codes produce meaningful strings.
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::Success), "Success");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::DriverNotFound), "DriverNotFound");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::NoDevice), "NoDevice");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::InvalidDevice), "InvalidDevice");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::ContextCreateFailed), "ContextCreateFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::NotInitialized), "NotInitialized");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::OutOfMemory), "OutOfMemory");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::InvalidPointer), "InvalidPointer");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::AlreadyFreed), "AlreadyFreed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::StreamCreateFailed), "StreamCreateFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::ModuleLoadFailed), "ModuleLoadFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::KernelNotFound), "KernelNotFound");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::LaunchFailed), "LaunchFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::InvalidKernelArgs), "InvalidKernelArgs");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::ExternalMemoryImportFailed), "ExternalMemoryImportFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::ExternalSemaphoreImportFailed), "ExternalSemaphoreImportFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::SyncFailed), "SyncFailed");
    EXPECT_EQ(RHI::CudaErrorToString(RHI::CudaError::Unknown), "Unknown");
}

TEST(CudaError, CudaExpectedTypeAlias)
{
    // CudaExpected<T> should be std::expected<T, CudaError>.
    static_assert(std::is_same_v<
        RHI::CudaExpected<int>,
        std::expected<int, RHI::CudaError>
    >);

    // Verify basic monadic usage compiles.
    RHI::CudaExpected<int> ok{42};
    EXPECT_TRUE(ok.has_value());
    EXPECT_EQ(*ok, 42);

    RHI::CudaExpected<int> err{std::unexpected(RHI::CudaError::OutOfMemory)};
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), RHI::CudaError::OutOfMemory);
}

TEST(CudaError, EnumValuesAreDistinct)
{
    // Sanity check: no accidental value collisions between error categories.
    EXPECT_NE(static_cast<uint32_t>(RHI::CudaError::DriverNotFound),
              static_cast<uint32_t>(RHI::CudaError::OutOfMemory));
    EXPECT_NE(static_cast<uint32_t>(RHI::CudaError::StreamCreateFailed),
              static_cast<uint32_t>(RHI::CudaError::ExternalMemoryImportFailed));
}

// =============================================================================
// CudaDevice — compile-time contract tests
// (CudaDevice module only available when INTRINSIC_HAS_CUDA is defined)
// =============================================================================

#ifdef INTRINSIC_HAS_CUDA

TEST(CudaDevice, NonCopyableNonMovable)
{
    static_assert(!std::is_copy_constructible_v<RHI::CudaDevice>);
    static_assert(!std::is_copy_assignable_v<RHI::CudaDevice>);
    static_assert(!std::is_move_constructible_v<RHI::CudaDevice>);
    static_assert(!std::is_move_assignable_v<RHI::CudaDevice>);
    SUCCEED();
}

TEST(CudaDevice, CreateReturnsExpected)
{
    // Create() should return std::expected<unique_ptr<CudaDevice>, CudaError>.
    static_assert(std::is_same_v<
        decltype(RHI::CudaDevice::Create()),
        std::expected<std::unique_ptr<RHI::CudaDevice>, RHI::CudaError>
    >);
    SUCCEED();
}

TEST(CudaDevice, BufferHandleBoolConversion)
{
    RHI::CudaBufferHandle null{};
    EXPECT_FALSE(static_cast<bool>(null));

    RHI::CudaBufferHandle valid{1234, 256};
    EXPECT_TRUE(static_cast<bool>(valid));
}

// Runtime test: only runs if CUDA driver is actually available.
TEST(CudaDevice, CreateAndQueryProperties)
{
    auto result = RHI::CudaDevice::Create();
    if (!result)
    {
        // No CUDA driver or no device — this is expected in CI/containers.
        GTEST_SKIP() << "CUDA not available: "
                     << RHI::CudaErrorToString(result.error());
    }

    auto& device = *result;
    EXPECT_GT(device->GetTotalMemory(), 0u);
    EXPECT_GT(device->GetComputeCapabilityMajor(), 0);

    std::string_view name = device->GetDeviceName();
    EXPECT_FALSE(name.empty());
}

TEST(CudaDevice, AllocateAndFreeBuffer)
{
    auto result = RHI::CudaDevice::Create();
    if (!result)
        GTEST_SKIP() << "CUDA not available";

    auto& device = *result;

    // Allocate a small buffer.
    auto bufResult = device->AllocateBuffer(1024);
    ASSERT_TRUE(bufResult.has_value()) << RHI::CudaErrorToString(bufResult.error());

    auto handle = *bufResult;
    EXPECT_TRUE(static_cast<bool>(handle));
    EXPECT_EQ(handle.Size, 1024u);

    // Free it.
    device->FreeBuffer(handle);
    EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(CudaDevice, AllocateZeroBytesReturnsError)
{
    auto result = RHI::CudaDevice::Create();
    if (!result)
        GTEST_SKIP() << "CUDA not available";

    auto& device = *result;
    auto bufResult = device->AllocateBuffer(0);
    EXPECT_FALSE(bufResult.has_value());
}

TEST(CudaDevice, CreateAndDestroyStream)
{
    auto result = RHI::CudaDevice::Create();
    if (!result)
        GTEST_SKIP() << "CUDA not available";

    auto& device = *result;

    auto streamResult = device->CreateStream();
    ASSERT_TRUE(streamResult.has_value()) << RHI::CudaErrorToString(streamResult.error());

    // Should not crash.
    device->DestroyStream(*streamResult);
}

TEST(CudaDevice, GetFreeMemory)
{
    auto result = RHI::CudaDevice::Create();
    if (!result)
        GTEST_SKIP() << "CUDA not available";

    auto& device = *result;
    auto freeMem = device->GetFreeMemory();
    ASSERT_TRUE(freeMem.has_value());
    EXPECT_GT(*freeMem, 0u);
    EXPECT_LE(*freeMem, device->GetTotalMemory());
}

#endif // INTRINSIC_HAS_CUDA
