#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

import Core.Logging;

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
