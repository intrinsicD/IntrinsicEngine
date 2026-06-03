module;

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Asset.ModelTextureIOBridge;

import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;

export namespace Extrinsic::Assets
{
    struct AssetExternalResourceRead
    {
        std::string Uri{};
        std::string ResolvedPath{};
        std::vector<std::byte> Bytes{};
    };

    using AssetExternalResourceReader =
        std::function<Core::Expected<AssetExternalResourceRead>(std::string_view uri)>;

    struct AssetModelTextureIORequest
    {
        AssetImportRoute Route{};
        std::string Path{};
        std::string BasePath{};
        std::span<const std::byte> SourceBytes{};
        AssetExternalResourceReader ReadExternalResource{};
    };

    using AssetModelSceneImportCallback =
        std::function<Core::Expected<AssetModelScenePayload>(
            const AssetModelTextureIORequest&)>;
    using AssetTexture2DImportCallback =
        std::function<Core::Expected<AssetTexture2DPayload>(
            const AssetModelTextureIORequest&)>;

    class AssetModelTextureIOBridge
    {
    public:
        AssetModelTextureIOBridge();
        ~AssetModelTextureIOBridge();
        AssetModelTextureIOBridge(const AssetModelTextureIOBridge&) = delete;
        AssetModelTextureIOBridge& operator=(const AssetModelTextureIOBridge&) = delete;
        AssetModelTextureIOBridge(AssetModelTextureIOBridge&&) = delete;
        AssetModelTextureIOBridge& operator=(AssetModelTextureIOBridge&&) = delete;

        [[nodiscard]] Core::Result RegisterModelSceneImporter(
            AssetFileFormat format,
            AssetModelSceneImportCallback callback);

        [[nodiscard]] Core::Result RegisterTextureImporter(
            AssetFileFormat format,
            AssetTexture2DImportCallback callback);

        void Clear() noexcept;

        [[nodiscard]] bool HasModelSceneImporter(
            AssetFileFormat format) const noexcept;

        [[nodiscard]] bool HasTextureImporter(
            AssetFileFormat format) const noexcept;

        [[nodiscard]] Core::Expected<AssetModelScenePayload> ImportModelScene(
            std::string_view path,
            Core::IO::IIOBackend& backend) const;

        [[nodiscard]] Core::Expected<AssetTexture2DPayload> ImportTexture2D(
            std::string_view path,
            Core::IO::IIOBackend& backend) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
