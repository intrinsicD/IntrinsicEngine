#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Backends.Null;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;

#include "MockRHI.hpp"

namespace
{
    namespace RHI = Extrinsic::RHI;

    struct NullProfilerHarness
    {
        std::unique_ptr<RHI::IDevice> Device =
            Extrinsic::Backends::Null::CreateNullDevice();

        [[nodiscard]] RHI::IProfiler& Profiler() const
        {
            return *Device->GetProfiler();
        }

        [[nodiscard]] RHI::ICommandContext& Context(
            const RHI::QueueAffinity queue = RHI::QueueAffinity::Graphics)
            const
        {
            return Device->GetQueueContext(queue, 0u);
        }
    };

    [[nodiscard]] std::span<const RHI::ProfilerScopeDesc>
    ScopeSpan(const std::vector<RHI::ProfilerScopeDesc>& scopes)
    {
        return std::span<const RHI::ProfilerScopeDesc>{scopes};
    }
}

TEST(ProfilerContract, NullReportsContractOnlyScopeAndAcceptedQueueProvenance)
{
    NullProfilerHarness harness{};
    RHI::IProfiler& profiler = harness.Profiler();
    ASSERT_NE(harness.Device->GetProfiler(), nullptr);

    const RHI::ProfilerStatusSnapshot status = profiler.GetStatus();
    EXPECT_EQ(status.Status, RHI::ProfilerBackendStatus::ContractOnly);
    EXPECT_EQ(status.Source, RHI::GpuTimestampSource::ContractOnly);
    EXPECT_FALSE(status.NativeTimestampsAvailable());
    EXPECT_FALSE(status.Diagnostic.empty());

    const RHI::ProfilerFrameKey frame{
        .FrameNumber = 41u,
        .FrameSlot = 1u,
    };
    const std::vector<RHI::ProfilerScopeDesc> scopes{
        RHI::ProfilerScopeDesc{
            .Ordinal = 7u,
            .Name = "Opaque",
            .Queue = RHI::QueueAffinity::Graphics,
        },
        RHI::ProfilerScopeDesc{
            .Ordinal = 9u,
            .Name = "Lighting",
            .Queue = RHI::QueueAffinity::AsyncCompute,
        },
    };

    const auto plan = profiler.BeginFrame(frame, ScopeSpan(scopes));
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->Frame, frame);
    ASSERT_EQ(plan->ScopeTokens.size(), scopes.size());
    EXPECT_TRUE(plan->ScopeTokens[0].IsValid());
    EXPECT_TRUE(plan->ScopeTokens[1].IsValid());
    EXPECT_NE(plan->ScopeTokens[0], plan->ScopeTokens[1]);

    RHI::ICommandContext& graphics =
        harness.Context(RHI::QueueAffinity::Graphics);
    RHI::ICommandContext& asyncCompute =
        harness.Context(RHI::QueueAffinity::AsyncCompute);
    ASSERT_TRUE(
        profiler.BeginQueue(graphics, RHI::QueueAffinity::Graphics)
            .has_value());
    ASSERT_TRUE(
        profiler.BeginQueue(asyncCompute, RHI::QueueAffinity::AsyncCompute)
            .has_value());
    ASSERT_TRUE(
        profiler.BeginScope(graphics, plan->ScopeTokens[0]).has_value());
    ASSERT_TRUE(
        profiler.EndScope(graphics, plan->ScopeTokens[0]).has_value());
    ASSERT_TRUE(
        profiler.BeginScope(asyncCompute, plan->ScopeTokens[1]).has_value());
    ASSERT_TRUE(
        profiler.EndScope(asyncCompute, plan->ScopeTokens[1]).has_value());
    ASSERT_TRUE(
        profiler.EndQueue(graphics, RHI::QueueAffinity::Graphics).has_value());
    ASSERT_TRUE(
        profiler.EndQueue(asyncCompute, RHI::QueueAffinity::AsyncCompute)
            .has_value());
    ASSERT_TRUE(
        profiler.EndFrame(frame, RHI::ProfilerFrameDisposition::Submitted)
            .has_value());

    const auto resolved = profiler.Resolve(frame);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->Frame, frame);
    EXPECT_EQ(resolved->Source, RHI::GpuTimestampSource::ContractOnly);
    ASSERT_EQ(resolved->QueueEnvelopes.size(), 2u);
    EXPECT_EQ(
        resolved->QueueEnvelopes[0].Queue,
        RHI::QueueAffinity::Graphics);
    EXPECT_FALSE(resolved->QueueEnvelopes[0].DurationNs.has_value());
    EXPECT_EQ(
        resolved->QueueEnvelopes[1].Queue,
        RHI::QueueAffinity::AsyncCompute);
    EXPECT_FALSE(resolved->QueueEnvelopes[1].DurationNs.has_value());

    ASSERT_EQ(resolved->Scopes.size(), scopes.size());
    EXPECT_EQ(resolved->Scopes[0].Ordinal, 7u);
    EXPECT_EQ(resolved->Scopes[0].Name, "Opaque");
    EXPECT_EQ(resolved->Scopes[0].Queue, RHI::QueueAffinity::Graphics);
    EXPECT_FALSE(resolved->Scopes[0].DurationNs.has_value());
    EXPECT_EQ(resolved->Scopes[1].Ordinal, 9u);
    EXPECT_EQ(resolved->Scopes[1].Name, "Lighting");
    EXPECT_EQ(
        resolved->Scopes[1].Queue,
        RHI::QueueAffinity::AsyncCompute);
    EXPECT_FALSE(resolved->Scopes[1].DurationNs.has_value());
}

TEST(ProfilerContract, TypedTokensAndQueueLifecycleFailClosed)
{
    NullProfilerHarness harness{};
    RHI::IProfiler& profiler = harness.Profiler();
    RHI::ICommandContext& context = harness.Context();

    const RHI::ProfilerFrameKey frame{
        .FrameNumber = 1u,
        .FrameSlot = 0u,
    };
    const std::vector<RHI::ProfilerScopeDesc> scopes{
        RHI::ProfilerScopeDesc{
            .Ordinal = 0u,
            .Name = "Only",
            .Queue = RHI::QueueAffinity::Graphics,
        },
    };
    const auto plan = profiler.BeginFrame(frame, ScopeSpan(scopes));
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->ScopeTokens.size(), 1u);
    EXPECT_FALSE(RHI::ProfilerScopeToken{}.IsValid());

    const auto overlappingPlan = profiler.BeginFrame(frame, ScopeSpan(scopes));
    ASSERT_FALSE(overlappingPlan.has_value());
    EXPECT_EQ(overlappingPlan.error(), RHI::ProfilerError::InvalidState);

    const auto transfer =
        profiler.BeginQueue(context, RHI::QueueAffinity::Transfer);
    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error(), RHI::ProfilerError::Unsupported);

    constexpr RHI::QueueAffinity invalidQueue =
        static_cast<RHI::QueueAffinity>(0xffu);
    const auto invalid = profiler.BeginQueue(context, invalidQueue);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), RHI::ProfilerError::InvalidArgument);

    const auto beforeQueue =
        profiler.BeginScope(context, plan->ScopeTokens[0]);
    ASSERT_FALSE(beforeQueue.has_value());
    EXPECT_EQ(beforeQueue.error(), RHI::ProfilerError::InvalidState);

    ASSERT_TRUE(
        profiler.BeginQueue(context, RHI::QueueAffinity::Graphics)
            .has_value());
    const auto invalidToken =
        profiler.BeginScope(context, RHI::ProfilerScopeToken{});
    ASSERT_FALSE(invalidToken.has_value());
    EXPECT_EQ(invalidToken.error(), RHI::ProfilerError::InvalidState);

    ASSERT_TRUE(
        profiler.BeginScope(context, plan->ScopeTokens[0]).has_value());
    const auto sealWithOpenRecording =
        profiler.EndFrame(frame, RHI::ProfilerFrameDisposition::Submitted);
    ASSERT_FALSE(sealWithOpenRecording.has_value());
    EXPECT_EQ(
        sealWithOpenRecording.error(),
        RHI::ProfilerError::InvalidState);

    const auto duplicateBegin =
        profiler.BeginScope(context, plan->ScopeTokens[0]);
    ASSERT_FALSE(duplicateBegin.has_value());
    EXPECT_EQ(duplicateBegin.error(), RHI::ProfilerError::InvalidState);

    const auto closeWithOpenScope =
        profiler.EndQueue(context, RHI::QueueAffinity::Graphics);
    ASSERT_FALSE(closeWithOpenScope.has_value());
    EXPECT_EQ(
        closeWithOpenScope.error(),
        RHI::ProfilerError::InvalidState);

    ASSERT_TRUE(
        profiler.EndScope(context, plan->ScopeTokens[0]).has_value());
    const auto duplicateEnd =
        profiler.EndScope(context, plan->ScopeTokens[0]);
    ASSERT_FALSE(duplicateEnd.has_value());
    EXPECT_EQ(duplicateEnd.error(), RHI::ProfilerError::InvalidState);

    ASSERT_TRUE(
        profiler.EndQueue(context, RHI::QueueAffinity::Graphics)
            .has_value());

    const auto endAfterQueueClose =
        profiler.EndScope(context, plan->ScopeTokens[0]);
    ASSERT_FALSE(endAfterQueueClose.has_value());
    EXPECT_EQ(
        endAfterQueueClose.error(),
        RHI::ProfilerError::InvalidState);

    ASSERT_TRUE(
        profiler.EndFrame(frame, RHI::ProfilerFrameDisposition::Submitted)
            .has_value());
    const auto duplicateSeal =
        profiler.EndFrame(frame, RHI::ProfilerFrameDisposition::Submitted);
    ASSERT_FALSE(duplicateSeal.has_value());
    EXPECT_EQ(duplicateSeal.error(), RHI::ProfilerError::InvalidState);
}

TEST(ProfilerContract, FrameIdentityRetainsOtherSlotResultAndRetiresReusedSlot)
{
    NullProfilerHarness harness{};
    RHI::IProfiler& profiler = harness.Profiler();
    const std::span<const RHI::ProfilerScopeDesc> noScopes{};

    const RHI::ProfilerFrameKey first{
        .FrameNumber = 100u,
        .FrameSlot = 0u,
    };
    ASSERT_TRUE(profiler.BeginFrame(first, noScopes).has_value());
    ASSERT_TRUE(
        profiler.EndFrame(first, RHI::ProfilerFrameDisposition::Submitted)
            .has_value());
    ASSERT_TRUE(profiler.Resolve(first).has_value());

    const RHI::ProfilerFrameKey otherSlot{
        .FrameNumber = 101u,
        .FrameSlot = 1u,
    };
    ASSERT_TRUE(profiler.BeginFrame(otherSlot, noScopes).has_value());
    EXPECT_TRUE(profiler.Resolve(first).has_value());
    const auto otherNotReady = profiler.Resolve(otherSlot);
    ASSERT_FALSE(otherNotReady.has_value());
    EXPECT_EQ(otherNotReady.error(), RHI::ProfilerError::NotReady);
    ASSERT_TRUE(
        profiler.EndFrame(
                    otherSlot,
                    RHI::ProfilerFrameDisposition::Discarded)
            .has_value());
    EXPECT_TRUE(profiler.Resolve(first).has_value());
    EXPECT_FALSE(profiler.Resolve(otherSlot).has_value());

    const RHI::ProfilerFrameKey reusedSlot{
        .FrameNumber = 102u,
        .FrameSlot = 0u,
    };
    ASSERT_TRUE(profiler.BeginFrame(reusedSlot, noScopes).has_value());
    const auto retired = profiler.Resolve(first);
    ASSERT_FALSE(retired.has_value());
    EXPECT_EQ(retired.error(), RHI::ProfilerError::NotReady);
    ASSERT_TRUE(
        profiler.EndFrame(
                    reusedSlot,
                    RHI::ProfilerFrameDisposition::Discarded)
            .has_value());
    EXPECT_FALSE(profiler.Resolve(reusedSlot).has_value());
}

TEST(ProfilerContract, DiscardReleasesOpenRecordingWithoutPublishing)
{
    NullProfilerHarness harness{};
    RHI::IProfiler& profiler = harness.Profiler();
    RHI::ICommandContext& context = harness.Context();
    const RHI::ProfilerFrameKey discarded{
        .FrameNumber = 8u,
        .FrameSlot = 0u,
    };
    const std::vector<RHI::ProfilerScopeDesc> scopes{
        RHI::ProfilerScopeDesc{
            .Ordinal = 4u,
            .Name = "Interrupted",
            .Queue = RHI::QueueAffinity::Graphics,
        },
    };

    const auto plan = profiler.BeginFrame(discarded, ScopeSpan(scopes));
    ASSERT_TRUE(plan.has_value());
    ASSERT_TRUE(
        profiler.BeginQueue(context, RHI::QueueAffinity::Graphics)
            .has_value());
    ASSERT_TRUE(
        profiler.BeginScope(context, plan->ScopeTokens[0]).has_value());
    ASSERT_TRUE(
        profiler.EndFrame(
                    discarded,
                    RHI::ProfilerFrameDisposition::Discarded)
            .has_value());

    const auto missing = profiler.Resolve(discarded);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), RHI::ProfilerError::NotReady);

    const RHI::ProfilerFrameKey next{
        .FrameNumber = 9u,
        .FrameSlot = 1u,
    };
    EXPECT_TRUE(
        profiler.BeginFrame(
                    next,
                    std::span<const RHI::ProfilerScopeDesc>{})
            .has_value());
    EXPECT_TRUE(
        profiler.EndFrame(next, RHI::ProfilerFrameDisposition::Discarded)
            .has_value());
}

TEST(ProfilerContract, PlanningRejectsInvalidDescriptorsAndExhaustion)
{
    NullProfilerHarness harness{};
    RHI::IProfiler& profiler = harness.Profiler();

    const auto invalidSlot = profiler.BeginFrame(
        RHI::ProfilerFrameKey{
            .FrameNumber = 1u,
            .FrameSlot = profiler.GetFramesInFlight(),
        },
        std::span<const RHI::ProfilerScopeDesc>{});
    ASSERT_FALSE(invalidSlot.has_value());
    EXPECT_EQ(invalidSlot.error(), RHI::ProfilerError::InvalidArgument);

    for (const RHI::ProfilerScopeDesc& invalidDescriptor :
         {
             RHI::ProfilerScopeDesc{
                 .Ordinal = 0u,
                 .Name = "",
                 .Queue = RHI::QueueAffinity::Graphics,
             },
             RHI::ProfilerScopeDesc{
                 .Ordinal = 0u,
                 .Name = "Transfer",
                 .Queue = RHI::QueueAffinity::Transfer,
             },
             RHI::ProfilerScopeDesc{
                 .Ordinal = 0u,
                 .Name = "Invalid",
                 .Queue = static_cast<RHI::QueueAffinity>(0xffu),
             },
         })
    {
        const std::array descriptors{invalidDescriptor};
        const auto rejected = profiler.BeginFrame(
            RHI::ProfilerFrameKey{
                .FrameNumber = 2u,
                .FrameSlot = 0u,
            },
            std::span<const RHI::ProfilerScopeDesc>{descriptors});
        ASSERT_FALSE(rejected.has_value());
        if (invalidDescriptor.Queue == RHI::QueueAffinity::Transfer)
        {
            EXPECT_EQ(rejected.error(), RHI::ProfilerError::Unsupported);
        }
        else
        {
            EXPECT_EQ(
                rejected.error(),
                RHI::ProfilerError::InvalidArgument);
        }
    }

    const std::array duplicateOrdinals{
        RHI::ProfilerScopeDesc{
            .Ordinal = 3u,
            .Name = "First",
            .Queue = RHI::QueueAffinity::Graphics,
        },
        RHI::ProfilerScopeDesc{
            .Ordinal = 3u,
            .Name = "Second",
            .Queue = RHI::QueueAffinity::AsyncCompute,
        },
    };
    const auto duplicate = profiler.BeginFrame(
        RHI::ProfilerFrameKey{
            .FrameNumber = 3u,
            .FrameSlot = 0u,
        },
        std::span<const RHI::ProfilerScopeDesc>{duplicateOrdinals});
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), RHI::ProfilerError::InvalidArgument);

    std::vector<RHI::ProfilerScopeDesc> exhausted;
    exhausted.reserve(RHI::kMaxTimestampScopesPerFrame + 1u);
    for (std::uint32_t index = 0;
         index <= RHI::kMaxTimestampScopesPerFrame;
         ++index)
    {
        exhausted.push_back(RHI::ProfilerScopeDesc{
            .Ordinal = index,
            .Name = "Scope" + std::to_string(index),
            .Queue = RHI::QueueAffinity::Graphics,
        });
    }
    const auto tooMany = profiler.BeginFrame(
        RHI::ProfilerFrameKey{
            .FrameNumber = 4u,
            .FrameSlot = 0u,
        },
        ScopeSpan(exhausted));
    ASSERT_FALSE(tooMany.has_value());
    EXPECT_EQ(tooMany.error(), RHI::ProfilerError::Exhausted);
}

TEST(ProfilerContract, PreplannedDistinctTokensRecordInParallelWithoutLocking)
{
    constexpr std::size_t kScopeCount = 16u;

    NullProfilerHarness harness{};
    RHI::IProfiler& profiler = harness.Profiler();
    std::vector<RHI::ProfilerScopeDesc> scopes;
    scopes.reserve(kScopeCount);
    for (std::uint32_t index = 0u; index < kScopeCount; ++index)
    {
        scopes.push_back(RHI::ProfilerScopeDesc{
            .Ordinal = index,
            .Name = "Parallel" + std::to_string(index),
            .Queue = RHI::QueueAffinity::Graphics,
        });
    }

    const RHI::ProfilerFrameKey frame{
        .FrameNumber = 22u,
        .FrameSlot = 0u,
    };
    const auto plan = profiler.BeginFrame(frame, ScopeSpan(scopes));
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->ScopeTokens.size(), kScopeCount);

    RHI::ICommandContext& queueContext = harness.Context();
    ASSERT_TRUE(
        profiler.BeginQueue(queueContext, RHI::QueueAffinity::Graphics)
            .has_value());

    std::array<Extrinsic::Tests::MockCommandContext, kScopeCount> contexts{};
    std::atomic<std::uint32_t> failures{0u};
    std::vector<std::thread> workers;
    workers.reserve(kScopeCount);
    for (std::size_t index = 0; index < kScopeCount; ++index)
    {
        workers.emplace_back(
            [&, index]
            {
                if (!profiler
                         .BeginScope(
                             contexts[index],
                             plan->ScopeTokens[index])
                         .has_value())
                {
                    failures.fetch_add(1u, std::memory_order_relaxed);
                    return;
                }
                if (!profiler
                         .EndScope(
                             contexts[index],
                             plan->ScopeTokens[index])
                         .has_value())
                {
                    failures.fetch_add(1u, std::memory_order_relaxed);
                }
            });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0u);
    ASSERT_TRUE(
        profiler.EndQueue(queueContext, RHI::QueueAffinity::Graphics)
            .has_value());
    ASSERT_TRUE(
        profiler.EndFrame(frame, RHI::ProfilerFrameDisposition::Submitted)
            .has_value());
    const auto resolved = profiler.Resolve(frame);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->Scopes.size(), kScopeCount);
}
