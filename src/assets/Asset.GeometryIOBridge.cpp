module;

#include <algorithm>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Asset.GeometryIOBridge;

namespace Extrinsic::Assets
{
    namespace
    {
        struct ImporterEntry
        {
            AssetFileFormat Format{};
            AssetPayloadKind PayloadKind{AssetPayloadKind::Unknown};
            AssetGeometryImportCallback Callback{};
        };

        struct ExporterEntry
        {
            AssetFileFormat Format{};
            AssetPayloadKind PayloadKind{AssetPayloadKind::Unknown};
            AssetGeometryExportCallback Callback{};
        };

        [[nodiscard]] bool SameKey(
            const AssetFileFormat lhsFormat,
            const AssetPayloadKind lhsKind,
            const AssetFileFormat rhsFormat,
            const AssetPayloadKind rhsKind) noexcept
        {
            return lhsFormat == rhsFormat && lhsKind == rhsKind;
        }

        [[nodiscard]] const AssetFileFormatInfo* FindFormatInfo(
            const AssetFileFormat format) noexcept
        {
            for (const AssetFileFormatInfo& info : SupportedAssetFileFormats())
            {
                if (info.Format == format)
                {
                    return &info;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool SupportsPayload(
            const AssetFileFormat format,
            const AssetRouteOperation operation,
            const AssetPayloadKind payloadKind) noexcept
        {
            const AssetFileFormatInfo* info = FindFormatInfo(format);
            if (info == nullptr)
            {
                return false;
            }

            const std::span<const AssetPayloadKind> payloads =
                operation == AssetRouteOperation::Import
                    ? info->ImportPayloads
                    : info->ExportPayloads;
            for (const AssetPayloadKind candidate : payloads)
            {
                if (candidate == payloadKind)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] Core::Result ValidateRegistration(
            const AssetFileFormat format,
            const AssetRouteOperation operation,
            const AssetPayloadKind payloadKind,
            const bool hasCallback) noexcept
        {
            if (!hasCallback || !IsGeometryPayloadKind(payloadKind))
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }
            if (!SupportsPayload(format, operation, payloadKind))
            {
                return Core::Err(Core::ErrorCode::AssetUnsupportedFormat);
            }
            return Core::Ok();
        }

        [[nodiscard]] Core::Expected<AssetGeometryPayload> ValidateDecodedPayload(
            const AssetImportRoute& route,
            AssetGeometryPayload payload)
        {
            if (!payload.IsValid())
            {
                return Core::Err<AssetGeometryPayload>(Core::ErrorCode::AssetInvalidData);
            }
            if (payload.PayloadKind != route.PayloadKind)
            {
                return Core::Err<AssetGeometryPayload>(Core::ErrorCode::AssetTypeMismatch);
            }
            return payload;
        }
    }

    bool IsGeometryPayloadKind(const AssetPayloadKind kind) noexcept
    {
        return kind == AssetPayloadKind::Mesh
            || kind == AssetPayloadKind::PointCloud
            || kind == AssetPayloadKind::Graph;
    }

    struct AssetGeometryIOBridge::Impl
    {
        mutable std::mutex Mutex{};
        std::vector<ImporterEntry> Importers{};
        std::vector<ExporterEntry> Exporters{};

        [[nodiscard]] AssetGeometryImportCallback FindImporter(
            const AssetFileFormat format,
            const AssetPayloadKind payloadKind) const
        {
            std::scoped_lock lock(Mutex);
            const auto it = std::ranges::find_if(
                Importers,
                [format, payloadKind](const ImporterEntry& entry)
                {
                    return SameKey(entry.Format, entry.PayloadKind, format, payloadKind);
                });
            return it == Importers.end() ? AssetGeometryImportCallback{} : it->Callback;
        }

        [[nodiscard]] AssetGeometryExportCallback FindExporter(
            const AssetFileFormat format,
            const AssetPayloadKind payloadKind) const
        {
            std::scoped_lock lock(Mutex);
            const auto it = std::ranges::find_if(
                Exporters,
                [format, payloadKind](const ExporterEntry& entry)
                {
                    return SameKey(entry.Format, entry.PayloadKind, format, payloadKind);
                });
            return it == Exporters.end() ? AssetGeometryExportCallback{} : it->Callback;
        }
    };

    AssetGeometryIOBridge::AssetGeometryIOBridge()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    AssetGeometryIOBridge::~AssetGeometryIOBridge() = default;

    Core::Result AssetGeometryIOBridge::RegisterImporter(
        const AssetFileFormat format,
        const AssetPayloadKind payloadKind,
        AssetGeometryImportCallback callback)
    {
        const Core::Result valid = ValidateRegistration(
            format,
            AssetRouteOperation::Import,
            payloadKind,
            static_cast<bool>(callback));
        if (!valid.has_value())
        {
            return valid;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        auto it = std::ranges::find_if(
            m_Impl->Importers,
            [format, payloadKind](const ImporterEntry& entry)
            {
                return SameKey(entry.Format, entry.PayloadKind, format, payloadKind);
            });
        if (it == m_Impl->Importers.end())
        {
            m_Impl->Importers.push_back(ImporterEntry{format, payloadKind, std::move(callback)});
        }
        else
        {
            it->Callback = std::move(callback);
        }
        return Core::Ok();
    }

    Core::Result AssetGeometryIOBridge::RegisterExporter(
        const AssetFileFormat format,
        const AssetPayloadKind payloadKind,
        AssetGeometryExportCallback callback)
    {
        const Core::Result valid = ValidateRegistration(
            format,
            AssetRouteOperation::Export,
            payloadKind,
            static_cast<bool>(callback));
        if (!valid.has_value())
        {
            return valid;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        auto it = std::ranges::find_if(
            m_Impl->Exporters,
            [format, payloadKind](const ExporterEntry& entry)
            {
                return SameKey(entry.Format, entry.PayloadKind, format, payloadKind);
            });
        if (it == m_Impl->Exporters.end())
        {
            m_Impl->Exporters.push_back(ExporterEntry{format, payloadKind, std::move(callback)});
        }
        else
        {
            it->Callback = std::move(callback);
        }
        return Core::Ok();
    }

    void AssetGeometryIOBridge::Clear() noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Importers.clear();
        m_Impl->Exporters.clear();
    }

    bool AssetGeometryIOBridge::HasImporter(
        const AssetFileFormat format,
        const AssetPayloadKind payloadKind) const noexcept
    {
        return static_cast<bool>(m_Impl->FindImporter(format, payloadKind));
    }

    bool AssetGeometryIOBridge::HasExporter(
        const AssetFileFormat format,
        const AssetPayloadKind payloadKind) const noexcept
    {
        return static_cast<bool>(m_Impl->FindExporter(format, payloadKind));
    }

    Core::Expected<AssetGeometryPayload> AssetGeometryIOBridge::Import(
        const std::string_view path,
        const AssetImportHint hint) const
    {
        auto route = ResolveAssetImportRoute(path, AssetRouteOperation::Import, hint);
        if (!route.has_value())
        {
            return std::unexpected(route.error());
        }
        if (!IsGeometryPayloadKind(route->PayloadKind))
        {
            return Core::Err<AssetGeometryPayload>(Core::ErrorCode::AssetUnsupportedFormat);
        }

        auto callback = m_Impl->FindImporter(route->Format, route->PayloadKind);
        if (!callback)
        {
            return Core::Err<AssetGeometryPayload>(Core::ErrorCode::AssetLoaderMissing);
        }

        auto decoded = callback(AssetGeometryIORequest{.Route = *route, .Path = std::string(path)});
        if (!decoded.has_value())
        {
            return std::unexpected(decoded.error());
        }
        return ValidateDecodedPayload(*route, std::move(*decoded));
    }

    Core::Result AssetGeometryIOBridge::Export(
        const std::string_view path,
        const AssetGeometryPayload& payload) const
    {
        if (!payload.IsValid() || !IsGeometryPayloadKind(payload.PayloadKind))
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        auto route = ResolveAssetImportRoute(
            path,
            AssetRouteOperation::Export,
            AssetImportHint{.PayloadKind = payload.PayloadKind});
        if (!route.has_value())
        {
            return std::unexpected(route.error());
        }

        auto callback = m_Impl->FindExporter(route->Format, route->PayloadKind);
        if (!callback)
        {
            return Core::Err(Core::ErrorCode::AssetLoaderMissing);
        }

        return callback(
            AssetGeometryIORequest{.Route = *route, .Path = std::string(path)},
            payload);
    }
}
