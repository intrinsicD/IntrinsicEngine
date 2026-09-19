// Consolidation action/view records shared by its method panel and integration tests.
// Include after runtime imports and Sandbox.PanelSupport.hpp; these declarations
// stay out of unrelated panel consumers. Definitions live in MethodPanels.cpp.
#pragma once

extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    using SandboxPointCloudConsolidationPanelConfig =
        Runtime::PointCloudConsolidationConfig;

    struct SandboxPointCloudConsolidationStrategyOption
    {
        Runtime::PointCloudConsolidationStrategy Strategy{
            Runtime::PointCloudConsolidationStrategy::Wlop};
        std::string_view Label{};
        std::string_view StableToken{};
        bool Available{false};
    };

    [[nodiscard]]
    std::array<SandboxPointCloudConsolidationStrategyOption, 4u>
    SandboxPointCloudConsolidationStrategyOptions() noexcept;

    struct SandboxPointCloudConsolidationPanelApplyRequest
    {
        Runtime::PointCloudConsolidationConfig Config{};
        Runtime::PointCloudConsolidationRequest Execute{};
        std::string SourceId{
            "sandbox.point_cloud_consolidation.panel"};
    };

    [[nodiscard]]
    std::optional<SandboxPointCloudConsolidationPanelApplyRequest>
    BuildSandboxPointCloudConsolidationPanelApplyRequest(
        std::uint32_t stableEntityId,
        const Runtime::PointCloudConsolidationPropertyRefs& properties,
        const SandboxPointCloudConsolidationPanelConfig& config);

    struct SandboxPointCloudConsolidationPanelActionResult
    {
        Runtime::RuntimeEngineConfigApplyResult Config{};
        std::optional<Runtime::PointCloudConsolidationResult> Submission{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Config.Succeeded() && Submission.has_value() &&
                   Submission->Status ==
                       Runtime::PointCloudConsolidationRunStatus::Queued;
        }
    };

    [[nodiscard]] SandboxPointCloudConsolidationPanelActionResult
    ApplySandboxPointCloudConsolidationPanelAction(
        const SandboxEditorContext& context,
        std::uint32_t stableEntityId,
        const Runtime::PointCloudConsolidationPropertyRefs& properties,
        const SandboxPointCloudConsolidationPanelConfig& config);

    struct SandboxPointCloudConsolidationResultSummary
    {
        bool Succeeded{false};
        bool Queued{false};
        std::string Status{};
        std::string ImplementationId{};
        std::string StrategyToken{};
        std::string RequestedBackend{};
        std::string ActualBackend{};
        bool FellBackToCpu{false};
        std::string BackendDiagnostic{};
        std::string SupportRadiusAnalysisStatus{};
        std::string SupportRadiusSource{};
        std::string SupportRadiusQuantile{};
        std::string Message{};
        std::uint32_t SupportRadiusEstimatorVersion{0u};
        std::uint32_t SupportRadiusProfileSampleCount{0u};
        std::uint32_t SupportRadiusRequestedNeighborRank{0u};
        std::uint32_t SupportRadiusNeighborRank{0u};
        bool SupportRadiusWorkloadAdjusted{false};
        double SupportRadiusNeighborDistance{0.0};
        double ResolvedSupportRadius{0.0};
        double SupportRadiusBoundingBoxDiagonal{0.0};
        double SupportNeighborsP50{0.0};
        double SupportNeighborsP95{0.0};
        std::uint32_t SupportNeighborsMax{0u};
        std::uint64_t PredictedSupportQueryCount{0u};
        std::uint64_t PredictedContributionCount{0u};
        std::uint32_t InputPointCount{0u};
        std::uint32_t OutputPointCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        double AverageDisplacement{0.0};
        double MaxDisplacement{0.0};
        bool UsedAuthoredNormals{false};
        bool EstimatedNormals{false};
        std::uint32_t NormalRefinementIterations{0u};
        std::uint32_t InsertedPointCount{0u};
    };

    [[nodiscard]] SandboxPointCloudConsolidationResultSummary
    BuildSandboxPointCloudConsolidationResultSummary(
        const Runtime::PointCloudConsolidationResult& result);

}
}
