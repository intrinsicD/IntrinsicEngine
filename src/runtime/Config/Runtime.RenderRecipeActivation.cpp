module;

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.RenderRecipeActivation;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;

namespace Extrinsic::Runtime
{
    Graphics::RenderRecipeConfigContext CreateRuntimeRenderRecipeConfigContext(
        const RuntimeRenderRecipeActivationKernel& kernel)
    {
        Core::Extent2D viewport{.Width = 1, .Height = 1};
        if (kernel.ActiveConfig != nullptr)
        {
            viewport = Core::Extent2D{
                .Width = std::max(kernel.ActiveConfig->Window.Width, 1),
                .Height = std::max(kernel.ActiveConfig->Window.Height, 1),
            };
        }
        if (kernel.ReadFramebufferExtent)
        {
            const Core::Extent2D extent = kernel.ReadFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                viewport = extent;
            }
        }

        const Graphics::RenderFrameInput recipeInput{
            .Viewport = viewport,
            .Camera = Graphics::CameraViewInput{.Valid = true},
        };
        return Graphics::RenderRecipeConfigContext{
            .Renderer = Graphics::MakeCurrentRendererDescriptor(),
            .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput =
                Graphics::MakeCurrentRendererViewOutputRecipe(recipeInput),
            .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
        };
    }

    Graphics::RenderRecipeConfigLoadResult
    PreviewRuntimeRenderRecipeConfigDocument(
        const RuntimeRenderRecipeActivationKernel& kernel,
        const std::string_view document,
        std::string sourceId)
    {
        return Graphics::PreviewRenderRecipeConfig(
            document,
            CreateRuntimeRenderRecipeConfigContext(kernel),
            Graphics::RenderRecipeConfigParseOptions{
                .SourceId = std::move(sourceId),
            });
    }

    Graphics::RenderRecipeConfigLoadResult
    LoadRuntimeRenderRecipeConfigPreviewFile(
        const RuntimeRenderRecipeActivationKernel& kernel,
        std::string path)
    {
        const std::string sourceId = path;
        return Graphics::LoadRenderRecipeConfigFile(
            path,
            CreateRuntimeRenderRecipeConfigContext(kernel),
            Graphics::RenderRecipeConfigParseOptions{
                .SourceId = sourceId,
            });
    }

    RuntimeRenderRecipeApplyResult ApplyRuntimeRenderRecipeConfigPreview(
        const RuntimeRenderRecipeActivationKernel& kernel,
        const Graphics::RenderRecipeConfigLoadResult& loadResult,
        const RuntimeRenderRecipeActivationSource source)
    {
        RuntimeRenderRecipeApplyResult result{
            .Source = source,
            .LoadResult = loadResult,
        };

        if (!kernel.SetFrameRecipeOverride || kernel.State == nullptr)
        {
            result.Status = RuntimeRenderRecipeApplyStatus::MissingRenderer;
            if (kernel.State != nullptr)
            {
                kernel.State->LastApply = result;
                kernel.State->HasLastApply = true;
            }
            return result;
        }

        if (!Graphics::IsConfigUsable(loadResult))
        {
            kernel.SetFrameRecipeOverride(std::nullopt);
            kernel.State->ActiveOverride.reset();
            kernel.State->ActiveConfig =
                Graphics::RenderRecipeConfigLoadResult{};
            kernel.State->HasActiveConfig = false;
            kernel.State->ActiveSource =
                RuntimeRenderRecipeActivationSource::None;
            result.Status = RuntimeRenderRecipeApplyStatus::Rejected;
            kernel.State->LastApply = result;
            kernel.State->HasLastApply = true;
            return result;
        }

        Graphics::FrameRecipeOverride recipeOverride{
            .Recipe = loadResult.Preview.Recipe,
            .DisabledExtensionSlots =
                loadResult.Preview.DisabledExtensionSlots,
            .SourceId = loadResult.SourceId,
        };
        kernel.SetFrameRecipeOverride(
            std::make_optional(recipeOverride));
        kernel.State->ActiveOverride = recipeOverride;
        kernel.State->ActiveConfig = loadResult;
        kernel.State->HasActiveConfig = true;
        kernel.State->ActiveSource = source;
        result.Status = RuntimeRenderRecipeApplyStatus::Applied;
        result.RendererOverrideInstalled = true;
        kernel.State->LastApply = result;
        kernel.State->HasLastApply = true;
        return result;
    }

    RuntimeRenderRecipeApplyResult ActivateRuntimeRenderRecipeConfigDocument(
        const RuntimeRenderRecipeActivationKernel& kernel,
        const std::string_view document,
        std::string sourceId,
        const RuntimeRenderRecipeActivationSource source)
    {
        const Graphics::RenderRecipeConfigLoadResult loadResult =
            PreviewRuntimeRenderRecipeConfigDocument(
                kernel,
                document,
                std::move(sourceId));
        return ApplyRuntimeRenderRecipeConfigPreview(
            kernel,
            loadResult,
            source);
    }

    RuntimeRenderRecipeApplyResult
    LoadAndApplyRuntimeRenderRecipeConfigFile(
        const RuntimeRenderRecipeActivationKernel& kernel,
        std::string path,
        const RuntimeRenderRecipeActivationSource source)
    {
        const Graphics::RenderRecipeConfigLoadResult loadResult =
            LoadRuntimeRenderRecipeConfigPreviewFile(
                kernel,
                std::move(path));
        return ApplyRuntimeRenderRecipeConfigPreview(
            kernel,
            loadResult,
            source);
    }

    void ClearRuntimeRenderRecipeOverride(
        const RuntimeRenderRecipeActivationKernel& kernel) noexcept
    {
        if (kernel.SetFrameRecipeOverride)
        {
            kernel.SetFrameRecipeOverride(std::nullopt);
        }
        if (kernel.State == nullptr)
        {
            return;
        }
        kernel.State->ActiveOverride.reset();
        kernel.State->ActiveConfig =
            Graphics::RenderRecipeConfigLoadResult{};
        kernel.State->HasActiveConfig = false;
        kernel.State->ActiveSource =
            RuntimeRenderRecipeActivationSource::None;
    }

    void ResetRuntimeRenderRecipeActivation(
        const RuntimeRenderRecipeActivationKernel& kernel) noexcept
    {
        if (kernel.SetFrameRecipeOverride)
        {
            kernel.SetFrameRecipeOverride(std::nullopt);
        }
        if (kernel.State != nullptr)
        {
            *kernel.State = RuntimeRenderRecipeState{};
        }
    }
}
