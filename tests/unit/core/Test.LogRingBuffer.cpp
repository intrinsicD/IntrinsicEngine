#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import Extrinsic.Core.Logging;

namespace Core = Extrinsic::Core;

// Each test clears the ring buffer to avoid cross-test contamination.

TEST(LogRingBuffer, InitiallyEmpty)
{
    Core::Log::ClearEntries();
    EXPECT_EQ(Core::Log::GetEntryCount(), 0u);
}

TEST(LogRingBuffer, SingleEntry)
{
    Core::Log::ClearEntries();

    Core::Log::Info("hello {}", 42);

    EXPECT_EQ(Core::Log::GetEntryCount(), 1u);

    const auto snap = Core::Log::TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 1u);
    EXPECT_EQ(snap.Entries[0].Lvl, Core::Log::Level::Info);
    EXPECT_EQ(snap.Entries[0].Message, "hello 42");
    EXPECT_GT(snap.Sequence, 0u);
}

TEST(LogRingBuffer, MultipleLevels)
{
    Core::Log::ClearEntries();

    Core::Log::Info("info msg");
    Core::Log::Warn("warn msg");
    Core::Log::Error("error msg");

    EXPECT_EQ(Core::Log::GetEntryCount(), 3u);

    const auto snap = Core::Log::TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 3u);
    EXPECT_EQ(snap.Entries[0].Lvl, Core::Log::Level::Info);
    EXPECT_EQ(snap.Entries[1].Lvl, Core::Log::Level::Warning);
    EXPECT_EQ(snap.Entries[2].Lvl, Core::Log::Level::Error);
}

TEST(LogRingBuffer, ChronologicalOrder)
{
    Core::Log::ClearEntries();

    for (int i = 0; i < 10; ++i)
        Core::Log::Info("msg {}", i);

    const auto snap = Core::Log::TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 10u);
    for (std::size_t i = 0; i < snap.Entries.size(); ++i)
        EXPECT_EQ(snap.Entries[i].Message, "msg " + std::to_string(i));
}

TEST(LogRingBuffer, ClearResetsEntriesButNotSequence)
{
    Core::Log::ClearEntries();
    Core::Log::Info("will be cleared");

    const uint64_t seqBefore = Core::Log::GetSequenceNumber();
    Core::Log::ClearEntries();

    EXPECT_EQ(Core::Log::GetEntryCount(), 0u);

    // Sequence must NOT reset — monotonicity preserved
    const uint64_t seqAfter = Core::Log::GetSequenceNumber();
    EXPECT_EQ(seqAfter, seqBefore);

    const auto snap = Core::Log::TakeSnapshot();
    EXPECT_TRUE(snap.Entries.empty());
}

TEST(LogRingBuffer, SequenceMonotonicallyIncreases)
{
    Core::Log::ClearEntries();

    const uint64_t s0 = Core::Log::GetSequenceNumber();
    Core::Log::Info("a");
    const uint64_t s1 = Core::Log::GetSequenceNumber();
    Core::Log::Warn("b");
    const uint64_t s2 = Core::Log::GetSequenceNumber();

    EXPECT_LT(s0, s1);
    EXPECT_LT(s1, s2);
}

TEST(LogRingBuffer, SnapshotSequenceMatchesAtomicSequence)
{
    Core::Log::ClearEntries();

    Core::Log::Info("entry a");
    Core::Log::Info("entry b");

    const auto snap = Core::Log::TakeSnapshot();
    EXPECT_EQ(snap.Sequence, Core::Log::GetSequenceNumber());
}

TEST(LogRingBuffer, OverwritesOldestWhenFull)
{
    Core::Log::ClearEntries();

    // The ring buffer capacity is 2048. Write 2060 entries to force wraparound.
    constexpr std::size_t kOverflow = 2060;
    for (std::size_t i = 0; i < kOverflow; ++i)
        Core::Log::Info("entry {}", i);

    // Count should be capped at capacity
    const std::size_t count = Core::Log::GetEntryCount();
    EXPECT_LE(count, 2048u);
    EXPECT_GT(count, 0u);

    // The oldest surviving entry should be "entry 12" (2060 - 2048)
    const auto snap = Core::Log::TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 2048u);
    EXPECT_EQ(snap.Entries.front().Message, "entry " + std::to_string(kOverflow - 2048u));
    EXPECT_EQ(snap.Entries.back().Message, "entry " + std::to_string(kOverflow - 1u));
}

#ifndef NDEBUG
TEST(LogRingBuffer, DebugLevelCaptured)
{
    Core::Log::ClearEntries();

    Core::Log::Debug("debug msg");

    EXPECT_EQ(Core::Log::GetEntryCount(), 1u);
    const auto snap = Core::Log::TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 1u);
    EXPECT_EQ(snap.Entries[0].Lvl, Core::Log::Level::Debug);
    EXPECT_EQ(snap.Entries[0].Message, "debug msg");
}
#endif

TEST(LogRingBuffer, CursorMetadataAndExactCategoryFilter)
{
    Core::Log::ClearEntries();
    const auto start = Core::Log::GetSequenceNumber();
    Core::Log::Info("[Geometry] accepted");
    Core::Log::Warn("[Geometry] warning");
    Core::Log::Error("[Other] ignored");
    const std::vector<std::string> categories{"Geometry"};
    const auto first = Core::Log::TakeSnapshotSince(start, 1, Core::Log::AllLevels, categories);
    ASSERT_EQ(first.Entries.size(), 1u);
    EXPECT_EQ(first.Entries[0].Sequence, start + 1);
    EXPECT_GT(first.Entries[0].TimestampNs, 0u);
    EXPECT_EQ(first.Entries[0].Category, "Geometry");
    EXPECT_EQ(first.NextCursor, start + 1);
    const auto second = Core::Log::TakeSnapshotSince(first.NextCursor, 1, Core::Log::AllLevels, categories);
    ASSERT_EQ(second.Entries.size(), 1u);
    EXPECT_EQ(second.Entries[0].Lvl, Core::Log::Level::Warning);
    EXPECT_EQ(second.NextCursor, start + 3) << "Trailing filtered entries advance the cursor";
    EXPECT_EQ(second.Dropped, 0u);
}

TEST(LogRingBuffer, FilteredPagingNeverSkipsAnUnreturnedMatch)
{
    Core::Log::ClearEntries();
    const auto start = Core::Log::GetSequenceNumber();
    Core::Log::Info("skip");
    Core::Log::Error("first");
    Core::Log::Warn("skip");
    Core::Log::Error("second");
    const auto first = Core::Log::TakeSnapshotSince(start, 1, Core::Log::Mask(Core::Log::Level::Error));
    ASSERT_EQ(first.Entries.size(), 1u);
    EXPECT_EQ(first.Entries[0].Message, "first");
    EXPECT_EQ(first.NextCursor, start + 3);
    const auto second = Core::Log::TakeSnapshotSince(first.NextCursor, 1, Core::Log::Mask(Core::Log::Level::Error));
    ASSERT_EQ(second.Entries.size(), 1u);
    EXPECT_EQ(second.Entries[0].Message, "second");
    EXPECT_EQ(second.NextCursor, start + 4);
    const auto none = Core::Log::TakeSnapshotSince(start, 10, 0);
    EXPECT_TRUE(none.Entries.empty());
    EXPECT_EQ(none.NextCursor, start + 4);
    EXPECT_EQ(Core::Log::TakeSnapshotSince(start, 0).NextCursor, start);
}

TEST(LogRingBuffer, CursorReportsWrapAndClearLossWithoutReset)
{
    Core::Log::ClearEntries();
    const auto start = Core::Log::GetSequenceNumber();
    for (int i = 0; i < 2050; ++i) Core::Log::Info("[Ring] {}", i);
    const auto page = Core::Log::TakeSnapshotSince(start, 2);
    EXPECT_EQ(page.Dropped, 2u);
    ASSERT_EQ(page.Entries.size(), 2u);
    EXPECT_EQ(page.Entries.front().Sequence, start + 3);
    EXPECT_EQ(page.NextCursor, start + 4);
    Core::Log::ClearEntries();
    const auto cleared = Core::Log::TakeSnapshotSince(page.NextCursor, 10);
    EXPECT_TRUE(cleared.Entries.empty());
    EXPECT_EQ(cleared.Dropped, 2046u);
    EXPECT_EQ(cleared.NextCursor, start + 2050);
    EXPECT_EQ(cleared.ClearedThrough, start + 2050);
    Core::Log::Warn("after clear");
    const auto next = Core::Log::TakeSnapshotSince(cleared.NextCursor, 10);
    ASSERT_EQ(next.Entries.size(), 1u);
    EXPECT_EQ(next.Entries.front().Sequence, start + 2051);
    EXPECT_EQ(next.Dropped, 0u);
}

TEST(LogRingBuffer, CategoryUsesOnlyANonemptyLeadingTag)
{
    Core::Log::ClearEntries();
    const auto start = Core::Log::GetSequenceNumber();
    Core::Log::Info("[VulkanDevice::Bootstrap] validation");
    Core::Log::Info("prefix [tag]");
    Core::Log::Info("[] empty");
    Core::Log::Info("[unterminated");
    const auto page = Core::Log::TakeSnapshotSince(start, 10);
    ASSERT_EQ(page.Entries.size(), 4u);
    EXPECT_EQ(page.Entries[0].Category, "VulkanDevice::Bootstrap");
    for (std::size_t i = 1; i < page.Entries.size(); ++i) EXPECT_TRUE(page.Entries[i].Category.empty());
}

TEST(LogRingBuffer, AheadCursorRestartsPagingAndReportsReset)
{
    Core::Log::ClearEntries();
    Core::Log::Info("[Restart] first");
    Core::Log::Error("[Restart] second");
    const auto newest = Core::Log::GetSequenceNumber();
    const auto ahead = newest + 1000;
    const auto first = Core::Log::TakeSnapshotSince(ahead, 1);
    EXPECT_TRUE(first.CursorReset);
    ASSERT_EQ(first.Entries.size(), 1u);
    EXPECT_EQ(first.Entries.front().Message, "[Restart] first");
    EXPECT_EQ(first.NextCursor, newest - 1);
    const auto second = Core::Log::TakeSnapshotSince(first.NextCursor, 1);
    EXPECT_FALSE(second.CursorReset);
    ASSERT_EQ(second.Entries.size(), 1u);
    EXPECT_EQ(second.Entries.front().Message, "[Restart] second");
    EXPECT_EQ(second.NextCursor, newest);
    const auto filtered = Core::Log::TakeSnapshotSince(ahead, 1, Core::Log::Mask(Core::Log::Level::Error));
    EXPECT_TRUE(filtered.CursorReset);
    ASSERT_EQ(filtered.Entries.size(), 1u);
    EXPECT_EQ(filtered.Entries.front().Message, "[Restart] second");
    EXPECT_EQ(filtered.NextCursor, newest);
    const auto zeroLimit = Core::Log::TakeSnapshotSince(ahead, 0);
    EXPECT_TRUE(zeroLimit.CursorReset);
    EXPECT_EQ(zeroLimit.NextCursor, 0u);
    EXPECT_TRUE(zeroLimit.Entries.empty());
}

TEST(LogRingBuffer, AheadCursorOnEmptyRingCanReadSubsequentEntries)
{
    Core::Log::ClearEntries();
    const auto newest = Core::Log::GetSequenceNumber();
    const auto empty = Core::Log::TakeSnapshotSince(newest + 1000, 10);
    EXPECT_TRUE(empty.CursorReset);
    EXPECT_TRUE(empty.Entries.empty());
    EXPECT_EQ(empty.NextCursor, newest);
    Core::Log::Info("after cursor reset");
    const auto next = Core::Log::TakeSnapshotSince(empty.NextCursor, 10);
    EXPECT_FALSE(next.CursorReset);
    ASSERT_EQ(next.Entries.size(), 1u);
    EXPECT_EQ(next.Entries.front().Message, "after cursor reset");
}
