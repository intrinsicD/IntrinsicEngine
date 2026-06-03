module;

export module Extrinsic.Runtime.AssetModelTextureIO;

import Extrinsic.Core.Error;
import Extrinsic.Asset.ModelTextureIOBridge;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] Extrinsic::Core::Result RegisterPromotedModelTextureIOCallbacks(
        Extrinsic::Assets::AssetModelTextureIOBridge& bridge);
}
