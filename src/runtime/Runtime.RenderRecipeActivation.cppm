module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.RenderRecipeActivation;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;

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

    export enum class RuntimeRenderRecipeApplyStatus : std::uint8_t
    {
        None = 0,
        Applied,
        Rejected,
        MissingRenderer,
    };

    export struct RuntimeRenderRecipeApplyResult
    {
        RuntimeRenderRecipeApplyStatus Status{
            RuntimeRenderRecipeApplyStatus::None};
        RuntimeRenderRecipeActivationSource Source{
            RuntimeRenderRecipeActivationSource::None};
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
            RuntimeRenderRecipeActivationSource::None};
        RuntimeRenderRecipeApplyResult LastApply{};
        bool HasLastApply{false};
    };

    // Plain, borrowed kernel capability shared by Engine startup and the
    // optional live-control module. It carries no renderer/window ownership.
    // EngineSetup's State is valid only for that boot's registration and
    // resolution callbacks. A module retaining this capability must first copy
    // the state into module-owned storage and retarget State, as
    // EngineConfigControl does.
    export struct RuntimeRenderRecipeActivationKernel
    {
        Core::Config::EngineConfig* ActiveConfig{};
        RuntimeRenderRecipeState* State{};
        std::function<Core::Extent2D()> ReadFramebufferExtent{};
        std::function<void(std::optional<Graphics::FrameRecipeOverride>)>
            SetFrameRecipeOverride{};
    };

    export [[nodiscard]] Graphics::RenderRecipeConfigContext
    CreateRuntimeRenderRecipeConfigContext(
        const RuntimeRenderRecipeActivationKernel& kernel);

    export [[nodiscard]] Graphics::RenderRecipeConfigLoadResult
    PreviewRuntimeRenderRecipeConfigDocument(
        const RuntimeRenderRecipeActivationKernel& kernel,
        std::string_view document,
        std::string sourceId = "<memory>");

    export [[nodiscard]] Graphics::RenderRecipeConfigLoadResult
    LoadRuntimeRenderRecipeConfigPreviewFile(
        const RuntimeRenderRecipeActivationKernel& kernel,
        std::string path);

    export [[nodiscard]] RuntimeRenderRecipeApplyResult
    ApplyRuntimeRenderRecipeConfigPreview(
        const RuntimeRenderRecipeActivationKernel& kernel,
        const Graphics::RenderRecipeConfigLoadResult& loadResult,
        RuntimeRenderRecipeActivationSource source =
            RuntimeRenderRecipeActivationSource::Programmatic);

    export [[nodiscard]] RuntimeRenderRecipeApplyResult
    ActivateRuntimeRenderRecipeConfigDocument(
        const RuntimeRenderRecipeActivationKernel& kernel,
        std::string_view document,
        std::string sourceId = "<memory>",
        RuntimeRenderRecipeActivationSource source =
            RuntimeRenderRecipeActivationSource::Programmatic);

    export [[nodiscard]] RuntimeRenderRecipeApplyResult
    LoadAndApplyRuntimeRenderRecipeConfigFile(
        const RuntimeRenderRecipeActivationKernel& kernel,
        std::string path,
        RuntimeRenderRecipeActivationSource source =
            RuntimeRenderRecipeActivationSource::Programmatic);

    export void ClearRuntimeRenderRecipeOverride(
        const RuntimeRenderRecipeActivationKernel& kernel) noexcept;

    export void ResetRuntimeRenderRecipeActivation(
        const RuntimeRenderRecipeActivationKernel& kernel) noexcept;
}
