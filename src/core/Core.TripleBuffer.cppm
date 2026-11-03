// ARCH-022 S3: the latest value from one writer to one reader. Three slots: the writer fills its back slot and
// publishes it, the reader takes the newest published slot; neither ever waits for the other, and neither touches the
// slot the other holds, so a read is never torn. A value published before the reader took the previous one replaces it
// (latest wins).
module;

#include <array>
#include <atomic>
#include <cstdint>

export module Extrinsic.Core.TripleBuffer;

export namespace Extrinsic::Core
{
    template <class T>
    class TripleBuffer
    {
    public:
        // Writer: the slot to fill (it keeps what the writer last left there, e.g. a vector's capacity).
        [[nodiscard]] T& Back() noexcept { return m_Slots[m_Back]; }
        // Writer: hands the back slot to the reader; an unread earlier value is replaced.
        void Publish() noexcept { m_Back = m_Middle.exchange(m_Back | kFresh, std::memory_order_acq_rel) & kIndex; }
        // Writer: whether the reader has not taken the last published value yet (a producer can skip a copy then).
        [[nodiscard]] bool Unread() const noexcept { return (m_Middle.load(std::memory_order_acquire) & kFresh) != 0u; }

        // Reader: takes the newest published value if there is one; true when it did.
        bool Update() noexcept
        {
            if ((m_Middle.load(std::memory_order_acquire) & kFresh) == 0u) return false;
            m_Front = m_Middle.exchange(m_Front, std::memory_order_acq_rel) & kIndex;
            return true;
        }
        // Reader: the value it holds (stable until its next Update).
        [[nodiscard]] const T& Front() const noexcept { return m_Slots[m_Front]; }

    private:
        static constexpr std::uint8_t kIndex = 0x3u, kFresh = 0x4u;
        std::array<T, 3> m_Slots{};
        std::uint8_t m_Back{0u}, m_Front{1u};             // owned by the writer and the reader
        std::atomic<std::uint8_t> m_Middle{2u}; // the slot in between, and whether it is fresh
    };
}
