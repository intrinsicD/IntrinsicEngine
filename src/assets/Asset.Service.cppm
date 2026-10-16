// Exposes the CPU asset facade that coordinates identity, paths, payloads,
// load state, and lifecycle events behind one invariant-preserving API.
module;

#include <string>
#include <string_view>
#include <span>
#include <cstdint>
#include <memory>
#include <expected>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

export module Extrinsic.Asset.Service;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.PayloadStore;
import Extrinsic.Asset.LoadPipeline;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.TypePool;

export namespace Extrinsic::Assets
{
    // Public calls serialize publication; decoders and listeners run unlocked.
    // The service must outlive its calls, outstanding callbacks, and borrowed reads.
    class AssetService
    {
    public:
        explicit AssetService(AssetLoadPipelineTestHooks testHooks = {});
        ~AssetService();
        AssetService(const AssetService&) = delete;
        AssetService& operator=(const AssetService&) = delete;
        AssetService(AssetService&&) = delete;
        AssetService& operator=(AssetService&&) = delete;

        // A successful read borrows storage until the next Reload/Destroy of id.
        // Callers must order span use against concurrent mutations of that asset.
        template <class T>
        [[nodiscard]] Core::Expected<std::span<const T>> Read(AssetId id) const;

        template <class T, class Loader>
        [[nodiscard]] Core::Expected<AssetId> Load(std::string_view path, Loader&& loader);

        // A competing successful reload wins over an older decode (ResourceBusy).
        template <class T, class Loader>
        Core::Result Reload(AssetId id, Loader&& loader);

        [[nodiscard]] Core::Expected<AssetMeta> GetMeta(AssetId id) const;
        [[nodiscard]] Core::Expected<std::string> GetPath(AssetId id) const;
        [[nodiscard]] Core::Expected<PayloadTicket> GetPayloadTicket(AssetId id) const;
        [[nodiscard]] bool IsAlive(AssetId id) const noexcept;

        Core::Result Destroy(AssetId id);
        void Tick();
        // Joins state/event publication, then drains callbacks without a service lock.
        Core::Result CompleteCpuLoadAndFlushEvent(AssetId id);

        template <class T>
        [[nodiscard]] static uint32_t TypeIdOf() noexcept;

        // Nonterminal events are delivered only while id is alive. Events describe
        // history, not a retained payload version; a later Reload may already be visible.
        [[nodiscard]] AssetEventBus::ListenerToken SubscribeAll(AssetEventBus::ListenerCallback cb);
        void UnsubscribeAll(AssetEventBus::ListenerToken token);

        // Read-only diagnostic seams do not grant component mutation.
        [[nodiscard]] bool PathIndexContains(std::string_view absolutePath) const;
        [[nodiscard]] std::size_t LiveAssetCount() const noexcept;
        // Test seam; production state transitions remain owned by the pipeline.
        Core::Result ForceAssetState(AssetId id, AssetState expected, AssetState next);

    private:
        using PayloadPublisher = std::move_only_function<Core::Expected<PayloadTicket>()>;
        using PreparedLoader = std::move_only_function<Core::Expected<PayloadPublisher>(std::string_view, AssetId)>;

        template <class T, class Loader>
        [[nodiscard]] PreparedLoader PrepareLoader(Loader&& loader);
        [[nodiscard]] Core::Expected<AssetId> LoadErased(
            std::string_view path, uint32_t typeId, PreparedLoader loader);
        Core::Result ReloadErased(AssetId id, uint32_t typeId, PreparedLoader loader);

        // Inline templates cannot dereference the incomplete Impl.
        [[nodiscard]] std::mutex& TransactionMutex() const noexcept;
        [[nodiscard]] const AssetRegistry& Registry() const noexcept;
        [[nodiscard]] AssetPayloadStore& PayloadStore() noexcept;
        [[nodiscard]] const AssetPayloadStore& PayloadStore() const noexcept;

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    template <class T>
    uint32_t AssetService::TypeIdOf() noexcept
    {
        return TypePools<AssetId>::Type32<T>();
    }

    template <class T, class Loader>
    AssetService::PreparedLoader AssetService::PrepareLoader(Loader&& loader)
    {
        return [this, captured = std::forward<Loader>(loader)](
                   std::string_view absPath, AssetId id) mutable -> Core::Expected<PayloadPublisher>
        {
            // Enforce the requested type at the decoding boundary, before publication.
            Core::Expected<std::remove_cvref_t<T>> payload = captured(absPath, id);
            if (!payload.has_value())
                return std::unexpected(payload.error());

            return PayloadPublisher{[this, id, decoded = std::move(*payload)]() mutable
            {
                return PayloadStore().Publish(id, std::move(decoded));
            }};
        };
    }

    template <class T, class Loader>
    Core::Expected<AssetId> AssetService::Load(std::string_view path, Loader&& loader)
    {
        return LoadErased(path, TypeIdOf<T>(), PrepareLoader<T>(std::forward<Loader>(loader)));
    }

    template <class T>
    Core::Expected<std::span<const T>> AssetService::Read(AssetId id) const
    {
        std::scoped_lock lock(TransactionMutex());
        if (!Registry().IsAlive(id))
            return Core::Err<std::span<const T>>(Core::ErrorCode::ResourceNotFound);
        return PayloadStore().ReadSpan<T>(id);
    }

    template <class T, class Loader>
    Core::Result AssetService::Reload(AssetId id, Loader&& loader)
    {
        return ReloadErased(id, TypeIdOf<T>(), PrepareLoader<T>(std::forward<Loader>(loader)));
    }
}
