#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Logging;

using Extrinsic::Core::Log::Level;
using Extrinsic::Core::Log::LogEntry;
using Extrinsic::Core::Log::LogSnapshot;
using Extrinsic::Core::Log::TakeSnapshot;
using Extrinsic::Core::Log::GetEntryCount;
using Extrinsic::Core::Log::GetSequenceNumber;
using Extrinsic::Core::Log::ClearEntries;
using Extrinsic::Core::Log::Info;
using Extrinsic::Core::Log::Warn;
using Extrinsic::Core::Log::Error;

namespace
{
    // The ring buffer is process-global. All tests in this file share it.
    // We guard against flakiness from prior tests in the same executable by
    // clearing at the start and tolerating concurrent writers if any.
    void ResetLog()
    {
        ClearEntries();
    }
}

// -----------------------------------------------------------------------------
// Basic write path
// -----------------------------------------------------------------------------

TEST(CoreLogging, InfoWriteAppendsEntry)
{
    ResetLog();
    const auto seqBefore = GetSequenceNumber();
    const auto countBefore = GetEntryCount();

    Info("info-line");

    EXPECT_EQ(GetEntryCount(), countBefore + 1);
    EXPECT_GT(GetSequenceNumber(), seqBefore);

    auto snap = TakeSnapshot();
    ASSERT_FALSE(snap.Entries.empty());
    EXPECT_EQ(snap.Entries.back().Lvl, Level::Info);
    EXPECT_EQ(snap.Entries.back().Message, "info-line");
}

TEST(CoreLogging, WarnAndErrorHaveCorrectLevel)
{
    ResetLog();
    Warn("warn-line");
    Error("err-line");

    auto snap = TakeSnapshot();
    ASSERT_GE(snap.Entries.size(), 2u);

    // Find the last warn & error entries written by us. On a clean ring they
    // are the last two but we scan defensively.
    bool sawWarn = false;
    bool sawError = false;
    for (const auto& e : snap.Entries)
    {
        if (!sawWarn && e.Lvl == Level::Warning && e.Message == "warn-line")
            sawWarn = true;
        if (!sawError && e.Lvl == Level::Error && e.Message == "err-line")
            sawError = true;
    }
    EXPECT_TRUE(sawWarn);
    EXPECT_TRUE(sawError);
}

TEST(CoreLogging, FormatArgumentsInterpolate)
{
    ResetLog();
    Info("hello {} #{}", std::string("world"), 42);

    auto snap = TakeSnapshot();
    ASSERT_FALSE(snap.Entries.empty());
    EXPECT_EQ(snap.Entries.back().Message, "hello world #42");
}

// -----------------------------------------------------------------------------
// Sequence number monotonicity
// -----------------------------------------------------------------------------

TEST(CoreLogging, SequenceNumberMonotonic)
{
    ResetLog();
    const auto seq0 = GetSequenceNumber();
    Info("one");
    const auto seq1 = GetSequenceNumber();
    Info("two");
    const auto seq2 = GetSequenceNumber();
    Info("three");
    const auto seq3 = GetSequenceNumber();

    EXPECT_LT(seq0, seq1);
    EXPECT_LT(seq1, seq2);
    EXPECT_LT(seq2, seq3);
    EXPECT_EQ(seq2 - seq1, seq3 - seq2);
}

TEST(CoreLogging, ClearDoesNotResetSequence)
{
    ResetLog();
    Info("before");
    const auto seqAfterWrite = GetSequenceNumber();
    ClearEntries();
    const auto seqAfterClear = GetSequenceNumber();

    EXPECT_EQ(seqAfterWrite, seqAfterClear);
    EXPECT_EQ(GetEntryCount(), 0u);
}

// -----------------------------------------------------------------------------
// Snapshot correctness
// -----------------------------------------------------------------------------

TEST(CoreLogging, SnapshotEmptyWhenCleared)
{
    ResetLog();
    auto snap = TakeSnapshot();
    EXPECT_TRUE(snap.Entries.empty());
    // Sequence still reflects the clear/prior writes; it is monotonic.
    EXPECT_EQ(snap.Sequence, GetSequenceNumber());
}

TEST(CoreLogging, SnapshotContainsChronologicalOrder)
{
    ResetLog();
    Info("first");
    Info("second");
    Info("third");

    auto snap = TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 3u);
    EXPECT_EQ(snap.Entries[0].Message, "first");
    EXPECT_EQ(snap.Entries[1].Message, "second");
    EXPECT_EQ(snap.Entries[2].Message, "third");
}

TEST(CoreLogging, EntryCountTracksWrites)
{
    ResetLog();
    EXPECT_EQ(GetEntryCount(), 0u);
    Info("a");
    EXPECT_EQ(GetEntryCount(), 1u);
    Warn("b");
    EXPECT_EQ(GetEntryCount(), 2u);
    Error("c");
    EXPECT_EQ(GetEntryCount(), 3u);
}

// -----------------------------------------------------------------------------
// Ring-buffer overwrite behaviour
// -----------------------------------------------------------------------------

TEST(CoreLogging, RingOverwritesOldestWhenCapacityReached)
{
    ResetLog();

    // The internal capacity is 2048. Writing more than that should cap the
    // count at capacity and the oldest entries must be evicted.
    constexpr std::size_t kOverflow = 2048 + 32;
    for (std::size_t i = 0; i < kOverflow; ++i)
    {
        Info("entry-{}", i);
    }

    EXPECT_EQ(GetEntryCount(), 2048u);

    auto snap = TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 2048u);

    // Oldest surviving entry should be index (kOverflow - 2048), newest should
    // be kOverflow - 1.
    const std::size_t expectedOldest = kOverflow - 2048u;
    EXPECT_EQ(snap.Entries.front().Message, "entry-" + std::to_string(expectedOldest));
    EXPECT_EQ(snap.Entries.back().Message, "entry-" + std::to_string(kOverflow - 1));
}

// -----------------------------------------------------------------------------
// Concurrent writers must not corrupt the ring.
// -----------------------------------------------------------------------------

TEST(CoreLogging, ConcurrentWritersProduceConsistentSnapshot)
{
    ResetLog();
    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i)
                Info("t{}-{}", t, i);
        });
    }
    for (auto& w : workers) w.join();

    const std::size_t total = static_cast<std::size_t>(kThreads * kPerThread);
    EXPECT_EQ(GetEntryCount(), total);

    auto snap = TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), total);

    // Sequence advanced by at least `total`.
    EXPECT_GE(snap.Sequence, total);
}
