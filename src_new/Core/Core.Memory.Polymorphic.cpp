module;

#include <cstdlib>

module Extrinsic.Core.Memory:Polymorphic.Impl;
import :Polymorphic;

namespace Extrinsic::Core::Memory
{
    MemoryExpected<void*> ArenaMemoryResource::TryAllocate(const size_t bytes, const size_t alignment) noexcept
    {
        return m_Arena.AllocBytes(bytes, alignment)
            .transform([](std::span<std::byte> mem) { return static_cast<void*>(mem.data()); });
    }

    void* ArenaMemoryResource::do_allocate(const size_t bytes, const size_t alignment)
    {
        auto res = TryAllocate(bytes, alignment);
        if (!res)
            std::terminate();
        return *res;
    }
}
