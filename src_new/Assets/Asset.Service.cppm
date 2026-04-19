module;

#include <string>
#include <string_view>
#include <span>
#include <cstdint>
#include <memory>
#include <expected>
#include <functional>

export module Extrinsic.Asset.Service;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.TypePool;
import Extrinsic.Core.Filesystem.PathResolver;

export namespace Extrinsic::Assets
{
    // Tag type for loader-callback tokens. Distinct from other StrongHandle
    // tags (AssetTag, etc.) so tokens cannot be accidentally mixed.
    struct AssetLoaderTag;

    struct LoaderToken
    {
        uint32_t Index = 0;
        uint32_t Generation = 0;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return Generation != 0; }
        auto operator<=>(const LoaderToken&) const = default;
    };

    class AssetService
    {
    public:
        AssetService();
        ~AssetService();
        AssetService(const AssetService&) = delete;
        AssetService& operator=(const AssetService&) = delete;
        AssetService(AssetService&&) = delete;
        AssetService& operator=(AssetService&&) = delete;

        // Non-template Load: caller provides decoded payload, type id, and path.
        [[nodiscard]] Core::Expected<AssetId> LoadErased(
            std::string_view path,
            uint32_t typeId,
            std::function<Core::Expected<PayloadTicket>(std::string_view absPath, AssetId id)> loader);

        // Non-template Reload: re-run a previously registered loader.
        Core::Result Reload(AssetId id);

        // Typed read.
        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> Read(AssetId id) const;

        // Typed load convenience wrapper.
        template <class T, class Loader>
        [[nodiscard]] Core::Expected<AssetId> Load(std::string_view path, Loader&& loader);

        template <class T, class Loader>
        Core::Result Reload(AssetId id, Loader&& loader);

        [[nodiscard]] Core::Expected<AssetMeta> GetMeta(AssetId id) const;
        [[nodiscard]] Core::Expected<std::string> GetPath(AssetId id) const;
        [[nodiscard]] Core::Expected<LoaderToken> GetReloadToken(AssetId id) const;
        [[nodiscard]] bool IsAlive(AssetId id) const noexcept;

        Core::Result Destroy(AssetId id);
        void Tick();

        // Exposed for advanced wiring / testing.
        [[nodiscard]] AssetRegistry& Registry() noexcept;
        [[nodiscard]] AssetEventBus& EventBus() noexcept;
        [[nodiscard]] AssetPayloadStore& PayloadStore() noexcept;
        [[nodiscard]] const AssetPayloadStore& PayloadStore() const noexcept;
        [[nodiscard]] AssetLoadPipeline& LoadPipeline() noexcept;

        template <class T>
        [[nodiscard]] static uint32_t TypeIdOf() noexcept;

        // Test/debug introspection (thin wrappers over internal state).
        [[nodiscard]] bool HasLoaderCallback(LoaderToken token) const;
        [[nodiscard]] std::size_t LoaderCallbackCount() const;
        [[nodiscard]] bool PathIndexContains(std::string_view absolutePath) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    template <class T>
    uint32_t AssetService::TypeIdOf() noexcept
    {
        const auto p = TypePools<AssetId>::Type<std::remove_cvref_t<T>>();
        if constexpr (sizeof(std::uintptr_t) > sizeof(uint32_t))
        {
            return static_cast<uint32_t>(p ^ (p >> 32));
        }
        else
        {
            return static_cast<uint32_t>(p);
        }
    }

    template <class T, class Loader>
    [[nodiscard]] Core::Expected<AssetId> AssetService::Load(std::string_view path, Loader&& loader)
    {
        const uint32_t typeId = TypeIdOf<T>();
        auto captured = std::forward<Loader>(loader);
        auto erased = [this, captured = std::move(captured)](
                          std::string_view absPath, AssetId id) mutable
            -> Core::Expected<PayloadTicket> {
            auto payload = captured(absPath, id);
            if (!payload.has_value())
            {
                return std::unexpected(payload.error());
            }
            return PayloadStore().Publish(id, std::move(*payload));
        };
        return LoadErased(path, typeId, std::move(erased));
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetService::Read(AssetId id) const
    {
        if (!IsAlive(id))
        {
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        }
        return PayloadStore().ReadSpan<T>(id);
    }

    template <class T, class Loader>
    Core::Result AssetService::Reload(AssetId id, Loader&& loader)
    {
        if (!IsAlive(id))
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        const auto meta = Registry().GetMeta(id);
        if (!meta.has_value())
        {
            return std::unexpected(meta.error());
        }

        if (meta->typeId != TypeIdOf<T>())
        {
            return Core::Err(Core::ErrorCode::TypeMismatch);
        }

        if (meta->state != AssetState::Ready)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto pathResult = GetPath(id);
        if (!pathResult.has_value())
        {
            return std::unexpected(pathResult.error());
        }
        std::string path = std::move(*pathResult);

        auto payload = loader(std::string_view(path), id);
        if (!payload.has_value())
        {
            return std::unexpected(payload.error());
        }

        auto toUnloaded = Registry().SetState(id, AssetState::Ready, AssetState::Unloaded);
        if (!toUnloaded.has_value())
        {
            return toUnloaded;
        }

        auto ticket = PayloadStore().Publish(id, std::move(*payload));
        if (!ticket.has_value())
        {
            (void)LoadPipeline().MarkFailed(id);
            return std::unexpected(ticket.error());
        }
        (void)Registry().SetPayloadSlot(id, static_cast<uint32_t>(ticket->slot));

        LoadRequest req{.id = id, .typeId = meta->typeId, .path = path, .needsGpuUpload = false};
        if (auto r = LoadPipeline().EnqueueIO(std::move(req)); !r.has_value())
        {
            (void)LoadPipeline().MarkFailed(id);
            return r;
        }

        EventBus().Publish(id, AssetEvent::Reloaded);
        return Core::Ok();
    }
}
