module;

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.EngineConfigControl;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Platform.Window;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] RuntimeRenderRecipeActivationSource ToRecipeActivationSource(
            const RuntimeConfigControlSource source) noexcept
        {
            switch (source)
            {
            case RuntimeConfigControlSource::AgentCli:
                return RuntimeRenderRecipeActivationSource::AgentCli;
            case RuntimeConfigControlSource::Editor:
                return RuntimeRenderRecipeActivationSource::Editor;
            case RuntimeConfigControlSource::Programmatic:
                return RuntimeRenderRecipeActivationSource::Programmatic;
            case RuntimeConfigControlSource::None:
                return RuntimeRenderRecipeActivationSource::None;
            }
            return RuntimeRenderRecipeActivationSource::None;
        }

        void RecordBootOnlyDifference(std::vector<std::string>& fields,
                                      const bool differs,
                                      std::string field)
        {
            if (differs)
            {
                fields.push_back(std::move(field));
            }
        }

        [[nodiscard]] bool ProgressivePoissonPlaygroundConfigEquals(
            const Core::Config::ProgressivePoissonPlaygroundConfig& lhs,
            const Core::Config::ProgressivePoissonPlaygroundConfig& rhs) noexcept
        {
            return lhs.Dimension == rhs.Dimension &&
                   lhs.GridWidth == rhs.GridWidth &&
                   lhs.MaxLevels == rhs.MaxLevels &&
                   lhs.HashLoadFactor == rhs.HashLoadFactor &&
                   lhs.RadiusAlpha == rhs.RadiusAlpha &&
                   lhs.RandomizeGridOrigin == rhs.RandomizeGridOrigin &&
                   lhs.GridOriginSeed == rhs.GridOriginSeed &&
                   lhs.ShuffleWithinLevels == rhs.ShuffleWithinLevels &&
                   lhs.ShuffleSeed == rhs.ShuffleSeed &&
                   lhs.PrefixCount == rhs.PrefixCount &&
                   lhs.Channel == rhs.Channel &&
                   lhs.Backend == rhs.Backend &&
                   lhs.MeshSurfaceSampleCount == rhs.MeshSurfaceSampleCount &&
                   lhs.MeshSurfaceSampleSeed == rhs.MeshSurfaceSampleSeed &&
                   lhs.MeshSurfaceMinTriangleArea == rhs.MeshSurfaceMinTriangleArea &&
                   lhs.MeshSurfaceInterpolateNormals == rhs.MeshSurfaceInterpolateNormals &&
                   lhs.AutoRunOnEdit == rhs.AutoRunOnEdit &&
                   lhs.DebounceSeconds == rhs.DebounceSeconds;
        }

        [[nodiscard]] bool ParameterizationUvConfigEquals(
            const Core::Config::ParameterizationUvConfig& lhs,
            const Core::Config::ParameterizationUvConfig& rhs) noexcept
        {
            return lhs.U == rhs.U && lhs.V == rhs.V;
        }

        [[nodiscard]] bool ParameterizationConfigEquals(
            const Core::Config::ParameterizationConfig& lhs,
            const Core::Config::ParameterizationConfig& rhs) noexcept
        {
            const bool pinnedUvsEqual =
                lhs.Harmonic.PinnedUvs.size() == rhs.Harmonic.PinnedUvs.size() &&
                std::equal(lhs.Harmonic.PinnedUvs.begin(),
                           lhs.Harmonic.PinnedUvs.end(),
                           rhs.Harmonic.PinnedUvs.begin(),
                           ParameterizationUvConfigEquals);
            return lhs.Strategy == rhs.Strategy &&
                   lhs.Lscm.AutoPins == rhs.Lscm.AutoPins &&
                   lhs.Lscm.PinVertex0 == rhs.Lscm.PinVertex0 &&
                   lhs.Lscm.PinVertex1 == rhs.Lscm.PinVertex1 &&
                   ParameterizationUvConfigEquals(lhs.Lscm.PinUv0,
                                                  rhs.Lscm.PinUv0) &&
                   ParameterizationUvConfigEquals(lhs.Lscm.PinUv1,
                                                  rhs.Lscm.PinUv1) &&
                   lhs.Lscm.SolverTolerance == rhs.Lscm.SolverTolerance &&
                   lhs.Lscm.MaxSolverIterations == rhs.Lscm.MaxSolverIterations &&
                   lhs.Harmonic.Boundary == rhs.Harmonic.Boundary &&
                   lhs.Harmonic.ArcLengthSpacing == rhs.Harmonic.ArcLengthSpacing &&
                   lhs.Harmonic.ClampNonConvexWeights ==
                       rhs.Harmonic.ClampNonConvexWeights &&
                   lhs.Harmonic.PinnedVertices == rhs.Harmonic.PinnedVertices &&
                   pinnedUvsEqual &&
                   lhs.Bff.Mode == rhs.Bff.Mode &&
                   lhs.Bff.BoundaryData == rhs.Bff.BoundaryData &&
                   lhs.Bff.AngleSumTolerance == rhs.Bff.AngleSumTolerance &&
                   lhs.Bff.DegeneracyTolerance == rhs.Bff.DegeneracyTolerance;
        }

        [[nodiscard]] std::vector<std::string> FindBootOnlyEngineConfigDifferences(
            const Core::Config::EngineConfig& current,
            const Core::Config::EngineConfig& candidate)
        {
            std::vector<std::string> fields{};
            RecordBootOnlyDifference(fields,
                                      current.Window.Title != candidate.Window.Title,
                                      "window.title");
            RecordBootOnlyDifference(fields,
                                      current.Window.Width != candidate.Window.Width,
                                      "window.width");
            RecordBootOnlyDifference(fields,
                                      current.Window.Height != candidate.Window.Height,
                                      "window.height");
            RecordBootOnlyDifference(fields,
                                      current.Window.Resizable != candidate.Window.Resizable,
                                      "window.resizable");
            RecordBootOnlyDifference(fields,
                                      current.Window.Backend != candidate.Window.Backend,
                                      "window.backend");
            RecordBootOnlyDifference(fields,
                                      current.Render.Backend != candidate.Render.Backend,
                                      "render.backend");
            RecordBootOnlyDifference(
                fields,
                current.Render.EnablePromotedVulkanDevice !=
                    candidate.Render.EnablePromotedVulkanDevice,
                "render.enable_promoted_vulkan_device");
            RecordBootOnlyDifference(fields,
                                      current.Render.EnableValidation !=
                                          candidate.Render.EnableValidation,
                                      "render.enable_validation");
            RecordBootOnlyDifference(fields,
                                      current.Render.EnableVSync !=
                                          candidate.Render.EnableVSync,
                                      "render.enable_vsync");
            RecordBootOnlyDifference(fields,
                                      current.Render.FramesInFlight !=
                                          candidate.Render.FramesInFlight,
                                      "render.frames_in_flight");
            RecordBootOnlyDifference(fields,
                                      current.Render.SynchronousExtraction !=
                                          candidate.Render.SynchronousExtraction,
                                      "render.synchronous_extraction");
            RecordBootOnlyDifference(fields,
                                      current.Simulation.WorkerThreadCount !=
                                          candidate.Simulation.WorkerThreadCount,
                                      "simulation.worker_thread_count");
            RecordBootOnlyDifference(fields,
                                      current.ReferenceScene.Enabled !=
                                          candidate.ReferenceScene.Enabled,
                                      "reference_scene.enabled");
            RecordBootOnlyDifference(fields,
                                      current.ReferenceScene.Selector !=
                                          candidate.ReferenceScene.Selector,
                                      "reference_scene.selector");
            RecordBootOnlyDifference(fields,
                                      current.Camera.Enabled != candidate.Camera.Enabled,
                                      "camera.enabled");
            RecordBootOnlyDifference(fields,
                                      current.Camera.Controller !=
                                          candidate.Camera.Controller,
                                      "camera.controller");
            return fields;
        }
    }

    EngineConfigControl::EngineConfigControl(
        EngineConfigControlDependencies dependencies)
    {
        SetDependencies(std::move(dependencies));
    }

    void EngineConfigControl::SetDependencies(
        EngineConfigControlDependencies dependencies)
    {
        m_Dependencies = std::move(dependencies);
        if (m_Dependencies.Config)
        {
            m_ConfigControlState.ActiveConfig = *m_Dependencies.Config;
        }
    }

    const Core::Config::EngineConfig& EngineConfigControl::CurrentConfig()
        const noexcept
    {
        static const Core::Config::EngineConfig kFallbackConfig{};
        return m_Dependencies.Config ? *m_Dependencies.Config : kFallbackConfig;
    }

    Core::Config::EngineConfig* EngineConfigControl::MutableConfig() const noexcept
    {
        return m_Dependencies.Config;
    }

    void EngineConfigControl::RecordConfigApply(
        RuntimeEngineConfigApplyResult result)
    {
        m_ConfigControlState.LastApply = std::move(result);
        m_ConfigControlState.HasLastApply = true;
    }

    Graphics::RenderRecipeConfigContext
    EngineConfigControl::CreateRenderRecipeConfigContext() const
    {
        const Core::Config::EngineConfig& config = CurrentConfig();
        Core::Extent2D viewport{
            .Width = std::max(config.Window.Width, 1),
            .Height = std::max(config.Window.Height, 1),
        };
        if (m_Dependencies.Window)
        {
            const Platform::Extent2D extent =
                m_Dependencies.Window->GetFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                viewport = Core::Extent2D{
                    .Width = extent.Width,
                    .Height = extent.Height,
                };
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
    EngineConfigControl::PreviewRenderRecipeConfigDocument(
        const std::string_view document,
        std::string sourceId) const
    {
        return Graphics::PreviewRenderRecipeConfig(
            document,
            CreateRenderRecipeConfigContext(),
            Graphics::RenderRecipeConfigParseOptions{
                .SourceId = std::move(sourceId),
            });
    }

    Graphics::RenderRecipeConfigLoadResult
    EngineConfigControl::LoadRenderRecipeConfigPreviewFile(std::string path) const
    {
        const std::string sourceId = path;
        return Graphics::LoadRenderRecipeConfigFile(
            path,
            CreateRenderRecipeConfigContext(),
            Graphics::RenderRecipeConfigParseOptions{
                .SourceId = sourceId,
            });
    }

    RuntimeRenderRecipeApplyResult
    EngineConfigControl::ActivateRenderRecipeConfigDocument(
        const std::string_view document,
        std::string sourceId,
        const RuntimeRenderRecipeActivationSource source)
    {
        const Graphics::RenderRecipeConfigLoadResult loadResult =
            PreviewRenderRecipeConfigDocument(document, std::move(sourceId));
        return ApplyRenderRecipeConfigPreview(loadResult, source);
    }

    RuntimeRenderRecipeApplyResult
    EngineConfigControl::ApplyRenderRecipeConfigPreview(
        const Graphics::RenderRecipeConfigLoadResult& loadResult,
        const RuntimeRenderRecipeActivationSource source)
    {
        RuntimeRenderRecipeApplyResult result{
            .Source = source,
            .LoadResult = loadResult,
        };

        if (!m_Dependencies.Renderer)
        {
            result.Status = RuntimeRenderRecipeApplyStatus::MissingRenderer;
            m_RenderRecipeState.LastApply = result;
            m_RenderRecipeState.HasLastApply = true;
            return result;
        }

        if (!Graphics::IsConfigUsable(loadResult))
        {
            m_Dependencies.Renderer->ClearActiveFrameRecipeOverride();
            m_RenderRecipeState.ActiveOverride.reset();
            m_RenderRecipeState.ActiveConfig = Graphics::RenderRecipeConfigLoadResult{};
            m_RenderRecipeState.HasActiveConfig = false;
            m_RenderRecipeState.ActiveSource =
                RuntimeRenderRecipeActivationSource::None;
            result.Status = RuntimeRenderRecipeApplyStatus::Rejected;
            m_RenderRecipeState.LastApply = result;
            m_RenderRecipeState.HasLastApply = true;
            return result;
        }

        Graphics::FrameRecipeOverride recipeOverride{
            .Recipe = loadResult.Preview.Recipe,
            .DisabledExtensionSlots = loadResult.Preview.DisabledExtensionSlots,
            .SourceId = loadResult.SourceId,
        };
        m_Dependencies.Renderer->SetActiveFrameRecipeOverride(
            std::make_optional(recipeOverride));
        m_RenderRecipeState.ActiveOverride = recipeOverride;
        m_RenderRecipeState.ActiveConfig = loadResult;
        m_RenderRecipeState.HasActiveConfig = true;
        m_RenderRecipeState.ActiveSource = source;
        result.Status = RuntimeRenderRecipeApplyStatus::Applied;
        result.RendererOverrideInstalled = true;
        m_RenderRecipeState.LastApply = result;
        m_RenderRecipeState.HasLastApply = true;
        return result;
    }

    RuntimeRenderRecipeApplyResult
    EngineConfigControl::LoadAndApplyRenderRecipeConfigFile(
        std::string path,
        const RuntimeRenderRecipeActivationSource source)
    {
        const std::string sourceId = path;
        const Graphics::RenderRecipeConfigLoadResult loadResult =
            Graphics::LoadRenderRecipeConfigFile(
                path,
                CreateRenderRecipeConfigContext(),
                Graphics::RenderRecipeConfigParseOptions{
                    .SourceId = sourceId,
                });
        return ApplyRenderRecipeConfigPreview(loadResult, source);
    }

    void EngineConfigControl::ClearActiveRenderRecipeOverride() noexcept
    {
        if (m_Dependencies.Renderer)
        {
            m_Dependencies.Renderer->ClearActiveFrameRecipeOverride();
        }
        m_RenderRecipeState.ActiveOverride.reset();
        m_RenderRecipeState.ActiveConfig = Graphics::RenderRecipeConfigLoadResult{};
        m_RenderRecipeState.HasActiveConfig = false;
        m_RenderRecipeState.ActiveSource = RuntimeRenderRecipeActivationSource::None;
    }

    const RuntimeRenderRecipeState& EngineConfigControl::GetRenderRecipeState()
        const noexcept
    {
        return m_RenderRecipeState;
    }

    Core::Config::EngineConfigLoadResult
    EngineConfigControl::PreviewEngineConfigControlDocument(
        const std::string_view document,
        std::string sourceId) const
    {
        return Core::Config::PreviewEngineConfig(
            document,
            CurrentConfig(),
            Core::Config::EngineConfigParseOptions{
                .SourceId = std::move(sourceId),
            });
    }

    Core::Config::EngineConfigLoadResult
    EngineConfigControl::LoadEngineConfigControlFile(std::string path) const
    {
        const std::string sourceId = path;
        return Core::Config::LoadEngineConfigFile(
            path,
            CurrentConfig(),
            Core::Config::EngineConfigParseOptions{
                .SourceId = sourceId,
            });
    }

    RuntimeEngineConfigApplyResult
    EngineConfigControl::ApplyEngineConfigHotSubset(
        const Core::Config::EngineConfigLoadResult& loadResult,
        const RuntimeConfigControlSource source)
    {
        RuntimeEngineConfigApplyResult result{
            .Source = source,
            .LoadResult = loadResult,
        };

        Core::Config::EngineConfig* config = MutableConfig();
        if (config == nullptr || !Core::Config::IsConfigUsable(loadResult))
        {
            result.Status = RuntimeEngineConfigApplyStatus::Rejected;
            RecordConfigApply(result);
            return result;
        }

        const Core::Config::EngineConfig& candidate = loadResult.Preview.Config;
        result.RejectedBootOnlyFields =
            FindBootOnlyEngineConfigDifferences(*config, candidate);
        if (!result.RejectedBootOnlyFields.empty())
        {
            result.Status = RuntimeEngineConfigApplyStatus::Rejected;
            RecordConfigApply(result);
            return result;
        }

        const bool recipePathChanged =
            config->Render.DefaultRecipeConfigPath !=
            candidate.Render.DefaultRecipeConfigPath;
        const bool progressivePoissonChanged =
            !ProgressivePoissonPlaygroundConfigEquals(
                config->Sandbox.ProgressivePoisson,
                candidate.Sandbox.ProgressivePoisson);
        const bool parameterizationChanged = !ParameterizationConfigEquals(
            config->Sandbox.Parameterization,
            candidate.Sandbox.Parameterization);
        result.DefaultRecipeConfigPathChanged = recipePathChanged;
        result.SandboxProgressivePoissonChanged = progressivePoissonChanged;
        result.SandboxParameterizationChanged = parameterizationChanged;
        if (!recipePathChanged && !progressivePoissonChanged &&
            !parameterizationChanged)
        {
            result.Status = RuntimeEngineConfigApplyStatus::NoChange;
            m_ConfigControlState.ActiveConfig = *config;
            RecordConfigApply(result);
            return result;
        }

        if (recipePathChanged && !candidate.Render.DefaultRecipeConfigPath.empty())
        {
            const Graphics::RenderRecipeConfigLoadResult recipeLoadResult =
                LoadRenderRecipeConfigPreviewFile(
                    candidate.Render.DefaultRecipeConfigPath);
            result.RecipeApply = RuntimeRenderRecipeApplyResult{
                .Source = ToRecipeActivationSource(source),
                .LoadResult = recipeLoadResult,
            };
            if (!Graphics::IsConfigUsable(recipeLoadResult))
            {
                result.RecipeApply.Status =
                    RuntimeRenderRecipeApplyStatus::Rejected;
                result.Status = RuntimeEngineConfigApplyStatus::Rejected;
                RecordConfigApply(result);
                return result;
            }

            result.RecipeApply = ApplyRenderRecipeConfigPreview(
                recipeLoadResult,
                ToRecipeActivationSource(source));
            if (!result.RecipeApply.Succeeded())
            {
                result.Status = RuntimeEngineConfigApplyStatus::Rejected;
                RecordConfigApply(result);
                return result;
            }
        }

        if (recipePathChanged)
        {
            config->Render.DefaultRecipeConfigPath =
                candidate.Render.DefaultRecipeConfigPath;
            if (candidate.Render.DefaultRecipeConfigPath.empty())
            {
                ClearActiveRenderRecipeOverride();
            }
        }
        if (progressivePoissonChanged)
        {
            config->Sandbox.ProgressivePoisson =
                candidate.Sandbox.ProgressivePoisson;
        }
        if (parameterizationChanged)
        {
            config->Sandbox.Parameterization = candidate.Sandbox.Parameterization;
        }
        m_ConfigControlState.ActiveConfig = *config;
        result.Status = RuntimeEngineConfigApplyStatus::Applied;
        result.EngineConfigApplied = true;
        RecordConfigApply(result);
        return result;
    }

    RuntimeEngineConfigApplyResult
    EngineConfigControl::LoadAndApplyEngineConfigHotSubsetFile(
        std::string path,
        const RuntimeConfigControlSource source)
    {
        const Core::Config::EngineConfigLoadResult loadResult =
            LoadEngineConfigControlFile(std::move(path));
        return ApplyEngineConfigHotSubset(loadResult, source);
    }

    const RuntimeEngineConfigControlState&
    EngineConfigControl::GetEngineConfigControlState() const noexcept
    {
        return m_ConfigControlState;
    }
}
