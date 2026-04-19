module;

#include <cstddef>
#include <unistd.h>
#include <span>
#include <memory>
#include <type_traits>
#include <utility>

export module Extrinsic.Asset.PayloadStore;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.TypePool;

namespace Extrinsic::Assets
{
    export struct PayloadTicket
    {
        uint64_t slot = 0;
        uint64_t generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return slot != 0; }
        auto operator<=>(const PayloadTicket&) const = default;
    };

    export class AssetPayloadStore
    {
    public:
        AssetPayloadStore();
        ~AssetPayloadStore();

        template <class T>
        [[nodiscard]] Core::Expected<PayloadTicket> Publish(AssetId id, T&& value);

        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> ReadSpan(AssetId id) const;

        [[nodiscard]] Core::Expected<PayloadTicket> GetTicket(AssetId id) const;

        Core::Result Retire(AssetId id);

        [[nodiscard]] std::size_t Size() const;

    private:
        using TypeId = TypePools<AssetId>::TypeId;
        static constexpr std::size_t kShardCount = 32;
        struct Impl;
        struct ReadView
        {
            const void* ptr = nullptr;
            std::size_t count = 0;
        };

        [[nodiscard]] static std::size_t ShardIndex(AssetId id) noexcept;
        [[nodiscard]] Core::Expected<PayloadTicket> PublishUntyped(
            AssetId id,
            TypeId typeId,
            std::shared_ptr<const void> payload,
            std::size_t count,
            const void* (*dataFn)(const std::shared_ptr<const void>&));
        [[nodiscard]] Core::Expected<ReadView> ReadUntyped(AssetId id, TypeId typeId) const;

        std::unique_ptr<Impl> m_Impl;
    };

    template <class T>
    Core::Expected<PayloadTicket> AssetPayloadStore::Publish(AssetId id, T&& value)
    {
        using StoredT = std::remove_cvref_t<T>;
        (void)::write(2, "P0\n", 3);
        if (!id.IsValid())
        {
            return Core::Err<PayloadTicket>(Core::ErrorCode::InvalidArgument);
        }

        (void)::write(2, "P1\n", 3);
        const auto newTypeId = TypePools<AssetId>::Type<StoredT>();
        (void)::write(2, "P2\n", 3);
        auto payload = std::make_shared<StoredT>(std::forward<T>(value));
        (void)::write(2, "P3\n", 3);
        auto dataFn = +[](const std::shared_ptr<const void>& p) -> const void*
        {
            return static_cast<const StoredT*>(p.get());
        };
        (void)::write(2, "P4\n", 3);
        return PublishUntyped(id, newTypeId, std::move(payload), 1, dataFn);
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetPayloadStore::ReadSpan(AssetId id) const
    {
        using StoredT = std::remove_cvref_t<T>;
        auto view = ReadUntyped(id, TypePools<AssetId>::Type<StoredT>());
        if (!view.has_value())
        {
            return Core::Err<std::span<const T>>(view.error());
        }

        const auto* typed = static_cast<const StoredT*>(view->ptr);
        return std::span<const T>(typed, view->count);
    }
}
