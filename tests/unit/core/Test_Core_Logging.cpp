#include <gtest/gtest.h>

import Extrinsic.Core.Logging;

using namespace Extrinsic::Core::Log;

TEST(CoreLogging, WritesSequenceAndSnapshot)
{
    ClearEntries();
    const auto seq0 = GetSequenceNumber();

    Info("hello {}", 1);
    Warn("warn");

    const auto seq1 = GetSequenceNumber();
    EXPECT_GE(seq1, seq0 + 2u);
    EXPECT_EQ(GetEntryCount(), 2u);

    auto snap = TakeSnapshot();
    ASSERT_EQ(snap.Entries.size(), 2u);
    EXPECT_EQ(snap.Entries[0].Lvl, Level::Info);
    EXPECT_EQ(snap.Entries[0].Message, "hello 1");
    EXPECT_EQ(snap.Entries[1].Lvl, Level::Warning);
    EXPECT_EQ(snap.Entries[1].Message, "warn");
}

TEST(CoreLogging, ClearDoesNotResetSequence)
{
    ClearEntries();
    Info("before-clear");
    const auto seqBefore = GetSequenceNumber();

    ClearEntries();

    EXPECT_EQ(GetEntryCount(), 0u);
    EXPECT_EQ(TakeSnapshot().Entries.size(), 0u);
    EXPECT_EQ(GetSequenceNumber(), seqBefore);
}
