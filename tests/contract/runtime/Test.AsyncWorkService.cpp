#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <utility>

import Extrinsic.Core.Error;
import Extrinsic.Runtime.AsyncWorkService;

namespace Runtime = Extrinsic::Runtime;
namespace Core = Extrinsic::Core;

TEST(RuntimeAsyncWorkService, ShutdownDrainsDerivedReadbackBeforeReturn)
{
    Runtime::AsyncWorkService asyncWork{};
    asyncWork.Initialize();

    std::atomic<std::uint32_t> workerCalls{0u};
    std::atomic<std::uint32_t> applyCalls{0u};
    Runtime::DerivedJobDesc readbackJob{
        .Name = "shutdown readback drain",
        .IsReadbackJob = true,
        .ReadbackByteSize = 4u,
        .Execute = [&workerCalls]() -> Runtime::DerivedJobWorkerResult
        {
            workerCalls.fetch_add(1u, std::memory_order_relaxed);
            return Runtime::DerivedJobOutput{
                .PayloadToken = 76u,
                .Diagnostic = "worker complete; readback ready",
            };
        },
        .IsReadbackReady = []()
        {
            return true;
        },
        .ApplyOnMainThread =
            [&applyCalls](Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            applyCalls.fetch_add(1u, std::memory_order_relaxed);
            return Core::Ok();
        },
    };

    const Runtime::DerivedJobHandle handle =
        asyncWork.SubmitDerivedJob(std::move(readbackJob));
    ASSERT_TRUE(handle.IsValid());

    asyncWork.PumpBackground(1u);
    asyncWork.ShutdownAndDrain();

    EXPECT_EQ(workerCalls.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 1u);
    ASSERT_NE(asyncWork.DerivedJobs(), nullptr);
    EXPECT_EQ(asyncWork.DerivedJobs()->GetStatus(handle),
              Runtime::DerivedJobStatus::Complete);

    asyncWork.Reset();
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 1u);
}

TEST(RuntimeAsyncWorkService, ShutdownCancelsUnreadiedDerivedReadback)
{
    Runtime::AsyncWorkService asyncWork{};
    asyncWork.Initialize();

    std::atomic<bool> readbackReady{false};
    std::atomic<std::uint32_t> applyCalls{0u};
    Runtime::DerivedJobDesc readbackJob{
        .Name = "shutdown unreadied readback",
        .IsReadbackJob = true,
        .ReadbackByteSize = 4u,
        .Execute = []() -> Runtime::DerivedJobWorkerResult
        {
            return Runtime::DerivedJobOutput{.PayloadToken = 77u};
        },
        .IsReadbackReady = [&readbackReady]()
        {
            return readbackReady.load(std::memory_order_relaxed);
        },
        .ApplyOnMainThread =
            [&applyCalls](Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            applyCalls.fetch_add(1u, std::memory_order_relaxed);
            return Core::Ok();
        },
    };

    const Runtime::DerivedJobHandle handle =
        asyncWork.SubmitDerivedJob(std::move(readbackJob));
    ASSERT_TRUE(handle.IsValid());

    asyncWork.PumpBackground(1u);
    asyncWork.ShutdownAndDrain();

    ASSERT_NE(asyncWork.DerivedJobs(), nullptr);
    EXPECT_EQ(asyncWork.DerivedJobs()->GetStatus(handle),
              Runtime::DerivedJobStatus::Cancelled);
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 0u);

    readbackReady.store(true, std::memory_order_relaxed);
    asyncWork.DrainCompletions();
    EXPECT_EQ(asyncWork.ApplyMainThreadResults(1u), 0u);
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 0u);

    asyncWork.Reset();
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 0u);
}
