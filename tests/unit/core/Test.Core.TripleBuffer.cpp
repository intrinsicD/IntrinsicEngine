// ARCH-022 S3: the triple buffer (latest wins, no blocking, no torn read) and the append log (whole prefixes, no
// blocking). Concurrency under ThreadSanitizer: tests/unit/core/tsan (Core.TripleBufferThreadSanitizer).
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.AppendLog;
import Extrinsic.Core.TripleBuffer;

using Extrinsic::Core::AppendLog;
using Extrinsic::Core::TripleBuffer;

TEST(CoreTripleBuffer, TheReaderGetsTheNewestPublishedValueOnce)
{
    TripleBuffer<int> buffer{};
    EXPECT_FALSE(buffer.Update()) << "nothing published yet";
    buffer.Back() = 1;
    buffer.Publish();
    buffer.Back() = 2;
    buffer.Publish(); // replaces the unread 1
    ASSERT_TRUE(buffer.Update());
    EXPECT_EQ(buffer.Front(), 2);
    EXPECT_FALSE(buffer.Update()) << "no newer value";
    EXPECT_EQ(buffer.Front(), 2);
}

TEST(CoreTripleBuffer, UnreadTellsTheWriterWhetherTheReaderTookTheLastValue)
{
    TripleBuffer<int> buffer{};
    EXPECT_FALSE(buffer.Unread());
    buffer.Back() = 7;
    buffer.Publish();
    EXPECT_TRUE(buffer.Unread());
    ASSERT_TRUE(buffer.Update());
    EXPECT_FALSE(buffer.Unread());
}

TEST(CoreTripleBuffer, AHeldValueStaysWhileTheWriterPublishesOnAndTheNewestComesNext)
{
    TripleBuffer<std::vector<int>> buffer{};
    buffer.Back().assign(4, 1);
    buffer.Publish();
    ASSERT_TRUE(buffer.Update());
    const auto* held = &buffer.Front();
    for (int value = 2; value <= 9; ++value)
    {
        buffer.Back().assign(4, value); // never the reader's slot
        buffer.Publish();
        EXPECT_EQ(buffer.Front(), std::vector<int>(4, 1)) << "the held value is untouched after " << value;
        EXPECT_EQ(&buffer.Front(), held);
    }
    ASSERT_TRUE(buffer.Update());
    EXPECT_EQ(buffer.Front(), std::vector<int>(4, 9));
}

TEST(CoreAppendLog, ReadersSeeEveryPublishedRecordAndAFullLogRefuses)
{
    AppendLog<double> log{3};
    EXPECT_TRUE(log.Records().empty());
    EXPECT_TRUE(log.Append(1.5));
    const auto first = log.Records();
    EXPECT_TRUE(log.Append(2.5));
    EXPECT_TRUE(log.Append(3.5));
    EXPECT_FALSE(log.Append(4.5)) << "full: capacity 3";
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], 1.5) << "an earlier prefix stays valid";
    const auto all = log.Records();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[2], 3.5);
}
