module;
#include <memory>
#include <utility>
#include <vector>

export module Asset.Pipeline;

import Asset.Manager;
import Core.Tasks;
import RHI.Transfer;

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
        [[nodiscard]] Core::Assets::AssetManager& GetAssetManager();
        [[nodiscard]] const Core::Assets::AssetManager& GetAssetManager() const;

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
            RegisterAssetLoadWithCallback(handle,
                                          token,
                                          Core::Tasks::LocalTask(std::forward<F>(onComplete)));
        }

        // Queue a task for execution on the main thread (thread-safe).
        template <typename F>
        void RunOnMainThread(F&& task)
        {
            EnqueueMainThreadTask(Core::Tasks::LocalTask(std::forward<F>(task)));
        }

        // --- Material keep-alive ---

        // Track a runtime-created material handle to keep it alive.
        void TrackMaterial(Core::Assets::AssetHandle handle);

        // Access the tracked material list (for teardown).
        [[nodiscard]] const std::vector<Core::Assets::AssetHandle>& GetLoadedMaterials() const;

        // Clear tracked materials (call during shutdown).
        void ClearLoadedMaterials();

    private:
        void RegisterAssetLoadWithCallback(Core::Assets::AssetHandle handle,
                                           RHI::TransferToken token,
                                           Core::Tasks::LocalTask onComplete);
        void EnqueueMainThreadTask(Core::Tasks::LocalTask task);

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

