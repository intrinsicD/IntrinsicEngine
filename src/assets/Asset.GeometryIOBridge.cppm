module;

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

export module Extrinsic.Asset.GeometryIOBridge;

import Extrinsic.Core.Error;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.TypePool;

export namespace Extrinsic::Assets
{
    struct AssetGeometryIOBridgeTag;

    [[nodiscard]] bool IsGeometryPayloadKind(AssetPayloadKind kind) noexcept;

    template <class T>
    [[nodiscard]] std::uint32_t AssetPayloadTypeIdOf() noexcept
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto p = TypePools<AssetId>::Type<StoredT>();
        if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
        {
            return static_cast<std::uint32_t>(p ^ (p >> 32));
        }
        else
        {
            return static_cast<std::uint32_t>(p);
        }
    }

    struct AssetGeometryIORequest
    {
        AssetImportRoute Route{};
        std::string Path{};
    };

    struct AssetGeometryPayload
    {
        AssetPayloadKind PayloadKind{AssetPayloadKind::Unknown};
        std::uint32_t TypeId{0};
        std::shared_ptr<const void> Payload{};
        std::string DebugTypeName{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return PayloadKind != AssetPayloadKind::Unknown && TypeId != 0u && Payload != nullptr;
        }

        template <class T>
        [[nodiscard]] static AssetGeometryPayload Make(
            AssetPayloadKind payloadKind,
            T&& value,
            std::string_view debugTypeName = {})
        {
            using StoredT = std::remove_cvref_t<T>;
            return AssetGeometryPayload{
                .PayloadKind = payloadKind,
                .TypeId = AssetPayloadTypeIdOf<StoredT>(),
                .Payload = std::make_shared<StoredT>(std::forward<T>(value)),

                .DebugTypeName = std::string(debugTypeName),
            };
        }

        template <class T>
        [[nodiscard]] Core::Expected<std::shared_ptr<const std::remove_cvref_t<T>>> Read() const
        {
            using StoredT = std::remove_cvref_t<T>;
            if (Payload == nullptr || TypeId == 0u)
            {
                return Core::Err<std::shared_ptr<const StoredT>>(Core::ErrorCode::AssetInvalidData);
            }
            if (TypeId != AssetPayloadTypeIdOf<StoredT>())
            {
                return Core::Err<std::shared_ptr<const StoredT>>(Core::ErrorCode::AssetTypeMismatch);
            }
            return std::static_pointer_cast<const StoredT>(Payload);
        }
    };

    using AssetGeometryImportCallback =
        std::function<Core::Expected<AssetGeometryPayload>(const AssetGeometryIORequest&)>;
    using AssetGeometryExportCallback =
        std::function<Core::Result(const AssetGeometryIORequest&, const AssetGeometryPayload&)>;

    class AssetGeometryIOBridge
    {
    public:
        AssetGeometryIOBridge();
        ~AssetGeometryIOBridge();
        AssetGeometryIOBridge(const AssetGeometryIOBridge&) = delete;
        AssetGeometryIOBridge& operator=(const AssetGeometryIOBridge&) = delete;
        AssetGeometryIOBridge(AssetGeometryIOBridge&&) = delete;
        AssetGeometryIOBridge& operator=(AssetGeometryIOBridge&&) = delete;

        [[nodiscard]] Core::Result RegisterImporter(
            AssetFileFormat format,
            AssetPayloadKind payloadKind,
            AssetGeometryImportCallback callback);

        [[nodiscard]] Core::Result RegisterExporter(
            AssetFileFormat format,
            AssetPayloadKind payloadKind,
            AssetGeometryExportCallback callback);

        template <class T, class Loader>
        [[nodiscard]] Core::Result RegisterTypedImporter(
            AssetFileFormat format,
            AssetPayloadKind payloadKind,
            Loader&& loader,
            std::string_view debugTypeName = {})
        {
            using StoredT = std::remove_cvref_t<T>;
            auto captured = std::forward<Loader>(loader);
            std::string typeName{debugTypeName};
            return RegisterImporter(
                format,
                payloadKind,
                [captured = std::move(captured),
                 payloadKind,
                 typeName = std::move(typeName)](
                    const AssetGeometryIORequest& request) mutable
                    -> Core::Expected<AssetGeometryPayload>
                {
                    Core::Expected<StoredT> decoded = captured(request);
                    if (!decoded.has_value())
                    {
                        return std::unexpected(decoded.error());
                    }
                    return AssetGeometryPayload::Make<StoredT>(
                        payloadKind,
                        std::move(*decoded),
                        typeName);
                });
        }

        template <class T, class Writer>
        [[nodiscard]] Core::Result RegisterTypedExporter(
            AssetFileFormat format,
            AssetPayloadKind payloadKind,
            Writer&& writer)
        {
            using StoredT = std::remove_cvref_t<T>;
            auto captured = std::forward<Writer>(writer);
            return RegisterExporter(
                format,
                payloadKind,
                [captured = std::move(captured)](
                    const AssetGeometryIORequest& request,
                    const AssetGeometryPayload& payload) mutable
                    -> Core::Result
                {
                    auto typed = payload.Read<StoredT>();
                    if (!typed.has_value())
                    {
                        return std::unexpected(typed.error());
                    }
                    return captured(request, **typed);
                });
        }

        void Clear() noexcept;

        [[nodiscard]] bool HasImporter(
            AssetFileFormat format,
            AssetPayloadKind payloadKind) const noexcept;

        [[nodiscard]] bool HasExporter(
            AssetFileFormat format,
            AssetPayloadKind payloadKind) const noexcept;

        [[nodiscard]] Core::Expected<AssetGeometryPayload> Import(
            std::string_view path,
            AssetImportHint hint = {}) const;

        [[nodiscard]] Core::Result Export(
            std::string_view path,
            const AssetGeometryPayload& payload) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
