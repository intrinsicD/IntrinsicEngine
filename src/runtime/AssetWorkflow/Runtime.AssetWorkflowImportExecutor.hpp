// Private import execution state; include only in AssetWorkflowModule
// implementation units after their imports. Queued work borrows this owner.
#pragma once

namespace Extrinsic::Runtime
{
    struct AssetWorkflowImportExecutorDependencies
    {
        const bool* Initialized{};
        const Core::Config::EngineConfig* Config{};
        JobService* Jobs{};
        WorldRegistry* Worlds{};
        WorldHandle World{DefaultWorldHandle};
        std::function<bool()> BindingValid{};
        Assets::AssetService* AssetService{};
        Graphics::GpuAssetCache* GpuAssetCache{};
        AssetWorkflowTextureResidency* TextureResidency{};
        AssetWorkflowModelMaterializer* ModelMaterializer{};
        RenderExtractionCache* RenderExtraction{};
        ECS::Scene::Registry* Scene{};
        SelectionController* Selection{};
        CameraControllerRegistry* CameraControllers{};
        EditorCommandHistory* CommandHistory{};
        TextureBakeService* TextureBake{};
    };

    class AssetWorkflowImportExecutor
    {
    public:
        AssetWorkflowImportExecutor() = default;

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
            GetTextureBakeServiceForTest() const noexcept;
        [[nodiscard]] std::size_t ClearCompletedAssetImports();
        [[nodiscard]] Core::Result CancelAssetImport(
            RuntimeAssetIngestHandle operation);
        void CancelActiveAssetImportsForShutdown();
        void ImportDroppedFilePaths(std::span<const std::string> paths);

    private:
        [[nodiscard]] Core::Expected<std::shared_ptr<AssetImportStageTrace>>
            SubmitQueuedImport(
                const RuntimeAssetImportRequest& request,
                const AssetImportRecipe& recipe,
                std::string_view payloadLabel);
        [[nodiscard]] Core::Result QueueImportDecode(
            const RuntimeAssetImportRequest& request,
            const Assets::AssetRouteDiagnostic& routeDiagnostic,
            AssetImportStageTrace& stageTrace);
        [[nodiscard]] bool BeginQueuedImportApply(
            const RuntimeAssetImportRequest& request,
            AssetImportStageTrace& stageTrace,
            const ECS::Scene::Registry* submissionScene);
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

        [[nodiscard]] bool IsInitialized() const noexcept;

        AssetWorkflowImportExecutorDependencies m_Dependencies{};
        std::uint64_t m_TargetBindingEpoch{0u};
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
