module;

#include <cstdio>
#include <cstdlib>

module Extrinsic.Core.Memory:Polymorphic.Impl;
import :Polymorphic;
import Extrinsic.Core.Error;

namespace Extrinsic::Core::Memory
{
    void* ArenaMemoryResource::do_allocate(const size_t bytes, const size_t alignment)
    {
        auto res = m_Arena.AllocBytes(bytes, alignment);
        if (!res)
        {
            // std::pmr::memory_resource::do_allocate must either return a valid
            // pointer or terminate; it cannot fail. We deliberately avoid
            // Core::Log here: logging sinks may themselves allocate through a
            // pmr resource and re-enter this path. Raw fprintf is reentrant-safe.
            std::fprintf(stderr,
                         "[EXTRINSIC] ArenaMemoryResource allocation failed: %s\n",
                         Core::Error::ToString(res.error()).data());
            std::abort();
        }
        return res->data();
    }
}
