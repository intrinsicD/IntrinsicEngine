module;

export module Extrinsic.Runtime.WorldHandle;

export import Extrinsic.Core.StrongHandle;

namespace Extrinsic::Runtime
{
    export struct RuntimeWorldTag;
    export using WorldHandle = Core::StrongHandle<RuntimeWorldTag>;
}
