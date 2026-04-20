module;

#include <new>

module Extrinsic.Core.Memory:Polymorphic.Impl;
import :Polymorphic;

namespace Extrinsic::Core::Memory
{
    void* ArenaMemoryResource::do_allocate(const size_t bytes, const size_t alignment)
    {
        auto res = m_Arena.AllocBytes(bytes, alignment);
        if (!res)
            throw std::bad_alloc{};
        return res->data();
    }
}
