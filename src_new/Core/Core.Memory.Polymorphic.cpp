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
            //TODO: Why not use Core::Log::Error() here?
            std::fprintf(stderr,
                         "[EXTRINSIC] ArenaMemoryResource allocation failed: %s\n",
                         Core::Error::ToString(res.error()).data());
            std::abort();
        }
        return res->data();
    }
}
