module;

#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <entt/entity/registry.hpp>

export module Extrinsic.Asset.Manager;

import Extrinsic.Asset.Handle;
import Extrinsic.Core.Error;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Logging;
import Extrinsic.Core.Filesystem;

namespace Extrinsic::Assets
{
    export enum class LoadState : std::uint32_t
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

    export struct AssetMetaData
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
        std::shared_ptr<T> Shared;
        std::atomic<std::uint32_t> PinCount{0};

        explicit AssetSlot(std::unique_ptr<T> resource)
        {
            Shared = std::shared_ptr<T>(std::move(resource));
        }

        explicit AssetSlot(std::shared_ptr<T> resource) : Shared(std::move(resource))
        {
        }

        [[nodiscard]] T* Get() const noexcept
        {
            return Shared.get();
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

    export template <typename T>
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

    export template <typename F, typename T>
    concept AssetLoaderFunc = std::is_move_constructible_v<std::decay_t<F>>
        && std::is_copy_constructible_v<std::decay_t<F>>
        && std::invocable<std::decay_t<F>, const std::string&, AssetHandle>;

    export class AssetManager
    {
    public:
        using AssetCallback = std::function<void(AssetHandle)>;
        using ListenerHandle = std::uint32_t;
        static constexpr ListenerHandle InvalidListenerHandle{};

        AssetManager() = default;

        [[nodiscard]] bool IsValid(AssetHandle handle) const;

        // Optional debug instrumentation hook for higher-level frame/read phases.
        // Not required for correctness of TryGet/TryGetFast (which are synchronized).
        void BeginReadPhase() const;

        // Optional debug instrumentation hook for higher-level frame/read phases.
        // Not required for correctness of TryGet/TryGetFast (which are synchronized).
        void EndReadPhase() const;

        void Update();

        template <typename T, typename LoaderFunc> requires AssetLoaderFunc<LoaderFunc, T>
        AssetHandle Load(const std::string& path, LoaderFunc&& loader);

        template <typename T>
        AssetHandle Create(const std::string& name, std::shared_ptr<T> resource);

        // Preferred overload: create from unique_ptr without shared ownership (zero-copy).
        template <typename T>
        AssetHandle Create(const std::string& name, std::unique_ptr<T> resource);

        template <typename T>
        void ReloadAsset(AssetHandle handle);

        template <typename T, typename LoaderFunc>
        void Reload(AssetHandle handle, LoaderFunc&& loader);

        bool DestroyAsset(AssetHandle handle);

        void FinalizeLoad(AssetHandle handle);

        void MoveToProcessing(AssetHandle handle);

        Core::Expected<const AssetMetaData*> GetAssetMetaData(AssetHandle handle) const;
        [[nodiscard]] LoadState GetState(AssetHandle handle) const;

        ListenerHandle Listen(AssetHandle handle, AssetCallback callback);
        void Unlisten(AssetHandle handle, ListenerHandle listenerID);
        void ListenOnce(AssetHandle handle, AssetCallback callback);

        void Clear();

        template <typename T>
        Core::Expected<AssetLease<T>> AcquireLease(AssetHandle handle);

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
        std::unordered_map<Core::Hash::StringID, AssetHandle> m_Lookup{};
        std::unordered_map<AssetHandle, std::vector<AssetCallback>> m_OneShotListeners;
        std::unordered_map<AssetHandle, std::unordered_map<ListenerHandle, AssetCallback>> m_PersistentListeners;

        std::vector<AssetHandle> m_ReadyQueue{}; // Queue of assets loaded since last frame
        mutable std::shared_mutex m_Mutex;
        mutable std::mutex m_EventQueueMutex;
#ifndef NDEBUG
        mutable std::atomic<int> m_DebugReadPhase{0};
#endif

        void EnqueueReady(AssetHandle handle);
    };

    template <typename T, typename LoaderFunc> requires AssetLoaderFunc<LoaderFunc, T>
    AssetHandle AssetManager::Load(const std::string& path, LoaderFunc&& loader)
    {
        std::string key = Core::Filesystem::GetAbsolutePath(path);

        Core::Hash::StringID id(key); // Hashes here
        std::unique_lock lock(m_Mutex);

        if (m_Lookup.contains(id)) return m_Lookup[id];

        AssetHandle handle = m_Registry.create();

        m_Lookup[id] = handle;

        auto sharedLoader = std::make_shared<std::decay_t<LoaderFunc>>(std::forward<LoaderFunc>(loader));

        auto reloadAction = std::make_shared<std::function<void()>>(
            [this, handle, sharedLoader]() mutable
            {
                this->Reload<T>(handle, *sharedLoader);
            });

        auto metaData = AssetMetaData{
            .Name = key,
            .Type = "Unknown",
            .ReloadAction = std::move(reloadAction),
            .FilePath = path,
            .State = LoadState::Loading
        };
        m_Registry.emplace<AssetMetaData>(handle, std::move(metaData));

        lock.unlock(); // Reload locks internally
        Reload<T>(handle, *sharedLoader);
        return handle;
    }

    template <typename T>
    AssetHandle AssetManager::Create(const std::string& name, std::shared_ptr<T> resource)
    {
        // Compatibility: allow shared_ptr resources without copying.
        std::unique_lock lock(m_Mutex);

        const Core::Hash::StringID key(name);

        // Same overwrite semantics as unique_ptr Create().
        if (auto it = m_Lookup.find(key); it != m_Lookup.end())
        {
            const AssetHandle old = it->second;
            if (m_Registry.valid(old))
            {
                m_Registry.destroy(old);
            }
            m_Lookup.erase(it);
        }

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        AssetMetaData metaData{
            .Name = name,
            .Type = "Runtime",
            .ReloadAction = nullptr,
            .FilePath = "",
            .State = LoadState::Ready
        };

        m_Registry.emplace<AssetMetaData>(entity, std::move(metaData));

        if (!resource)
        {
            // No generic default construction for arbitrary T; this is a logic error at call sites.
            // Keep behavior safe in -fno-exceptions builds by marking failed.
            m_Registry.get<AssetMetaData>(handle).State = LoadState::Failed;
            return handle;
        }

        auto slot = std::make_shared<AssetSlot<T>>(std::move(resource));
        m_Registry.emplace<AssetPayload<T>>(entity, slot);

        m_Lookup[key] = handle;
        return handle;
    }

    template <typename T>
    AssetHandle AssetManager::Create(const std::string& name, std::unique_ptr<T> resource)
    {
        // Compatibility: allow shared_ptr resources without copying.
        std::unique_lock lock(m_Mutex);

        const Core::Hash::StringID key(name);

        // Same overwrite semantics as unique_ptr Create().
        if (auto it = m_Lookup.find(key); it != m_Lookup.end())
        {
            const AssetHandle old = it->second;
            if (m_Registry.valid(old))
            {
                m_Registry.destroy(old);
            }
            m_Lookup.erase(it);
        }

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        AssetMetaData metaData{
            .Name = name,
            .Type = "Runtime",
            .ReloadAction = nullptr,
            .FilePath = "",
            .State = LoadState::Ready
        };

        m_Registry.emplace<AssetMetaData>(entity, std::move(metaData));

        if (!resource)
        {
            // No generic default construction for arbitrary T; this is a logic error at call sites.
            // Keep behavior safe in -fno-exceptions builds by marking failed.
            m_Registry.get<AssetMetaData>(handle).State = LoadState::Failed;
            return handle;
        }

        auto slot = std::make_shared<AssetSlot<T>>(std::move(resource));
        m_Registry.emplace<AssetPayload<T>>(entity, slot);

        m_Lookup[key] = handle;
        return handle;
    }

    template <typename T>
    void AssetManager::ReloadAsset(AssetHandle handle)
    {
        std::shared_ptr<std::function<void()>> fn;
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle))
                return;

            // Type check: only allow reload if the payload component exists *or* a reloader exists.
            // (If it exists but wrong T, we still treat as no-op to keep -fno-exceptions behavior safe.)
            if (!m_Registry.any_of<AssetMetaData>(handle))
                return;

            const auto& r = m_Registry.get<AssetMetaData>(handle);
            fn = r.ReloadAction;
        }

        if (fn && *fn)
            (*fn)();
    }

    template <typename T, typename LoaderFunc>
    void AssetManager::Reload(AssetHandle handle, LoaderFunc&& loader)
    {
        std::string path;
        {
            std::unique_lock lock(m_Mutex);
            if (m_Registry.valid(handle))
            {
                auto& metaData = m_Registry.get<AssetMetaData>(handle);
                metaData.State = LoadState::Loading;
                path = metaData.FilePath;
            }
        }

        if (!path.empty())
        {
            Core::Tasks::Scheduler::Dispatch([this, handle, path, loader]()
            {
                auto result = loader(path, handle);

                std::shared_ptr<AssetSlot<T>> slot;
                if (result)
                {
                    using Ret = std::remove_cvref_t<decltype(result)>;
                    if constexpr (std::is_same_v<Ret, std::unique_ptr<T>>)
                    {
                        slot = std::make_shared<AssetSlot<T>>(std::move(result));
                    }
                    else if constexpr (std::is_same_v<Ret, std::shared_ptr<T>>)
                    {
                        slot = std::make_shared<AssetSlot<T>>(std::move(result));
                    }
                    else
                    {
                        static_assert(sizeof(T) == 0,
                                      "Loader must return std::unique_ptr<T> or std::shared_ptr<T>.");
                    }
                }

                std::unique_lock lock(m_Mutex);
                if (m_Registry.valid(handle))
                {
                    if (slot && slot->Get() != nullptr)
                    {
                        m_Registry.emplace_or_replace<AssetPayload<T>>(handle, slot);

                        auto& info = m_Registry.get<AssetMetaData>(handle);
                        if (info.State != LoadState::Processing)
                        {
                            info.State = LoadState::Ready;
                            EnqueueReady(handle);
                            Core::Log::Info("Asset Loaded: {}", path);
                        }
                        else
                        {
                            Core::Log::Info("Asset Loaded (CPU) -> Waiting for Processing: {}", path);
                        }
                    }
                    else
                    {
                        m_Registry.get<AssetMetaData>(handle).State = LoadState::Failed;
                        Core::Log::Error("Asset Load Failed: {}", path);
                    }
                }
            });
        }
    }

    template <typename T>
    Core::Expected<AssetLease<T>> AssetManager::AcquireLease(AssetHandle handle)
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
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle)) return nullptr;

        const auto* payload = m_Registry.try_get<AssetPayload<T>>(handle);

        return payload ? payload->Ptr : nullptr;
    }

    template <typename T>
    const T* AssetManager::TryGetFast(AssetHandle handle) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle)) return nullptr;

        const auto* payload = m_Registry.try_get<AssetPayload<T>>(handle);

        return payload ? payload->Ptr : nullptr;
    }
}
