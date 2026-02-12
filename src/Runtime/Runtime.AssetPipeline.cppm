module;
#include <memory>
#include <string>
#include <vector>
#include <mutex>

#include "RHI.Vulkan.hpp"

export module Runtime.AssetPipeline;

import Core;
import RHI;

export namespace Runtime
{
    // Owns the asset-management subsystem: AssetManager, pending GPU
    // transfers, the main-thread task queue, and material keep-alive list.
    // Extracted from Engine following the GraphicsBackend pattern.
    class AssetPipeline
    {
    public:
        // Construct with a reference to the TransferManager (borrowed from
        // GraphicsBackend) for polling GPU transfer completion.
        explicit AssetPipeline(RHI::TransferManager& transferManager);
        ~AssetPipeline();

        // Non-copyable, non-movable (owns mutexes and asset state).
        AssetPipeline(const AssetPipeline&) = delete;
        AssetPipeline& operator=(const AssetPipeline&) = delete;
        AssetPipeline(AssetPipeline&&) = delete;
        AssetPipeline& operator=(AssetPipeline&&) = delete;

        // --- Accessors ---
        [[nodiscard]] Core::Assets::AssetManager& GetAssetManager() { return m_AssetManager; }
        [[nodiscard]] const Core::Assets::AssetManager& GetAssetManager() const { return m_AssetManager; }

        // --- Per-frame processing (call from main thread) ---

        // Garbage-collect completed staging memory and finalize any pending
        // asset loads whose GPU transfers have completed.
        void ProcessUploads();

        // Drain the main-thread task queue (swap-and-execute pattern).
        void ProcessMainThreadQueue();

        // --- Thread-safe registration ---

        // Register a pending GPU transfer for an asset.
        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token);

        // Register a pending GPU transfer with a completion callback.
        template <typename F>
        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token, F&& onComplete)
        {
            std::lock_guard lock(m_LoadMutex);
            PendingLoad l{};
            l.Handle = handle;
            l.Token = token;
            l.OnComplete = Core::Tasks::LocalTask(std::forward<F>(onComplete));
            m_PendingLoads.push_back(std::move(l));
        }

        // Queue a task for execution on the main thread (thread-safe).
        template <typename F>
        void RunOnMainThread(F&& task)
        {
            std::lock_guard lock(m_MainThreadQueueMutex);
            m_MainThreadQueue.emplace_back(Core::Tasks::LocalTask(std::forward<F>(task)));
        }

        // --- Material keep-alive ---

        // Track a runtime-created material handle to keep it alive.
        void TrackMaterial(Core::Assets::AssetHandle handle);

        // Access the tracked material list (for teardown).
        [[nodiscard]] const std::vector<Core::Assets::AssetHandle>& GetLoadedMaterials() const { return m_LoadedMaterials; }

        // Clear tracked materials (call during shutdown).
        void ClearLoadedMaterials() { m_LoadedMaterials.clear(); }

    private:
        // Core asset database.
        Core::Assets::AssetManager m_AssetManager;

        // Internal tracking struct (POD).
        struct PendingLoad
        {
            Core::Assets::AssetHandle Handle;
            RHI::TransferToken Token;
            Core::Tasks::LocalTask OnComplete{}; // optional main-thread completion work
        };

        // Protected by mutex because loaders call RegisterAssetLoad from worker threads.
        std::mutex m_LoadMutex;
        std::vector<PendingLoad> m_PendingLoads;

        // Main-thread task queue (deferred work from worker threads).
        std::mutex m_MainThreadQueueMutex;
        std::vector<Core::Tasks::LocalTask> m_MainThreadQueue;

        // Keep-alive list for runtime-created materials (handle-based).
        std::vector<Core::Assets::AssetHandle> m_LoadedMaterials;

        // Borrowed reference to the GPU transfer manager.
        RHI::TransferManager& m_TransferManager;
    };
}
