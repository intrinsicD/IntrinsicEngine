module;

#include <string_view>

export module Extrinsic.Runtime.AssetWorkflowModelTextureDecode;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] Extrinsic::Core::Expected<
        Extrinsic::Assets::AssetModelScenePayload> DecodeModelSceneAsset(
            std::string_view path,
            Extrinsic::Core::IO::IIOBackend& backend);

    [[nodiscard]] Extrinsic::Core::Expected<
        Extrinsic::Assets::AssetTexture2DPayload> DecodeTextureAsset(
            std::string_view path,
            Extrinsic::Core::IO::IIOBackend& backend);
}
