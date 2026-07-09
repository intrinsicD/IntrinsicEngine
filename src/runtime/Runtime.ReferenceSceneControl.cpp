module;

#include <exception>
#include <optional>

module Extrinsic.Runtime.ReferenceSceneControl;

namespace Extrinsic::Runtime
{
    ReferenceSceneRegistry& ReferenceSceneControl::Registry() noexcept
    {
        return m_Registry;
    }

    const ReferenceSceneRegistry& ReferenceSceneControl::Registry() const noexcept
    {
        return m_Registry;
    }

    bool ReferenceSceneControl::IsInstalled() const noexcept
    {
        return m_Installed;
    }

    const std::optional<Graphics::CameraViewInput>&
    ReferenceSceneControl::CameraSeed() const noexcept
    {
        return m_CameraSeed;
    }

    void ReferenceSceneControl::InstallIfEnabled(
        const Core::Config::ReferenceSceneConfig& config,
        ECS::Scene::Registry& scene)
    {
        if (!config.Enabled)
            return;

        if (m_Installed)
            std::terminate();

        RegisterDefaultReferenceProvidersIfAbsent(m_Registry);

        IReferenceSceneProvider& provider = m_Registry.Resolve(config.Selector);
        m_Population = provider.Populate(scene);
        m_CameraSeed = m_Population.Camera;
        m_Installed = true;
    }

    void ReferenceSceneControl::TeardownIfInstalled(
        const Core::Config::ReferenceSceneConfig& config,
        ECS::Scene::Registry* scene)
    {
        if (config.Enabled && m_Installed && scene != nullptr)
        {
            if (IReferenceSceneProvider* provider =
                    m_Registry.ResolveOrNull(config.Selector))
            {
                provider->Teardown(*scene, m_Population.Entities);
            }
            m_Population = ReferenceScenePopulation{};
            m_Installed = false;
        }
        m_CameraSeed.reset();
    }
}
