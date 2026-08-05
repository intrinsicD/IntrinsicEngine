module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

export module Extrinsic.Sandbox.Editor.MethodPanels;

import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Sandbox.Editor.Shell;

export namespace Extrinsic::Sandbox::Editor
{
    using SandboxParameterizationPanelConfig = decltype(
        Runtime::EditorParameterizationConfigCommand{}.Config);

    struct SandboxParameterizationStrategyOption
    {
        Runtime::EditorParameterizationStrategy Strategy{
            Runtime::EditorParameterizationStrategy::Lscm};
        std::string_view Label{};
        std::string_view StableToken{};
    };

    [[nodiscard]] std::array<SandboxParameterizationStrategyOption, 4u>
    SandboxParameterizationStrategyOptions() noexcept;

    struct SandboxParameterizationPanelApplyRequest
    {
        Runtime::EditorParameterizationConfigCommand Config{};
        Runtime::EditorConfiguredParameterizationCommand Execute{};
    };

    [[nodiscard]] std::optional<SandboxParameterizationPanelApplyRequest>
    BuildSandboxParameterizationPanelApplyRequest(
        std::uint32_t stableEntityId,
        const SandboxParameterizationPanelConfig& config);

    struct SandboxParameterizationPanelActionResult
    {
        Runtime::EditorParameterizationConfigResult Config{};
        std::optional<Runtime::EditorParameterizationResult> Execution{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Config.Succeeded() && Execution.has_value() &&
                   Execution->Succeeded();
        }
    };

    [[nodiscard]] SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const SandboxEditorContext& context,
        std::uint32_t stableEntityId,
        const SandboxParameterizationPanelConfig& config);

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

    struct SandboxParameterizationUvPane
    {
        glm::vec2 Min{0.0f};
        glm::vec2 Max{0.0f};
        float Padding{20.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
        bool IncludeUnitSquare{false};
    };

    struct SandboxParameterizationUvProjection
    {
        bool Valid{false};
        bool FitsPane{false};
        glm::vec2 PaneCenter{0.0f};
        glm::vec2 UvCenter{0.0f};
        float Scale{1.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
        std::vector<glm::vec2> Vertices{};
        std::vector<std::array<std::uint32_t, 3u>> Triangles{};
        std::string Message{};
    };

    [[nodiscard]] SandboxParameterizationUvProjection
    BuildSandboxParameterizationUvProjection(
        const Runtime::EditorParameterizationViewModel& model,
        const SandboxParameterizationUvPane& pane);

    [[nodiscard]] glm::vec2 ProjectSandboxParameterizationUvPoint(
        const SandboxParameterizationUvProjection& projection,
        glm::vec2 uv) noexcept;

    struct SandboxParameterizationResultSummary
    {
        bool Succeeded{false};
        bool HasDiagnostics{false};
        std::string StrategyToken{};
        std::string CommandStatus{};
        std::string SolverStatus{};
        std::string Message{};
        std::size_t EvaluatedFaceCount{0u};
        std::size_t SkippedFaceCount{0u};
        std::size_t FlippedElementCount{0u};
        std::size_t BoundaryEdgeCount{0u};
        double MeanConformalDistortion{0.0};
        double MeanAreaDistortion{0.0};
        double MeanStretch{0.0};
    };

    [[nodiscard]] SandboxParameterizationResultSummary
    BuildSandboxParameterizationResultSummary(
        const Runtime::EditorParameterizationResult& result);

    class MethodPanels final
    {
    public:
        MethodPanels();
        ~MethodPanels();

        MethodPanels(const MethodPanels&) = delete;
        MethodPanels& operator=(const MethodPanels&) = delete;
        MethodPanels(MethodPanels&&) = delete;
        MethodPanels& operator=(MethodPanels&&) = delete;

        void Register(EditorShell& editorShell);
        void Unregister();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
