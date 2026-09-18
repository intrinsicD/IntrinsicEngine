// Asset import recipes, stage records and the runtime composition module.
// Validation and workflow execution compile in the implementation units.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module Extrinsic.Runtime.AssetWorkflowModule;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;

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

    export struct AssetImportRouteResult
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{
            Assets::AssetPayloadKind::Unknown};
    };

    export struct AssetImportDecodeResult
    {
        Assets::AssetPayloadKind PayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::size_t OwnedValueCount{0u};
    };

    export struct AssetImportCpuMaterializationResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveCount{0u};
        std::uint64_t EmbeddedTextureCount{0u};
    };

    export struct AssetImportEcsAuthorResult
    {
        std::uint64_t CreatedEntityCount{0u};
    };

    export struct AssetImportPostprocessResult
    {
        AssetImportPostprocessPolicy Policy{
            AssetImportPostprocessPolicy::None};
        bool Requested{false};
    };

    export struct AssetImportGpuResidencyResult
    {
        std::uint64_t RequestCount{0u};
        bool Requested{false};
    };

    export struct AssetImportCompletionResult
    {
        bool SelectFirstCreatedEntity{false};
        bool FocusCameraOnCreatedGeometry{false};
    };

    export using AssetImportStagePayload = std::variant<
        std::monostate,
        AssetImportRouteResult,
        AssetImportDecodeResult,
        AssetImportCpuMaterializationResult,
        AssetImportEcsAuthorResult,
        AssetImportPostprocessResult,
        AssetImportGpuResidencyResult,
        AssetImportCompletionResult>;

    export struct AssetImportStageResult
    {
        AssetImportExecutionIdentity Identity{};
        AssetImportStage Stage{AssetImportStage::Route};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic Diagnostic{
            RuntimeAssetIngestDiagnostic::None};
        AssetImportStagePayload Payload{};

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

    export [[nodiscard]] bool AssetImportStagePayloadMatches(
        AssetImportStage stage, const AssetImportStagePayload& payload) noexcept;

    export [[nodiscard]] Core::Result ValidateAssetImportRecipe(
        const AssetImportRecipe& recipe) noexcept;

    export [[nodiscard]] Core::Result AppendAssetImportStageResult(
        AssetImportStageTrace& trace, AssetImportStageResult result);

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
        Assets::AssetPayloadKind RequestedPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic IngestDiagnostic{
            RuntimeAssetIngestDiagnostic::None};
        std::optional<RuntimeAssetImportResult> Result{};
        std::optional<AssetImportStageTrace> StageTrace{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Result.has_value() &&
                Error == Core::ErrorCode::Success;
        }
    };

    export struct RuntimeQueuedAssetImport
    {
        RuntimeAssetIngestHandle Operation{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export class AssetWorkflowModule final : public IRuntimeModule
    {
    public:
        AssetWorkflowModule();
        ~AssetWorkflowModule() override;

        AssetWorkflowModule(const AssetWorkflowModule&) = delete;
        AssetWorkflowModule& operator=(const AssetWorkflowModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueAssetImport(AssetImportRecipe recipe);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
            ImportAssetFromPath(RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueModelTextureImport(RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueGeometryImport(RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
            ReimportAsset(RuntimeAssetReimportRequest request);
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
        void RunFrameMaintenance();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
