// Editor dispatch and prepared frames for service-queued K-Means and consolidation.
// Borrowed service access is valid only while Commands.IsBound(); attachment
// validation prevents dereferencing services after workspace detach.
module;
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.PointCloudServiceOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.ClusteringConfig;
export import Extrinsic.Runtime.ClusteringTypes;
export import Extrinsic.Runtime.PointCloudConsolidationConfig;
export import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    enum class EditorPointCloudServiceResultSlot : std::uint8_t
    {
        KMeans,
        PointCloudConsolidation,
    };
    // The services publish completion through their own subscriptions, so the
    // session needs only the explicit dismissal here. Borrowed incomplete
    // containers keep sibling features free of these records.
    extern "C++"
    {
        struct EditorPointCloudServiceResultSinks
        {
            std::function<void(EditorPointCloudServiceResultSlot)> DismissResult{};
        };
        struct EditorPointCloudServiceResultsSnapshot
        {
            std::optional<KMeansRunCompleted> LastKMeansResult{};
            std::optional<PointCloudConsolidationResult> LastPointCloudConsolidationResult{};
        };
        // Service pointers may only be used while the prepared Commands.IsBound().
        struct EditorPointCloudServiceBorrowedServices
        {
            ClusteringService* Clustering{nullptr};
            PointCloudConsolidationService* PointCloudConsolidation{nullptr};
        };
        struct EditorPointCloudServicePreparedFrame
        {
            EditorProcessingCommands Commands{};
            // Borrowed while `Commands.IsBound()`; never dereferenced otherwise.
            ClusteringService* Clustering{nullptr};
            PointCloudConsolidationService* PointCloudConsolidation{nullptr};
            EditorPointCloudServiceResultSinks ResultSinks{};
            EditorPointCloudServiceResultsSnapshot Results{};
            bool ClusteringAvailable{false};
            bool PointCloudConsolidationAvailable{false};
        };
    }
    [[nodiscard]] EditorPointCloudServicePreparedFrame
    PrepareEditorPointCloudServiceFrame(const EditorWorkspaceAttachment&);

    [[nodiscard]] bool IsEditorClusteringAvailable(
        const EditorProcessingCommands&, const ClusteringService*) noexcept;
    [[nodiscard]] bool IsEditorPointCloudConsolidationAvailable(
        const EditorProcessingCommands&, const PointCloudConsolidationService*) noexcept;

    [[nodiscard]] ActionReadiness PreviewEditorKMeansRun(
        const EditorProcessingCommands&, const ClusteringService*, const RunKMeans&);
    [[nodiscard]] KMeansRunCompleted SubmitKMeansRun(
        const EditorProcessingCommands&, ClusteringService*, const RunKMeans&);
    [[nodiscard]] PointCloudConsolidationResult SubmitEditorPointCloudConsolidation(
        const EditorProcessingCommands&, PointCloudConsolidationService*,
        PointCloudConsolidationRequest);
    [[nodiscard]] PointCloudConsolidationAvailability
    PrepareEditorPointCloudConsolidationAvailability(
        const EditorProcessingCommands&, PointCloudConsolidationService*,
        const PointCloudConsolidationRequest&);

    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorClusteringConfig(
        const EditorProcessingCommands&, const ClusteringConfig&,
        std::string sourceId = "sandbox.clustering");
    [[nodiscard]] std::optional<ClusteringConfig> GetEditorClusteringConfig(
        const EditorProcessingCommands&) noexcept;

    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointCloudConsolidationConfig(
        const EditorProcessingCommands&, const PointCloudConsolidationConfig&,
        std::string sourceId = "sandbox.point_cloud_consolidation");
    [[nodiscard]] bool IsValidEditorPointCloudConsolidationConfig(
        const PointCloudConsolidationConfig&);
    [[nodiscard]] std::optional<PointCloudConsolidationConfig>
    GetEditorPointCloudConsolidationConfig(const EditorProcessingCommands&) noexcept;
}
