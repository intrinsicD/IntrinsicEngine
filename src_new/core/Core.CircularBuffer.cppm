module;

#include <array>

export module Core.CircularBuffer;

namespace Extrinsic::Core
{
    template <typename T, std::size_t Capacity>
    class CircularBuffer
    {
    public:
        void Push(T value)
        {
            m_Data[m_WritePos % Capacity] = std::move(value);
            ++m_WritePos;

            if (m_Count < Capacity)
                ++m_Count;
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_Count;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return m_Count == 0;
        }

        void Clear() noexcept
        {
            m_WritePos = 0;
            m_Count = 0;
        }

        template <typename F>
        void ForEach(F&& fn) const
        {
            const std::size_t oldest = m_WritePos - m_Count;

            for (std::size_t i = oldest; i < m_WritePos; ++i)
                fn(m_Data[i % Capacity]);
        }

    private:
        std::array<T, Capacity> m_Data{};
        std::size_t m_WritePos = 0;
        std::size_t m_Count = 0;
    };
}
