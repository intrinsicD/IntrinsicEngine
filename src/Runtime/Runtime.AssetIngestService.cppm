module;
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Runtime.AssetIngestService;

import Core.Assets;
import Core.IOBackend;
import RHI.Buffer;
import RHI.Device;
import RHI.Transfer;
import Graphics.Geometry;
import Graphics.IORegistry;
import Graphics.MaterialRegistry;
import Graphics.Model;
import Runtime.AssetPipeline;
import Runtime.SceneManager;

export namespace Runtime
{
    struct ImportedAssetHandles
    {
        Core::Assets::AssetHandle ModelHandle{};
        Core::Assets::AssetHandle MaterialHandle{};
    };

    // Coordinates external asset ingest workflows (drag-drop and synchronous
    // re-import) without leaking import orchestration into Engine. Asynchronous
    // drag-drop imports now advance through an explicit streaming-lane state
    // machine so Engine only enqueues requests while the streaming lane owns
    // worker dispatch and main-thread finalization.
    class AssetIngestService
    {
    public:
        AssetIngestService(std::shared_ptr<RHI::VulkanDevice> device,
                           RHI::TransferManager& transferManager,
                           RHI::BufferManager& bufferManager,
                           Graphics::GeometryPool& geometryStorage,
                           Graphics::MaterialRegistry& materialRegistry,
                           AssetPipeline& assetPipeline,
                           SceneManager& sceneManager,
                           Graphics::IORegistry& ioRegistry,
                           Core::IO::IIOBackend& ioBackend,
                           uint32_t defaultTextureId);
        ~AssetIngestService();

        AssetIngestService(const AssetIngestService&) = delete;
        AssetIngestService& operator=(const AssetIngestService&) = delete;
        AssetIngestService(AssetIngestService&&) = delete;
        AssetIngestService& operator=(AssetIngestService&&) = delete;

        void EnqueueDropImport(const std::string& path);
        void PumpStreamingStateMachine();

        [[nodiscard]] std::optional<ImportedAssetHandles> ImportModelSync(
            const std::string& path,
            std::string_view assetNamespace = "scene");

    private:
        struct PendingAsyncImport
        {
            std::string CanonicalPath;
            std::string AssetNamespace;
        };

        struct CompletedAsyncImport
        {
            std::string CanonicalPath;
            std::string AssetNamespace;
            std::unique_ptr<Graphics::Model> Model;
        };

        [[nodiscard]] std::optional<ImportedAssetHandles> MaterializeImportedModel(
            const std::string& sourcePath,
            std::unique_ptr<Graphics::Model> model,
            std::string_view assetNamespace);

        void ScheduleQueuedImports();
        void FinalizeCompletedImports();

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::TransferManager& m_TransferManager;
        RHI::BufferManager& m_BufferManager;
        Graphics::GeometryPool& m_GeometryStorage;
        Graphics::MaterialRegistry& m_MaterialRegistry;
        AssetPipeline& m_AssetPipeline;
        SceneManager& m_SceneManager;
        Graphics::IORegistry& m_IORegistry;
        Core::IO::IIOBackend& m_IOBackend;
        uint32_t m_DefaultTextureId = 0;
        uint64_t m_DropAssetCounter = 0;
        uint64_t m_ReimportAssetCounter = 0;

        std::mutex m_AsyncImportMutex;
        std::vector<PendingAsyncImport> m_QueuedAsyncImports;
        std::vector<CompletedAsyncImport> m_CompletedAsyncImports;
    };
}
