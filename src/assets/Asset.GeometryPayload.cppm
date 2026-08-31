// Defines type-erased CPU geometry payloads so assets can carry mesh,
// point-cloud, and graph imports without depending on geometry modules.
module;

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

export module Extrinsic.Asset.GeometryPayload;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.TypePool;
import Extrinsic.Core.Error;

export namespace Extrinsic::Assets
{
    [[nodiscard]] inline bool IsGeometryPayloadKind(
        const AssetPayloadKind kind) noexcept
    {
        return kind == AssetPayloadKind::Mesh ||
            kind == AssetPayloadKind::PointCloud ||
            kind == AssetPayloadKind::Graph;
    }

    template <class T>
    [[nodiscard]] std::uint32_t AssetPayloadTypeIdOf() noexcept
    {
        using StoredT = std::remove_cvref_t<T>;
        const auto value = TypePools<AssetId>::Type<StoredT>();
        if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
        {
            return static_cast<std::uint32_t>(value ^ (value >> 32));
        }
        return static_cast<std::uint32_t>(value);
    }

    struct AssetGeometryPayload
    {
        AssetPayloadKind PayloadKind{AssetPayloadKind::Unknown};
        std::uint32_t TypeId{0};
        std::shared_ptr<const void> Payload{};
        std::string DebugTypeName{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return IsGeometryPayloadKind(PayloadKind) &&
                TypeId != 0u && Payload != nullptr;
        }

        template <class T>
        [[nodiscard]] static AssetGeometryPayload Make(
            const AssetPayloadKind payloadKind,
            T&& value,
            const std::string_view debugTypeName = {})
        {
            using StoredT = std::remove_cvref_t<T>;
            return AssetGeometryPayload{
                .PayloadKind = payloadKind,
                .TypeId = AssetPayloadTypeIdOf<StoredT>(),
                .Payload = std::make_shared<StoredT>(
                    std::forward<T>(value)),
                .DebugTypeName = std::string(debugTypeName),
            };
        }

        template <class T>
        [[nodiscard]] Core::Expected<
            std::shared_ptr<const std::remove_cvref_t<T>>> Read() const
        {
            using StoredT = std::remove_cvref_t<T>;
            if (Payload == nullptr || TypeId == 0u)
            {
                return Core::Err<std::shared_ptr<const StoredT>>(
                    Core::ErrorCode::AssetInvalidData);
            }
            if (TypeId != AssetPayloadTypeIdOf<StoredT>())
            {
                return Core::Err<std::shared_ptr<const StoredT>>(
                    Core::ErrorCode::AssetTypeMismatch);
            }
            return std::static_pointer_cast<const StoredT>(Payload);
        }
    };
}
