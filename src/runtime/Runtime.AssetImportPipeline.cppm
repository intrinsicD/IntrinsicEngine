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

export module Extrinsic.Runtime.AssetImportPipeline;

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
    export enum class AssetImportStage : std::uint8_t
    {
        Route,
        Decode,
        CpuMaterialize,
        EcsAuthor,
        Postprocess,
        GpuResidency,
        Complete,
    };

    export struct ImportAuthoringRecipe
    {
        bool AuthorRenderableComponents{true};
        bool AuthorSelectableIdentity{true};
    };

    export enum class AssetImportPostprocessPolicy : std::uint8_t
    {
        None,
        PrepareRenderableGeometry,
    };

    export struct AssetImportCompletionRecipe
    {
        bool SelectFirstCreatedEntity{true};
        bool FocusCameraOnCreatedGeometry{true};
    };

    export struct AssetImportRecipe
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        RuntimeAssetIngestSource Source{RuntimeAssetIngestSource::ManualImport};
        Assets::AssetId ExistingAsset{};
        ImportAuthoringRecipe Authoring{};
        AssetImportPostprocessPolicy Postprocess{
            AssetImportPostprocessPolicy::PrepareRenderableGeometry};
        AssetImportCompletionRecipe Completion{};
    };

    export struct AssetImportExecutionIdentity
    {
        RuntimeAssetIngestHandle Request{};
        WorldHandle World{};
        std::uint64_t BindingGeneration{0u};
        std::uint64_t CancellationGeneration{0u};

        [[nodiscard]] friend bool operator==(
            const AssetImportExecutionIdentity&,
            const AssetImportExecutionIdentity&) noexcept = default;
    };

    export struct AssetImportStageResult
    {
        AssetImportExecutionIdentity Identity{};
        AssetImportStage Stage{AssetImportStage::Route};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic Diagnostic{RuntimeAssetIngestDiagnostic::None};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Error == Core::ErrorCode::Success;
        }
    };

    export struct AssetImportStageTrace
    {
        AssetImportExecutionIdentity Identity{};
        std::vector<AssetImportStageResult> Results{};
        bool Terminal{false};
    };

    export [[nodiscard]] Core::Result ValidateAssetImportRecipe(
        const AssetImportRecipe& recipe) noexcept;

    export [[nodiscard]] Core::Result AppendAssetImportStageResult(
        AssetImportStageTrace& trace,
        AssetImportStageResult result);

    export struct RuntimeAssetImportRequest
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export struct RuntimeAssetReimportRequest
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export using RuntimeIOBackendFactory =
        std::function<std::unique_ptr<Core::IO::IIOBackend>()>;

    export struct RuntimeAssetImportResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
    };

    export struct RuntimeAssetImportEvent
    {
        std::uint64_t Sequence{0};
        std::string Path{};
        Assets::AssetPayloadKind RequestedPayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic IngestDiagnostic{RuntimeAssetIngestDiagnostic::None};
        std::optional<RuntimeAssetImportResult> Result{};
        std::optional<AssetImportStageTrace> StageTrace{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Result.has_value() && Error == Core::ErrorCode::Success;
        }
    };

    export struct RuntimeQueuedAssetImport
    {
        RuntimeAssetIngestHandle Operation{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export struct AssetImportPipelineDependencies
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

    export class AssetImportPipeline
    {
    public:
        AssetImportPipeline() = default;
        explicit AssetImportPipeline(AssetImportPipelineDependencies dependencies);

        AssetImportPipeline(const AssetImportPipeline&) = delete;
        AssetImportPipeline& operator=(const AssetImportPipeline&) = delete;
        AssetImportPipeline(AssetImportPipeline&&) = delete;
        AssetImportPipeline& operator=(AssetImportPipeline&&) = delete;

        void SetDependencies(AssetImportPipelineDependencies dependencies) noexcept;

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
