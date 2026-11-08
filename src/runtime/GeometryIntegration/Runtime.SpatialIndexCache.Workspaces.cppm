// Engine-only workspace factories; each run owner instantiates its own pooled types.
module;
#include <memory>
export module Extrinsic.Runtime.Private.WorkspaceLease;
import Extrinsic.Runtime.SpatialIndexCache;

extern "C++"
{
    namespace Extrinsic::RHI
    {
        class IDevice;
        class BufferManager;
    }
    namespace Extrinsic::Runtime
    {
        template <class T> std::shared_ptr<T> SpatialIndexCache::LeaseGpuWorkspace()
        {
            static const char kind{};
            return std::static_pointer_cast<T>(LeaseErasedGpuWorkspace(
                &kind,
                [](RHI::IDevice& device, RHI::BufferManager& scratch) -> std::shared_ptr<void>
                {
                    return std::make_shared<T>(device, scratch);
                },
                [](void* object)
                {
                    static_cast<T*>(object)->ReleaseScratch();
                }));
        }
    }
}
