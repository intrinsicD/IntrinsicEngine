// ARCH-024: WorkerGpuPages' mid-run input ordering, the one a K-Means restart needs after its reseed upload: only the
// next accepted page or copy waits for the issued transfers, and ordering never resumes an aborted run.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"

namespace R = Extrinsic::Runtime;
namespace RHI = Extrinsic::RHI;

namespace
{
    RHI::BufferHandle RecordNothing(RHI::ICommandContext&)
    {
        return {};
    }
}

// Every page is accepted and completes at once (the mock's default wait), its empty bytes taken. Predictions: the
// run's first page waits for the issued transfers, the second follows the lane alone; ordered again, the next page
// waits once more and the copy after it follows the lane alone. Mutation: an empty OrderNextAfterIssuedTransfers makes
// the third entry LaneOrderOnly.
TEST(WorkerGpuPages, OrderingAgainAppliesToTheNextAcceptedPageOnly)
{
    Extrinsic::Tests::MockDevice device;
    device.ComputeReadback = [](auto, std::uint64_t, auto)
    {
        return RHI::ReadbackToken{1u};
    };
    device.TransferQueue.TakeReadback = [](auto, auto)
    {
        return true;
    };
    R::WorkerGpuPages pages{device};
    ASSERT_EQ(pages.Run(RecordNothing, {}), R::WorkerGpuPageStatus::Complete);
    ASSERT_EQ(pages.Run(RecordNothing, {}), R::WorkerGpuPageStatus::Complete);
    pages.OrderNextAfterIssuedTransfers();
    ASSERT_EQ(pages.Run(RecordNothing, {}), R::WorkerGpuPageStatus::Complete);
    ASSERT_TRUE(pages.SubmitCopy(RecordNothing).IsValid());
    using Order = RHI::ComputeInputOrder;
    const std::vector<Order> expected{
        Order::AfterIssuedTransfers, Order::LaneOrderOnly, Order::AfterIssuedTransfers, Order::LaneOrderOnly};
    EXPECT_EQ(device.ComputeInputOrders, expected);
    EXPECT_EQ(pages.Submitted(), 4u);
    EXPECT_FALSE(pages.Aborted());
}

// An owner's abort racing a reseed: the worker orders its next page after the abort. Predictions: the run stays
// aborted, its next page and copy are not submitted, the device sees no submission. Mutation: ordering through
// ClearAbort resumes the run (its page reaches the device, which refuses it).
TEST(WorkerGpuPages, OrderingAnAbortedRunKeepsItAbortedAndSubmitsNothing)
{
    Extrinsic::Tests::MockDevice device;
    R::WorkerGpuPages pages{device};
    pages.Abort();
    pages.OrderNextAfterIssuedTransfers();
    EXPECT_TRUE(pages.Aborted());
    EXPECT_EQ(pages.Run(RecordNothing, {}), R::WorkerGpuPageStatus::Aborted);
    EXPECT_FALSE(pages.SubmitCopy(RecordNothing).IsValid());
    EXPECT_TRUE(device.ComputeInputOrders.empty());
    EXPECT_EQ(pages.Submitted(), 0u);
}
