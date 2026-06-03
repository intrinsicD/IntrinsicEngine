module;

export module Extrinsic.Runtime.AssetGeometryIO;

import Extrinsic.Core.Error;
import Extrinsic.Asset.GeometryIOBridge;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] Extrinsic::Core::Result RegisterPromotedGeometryIOCallbacks(
        Extrinsic::Assets::AssetGeometryIOBridge& bridge);
}
