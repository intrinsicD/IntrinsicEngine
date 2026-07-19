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
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.HalfedgeMesh.IO;

namespace Extrinsic::Runtime
{
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

    export struct RuntimePostImportProcessorHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimePostImportProcessorHandle,
            RuntimePostImportProcessorHandle) noexcept = default;
    };

    export struct RuntimePostImportProcessorContext
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        const Geometry::MeshIO::MeshIOResult* MeshPayload{};
    };

    export struct RuntimePostImportProcessorServices
    {
        StreamingExecutor* Streaming{};
        WorldHandle World{DefaultWorldHandle};
        Assets::AssetService* AssetService{};
        Graphics::GpuAssetCache* GpuAssetCache{};
        RenderExtractionCache* RenderExtraction{};
        ECS::Scene::Registry* Scene{};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{};
        bool ObjectSpaceNormalBakeGraphicsBackendOperational{false};
    };

    export struct RuntimePostImportProcessorDesc
    {
        std::string DebugName{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::function<Core::Result(
            const RuntimePostImportProcessorContext&,
            RuntimePostImportProcessorServices&)> Process{};
    };

    struct RuntimePostImportProcessorRecord
    {
        RuntimePostImportProcessorHandle Handle{};
        RuntimePostImportProcessorDesc Desc{};
    };

    export struct RuntimeImportEntityAuthoringPolicyHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeImportEntityAuthoringPolicyHandle,
            RuntimeImportEntityAuthoringPolicyHandle) noexcept = default;
    };

    export struct RuntimeImportEntityAuthoringPolicyContext
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
    };

    export struct RuntimeImportEntityAuthoringPolicyServices
    {
        ECS::Scene::Registry* Scene{};
    };

    export struct RuntimeImportEntityAuthoringPolicyDesc
    {
        std::string DebugName{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::function<Core::Result(
            const RuntimeImportEntityAuthoringPolicyContext&,
            RuntimeImportEntityAuthoringPolicyServices&)> Apply{};
    };

    struct RuntimeImportEntityAuthoringPolicyRecord
    {
        RuntimeImportEntityAuthoringPolicyHandle Handle{};
        RuntimeImportEntityAuthoringPolicyDesc Desc{};
    };

    export struct RuntimeImportCompletedHandlerHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeImportCompletedHandlerHandle,
            RuntimeImportCompletedHandlerHandle) noexcept = default;
    };

    export struct RuntimeImportCompletedContext
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::span<const ECS::EntityHandle> CreatedEntities{};
        std::optional<CameraFocusTarget> FocusTarget{};
    };

    export struct RuntimeImportCompletedServices
    {
        ECS::Scene::Registry* Scene{};
        CameraControllerRegistry* CameraControllers{};
        SelectionController* Selection{};
        const Core::Config::EngineConfig* Config{};
    };

    export struct RuntimeImportCompletedHandlerDesc
    {
        std::string DebugName{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::function<Core::Result(
            const RuntimeImportCompletedContext&,
            RuntimeImportCompletedServices&)> Handle{};
    };

    struct RuntimeImportCompletedHandlerRecord
    {
        RuntimeImportCompletedHandlerHandle Handle{};
        RuntimeImportCompletedHandlerDesc Desc{};
    };

    export struct RuntimeAssetImportResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t GeneratedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        std::uint64_t GeneratedTextureUploadRequests{0};
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
        StreamingExecutor* Streaming{};
        WorldRegistry* Worlds{};
        WorldHandle World{DefaultWorldHandle};
        Assets::AssetService* AssetService{};
        Graphics::GpuAssetCache* GpuAssetCache{};
        AssetModelTextureHandoff* ModelTextureHandoff{};
        AssetModelSceneHandoff* ModelSceneHandoff{};
        RenderExtractionCache* RenderExtraction{};
        ECS::Scene::Registry* Scene{};
        CameraControllerRegistry* CameraControllers{};
        SelectionController* Selection{};
        EditorCommandHistory* CommandHistory{};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{};
        const RHI::IDevice* Device{};
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

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPath(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport> QueueModelTextureImport(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport> QueueGeometryImport(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ReimportAsset(
            RuntimeAssetReimportRequest request);
        [[nodiscard]] RuntimePostImportProcessorHandle RegisterPostImportProcessor(
            RuntimePostImportProcessorDesc desc);
        void UnregisterPostImportProcessor(RuntimePostImportProcessorHandle handle);
        [[nodiscard]] RuntimeImportEntityAuthoringPolicyHandle
            RegisterImportEntityAuthoringPolicy(
                RuntimeImportEntityAuthoringPolicyDesc desc);
        void UnregisterImportEntityAuthoringPolicy(
            RuntimeImportEntityAuthoringPolicyHandle handle);
        [[nodiscard]] RuntimeImportCompletedHandlerHandle
            RegisterImportCompletedHandler(RuntimeImportCompletedHandlerDesc desc);
        void UnregisterImportCompletedHandler(RuntimeImportCompletedHandlerHandle handle);
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
        [[nodiscard]] std::size_t ClearCompletedAssetImports();
        [[nodiscard]] Core::Result CancelAssetImport(
            RuntimeAssetIngestHandle operation);
        void CancelActiveAssetImportsForShutdown();
        void ImportDroppedFilePaths(std::span<const std::string> paths);

    private:
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueGeometryImportWithIngest(
                RuntimeAssetImportRequest request,
                RuntimeAssetIngestSource source,
                std::vector<Assets::AssetPayloadKind> payloadKinds);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueModelTextureImportWithIngest(
                RuntimeAssetImportRequest request,
                RuntimeAssetIngestSource source,
                Assets::AssetId existingAsset = {});
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
        void FinalizeCancelledStreamingImport(
            RuntimeAssetIngestHandle operation,
            RuntimeAssetImportRequest request);
        void RecordAssetImportEvent(
            const RuntimeAssetImportRequest& request,
            const Core::Expected<RuntimeAssetImportResult>& result,
            RuntimeAssetIngestDiagnostic ingestDiagnostic);
        [[nodiscard]] bool IsCurrentSubmissionTarget(
            WorldHandle world,
            const ECS::Scene::Registry* scene,
            std::uint64_t bindingEpoch) const noexcept;

        BorrowedBool m_Initialized{};
        BorrowedSubsystem<const Core::Config::EngineConfig> m_Config{};
        BorrowedSubsystem<StreamingExecutor> m_StreamingExecutor{};
        BorrowedSubsystem<WorldRegistry> m_WorldRegistry{};
        WorldHandle m_World{DefaultWorldHandle};
        std::uint64_t m_TargetBindingEpoch{0u};
        BorrowedSubsystem<Assets::AssetService> m_AssetService{};
        BorrowedSubsystem<Graphics::GpuAssetCache> m_GpuAssetCache{};
        BorrowedSubsystem<AssetModelTextureHandoff> m_AssetModelTextureHandoff{};
        BorrowedSubsystem<AssetModelSceneHandoff> m_AssetModelSceneHandoff{};
        BorrowedSubsystem<RenderExtractionCache> m_RenderExtraction{};
        BorrowedSubsystem<ECS::Scene::Registry> m_Scene{};
        BorrowedSubsystem<CameraControllerRegistry> m_CameraControllers{};
        BorrowedSubsystem<SelectionController> m_SelectionController{};
        BorrowedSubsystem<EditorCommandHistory> m_EditorCommandHistory{};
        BorrowedSubsystem<RuntimeObjectSpaceNormalBakeQueue> m_ObjectSpaceNormalBakeQueue{};
        BorrowedSubsystem<const RHI::IDevice> m_Device{};
        RuntimeIOBackendFactory m_ModelTextureImportIOBackendFactoryForTest{};
        std::function<void(const RuntimeAssetImportRequest&)>
            m_QueuedGeometryImportBeforeDecodeHookForTest{};
        RuntimeAssetIngestStateMachine m_AssetIngestStateMachine{};
        std::vector<RuntimePostImportProcessorRecord> m_PostImportProcessors{};
        std::uint64_t m_NextPostImportProcessorHandle{1u};
        std::vector<RuntimeImportEntityAuthoringPolicyRecord>
            m_ImportEntityAuthoringPolicies{};
        std::uint64_t m_NextImportEntityAuthoringPolicyHandle{1u};
        std::vector<RuntimeImportCompletedHandlerRecord>
            m_ImportCompletedHandlers{};
        std::uint64_t m_NextImportCompletedHandlerHandle{1u};
        struct RuntimeAssetImportStreamingTask
        {
            RuntimeAssetIngestHandle Ingest{};
            StreamingTaskHandle Streaming{};
        };
        std::vector<RuntimeAssetImportStreamingTask> m_AssetImportStreamingTasks{};
        std::optional<RuntimeAssetImportEvent> m_LastAssetImportEvent{};
        std::uint64_t m_AssetImportEventSequence{0};
    };
}
