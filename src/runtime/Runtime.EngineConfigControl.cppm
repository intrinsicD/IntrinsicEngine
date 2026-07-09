module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EngineConfigControl;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Window;

namespace Extrinsic::Runtime
{
    export enum class RuntimeRenderRecipeActivationSource : std::uint8_t
    {
        None = 0,
        StartupConfigFile,
        AgentCli,
        Editor,
        Programmatic,
    };

    export enum class RuntimeConfigControlSource : std::uint8_t
    {
        None = 0,
        AgentCli,
        Editor,
        Programmatic,
    };

    export enum class RuntimeRenderRecipeApplyStatus : std::uint8_t
    {
        None = 0,
        Applied,
        Rejected,
        MissingRenderer,
    };

    export struct RuntimeRenderRecipeApplyResult
    {
        RuntimeRenderRecipeApplyStatus Status{RuntimeRenderRecipeApplyStatus::None};
        RuntimeRenderRecipeActivationSource Source{
            RuntimeRenderRecipeActivationSource::None
        };
        Graphics::RenderRecipeConfigLoadResult LoadResult{};
        bool RendererOverrideInstalled{false};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeRenderRecipeApplyStatus::Applied;
        }
    };

    export struct RuntimeRenderRecipeState
    {
        std::optional<Graphics::FrameRecipeOverride> ActiveOverride{};
        Graphics::RenderRecipeConfigLoadResult ActiveConfig{};
        bool HasActiveConfig{false};
        RuntimeRenderRecipeActivationSource ActiveSource{
            RuntimeRenderRecipeActivationSource::None
        };
        RuntimeRenderRecipeApplyResult LastApply{};
        bool HasLastApply{false};
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
        bool SandboxProgressivePoissonChanged{false};
        std::vector<std::string> RejectedBootOnlyFields{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeEngineConfigApplyStatus::Applied ||
                Status == RuntimeEngineConfigApplyStatus::NoChange;
        }
    };

    export struct RuntimeEngineConfigControlState
    {
        Core::Config::EngineConfig ActiveConfig{};
        RuntimeEngineConfigApplyResult LastApply{};
        bool HasLastApply{false};
    };

    export struct EngineConfigControlDependencies
    {
        Core::Config::EngineConfig* Config{};
        const Platform::IWindow* Window{};
        Graphics::IRenderer* Renderer{};
    };

    export class EngineConfigControl
    {
    public:
        EngineConfigControl() = default;
        explicit EngineConfigControl(EngineConfigControlDependencies dependencies);

        void SetDependencies(EngineConfigControlDependencies dependencies);

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

        EngineConfigControlDependencies m_Dependencies{};
        RuntimeRenderRecipeState m_RenderRecipeState{};
        RuntimeEngineConfigControlState m_ConfigControlState{};
    };
}
