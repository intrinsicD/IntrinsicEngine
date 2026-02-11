module;
#include <entt/entt.hpp>
#include <string>
#include <filesystem>
#include <memory>
#include <span>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <expected>
#include <atomic>
#include <cassert>

export module Core:Assets;

import :Error;
import :Logging;
import :Filesystem;
import :Tasks; // Using your Async Task Graph
import :Hash;

using namespace Core::Hash;

export namespace Core::Assets
{
    // --- Asset Loader Concept ---
    // Loaders passed to AssetManager::Load() MUST be:
    //   1. Move-constructible (stored in shared ownership internally)
    //   2. Copy-constructible (captured in long-lived callbacks)
    //   3. Invocable with (const std::string& path, AssetHandle handle)
    //   4. Return std::unique_ptr<T> or std::shared_ptr<T>
    //
    // IMPORTANT CAPTURE RULES:
    //   - Loaders are stored persistently for hot-reload. They outlive the
    //     calling scope of Load().
    //   - NEVER capture stack references or raw pointers to local variables.
    //   - Safe captures: values, shared_ptr, global/static refs, `this` (if
    //     the object outlives the AssetManager).
    template <typename F, typename T>
    concept AssetLoaderFunc = std::is_move_constructible_v<std::decay_t<F>>
        && std::is_copy_constructible_v<std::decay_t<F>>
        && std::invocable<std::decay_t<F>, const std::string&, struct AssetHandle>;

    // --- Strong Handle Wrapper ---
    struct AssetHandle
    {
        entt::entity ID = entt::null;

        [[nodiscard]] bool IsValid() const { return ID != entt::null; }
        bool operator==(const AssetHandle& other) const { return ID == other.ID; }

        // Allow hashing for maps
        struct Hash
        {
            size_t operator()(const AssetHandle& h) const { return (size_t)h.ID; }
        };
    };

    // --- Components ---

    enum class LoadState
    {
        Unloaded,
        Loading,
        Processing,
        Ready,
        Failed
    };

    struct AssetInfo
    {
        std::string Name;
        std::string Type;
        LoadState State = LoadState::Unloaded;
    };

    struct AssetSource
    {
        std::filesystem::path FilePath;
    };

    // Stores the per-asset reload action (used by file watching and manual reloads).
    // The ReloadAction is stored in a shared_ptr so the same underlying loader
    // is shared between the AssetReloader and FileWatcher callback, preventing
    // redundant copies of loader captures (and reducing the chance of accidentally
    // copying dangling references multiple times).
    struct AssetReloader
    {
        std::shared_ptr<std::function<void()>> ReloadAction;
    };

    // Manager-owned storage slot for a given asset payload.
    // Keeps the underlying object alive until all leases are released.
    template <typename T>
    struct AssetSlot
    {
        // Store either unique or shared ownership.
        std::unique_ptr<T> Unique{};
        std::shared_ptr<T> Shared{};
        std::atomic<uint32_t> PinCount{0};

        explicit AssetSlot(std::unique_ptr<T> resource)
            : Unique(std::move(resource))
        {
        }

        explicit AssetSlot(std::shared_ptr<T> resource)
            : Shared(std::move(resource))
        {
        }

        [[nodiscard]] T* Get() const noexcept
        {
            if (Unique) return Unique.get();
            return Shared.get();
        }
    };

    // Type-erased payload.
    // Stage 3B: we store a slot pointer (type-erased) + raw pointer for hot access.
    // - Slot lifetime is managed by shared ownership (slot is NOT a shared_ptr<T> control block).
    // - AcquireLease() increments PinCount and holds the slot.
    // - Reload swaps the slot; old slot lives until last pin is released.
    template <typename T>
    struct AssetPayload
    {
        std::shared_ptr<void> Slot; // actually std::shared_ptr<AssetSlot<T>>
        T* Ptr = nullptr;

        AssetPayload() = default;

        explicit AssetPayload(std::shared_ptr<AssetSlot<T>> slot)
            : Slot(std::move(slot))
        {
            Ptr = std::static_pointer_cast<AssetSlot<T>>(Slot)->Get();
        }
    };

    // A pinned read-only view of an asset payload.
    // Stage 3B: backed by the slot, not a shared_ptr<T>.
    template <typename T>
    class AssetLease
    {
    public:
        AssetLease() = default;

        explicit AssetLease(std::shared_ptr<AssetSlot<T>> slot)
            : m_Slot(std::move(slot))
        {
            PinIfValid();
        }

        AssetLease(const AssetLease& other)
            : m_Slot(other.m_Slot)
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

        [[nodiscard]] T* get() const noexcept { return m_Slot ? m_Slot->Get() : nullptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }

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

    struct ListenerHandle
    {
        uint32_t ID = 0;
        [[nodiscard]] bool Valid() const { return ID != 0; }
    };

    // --- Asset Manager ---
    class AssetManager
    {
    public:
        using AssetCallback = std::function<void(AssetHandle)>;

        AssetManager() = default;

        // Debug-verified phase ordering for TryGetFast.
        // BeginReadPhase/EndReadPhase must bracket any parallel read usage.
        void BeginReadPhase() const
        {
#ifndef NDEBUG
            m_DebugReadPhase.fetch_add(1, std::memory_order_relaxed);
#endif
        }

        void EndReadPhase() const
        {
#ifndef NDEBUG
            const int prev = m_DebugReadPhase.fetch_sub(1, std::memory_order_relaxed);
            assert(prev > 0 && "AssetManager::EndReadPhase without matching BeginReadPhase.");
#endif
        }

        // Call this once per frame on the Main Thread
        void Update();

        // 1. Load: Returns handle immediately. Starts async task.
        //    The loader is stored persistently for hot-reload. See AssetLoaderFunc
        //    concept documentation for capture rules.
        template <typename T, typename LoaderFunc>
            requires AssetLoaderFunc<LoaderFunc, T>
        AssetHandle Load(const std::filesystem::path& path, LoaderFunc&& loader);

        // 2. Persistent Listener (Updates every reload)
        ListenerHandle Listen(AssetHandle handle, AssetCallback callback);
        void Unlisten(AssetHandle handle, ListenerHandle listenerId);

        // 3. Request Notify: Register a callback for when the asset is Ready.
        // If already ready, callback fires immediately (synchronously).
        void RequestNotify(AssetHandle handle, AssetCallback callback);

        void FinalizeLoad(AssetHandle handle);

        void MoveToProcessing(AssetHandle handle);

        // 4. Asset Access
        // Use GetRaw() for error-coded access, TryGetFast() for hot loops,
        // AcquireLease() when you need lifetime stability across reloads.

        template <typename T>
        [[nodiscard]] Expected<T*> GetRaw(AssetHandle handle);

        template <typename T>
        [[nodiscard]] Expected<const T*> GetRaw(AssetHandle handle) const;

        // OPTIMIZATION: Lightweight accessor for hot loops.
        // Returns nullptr if not loaded/ready, invalid handle, or wrong type.
        // Intentionally does not return error codes to minimize overhead.
        template <typename T>
        [[nodiscard]] T* TryGet(AssetHandle handle) const;

        // OPTIMIZATION: Lightweight accessor for hot loops.
        // Returns nullptr if not loaded/ready, invalid handle, or wrong type.
        // Intentionally does not return error codes to minimize overhead.
        // SAFETY: must be called inside a declared AssetManager read phase.
        template <typename T>
       [[nodiscard]] T* TryGetFast(AssetHandle handle) const
        {
#ifndef NDEBUG
            assert(m_DebugReadPhase.load(std::memory_order_relaxed) > 0 &&
                   "AssetManager::TryGetFast called outside read phase. Use BeginReadPhase/EndReadPhase or TryGet/AcquireLease.");
#endif
            // 1. Validation check (fast bitmask)
            if (!handle.IsValid() || !m_Registry.valid(handle.ID))
                return nullptr;

            // 2. Single Lookup Optimization
            // We don't need to check AssetInfo::State because Payload is only attached
            // when the asset is successfully loaded.
            const auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID);

            // Branchless return if possible (compiler optimizes this well)
            return payload ? payload->Ptr : nullptr;
        }

        [[nodiscard]] LoadState GetState(AssetHandle handle);

        void Clear();
        void AssetsUiPanel();

        template <typename T>
        AssetHandle Create(const std::string& name, std::shared_ptr<T> resource);

        // Preferred overload: create from unique_ptr without shared ownership (zero-copy).
        template <typename T>
        AssetHandle Create(const std::string& name, std::unique_ptr<T> resource);

        // Acquire a lease to ensure lifetime across reloads.
        // Prefer this in long-lived access paths that must survive reloads.
        template <typename T>
        [[nodiscard]] Expected<AssetLease<T>> AcquireLease(AssetHandle handle);

        // Manually trigger a reload for an asset that was created via Load().
        // Returns ResourceNotFound if the handle is invalid.
        // No-op if the asset doesn't have a reloader (e.g., runtime-created via Create).
        template <typename T>
        void ReloadAsset(AssetHandle handle);

        // OPTIMIZATION: Batch Resolve
        // Acquires the read lock ONCE, then resolves all handles.
        // Result vector will correspond 1:1 to the input handles.
        // Returns nullptr for any handle that is invalid, not loaded, or wrong type.
        template <typename T>
        void BatchResolve(std::span<const AssetHandle> handles, std::vector<T*>& outResults) const;

    private:
        entt::registry m_Registry;
        std::unordered_map<StringID, AssetHandle> m_Lookup;

        // Separate map for Persistent Listeners
        std::unordered_map<AssetHandle, std::unordered_map<uint32_t, AssetCallback>, AssetHandle::Hash> m_PersistentListeners;
        // Map for One-Shot Listeners
        std::unordered_map<AssetHandle, std::vector<AssetCallback>, AssetHandle::Hash> m_OneShotListeners;

        std::vector<AssetHandle> m_ReadyQueue; // Queue of assets loaded since last frame
        mutable std::shared_mutex m_Mutex;
        std::mutex m_EventQueueMutex;
#ifndef NDEBUG
        mutable std::atomic<int> m_DebugReadPhase{0};
#endif

        void EnqueueReadyEvent(AssetHandle handle);

        // Internal reload helper
        template <typename T, typename LoaderFunc>
        void Reload(AssetHandle handle, LoaderFunc&& loader);
    };

    // Helper: normalize a loader/resource return type into a std::unique_ptr<T> without copying when possible.
    template <typename T, typename R>
    [[nodiscard]] std::unique_ptr<T> ToUnique(R&& r)
    {
        using Ret = std::remove_cvref_t<R>;
        if constexpr (std::is_same_v<Ret, std::unique_ptr<T>>)
        {
            return std::forward<R>(r);
        }
        else if constexpr (std::is_same_v<Ret, std::shared_ptr<T>>)
        {
            // If uniquely owned, we can steal via custom deleter handshake by moving into a unique_ptr.
            // Otherwise, fall back to keeping the shared_ptr alive by aliasing through a unique_ptr wrapper.
            // For now, we take the safe path: keep it shared without copying by storing the shared_ptr in a slot.
            // (Handled in Reload/Create overload logic below.)
            return nullptr;
        }
        else
        {
            static_assert(sizeof(T) == 0, "Loader must return std::unique_ptr<T> or std::shared_ptr<T>.");
            __builtin_unreachable();
        }
    }

    template <typename T, typename LoaderFunc>
        requires AssetLoaderFunc<LoaderFunc, T>
    AssetHandle AssetManager::Load(const std::filesystem::path& path, LoaderFunc&& loader)
    {
        std::string key = std::filesystem::absolute(path).string();

        StringID id(key); // Hashes here
        std::unique_lock lock(m_Mutex);

        if (m_Lookup.contains(id)) return m_Lookup[id];

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        m_Registry.emplace<AssetInfo>(entity, key, "Unknown", LoadState::Loading);
        m_Registry.emplace<AssetSource>(entity, key);
        m_Lookup[id] = handle;

        // Store loader in shared ownership. Both the AssetReloader and the
        // FileWatcher callback share the same underlying loader, preventing
        // redundant copies and reducing capture-related lifetime bugs.
        auto sharedLoader = std::make_shared<std::decay_t<LoaderFunc>>(std::forward<LoaderFunc>(loader));

        auto reloadAction = std::make_shared<std::function<void()>>(
            [this, handle, sharedLoader]() mutable
            {
                this->Reload<T>(handle, *sharedLoader);
            });

        m_Registry.emplace<AssetReloader>(entity, AssetReloader{reloadAction});

        // WATCHER REGISTRATION
        // Both the watcher and the reloader share the same sharedLoader.
        // No extra copies of the user's loader captures are made.
        Filesystem::FileWatcher::Watch(key, [reloadAction](const std::string&)
        {
            if (reloadAction) (*reloadAction)();
        });

        // Trigger initial load via Reload logic to avoid duplication
        lock.unlock(); // Reload locks internally
        Reload<T>(handle, *sharedLoader);

        return handle;
    }

    template <typename T, typename LoaderFunc>
    void AssetManager::Reload(AssetHandle handle, LoaderFunc&& loader)
    {
        // 1. Mark Loading (Thread Safe)
        {
            std::unique_lock lock(m_Mutex);
            if (m_Registry.valid(handle.ID))
            {
                m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Loading;
            }
        }

        // 2. Get Path
        std::string path;
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID)) return;
            path = m_Registry.get<AssetSource>(handle.ID).FilePath.string();
        }

        // 3. Async Task
        Tasks::Scheduler::Dispatch([this, handle, path, loader]()
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
                    static_assert(sizeof(T) == 0, "Loader must return std::unique_ptr<T> or std::shared_ptr<T>.");
                }
            }

            std::unique_lock lock(m_Mutex);
            if (m_Registry.valid(handle.ID))
            {
                if (slot && slot->Get() != nullptr)
                {
                    m_Registry.emplace_or_replace<AssetPayload<T>>(handle.ID, slot);

                    auto& info = m_Registry.get<AssetInfo>(handle.ID);
                    if (info.State != LoadState::Processing)
                    {
                        info.State = LoadState::Ready;
                        EnqueueReadyEvent(handle);
                        Log::Info("Asset Loaded: {}", path);
                    }
                    else
                    {
                        Log::Info("Asset Loaded (CPU) -> Waiting for Processing: {}", path);
                    }
                }
                else
                {
                    m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Failed;
                    Log::Error("Asset Load Failed: {}", path);
                }
            }
        });
    }

    // 2. Data Access - Returns Expected with proper error information
    template <typename T>
    Expected<T*> AssetManager::GetRaw(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID))
            return std::unexpected(ErrorCode::ResourceNotFound);

        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready)
        {
            if (info.State == LoadState::Failed)
                return std::unexpected(ErrorCode::AssetLoadFailed);
            return std::unexpected(ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
        {
            return payload->Ptr;
        }
        return std::unexpected(ErrorCode::AssetTypeMismatch);
    }


    template <typename T>
    Expected<const T*> AssetManager::GetRaw(AssetHandle handle) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID))
            return std::unexpected(ErrorCode::ResourceNotFound);

        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready)
        {
            if (info.State == LoadState::Failed)
                return std::unexpected(ErrorCode::AssetLoadFailed);
            return std::unexpected(ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
            return payload->Ptr;

        return std::unexpected(ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    T* AssetManager::TryGet(AssetHandle handle) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID)) return nullptr;

        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready) return nullptr;

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
        {
            return payload->Ptr;
        }
        return nullptr;
    }

    template <typename T>
    AssetHandle AssetManager::Create(const std::string& name, std::shared_ptr<T> resource)
    {
        // Compatibility: allow shared_ptr resources without copying.
        std::unique_lock lock(m_Mutex);

        const StringID key(name);

        // Same overwrite semantics as unique_ptr Create().
        if (auto it = m_Lookup.find(key); it != m_Lookup.end())
        {
            const AssetHandle old = it->second;
            if (old.IsValid() && m_Registry.valid(old.ID))
            {
                m_Registry.destroy(old.ID);
            }
            m_Lookup.erase(it);
        }

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        m_Registry.emplace<AssetInfo>(entity, name, "Runtime", LoadState::Ready);

        if (!resource)
        {
            // No generic default construction for arbitrary T; this is a logic error at call sites.
            // Keep behavior safe in -fno-exceptions builds by marking failed.
            m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Failed;
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
        std::unique_lock lock(m_Mutex);

        const StringID key(name);

        // If an asset with this name already exists, destroy it first.
        // This keeps name->handle lookup coherent and avoids having live entities reference an asset
        // whose payload was effectively replaced out from under them.
        if (auto it = m_Lookup.find(key); it != m_Lookup.end())
        {
            const AssetHandle old = it->second;
            if (old.IsValid() && m_Registry.valid(old.ID))
            {
                m_Registry.destroy(old.ID);
            }
            m_Lookup.erase(it);
        }

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        m_Registry.emplace<AssetInfo>(entity, name, "Runtime", LoadState::Ready);

        if (!resource)
        {
            m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Failed;
            return handle;
        }

        auto slot = std::make_shared<AssetSlot<T>>(std::move(resource));
        m_Registry.emplace<AssetPayload<T>>(entity, slot);
        m_Lookup[key] = handle;
        return handle;
    }

    template <typename T>
    Expected<AssetLease<T>> AssetManager::AcquireLease(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID))
            return std::unexpected(ErrorCode::ResourceNotFound);

        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready)
        {
            if (info.State == LoadState::Failed)
                return std::unexpected(ErrorCode::AssetLoadFailed);
            return std::unexpected(ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
        {
            return AssetLease<T>(std::static_pointer_cast<AssetSlot<T>>(payload->Slot));
        }

        return std::unexpected(ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    void AssetManager::BatchResolve(std::span<const AssetHandle> handles, std::vector<T*>& outResults) const
    {
        // 1. Acquire Shared Lock (Read-Only)
        // This prevents the background thread from modifying the registry (e.g. finishing a load)
        // while we are reading, ensuring iterator stability and data consistency.
        std::shared_lock lock(m_Mutex);

        // 2. Prepare Output
        // We clear to ensure the indices match the input span exactly [0] -> [0]
        outResults.clear();
        outResults.reserve(handles.size());

        // 3. Hot Loop
        for (const auto& handle : handles)
        {
            T* result = nullptr;

            // Logic mirrors TryGetFast, but we already hold the lock.
            // 1. Validate Entity
            if (handle.IsValid() && m_Registry.valid(handle.ID))
            {
                // 2. Check Payload
                // Note: In our architecture, AssetPayload<T> is ONLY attached
                // when the asset transitions to LoadState::Ready.
                // Therefore, checking for component existence is sufficient validation.
                if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
                {
                    result = payload->Ptr;
                }
            }

            outResults.push_back(result);
        }
    }

    template <typename T>
    void AssetManager::ReloadAsset(AssetHandle handle)
    {
        // Contract:
        // - If handle is invalid => no-op.
        // - If no reloader is registered (runtime-created asset) => no-op.
        // - Otherwise invoke the stored reloader (which schedules the async reload).
        if (!handle.IsValid())
            return;

        std::shared_ptr<std::function<void()>> fn;
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID))
                return;

            // Type check: only allow reload if the payload component exists *or* a reloader exists.
            // (If it exists but wrong T, we still treat as no-op to keep -fno-exceptions behavior safe.)
            if (!m_Registry.any_of<AssetReloader>(handle.ID))
                return;

            const auto& r = m_Registry.get<AssetReloader>(handle.ID);
            fn = r.ReloadAction;
        }

        if (fn && *fn)
            (*fn)();
    }
}
