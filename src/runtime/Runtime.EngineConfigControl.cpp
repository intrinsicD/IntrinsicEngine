module;

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.EngineConfigControl;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderRecipeActivation;
import Extrinsic.Runtime.ServiceRegistry;

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

        [[nodiscard]] std::vector<std::string> FindChangedSectionNames(
            const std::vector<Core::Config::EngineConfigSection>& current,
            const std::vector<Core::Config::EngineConfigSection>& candidate)
        {
            std::vector<std::string> names{};
            names.reserve(current.size() + candidate.size());
            for (const Core::Config::EngineConfigSection& section : current)
            {
                names.push_back(section.Name);
            }
            for (const Core::Config::EngineConfigSection& section : candidate)
            {
                names.push_back(section.Name);
            }
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());

            std::vector<std::string> changed{};
            changed.reserve(names.size());
            for (const std::string& name : names)
            {
                const Core::Config::EngineConfigSection* previous =
                    Core::Config::FindEngineConfigSection(current, name);
                const Core::Config::EngineConfigSection* next =
                    Core::Config::FindEngineConfigSection(candidate, name);
                if (previous == nullptr || next == nullptr || *previous != *next)
                {
                    changed.push_back(name);
                }
            }
            return changed;
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

    bool RuntimeEngineConfigApplyResult::SectionChanged(
        const std::string_view name) const noexcept
    {
        return std::find(
                   ChangedSectionNames.begin(),
                   ChangedSectionNames.end(),
                   name) != ChangedSectionNames.end();
    }

    EngineConfigControl::EngineConfigControl(
        RuntimeEngineConfigSectionRegistry sectionRegistry)
        : m_SectionRegistry(std::move(sectionRegistry))
    {
    }

    RuntimeEngineConfigSectionRegistry&
    EngineConfigControl::SectionRegistry() noexcept
    {
        return m_SectionRegistry;
    }

    const RuntimeEngineConfigSectionRegistry&
    EngineConfigControl::SectionRegistry() const noexcept
    {
        return m_SectionRegistry;
    }

    std::string_view EngineConfigControl::Name() const noexcept
    {
        return "Runtime.EngineConfigControl";
    }

    Core::Result EngineConfigControl::Bind(
        const RuntimeRenderRecipeActivationKernel& kernel)
    {
        if (kernel.ActiveConfig == nullptr || kernel.State == nullptr ||
            !kernel.SetFrameRecipeOverride)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        m_RenderRecipeState = *kernel.State;
        m_RecipeActivation = kernel;
        m_RecipeActivation.State = &m_RenderRecipeState;
        Core::Config::PopulateEngineConfigSectionDefaults(
            *m_RecipeActivation.ActiveConfig,
            m_SectionRegistry);
        m_ConfigControlState = RuntimeEngineConfigControlState{
            .ActiveConfig = *m_RecipeActivation.ActiveConfig,
        };
        return Core::Ok();
    }

    void EngineConfigControl::ClearBinding() noexcept
    {
        m_RecipeActivation = {};
        m_RenderRecipeState = {};
        m_ConfigControlState = {};
    }

    Core::Result EngineConfigControl::OnRegister(EngineSetup& setup)
    {
        if (m_Published ||
            setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<EngineConfigControl>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        if (Core::Result bound = Bind(setup.RenderRecipeActivation());
            !bound.has_value())
        {
            ClearBinding();
            return bound;
        }
        if (Core::Result provided =
                setup.Services().Provide<EngineConfigControl>(
                    *this,
                    Name());
            !provided.has_value())
        {
            ClearBinding();
            return provided;
        }
        m_Published = true;
        return Core::Ok();
    }

    Core::Result EngineConfigControl::OnResolve(EngineSetup& setup)
    {
        if (!m_Published ||
            setup.Services().Find<EngineConfigControl>() != this)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return Core::Ok();
    }

    void EngineConfigControl::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        ResetRuntimeRenderRecipeActivation(m_RecipeActivation);
        if (m_Published)
        {
            (void)context.Services.Withdraw<EngineConfigControl>(*this);
        }
        m_Published = false;
        ClearBinding();
    }

    const Core::Config::EngineConfig& EngineConfigControl::CurrentConfig()
        const noexcept
    {
        static const Core::Config::EngineConfig kFallbackConfig{};
        return m_RecipeActivation.ActiveConfig
            ? *m_RecipeActivation.ActiveConfig
            : kFallbackConfig;
    }

    Core::Config::EngineConfig* EngineConfigControl::MutableConfig() const noexcept
    {
        return m_RecipeActivation.ActiveConfig;
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
        return CreateRuntimeRenderRecipeConfigContext(
            m_RecipeActivation);
    }

    Graphics::RenderRecipeConfigLoadResult
    EngineConfigControl::PreviewRenderRecipeConfigDocument(
        const std::string_view document,
        std::string sourceId) const
    {
        return PreviewRuntimeRenderRecipeConfigDocument(
            m_RecipeActivation,
            document,
            std::move(sourceId));
    }

    Graphics::RenderRecipeConfigLoadResult
    EngineConfigControl::LoadRenderRecipeConfigPreviewFile(std::string path) const
    {
        return LoadRuntimeRenderRecipeConfigPreviewFile(
            m_RecipeActivation,
            std::move(path));
    }

    RuntimeRenderRecipeApplyResult
    EngineConfigControl::ActivateRenderRecipeConfigDocument(
        const std::string_view document,
        std::string sourceId,
        const RuntimeRenderRecipeActivationSource source)
    {
        return ActivateRuntimeRenderRecipeConfigDocument(
            m_RecipeActivation,
            document,
            std::move(sourceId),
            source);
    }

    RuntimeRenderRecipeApplyResult
    EngineConfigControl::ApplyRenderRecipeConfigPreview(
        const Graphics::RenderRecipeConfigLoadResult& loadResult,
        const RuntimeRenderRecipeActivationSource source)
    {
        return ApplyRuntimeRenderRecipeConfigPreview(
            m_RecipeActivation,
            loadResult,
            source);
    }

    RuntimeRenderRecipeApplyResult
    EngineConfigControl::LoadAndApplyRenderRecipeConfigFile(
        std::string path,
        const RuntimeRenderRecipeActivationSource source)
    {
        return LoadAndApplyRuntimeRenderRecipeConfigFile(
            m_RecipeActivation,
            std::move(path),
            source);
    }

    void EngineConfigControl::ClearActiveRenderRecipeOverride() noexcept
    {
        ClearRuntimeRenderRecipeOverride(m_RecipeActivation);
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
                .SectionRegistry = &m_SectionRegistry,
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
                .SectionRegistry = &m_SectionRegistry,
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
        result.DefaultRecipeConfigPathChanged = recipePathChanged;
        result.ChangedSectionNames =
            FindChangedSectionNames(config->AppSections, candidate.AppSections);
        if (!recipePathChanged && result.ChangedSectionNames.empty())
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

        const std::vector<Core::Config::EngineConfigSection> previousSections =
            config->AppSections;
        if (recipePathChanged)
        {
            config->Render.DefaultRecipeConfigPath =
                candidate.Render.DefaultRecipeConfigPath;
            if (candidate.Render.DefaultRecipeConfigPath.empty())
            {
                ClearActiveRenderRecipeOverride();
            }
        }
        config->AppSections = candidate.AppSections;
        m_ConfigControlState.ActiveConfig = *config;
        result.Status = RuntimeEngineConfigApplyStatus::Applied;
        result.EngineConfigApplied = true;
        RecordConfigApply(result);

        for (const std::string& name : result.ChangedSectionNames)
        {
            const Core::Config::EngineConfigSectionRegistration* registration =
                m_SectionRegistry.Find(name);
            const Core::Config::EngineConfigSection* previous =
                Core::Config::FindEngineConfigSection(previousSections, name);
            const Core::Config::EngineConfigSection* current =
                Core::Config::FindEngineConfigSection(
                    config->AppSections,
                    name);
            if (registration && registration->OnChanged &&
                previous && current)
            {
                registration->OnChanged(*previous, *current);
            }
        }
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
