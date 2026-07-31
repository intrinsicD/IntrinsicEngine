module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.AssetWorkflowImportExecutor;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.HalfedgeMesh.IO;

namespace Extrinsic::Runtime
{
    export struct AssetWorkflowImportExecutorDependencies
    {
        const bool* Initialized{};
        const Core::Config::EngineConfig* Config{};
        JobService* Jobs{};
        WorldRegistry* Worlds{};
        WorldHandle World{DefaultWorldHandle};
        std::function<bool()> BindingValid{};
        Assets::AssetService* AssetService{};
        Graphics::GpuAssetCache* GpuAssetCache{};
        AssetModelTextureHandoff* ModelTextureHandoff{};
        AssetModelSceneHandoff* ModelSceneHandoff{};
        RenderExtractionCache* RenderExtraction{};
        ECS::Scene::Registry* Scene{};
        SelectionController* Selection{};
        CameraControllerRegistry* CameraControllers{};
        EditorCommandHistory* CommandHistory{};
        TextureBakeService* TextureBake{};
    };

    struct BorrowedBool
    {
        const bool* Ptr{};
        [[nodiscard]] operator bool() const noexcept { return Ptr != nullptr && *Ptr; }
    };

    template <typename T>
    struct BorrowedSubsystem
    {
        T* Ptr{};

        [[nodiscard]] T* get() const noexcept { return Ptr; }
        [[nodiscard]] T& operator*() const noexcept { return *Ptr; }
        [[nodiscard]] T* operator->() const noexcept { return Ptr; }
        [[nodiscard]] operator bool() const noexcept { return Ptr != nullptr; }
        [[nodiscard]] operator T*() const noexcept { return Ptr; }
        [[nodiscard]] operator T&() const noexcept { return *Ptr; }

        [[nodiscard]] friend bool operator==(
            const BorrowedSubsystem subsystem,
            std::nullptr_t) noexcept
        {
            return subsystem.Ptr == nullptr;
        }

        [[nodiscard]] friend bool operator!=(
            const BorrowedSubsystem subsystem,
            std::nullptr_t) noexcept
        {
            return subsystem.Ptr != nullptr;
        }
    };

    export class AssetWorkflowImportExecutor
    {
    public:
        AssetWorkflowImportExecutor() = default;
        explicit AssetWorkflowImportExecutor(
            AssetWorkflowImportExecutorDependencies dependencies);

        AssetWorkflowImportExecutor(const AssetWorkflowImportExecutor&) = delete;
        AssetWorkflowImportExecutor& operator=(
            const AssetWorkflowImportExecutor&) = delete;
        AssetWorkflowImportExecutor(AssetWorkflowImportExecutor&&) = delete;
        AssetWorkflowImportExecutor& operator=(
            AssetWorkflowImportExecutor&&) = delete;

        void SetDependencies(
            AssetWorkflowImportExecutorDependencies dependencies) noexcept;

        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport> QueueAssetImport(
            AssetImportRecipe recipe);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPath(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport> QueueModelTextureImport(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport> QueueGeometryImport(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ReimportAsset(
            RuntimeAssetReimportRequest request);
        [[nodiscard]] const std::optional<RuntimeAssetImportEvent>&
            GetLastAssetImportEvent() const noexcept;
        [[nodiscard]] std::vector<RuntimeAssetIngestRecord>
            GetAssetIngestRecordsForTest() const;
        void SetModelTextureImportIOBackendFactoryForTest(
            RuntimeIOBackendFactory factory);
        void SetQueuedGeometryImportBeforeDecodeHookForTest(
            std::function<void(const RuntimeAssetImportRequest&)> hook);
        [[nodiscard]] RuntimeAssetImportQueueSnapshot
            GetAssetImportQueueSnapshot() const;
        [[nodiscard]] TextureBakeService*
            GetTextureBakeServiceForTest() const noexcept
        {
            return m_TextureBake.get();
        }
        [[nodiscard]] std::size_t ClearCompletedAssetImports();
        [[nodiscard]] Core::Result CancelAssetImport(
            RuntimeAssetIngestHandle operation);
        void CancelActiveAssetImportsForShutdown();
        void ImportDroppedFilePaths(std::span<const std::string> paths);

    private:
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueGeometryImportWithIngest(
                AssetImportRecipe recipe,
                std::vector<Assets::AssetPayloadKind> payloadKinds);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueModelTextureImportWithIngest(
                AssetImportRecipe recipe);
        void QueueDroppedModelTextureImport(
            std::string path,
            Assets::AssetPayloadKind payloadKind);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPathWithIngest(
            RuntimeAssetImportRequest request,
            RuntimeAssetIngestSource source,
            Assets::AssetId existingAsset);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPathImpl(
            RuntimeAssetImportRequest request,
            Assets::AssetId existingAsset);
        [[nodiscard]] Core::Result CancelAssetImportImpl(
            RuntimeAssetIngestHandle operation,
            bool allowWaitingForMainThreadApply);
        void FinalizeUnpublishedImport(
            RuntimeAssetIngestHandle operation,
            RuntimeAssetImportRequest request,
            AssetImportStageTrace* stageTrace);
        void RecordAssetImportEvent(
            const RuntimeAssetImportRequest& request,
            const Core::Expected<RuntimeAssetImportResult>& result,
            RuntimeAssetIngestDiagnostic ingestDiagnostic,
            const AssetImportStageTrace* stageTrace = nullptr);
        [[nodiscard]] bool IsCurrentSubmissionTarget(
            WorldHandle world,
            const ECS::Scene::Registry* scene,
            std::uint64_t bindingEpoch) const noexcept;

        BorrowedBool m_Initialized{};
        BorrowedSubsystem<const Core::Config::EngineConfig> m_Config{};
        BorrowedSubsystem<JobService> m_Jobs{};
        BorrowedSubsystem<WorldRegistry> m_WorldRegistry{};
        WorldHandle m_World{DefaultWorldHandle};
        std::uint64_t m_TargetBindingEpoch{0u};
        std::function<bool()> m_BindingValid{};
        BorrowedSubsystem<Assets::AssetService> m_AssetService{};
        BorrowedSubsystem<Graphics::GpuAssetCache> m_GpuAssetCache{};
        BorrowedSubsystem<AssetModelTextureHandoff> m_AssetModelTextureHandoff{};
        BorrowedSubsystem<AssetModelSceneHandoff> m_AssetModelSceneHandoff{};
        BorrowedSubsystem<RenderExtractionCache> m_RenderExtraction{};
        BorrowedSubsystem<ECS::Scene::Registry> m_Scene{};
        BorrowedSubsystem<SelectionController> m_SelectionController{};
        BorrowedSubsystem<CameraControllerRegistry> m_CameraControllers{};
        BorrowedSubsystem<EditorCommandHistory> m_EditorCommandHistory{};
        BorrowedSubsystem<TextureBakeService> m_TextureBake{};
        RuntimeIOBackendFactory m_ModelTextureImportIOBackendFactoryForTest{};
        std::function<void(const RuntimeAssetImportRequest&)>
            m_QueuedGeometryImportBeforeDecodeHookForTest{};
        RuntimeAssetIngestStateMachine m_AssetIngestStateMachine{};
        struct RuntimeAssetImportJobRecord
        {
            RuntimeAssetIngestHandle Ingest{};
            JobToken Job{};
            std::shared_ptr<AssetImportStageTrace> StageTrace{};
        };
        std::vector<RuntimeAssetImportJobRecord> m_AssetImportJobs{};
        std::optional<RuntimeAssetImportEvent> m_LastAssetImportEvent{};
        std::uint64_t m_AssetImportEventSequence{0};
    };
}
