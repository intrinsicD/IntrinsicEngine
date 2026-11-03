// ARCH-022 S3: records from one writer, read whole by any reader without waiting. The storage is allocated up front
// (a run's iteration cap); the writer fills the next record and then publishes the count, and a published record is
// never written again, so a reader sees every record up to the count it loaded, complete.
module;

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

export module Extrinsic.Core.AppendLog;

export namespace Extrinsic::Core
{
    template <class T>
    class AppendLog
    {
    public:
        explicit AppendLog(const std::size_t capacity) : m_Records(capacity) {}

        // Writer: false when the log is full (the record is dropped).
        bool Append(const T& record) noexcept
        {
            const std::size_t count = m_Count.load(std::memory_order_relaxed);
            if (count == m_Records.size()) return false;
            m_Records[count] = record;
            m_Count.store(count + 1u, std::memory_order_release);
            return true;
        }
        // Any reader: every record published so far.
        [[nodiscard]] std::span<const T> Records() const noexcept
        {
            return {m_Records.data(), m_Count.load(std::memory_order_acquire)};
        }

    private:
        std::vector<T> m_Records;
        std::atomic<std::size_t> m_Count{0u};
    };
}
