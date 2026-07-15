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

import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Sandbox.Editor.Shell;

export namespace Extrinsic::Sandbox::Editor
{
    using SandboxParameterizationPanelConfig = decltype(
        Runtime::SandboxEditorParameterizationConfigCommand{}.Config);

    struct SandboxParameterizationStrategyOption
    {
        Runtime::SandboxEditorParameterizationStrategy Strategy{
            Runtime::SandboxEditorParameterizationStrategy::Lscm};
        std::string_view Label{};
        std::string_view StableToken{};
    };

    [[nodiscard]] std::array<SandboxParameterizationStrategyOption, 4u>
    SandboxParameterizationStrategyOptions() noexcept;

    struct SandboxParameterizationPanelApplyRequest
    {
        Runtime::SandboxEditorParameterizationConfigCommand Config{};
        Runtime::SandboxEditorConfiguredParameterizationCommand Execute{};
    };

    [[nodiscard]] std::optional<SandboxParameterizationPanelApplyRequest>
    BuildSandboxParameterizationPanelApplyRequest(
        std::uint32_t stableEntityId,
        const SandboxParameterizationPanelConfig& config);

    struct SandboxParameterizationPanelActionResult
    {
        Runtime::SandboxEditorParameterizationConfigResult Config{};
        std::optional<Runtime::SandboxEditorParameterizationResult> Execution{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Config.Succeeded() && Execution.has_value() &&
                   Execution->Succeeded();
        }
    };

    [[nodiscard]] SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const Runtime::SandboxEditorContext& context,
        std::uint32_t stableEntityId,
        const SandboxParameterizationPanelConfig& config);

    struct SandboxParameterizationUvPane
    {
        glm::vec2 Min{0.0f};
        glm::vec2 Max{0.0f};
        float Padding{20.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
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
        const Runtime::SandboxEditorParameterizationViewModel& model,
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
        const Runtime::SandboxEditorParameterizationResult& result);

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
