module;

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Asset.ModelTextureIOBridge;

namespace Extrinsic::Assets
{
    namespace
    {
        struct ModelSceneImporterEntry
        {
            AssetFileFormat Format{AssetFileFormat::Unknown};
            AssetModelSceneImportCallback Callback{};
        };

        struct TextureImporterEntry
        {
            AssetFileFormat Format{AssetFileFormat::Unknown};
            AssetTexture2DImportCallback Callback{};
        };

        [[nodiscard]] Core::Result ValidateModelSceneRegistration(
            const AssetFileFormat format,
            const bool hasCallback) noexcept
        {
            if (!hasCallback)
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }
            if (!IsSupportedModelSceneImportFormat(format))
            {
                return Core::Err(Core::ErrorCode::AssetUnsupportedFormat);
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Result ValidateTextureRegistration(
            const AssetFileFormat format,
            const bool hasCallback) noexcept
        {
            if (!hasCallback)
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }
            if (!IsSupportedTextureImportFormat(format))
            {
                return Core::Err(Core::ErrorCode::AssetUnsupportedFormat);
            }
            return Core::Ok();
        }

        [[nodiscard]] std::string ParentPathOf(const std::string_view path)
        {
            return std::filesystem::path(std::string(path)).parent_path().string();
        }

        [[nodiscard]] std::string ResolveExternalPath(
            const std::string_view basePath,
            const std::string_view uri)
        {
            const std::filesystem::path uriPath{std::string(uri)};
            if (uriPath.is_absolute() || basePath.empty())
            {
                return uriPath.string();
            }
            return (std::filesystem::path(std::string(basePath)) / uriPath)
                .lexically_normal()
                .string();
        }

        [[nodiscard]] Core::Expected<std::vector<std::byte>> ReadBytes(
            Core::IO::IIOBackend& backend,
            const std::string_view path)
        {
            auto read = backend.Read(Core::IO::IORequest{.Path = std::string(path)});
            if (!read.has_value())
            {
                return std::unexpected(read.error());
            }
            return std::move(read->Data);
        }

        [[nodiscard]] Core::Expected<AssetExternalResourceRead> ReadExternalResource(
            Core::IO::IIOBackend& backend,
            const std::string_view basePath,
            const std::string_view uri)
        {
            if (uri.empty())
            {
                return Core::Err<AssetExternalResourceRead>(
                    Core::ErrorCode::InvalidPath);
            }

            const std::string resolvedPath = ResolveExternalPath(basePath, uri);
            auto bytes = ReadBytes(backend, resolvedPath);
            if (!bytes.has_value())
            {
                return std::unexpected(bytes.error());
            }
            return AssetExternalResourceRead{
                .Uri = std::string(uri),
                .ResolvedPath = resolvedPath,
                .Bytes = std::move(*bytes),
            };
        }

        [[nodiscard]] AssetModelTextureIORequest MakeRequest(
            const AssetImportRoute& route,
            const std::string_view path,
            const std::span<const std::byte> sourceBytes,
            Core::IO::IIOBackend& backend)
        {
            const std::string pathString{path};
            std::string basePath = ParentPathOf(pathString);
            return AssetModelTextureIORequest{
                .Route = route,
                .Path = pathString,
                .BasePath = basePath,
                .SourceBytes = sourceBytes,
                .ReadExternalResource =
                    [&backend, basePath = std::move(basePath)](
                        const std::string_view uri)
                        -> Core::Expected<AssetExternalResourceRead>
                    {
                        return ReadExternalResource(backend, basePath, uri);
                    },
            };
        }

        [[nodiscard]] Core::Expected<AssetModelScenePayload> ValidateDecodedModel(
            AssetModelScenePayload payload)
        {
            const Core::Result valid = ValidateAssetModelScenePayload(payload);
            if (!valid.has_value())
            {
                return std::unexpected(valid.error());
            }
            return payload;
        }

        [[nodiscard]] Core::Expected<AssetTexture2DPayload> ValidateDecodedTexture(
            AssetTexture2DPayload payload)
        {
            const Core::Result valid = ValidateAssetTexture2DPayload(payload);
            if (!valid.has_value())
            {
                return std::unexpected(valid.error());
            }
            return payload;
        }
    }

    struct AssetModelTextureIOBridge::Impl
    {
        mutable std::mutex Mutex{};
        std::vector<ModelSceneImporterEntry> ModelSceneImporters{};
        std::vector<TextureImporterEntry> TextureImporters{};

        [[nodiscard]] AssetModelSceneImportCallback FindModelSceneImporter(
            const AssetFileFormat format) const
        {
            std::scoped_lock lock(Mutex);
            const auto it = std::ranges::find_if(
                ModelSceneImporters,
                [format](const ModelSceneImporterEntry& entry)
                {
                    return entry.Format == format;
                });
            return it == ModelSceneImporters.end()
                ? AssetModelSceneImportCallback{}
                : it->Callback;
        }

        [[nodiscard]] AssetTexture2DImportCallback FindTextureImporter(
            const AssetFileFormat format) const
        {
            std::scoped_lock lock(Mutex);
            const auto it = std::ranges::find_if(
                TextureImporters,
                [format](const TextureImporterEntry& entry)
                {
                    return entry.Format == format;
                });
            return it == TextureImporters.end()
                ? AssetTexture2DImportCallback{}
                : it->Callback;
        }
    };

    AssetModelTextureIOBridge::AssetModelTextureIOBridge()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    AssetModelTextureIOBridge::~AssetModelTextureIOBridge() = default;

    Core::Result AssetModelTextureIOBridge::RegisterModelSceneImporter(
        const AssetFileFormat format,
        AssetModelSceneImportCallback callback)
    {
        const Core::Result valid =
            ValidateModelSceneRegistration(format, static_cast<bool>(callback));
        if (!valid.has_value())
        {
            return valid;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        const auto it = std::ranges::find_if(
            m_Impl->ModelSceneImporters,
            [format](const ModelSceneImporterEntry& entry)
            {
                return entry.Format == format;
            });
        if (it == m_Impl->ModelSceneImporters.end())
        {
            m_Impl->ModelSceneImporters.push_back(
                ModelSceneImporterEntry{format, std::move(callback)});
        }
        else
        {
            it->Callback = std::move(callback);
        }
        return Core::Ok();
    }

    Core::Result AssetModelTextureIOBridge::RegisterTextureImporter(
        const AssetFileFormat format,
        AssetTexture2DImportCallback callback)
    {
        const Core::Result valid =
            ValidateTextureRegistration(format, static_cast<bool>(callback));
        if (!valid.has_value())
        {
            return valid;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        const auto it = std::ranges::find_if(
            m_Impl->TextureImporters,
            [format](const TextureImporterEntry& entry)
            {
                return entry.Format == format;
            });
        if (it == m_Impl->TextureImporters.end())
        {
            m_Impl->TextureImporters.push_back(
                TextureImporterEntry{format, std::move(callback)});
        }
        else
        {
            it->Callback = std::move(callback);
        }
        return Core::Ok();
    }

    void AssetModelTextureIOBridge::Clear() noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->ModelSceneImporters.clear();
        m_Impl->TextureImporters.clear();
    }

    bool AssetModelTextureIOBridge::HasModelSceneImporter(
        const AssetFileFormat format) const noexcept
    {
        return static_cast<bool>(m_Impl->FindModelSceneImporter(format));
    }

    bool AssetModelTextureIOBridge::HasTextureImporter(
        const AssetFileFormat format) const noexcept
    {
        return static_cast<bool>(m_Impl->FindTextureImporter(format));
    }

    Core::Expected<AssetModelScenePayload>
    AssetModelTextureIOBridge::ImportModelScene(
        const std::string_view path,
        Core::IO::IIOBackend& backend) const
    {
        auto route = ResolveAssetImportRoute(
            path,
            AssetRouteOperation::Import,
            AssetImportHint{.PayloadKind = AssetPayloadKind::ModelScene});
        if (!route.has_value())
        {
            return std::unexpected(route.error());
        }
        if (!IsSupportedModelSceneImportFormat(route->Format))
        {
            return Core::Err<AssetModelScenePayload>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        auto callback = m_Impl->FindModelSceneImporter(route->Format);
        if (!callback)
        {
            return Core::Err<AssetModelScenePayload>(
                Core::ErrorCode::AssetLoaderMissing);
        }

        auto bytes = ReadBytes(backend, path);
        if (!bytes.has_value())
        {
            return std::unexpected(bytes.error());
        }

        AssetModelTextureIORequest request =
            MakeRequest(*route, path, std::span<const std::byte>(*bytes), backend);
        auto decoded = callback(request);
        if (!decoded.has_value())
        {
            return std::unexpected(decoded.error());
        }
        return ValidateDecodedModel(std::move(*decoded));
    }

    Core::Expected<AssetTexture2DPayload>
    AssetModelTextureIOBridge::ImportTexture2D(
        const std::string_view path,
        Core::IO::IIOBackend& backend) const
    {
        auto route = ResolveAssetImportRoute(
            path,
            AssetRouteOperation::Import,
            AssetImportHint{.PayloadKind = AssetPayloadKind::Texture2D});
        if (!route.has_value())
        {
            return std::unexpected(route.error());
        }
        if (!IsSupportedTextureImportFormat(route->Format))
        {
            return Core::Err<AssetTexture2DPayload>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        auto callback = m_Impl->FindTextureImporter(route->Format);
        if (!callback)
        {
            return Core::Err<AssetTexture2DPayload>(
                Core::ErrorCode::AssetLoaderMissing);
        }

        auto bytes = ReadBytes(backend, path);
        if (!bytes.has_value())
        {
            return std::unexpected(bytes.error());
        }

        AssetModelTextureIORequest request =
            MakeRequest(*route, path, std::span<const std::byte>(*bytes), backend);
        auto decoded = callback(request);
        if (!decoded.has_value())
        {
            return std::unexpected(decoded.error());
        }
        return ValidateDecodedTexture(std::move(*decoded));
    }
}
