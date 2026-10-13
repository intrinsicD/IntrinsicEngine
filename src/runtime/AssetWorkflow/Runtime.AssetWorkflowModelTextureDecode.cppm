// Decodes model and texture files while retaining import validation evidence.
module;

#include <string_view>
#include <memory>

export module Extrinsic.Runtime.AssetWorkflowModelTextureDecode;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.ImportValidation;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] Extrinsic::Core::Expected<
        Extrinsic::Assets::AssetModelScenePayload> DecodeModelSceneAsset(
            std::string_view path,
            Extrinsic::Core::IO::IIOBackend& backend,
            Extrinsic::Assets::ImportValidationOptions options = {},
            std::shared_ptr<Extrinsic::Assets::ImportValidationReport> report = {});

    [[nodiscard]] Extrinsic::Core::Expected<
        Extrinsic::Assets::AssetTexture2DPayload> DecodeTextureAsset(
            std::string_view path,
            Extrinsic::Core::IO::IIOBackend& backend,
            Extrinsic::Assets::ImportValidationOptions options = {},
            std::shared_ptr<Extrinsic::Assets::ImportValidationReport> report = {});
}
