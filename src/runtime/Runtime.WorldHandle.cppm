module;

#include <cstdint>

export module Extrinsic.Runtime.WorldHandle;

import Extrinsic.Core.StrongHandle;

namespace Extrinsic::Runtime
{
    export struct WorldHandleTag;
    export using WorldHandle = Core::StrongHandle<WorldHandleTag>;

    // Boot-world identity reserved by WorldRegistry so frame-0 work is never
    // unscoped and pre-ARCH-010 jobs keep their existing default scope.
    export inline constexpr WorldHandle DefaultWorldHandle{0u, 1u};
}
