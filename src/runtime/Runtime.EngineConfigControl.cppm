module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EngineConfigControl;

export import Extrinsic.Runtime.RenderRecipeActivation;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Runtime.Module;

namespace Extrinsic::Runtime
{
    export using RuntimeEngineConfigSectionRegistry =
        Core::Config::EngineConfigSectionRegistry;

    export enum class RuntimeConfigControlSource : std::uint8_t
    {
        None = 0,
        AgentCli,
        Editor,
        Programmatic,
    };

    export enum class RuntimeEngineConfigApplyStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        Rejected,
    };

    export struct RuntimeEngineConfigApplyResult
    {
        RuntimeEngineConfigApplyStatus Status{
            RuntimeEngineConfigApplyStatus::None
        };
        RuntimeConfigControlSource Source{RuntimeConfigControlSource::None};
        Core::Config::EngineConfigLoadResult LoadResult{};
        RuntimeRenderRecipeApplyResult RecipeApply{};
        bool EngineConfigApplied{false};
        bool DefaultRecipeConfigPathChanged{false};
        bool GpuProfilingChanged{false};
        std::vector<std::string> ChangedSectionNames{};
        std::vector<std::string> RejectedBootOnlyFields{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeEngineConfigApplyStatus::Applied ||
                Status == RuntimeEngineConfigApplyStatus::NoChange;
        }

        [[nodiscard]] bool SectionChanged(std::string_view name) const noexcept;
    };

    export struct RuntimeEngineConfigControlState
    {
        Core::Config::EngineConfig ActiveConfig{};
        RuntimeEngineConfigApplyResult LastApply{};
        bool HasLastApply{false};
    };

    export class EngineConfigControl final : public IRuntimeModule
    {
    public:
        EngineConfigControl() = default;
        explicit EngineConfigControl(
            RuntimeEngineConfigSectionRegistry sectionRegistry);

        [[nodiscard]] RuntimeEngineConfigSectionRegistry&
        SectionRegistry() noexcept;
        [[nodiscard]] const RuntimeEngineConfigSectionRegistry&
        SectionRegistry() const noexcept;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] Graphics::RenderRecipeConfigContext
        CreateRenderRecipeConfigContext() const;
        [[nodiscard]] Graphics::RenderRecipeConfigLoadResult
        PreviewRenderRecipeConfigDocument(
            std::string_view document,
            std::string sourceId = "<memory>") const;
        [[nodiscard]] Graphics::RenderRecipeConfigLoadResult
        LoadRenderRecipeConfigPreviewFile(std::string path) const;
        [[nodiscard]] RuntimeRenderRecipeApplyResult
        ActivateRenderRecipeConfigDocument(
            std::string_view document,
            std::string sourceId = "<memory>",
            RuntimeRenderRecipeActivationSource source =
                RuntimeRenderRecipeActivationSource::Programmatic);
        [[nodiscard]] RuntimeRenderRecipeApplyResult ApplyRenderRecipeConfigPreview(
            const Graphics::RenderRecipeConfigLoadResult& loadResult,
            RuntimeRenderRecipeActivationSource source =
                RuntimeRenderRecipeActivationSource::Programmatic);
        [[nodiscard]] RuntimeRenderRecipeApplyResult LoadAndApplyRenderRecipeConfigFile(
            std::string path,
            RuntimeRenderRecipeActivationSource source =
                RuntimeRenderRecipeActivationSource::Programmatic);
        void ClearActiveRenderRecipeOverride() noexcept;
        [[nodiscard]] const RuntimeRenderRecipeState&
        GetRenderRecipeState() const noexcept;

        [[nodiscard]] Core::Config::EngineConfigLoadResult
        PreviewEngineConfigControlDocument(
            std::string_view document,
            std::string sourceId = "<memory>") const;
        [[nodiscard]] Core::Config::EngineConfigLoadResult
        LoadEngineConfigControlFile(std::string path) const;
        [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEngineConfigHotSubset(
            const Core::Config::EngineConfigLoadResult& loadResult,
            RuntimeConfigControlSource source =
                RuntimeConfigControlSource::Programmatic);
        [[nodiscard]] RuntimeEngineConfigApplyResult
        LoadAndApplyEngineConfigHotSubsetFile(
            std::string path,
            RuntimeConfigControlSource source =
                RuntimeConfigControlSource::Programmatic);
        [[nodiscard]] const RuntimeEngineConfigControlState&
        GetEngineConfigControlState() const noexcept;

    private:
        [[nodiscard]] const Core::Config::EngineConfig& CurrentConfig() const noexcept;
        [[nodiscard]] Core::Config::EngineConfig* MutableConfig() const noexcept;
        void RecordConfigApply(RuntimeEngineConfigApplyResult result);
        [[nodiscard]] Core::Result Bind(
            const RuntimeRenderRecipeActivationKernel& kernel);
        void ClearBinding() noexcept;

        RuntimeEngineConfigSectionRegistry m_SectionRegistry{};
        RuntimeRenderRecipeActivationKernel m_RecipeActivation{};
        RuntimeRenderRecipeState m_RenderRecipeState{};
        RuntimeEngineConfigControlState m_ConfigControlState{};
        bool m_Published{false};
    };
}
