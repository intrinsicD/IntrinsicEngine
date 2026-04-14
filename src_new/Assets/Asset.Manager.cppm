module;

#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <shared_mutex>
#include <entt/entity/registry.hpp>

export module Extrinsic.Asset.Manager;

import Extrinsic.Asset.Handle;
import Extrinsic.Core.Error;
import Extrinsic.Core.Hash;

namespace Extrinsic::Assets
{
    enum class LoadState : std::uint32_t
    {
        Unloaded,
        Loading,
        Processing,
        Ready,
        Failed
    };

    [[nodiscard]] constexpr std::string_view ToString(LoadState ls) noexcept
    {
        switch (ls)
        {
        case LoadState::Unloaded: return "Unloaded";
        case LoadState::Loading: return "Loading";
        case LoadState::Processing: return "Processing";
        case LoadState::Ready: return "Ready";
        case LoadState::Failed: return "Failed";
        default: return "Unknown";
        }
    }

    struct AssetMetaData
    {
        std::string Name;
        std::string Type;

        std::shared_ptr<std::function<void()>> ReloadAction;
        std::string FilePath;
        LoadState State = LoadState::Unloaded;
    };

    template <typename T>
    struct AssetSlot
    {
        std::unique_ptr<T> Unique;
        std::shared_ptr<T> Shared;
        std::atomic<std::uint32_t> PinCount{0};

        explicit AssetSlot(std::unique_ptr<T> resource) : Unique(std::move(resource))
        {
        }

        explicit AssetSlot(std::shared_ptr<T> resource) : Shared(std::move(resource))
        {
        }

        [[nodiscard]] T* Get() const noexcept
        {
            return Unique ? Unique.get() : Shared.get();
        }
    };

    template <typename T>
    struct AssetPayload
    {
        std::shared_ptr<AssetSlot<T>> Slot;
        T* Ptr = nullptr;

        explicit AssetPayload(std::shared_ptr<AssetSlot<T>> slot) : Slot(std::move(slot))
        {
            Ptr = std::static_pointer_cast<AssetSlot<T>>(Slot)->Get();
        }
    };

    template <typename T>
    struct AssetLease
    {
        AssetLease() = default;

        explicit AssetLease(std::shared_ptr<AssetSlot<T>> slot) : m_Slot(std::move(slot))
        {
            PinIfValid();
        }

        AssetLease(const AssetLease& other) : m_Slot(other.m_Slot)
        {
            PinIfValid();
        }

        AssetLease(AssetLease&& other) noexcept
            : m_Slot(std::move(other.m_Slot))
        {
        }

        AssetLease& operator=(const AssetLease& other)
        {
            if (this == &other) return *this;
            Reset();
            m_Slot = other.m_Slot;
            PinIfValid();
            return *this;
        }

        AssetLease& operator=(AssetLease&& other) noexcept
        {
            if (this == &other) return *this;
            Reset();
            m_Slot = std::move(other.m_Slot);
            return *this;
        }

        ~AssetLease() { Reset(); }

        void Reset()
        {
            if (m_Slot)
            {
                // release ensures all accesses through this lease are visible to the
                // thread that performs the reload (which observes PinCount == 0).
                m_Slot->PinCount.fetch_sub(1, std::memory_order_release);
                m_Slot.reset();
            }
        }

        [[nodiscard]] T* Get() const noexcept { return m_Slot ? m_Slot->Get() : nullptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return Get() != nullptr; }

        [[nodiscard]] T& operator*() const noexcept { return *m_Slot->Get(); }
        [[nodiscard]] T* operator->() const noexcept { return m_Slot->Get(); }

    private:
        void PinIfValid()
        {
            if (m_Slot)
            {
                // acquire ensures subsequent reads through this lease see writes made
                // by the thread that published (or reloaded) the resource.
                m_Slot->PinCount.fetch_add(1, std::memory_order_acquire);
            }
        }

        std::shared_ptr<AssetSlot<T>> m_Slot{};
    };

    class AssetManager
    {
    public:
        using AssetCallback = std::function<void(AssetHandle)>;
        using ListenerHandle = std::uint32_t;
        static constexpr ListenerHandle InvalidListenerHandle{};

        AssetManager() = default;

        // Debug-verified phase ordering for TryGetFast.
        // BeginReadPhase/EndReadPhase must bracket any parallel read usage.
        void BeginReadPhase() const;

        // Debug-verified phase ordering for TryGetFast.
        // BeginReadPhase/EndReadPhase must bracket any parallel read usage.
        void EndReadPhase() const;

        void Update();

        Core::Expected<AssetHandle> CreateAsset(AssetMetaData metaData, std::function<void(std::string_view)> &&loader);

        void Reload(AssetHandle handle, std::function<void(std::string_view)> &&loader);

        bool DestroyAsset(AssetHandle handle);

        void FinalizeLoad(AssetHandle handle);

        Core::Expected<const AssetMetaData&> GetAssetMetaData(AssetHandle handle) const;
        [[nodiscard]] LoadState GetState(AssetHandle handle) const;

        ListenerHandle Listen(AssetHandle handle, AssetCallback callback);
        void Unlisten(AssetHandle handle, ListenerHandle listenerID);
        void ListenOnce(AssetHandle handle, AssetCallback callback);

        void Clear();

        template <typename T>
        Core::Expected<AssetLease<T>> AquireLease(AssetHandle handle);

        template <typename T>
        Core::Expected<T*> TryGet(AssetHandle handle);

        template <typename T>
        Core::Expected<const T*> TryGet(AssetHandle handle) const;

        template <typename T>
        T* TryGetFast(AssetHandle handle);

        template <typename T>
        const T* TryGetFast(AssetHandle handle) const;

    private:
        entt::registry m_Registry;
        std::unordered_map<Core::Hash::StringID, AssetHandle> m_Lookup;
        std::unordered_map<AssetHandle, std::vector<AssetCallback>> m_OneShotListeners;
        std::unordered_map<AssetHandle, std::unordered_map<ListenerHandle, AssetCallback>> m_PersistentListeners;

        std::vector<AssetHandle> m_ReadyQueue; // Queue of assets loaded since last frame
        mutable std::mutex m_Mutex;
        mutable std::mutex m_EventQueueMutex;
#ifndef NDEBUG
        mutable std::atomic<int> m_DebugReadPhase{0};
#endif

        void EnqueueReady(AssetHandle handle);
    };

    template <typename T>
    Core::Expected<AssetLease<T>> AssetManager::AquireLease(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle))
        {
            return Core::Err<AssetLease<T>>(Core::ErrorCode::ResourceNotFound);
        }
        const auto& metaData = m_Registry.get<AssetMetaData>(handle);

        if (metaData.State != LoadState::Ready)
        {
            if (metaData.State == LoadState::Failed)
            {
                return Core::Err<AssetLease<T>>(Core::ErrorCode::AssetLoadFailed);
            }
            return Core::Err<AssetLease<T>>(Core::ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle))
        {
            return AssetLease<T>(std::static_pointer_cast<AssetSlot<T>>(payload->Slot));
        }

        return Core::Err<AssetLease<T>>(Core::ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    Core::Expected<T*> AssetManager::TryGet(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle))
        {
            return Core::Err<T*>(Core::ErrorCode::ResourceNotFound);
        }

        const auto& metaData = m_Registry.get<AssetMetaData>(handle);
        if (metaData.State != LoadState::Ready)
        {
            if (metaData.State == LoadState::Failed)
            {
                return Core::Err<T*>(Core::ErrorCode::AssetLoadFailed);
            }
            return Core::Err<T*>(Core::ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle))
        {
            return payload->Ptr;
        }
        return Core::Err<T*>(Core::ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    Core::Expected<const T*> AssetManager::TryGet(AssetHandle handle) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle))
        {
            return Core::Err<const T*>(Core::ErrorCode::ResourceNotFound);
        }

        const auto& metaData = m_Registry.get<AssetMetaData>(handle);
        if (metaData.State != LoadState::Ready)
        {
            if (metaData.State == LoadState::Failed)
            {
                return Core::Err<const T*>(Core::ErrorCode::AssetLoadFailed);
            }
            return Core::Err<const T*>(Core::ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle))
        {
            return payload->Ptr;
        }
        return Core::Err<const T*>(Core::ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    T* AssetManager::TryGetFast(AssetHandle handle)
    {
#ifndef NDEBUG
        assert(m_DebugReadPhase.load(std::memory_order_relaxed) > 0 &&
            "AssetManager::GetLockfree called outside read phase. Use BeginReadPhase/EndReadPhase or TryGet/AcquireLease.");
#endif
        if (!m_Registry.valid(handle)) return nullptr;

        const auto* payload = m_Registry.try_get<AssetPayload<T>>(handle);

        return payload ? payload->Ptr : nullptr;
    }

    template <typename T>
    const T* AssetManager::TryGetFast(AssetHandle handle) const
    {
#ifndef NDEBUG
        assert(m_DebugReadPhase.load(std::memory_order_relaxed) > 0 &&
            "AssetManager::GetLockfree called outside read phase. Use BeginReadPhase/EndReadPhase or TryGet/AcquireLease.");
#endif
        if (!m_Registry.valid(handle)) return nullptr;

        const auto* payload = m_Registry.try_get<AssetPayload<T>>(handle);

        return payload ? payload->Ptr : nullptr;
    }
}
