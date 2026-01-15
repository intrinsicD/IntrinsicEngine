module;
#include <entt/entt.hpp>
#include <string>
#include <filesystem>
#include <memory>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <expected>
#include <atomic>

export module Core:Assets;

import :Error;
import :Logging;
import :Filesystem;
import :Tasks; // Using your Async Task Graph
import :Hash;

using namespace Core::Hash;

export namespace Core::Assets
{
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
    struct AssetReloader
    {
        std::function<void()> ReloadAction;
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
        {}

        explicit AssetSlot(std::shared_ptr<T> resource)
            : Shared(std::move(resource))
        {}

        [[nodiscard]] T* Get() const noexcept
        {
            if (Unique) return Unique.get();
            return Shared.get();
        }
    };

    // Type-erased payload.
    // Stage 3B: we store a slot pointer (type-erased) + raw pointer for hot access.
    // - Slot lifetime is managed by shared ownership (slot is NOT a shared_ptr<T> control block).
    // - Pin() increments PinCount and holds the slot.
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
        { PinIfValid(); }

        AssetLease(const AssetLease& other)
            : m_Slot(other.m_Slot)
        { PinIfValid(); }

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
                m_Slot->PinCount.fetch_sub(1, std::memory_order_relaxed);
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
                m_Slot->PinCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

         std::shared_ptr<AssetSlot<T>> m_Slot{};
    };

    // --- Asset Manager ---

    class AssetManager
    {
    public:
        using AssetCallback = std::function<void(AssetHandle)>;

        AssetManager() = default;

        // Call this once per frame on the Main Thread
        void Update();

        // 1. Load: Returns handle immediately. Starts async task.
        template <typename T, typename LoaderFunc>
        AssetHandle Load(const std::filesystem::path& path, LoaderFunc&& loader);

        // 2. Persistent Listener (Updates every reload)
        void Listen(AssetHandle handle, AssetCallback callback);

        // 3. Request Notify: Register a callback for when the asset is Ready.
        // If already ready, callback fires immediately (synchronously).
        void RequestNotify(AssetHandle handle, AssetCallback callback);

        void FinalizeLoad(AssetHandle handle);

        void MoveToProcessing(AssetHandle handle);

        // 4. Get Resource - Returns Expected with proper error codes
        // Use Get() for shared ownership, GetRaw() for temporary access
        template <typename T>
        [[nodiscard]] Expected<std::shared_ptr<T>> Get(AssetHandle handle);

        template <typename T>
        [[nodiscard]] Expected<T*> GetRaw(AssetHandle handle);

        // OPTIMIZATION: Lightweight accessor for hot loops.
        // Returns nullptr if not loaded/ready, invalid handle, or wrong type.
        // Intentionally does not return error codes to minimize overhead.
        template <typename T>
        [[nodiscard]] T* TryGet(AssetHandle handle);

        [[nodiscard]] LoadState GetState(AssetHandle handle);

        void Clear();
        void AssetsUiPanel();

        template <typename T>
        AssetHandle Create(const std::string& name, std::shared_ptr<T> resource);

        // Preferred overload: create from unique_ptr without shared ownership (zero-copy).
        template <typename T>
        AssetHandle Create(const std::string& name, std::unique_ptr<T> resource);

        // Pin (lease) the asset to ensure lifetime across reloads.
        // Prefer this over Get() when you want to avoid shared_ptr churn at call sites.
        template <typename T>
        [[nodiscard]] Expected<AssetLease<T>> Pin(AssetHandle handle);

        // Manually trigger a reload for an asset that was created via Load().
        // Returns ResourceNotFound if the handle is invalid.
        // No-op if the asset doesn't have a reloader (e.g., runtime-created via Create).
        template <typename T>
        void ReloadAsset(AssetHandle handle);

    private:
        entt::registry m_Registry;
        std::unordered_map<StringID, AssetHandle> m_Lookup;

        // Separate map for Persistent Listeners
        std::unordered_map<AssetHandle, std::vector<AssetCallback>, AssetHandle::Hash> m_PersistentListeners;
        // Map for One-Shot Listeners
        std::unordered_map<AssetHandle, std::vector<AssetCallback>, AssetHandle::Hash> m_OneShotListeners;

        std::vector<AssetHandle> m_ReadyQueue; // Queue of assets loaded since last frame
        std::shared_mutex m_Mutex;
        std::mutex m_EventQueueMutex;

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
        }
    }

    template <typename T, typename LoaderFunc>
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

        m_Registry.emplace<AssetReloader>(entity, [this, handle, loader]() mutable
        {
            this->Reload<T>(handle, loader);
        });

        // WATCHER REGISTRATION
        // We capture 'loader' by value (copy) for the lambda.
        // NOTE: 'loader' usually captures pointers/refs, ensure they are safe or copyable!
        Filesystem::FileWatcher::Watch(key, [this, handle, loader](const std::string&)
        {
            this->Reload<T>(handle, loader);
        });

        // Trigger initial load via Reload logic to avoid duplication
        lock.unlock(); // Reload locks internally
        Reload<T>(handle, std::forward<LoaderFunc>(loader));

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
    Expected<std::shared_ptr<T>> AssetManager::Get(AssetHandle handle)
    {
        // Stage 3B: keep API for compatibility, but create a shared_ptr aliasing the slot.
        // Prefer Pin() or GetRaw()/TryGet() to avoid shared_ptr churn.
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
            // aliasing ctor: control block is Slot, pointee is payload->Ptr
            return std::shared_ptr<T>(std::static_pointer_cast<AssetSlot<T>>(payload->Slot), payload->Ptr);
        }
        return std::unexpected(ErrorCode::AssetTypeMismatch);
    }

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
    T* AssetManager::TryGet(AssetHandle handle)
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

        m_Lookup[StringID(name)] = handle;
        return handle;
    }

    template <typename T>
    void AssetManager::ReloadAsset(AssetHandle handle)
    {
        std::function<void()> action;
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID)) return;
            if (auto* reloader = m_Registry.try_get<AssetReloader>(handle.ID))
            {
                action = reloader->ReloadAction;
            }
        }

        if (action)
        {
            action();
        }
    }

    template <typename T>
    AssetHandle AssetManager::Create(const std::string& name, std::unique_ptr<T> resource)
    {
        std::unique_lock lock(m_Mutex);

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
        m_Lookup[StringID(name)] = handle;
        return handle;
    }

    template <typename T>
    Expected<AssetLease<T>> AssetManager::Pin(AssetHandle handle)
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
}
