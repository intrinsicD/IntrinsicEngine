module;

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ReferenceScene;

import Extrinsic.ECS.Scene.Registry;

namespace Extrinsic::Runtime
{
    namespace
    {
        class NoopReferenceSceneProvider final : public IReferenceSceneProvider
        {
        public:
            ReferenceScenePopulation Populate(ECS::Scene::Registry& /*scene*/) override
            {
                return ReferenceScenePopulation{};
            }

            void Teardown(ECS::Scene::Registry& /*scene*/,
                          const std::vector<ReferenceSceneEntity>& /*entities*/) override
            {
                // No-op default: GRAPHICS-029B replaces this with TriangleProvider.
            }
        };
    }

    ReferenceSceneRegistry::ReferenceSceneRegistry()
        : m_Default(std::make_unique<NoopReferenceSceneProvider>())
    {
    }

    ReferenceSceneRegistry::~ReferenceSceneRegistry() = default;

    ReferenceSceneRegistry::ReferenceSceneRegistry(ReferenceSceneRegistry&&) noexcept = default;

    ReferenceSceneRegistry& ReferenceSceneRegistry::operator=(ReferenceSceneRegistry&&) noexcept = default;

    void ReferenceSceneRegistry::Register(Core::Config::ReferenceSceneSelector selector,
                                          std::unique_ptr<IReferenceSceneProvider> provider)
    {
        // GRAPHICS-029 Decision 7: double-install fires std::terminate so
        // silent shadowing never hides a provider mix-up.
        if (!provider)
            std::terminate();

        const auto existing = std::find_if(m_Slots.begin(), m_Slots.end(),
            [selector](const Slot& slot) { return slot.Selector == selector; });
        if (existing != m_Slots.end())
            std::terminate();

        m_Slots.push_back(Slot{selector, std::move(provider)});
    }

    IReferenceSceneProvider& ReferenceSceneRegistry::Resolve(
        Core::Config::ReferenceSceneSelector selector)
    {
        if (auto* provider = ResolveOrNull(selector))
            return *provider;
        return *m_Default;
    }

    IReferenceSceneProvider* ReferenceSceneRegistry::ResolveOrNull(
        Core::Config::ReferenceSceneSelector selector) noexcept
    {
        for (auto& slot : m_Slots)
        {
            if (slot.Selector == selector)
                return slot.Provider.get();
        }
        return nullptr;
    }

    ReferenceSceneRegistry MakeDefaultReferenceSceneRegistry()
    {
        return ReferenceSceneRegistry{};
    }
}
