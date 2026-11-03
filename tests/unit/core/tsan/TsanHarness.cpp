// ARCH-022 S3: one writer and one reader stress the triple buffer and the append log under ThreadSanitizer. Asserts,
// in every build: every value the reader sees is one the writer wrote whole (no torn read) and values never go back
// (latest wins, monotonic); the writer finishes all publications while the reader holds a value and sleeps (it never
// waits for the reader); readers of the log see complete records up to any count they load. Exit 1 on a failure,
// 2 on the timeout.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

import Extrinsic.Core.AppendLog;
import Extrinsic.Core.TripleBuffer;

namespace
{
    struct Value
    {
        std::array<std::uint64_t, 8> Words{}; // all equal to the publication number
    };

    constexpr std::uint64_t kPublications = 200000;
    std::atomic<int> g_Failures{0};

    void Fail(const char* what)
    {
        std::fprintf(stderr, "FAIL %s\n", what);
        g_Failures.fetch_add(1);
    }

    bool Whole(const Value& value)
    {
        for (const auto word : value.Words)
            if (word != value.Words[0]) return false;
        return true;
    }
}

int main()
{
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(120);

    // Triple buffer: the reader polls, and once holds its value for a while; the writer never waits.
    Extrinsic::Core::TripleBuffer<Value> buffer{};
    std::atomic<bool> writerDone{false}, readerHolding{false};
    std::uint64_t written = 0;
    std::thread writer([&]
    {
        while (!readerHolding.load()) std::this_thread::yield();
        for (std::uint64_t n = 1; n <= kPublications; ++n)
        {
            buffer.Back().Words.fill(n);
            buffer.Publish();
            written = n;
        }
        writerDone.store(true);
    });
    std::uint64_t reads = 0, last = 0;
    std::thread reader([&]
    {
        (void)buffer.Update();
        readerHolding.store(true);
        // Hold the value until the writer has finished every publication: it must not need this slot.
        while (!writerDone.load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!writerDone.load()) Fail("the writer waited for a reader that held its value");
        for (int round = 0; round < 3; ++round)
        {
            if (buffer.Update()) ++reads;
            const Value& value = buffer.Front();
            if (!Whole(value)) Fail("torn read");
            if (value.Words[0] < last) Fail("a value went back");
            last = value.Words[0];
        }
    });
    writer.join();
    reader.join();
    if (written != kPublications) Fail("the writer did not finish");
    if (last != kPublications) Fail("the reader did not end on the newest value");

    // Concurrent polling: values monotonic and whole while the writer runs. The writer publishes until the poller has
    // taken kMinTaken values, counted through Unread (the buffer's own ordering), so the reads overlap publication.
    constexpr std::uint64_t kMinTaken = 1000;
    Extrinsic::Core::TripleBuffer<Value> live{};
    std::atomic<bool> liveDone{false}, pollerStarted{false};
    std::uint64_t liveWritten = 0;
    std::thread liveWriter([&]
    {
        while (!pollerStarted.load()) std::this_thread::yield();
        std::uint64_t taken = 0;
        for (std::uint64_t n = 1; (n <= kPublications || taken < kMinTaken) && Clock::now() < deadline; ++n)
        {
            if (n > 1 && !live.Unread()) ++taken; // the poller took a value since the last publication
            live.Back().Words.fill(n);
            live.Publish();
            liveWritten = n;
        }
        liveDone.store(true);
    });
    std::uint64_t liveLast = 0, liveReads = 0;
    const auto checkLive = [&]
    {
        const Value& value = live.Front();
        if (!Whole(value)) Fail("torn read while polling");
        if (value.Words[0] < liveLast) Fail("a polled value went back");
        liveLast = value.Words[0];
    };
    pollerStarted.store(true);
    while (!liveDone.load() && Clock::now() < deadline)
    {
        if (live.Update()) ++liveReads; // only reads made while the writer runs count
        checkLive();
    }
    liveWriter.join();
    (void)live.Update();
    checkLive();
    if (liveReads < kMinTaken) Fail("the poller took too few values while the writer ran");
    if (liveLast != liveWritten) Fail("the poller did not end on the newest value");

    // Append log: two readers load prefixes while one writer appends. The writer starts once both readers run and
    // waits halfway until each has checked records (that signal goes from the readers to the writer, so it orders
    // none of the records they read).
    Extrinsic::Core::AppendLog<Value> log{kPublications};
    std::atomic<bool> logDone{false};
    std::atomic<int> logReadersStarted{0}, logReadersChecked{0};
    std::thread logWriter([&]
    {
        while (logReadersStarted.load() < 2) std::this_thread::yield();
        Value value{};
        for (std::uint64_t n = 0; n < kPublications; ++n)
        {
            if (n == kPublications / 2)
                while (logReadersChecked.load() < 2 && Clock::now() < deadline) std::this_thread::yield();
            value.Words.fill(n);
            if (!log.Append(value)) Fail("the log refused below its capacity");
        }
        logDone.store(true);
    });
    const auto readLog = [&]
    {
        logReadersStarted.fetch_add(1);
        std::size_t seen = 0;
        bool checked = false;
        while (!logDone.load() && Clock::now() < deadline)
        {
            const auto records = log.Records();
            if (records.size() < seen) Fail("the log's count went back");
            seen = records.size();
            for (std::size_t i = seen > 64 ? seen - 64 : 0; i < seen; ++i)
                if (!Whole(records[i]) || records[i].Words[0] != i) Fail("an incomplete record");
            if (!checked && seen > 0)
            {
                checked = true;
                logReadersChecked.fetch_add(1);
            }
        }
        if (!checked) Fail("a log reader checked no record while the writer appended");
    };
    std::thread logReaderA(readLog), logReaderB(readLog);
    logWriter.join();
    logReaderA.join();
    logReaderB.join();
    if (log.Records().size() != kPublications) Fail("the log lost records");

    if (Clock::now() >= deadline)
    {
        std::fprintf(stderr, "TIMEOUT\n");
        return 2;
    }
    std::printf("tsan harness: %llu buffer reads, %llu polled reads of %llu publications, %d failures\n",
                static_cast<unsigned long long>(reads), static_cast<unsigned long long>(liveReads),
                static_cast<unsigned long long>(liveWritten), g_Failures.load());
    return g_Failures.load() == 0 ? 0 : 1;
}
