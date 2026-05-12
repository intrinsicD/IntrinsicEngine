module;

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.ReferenceScene;

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;

// ============================================================
// GRAPHICS-029A — Reference scene skeleton.
//
// Provider interface + registry that Runtime::Engine invokes once
// (when EngineConfig::ReferenceScene::Enabled is true) to populate a
// deterministic, opt-in renderable. No provider body lands in this
// slice; the default registry resolves every selector to a no-op
// provider so existing CPU/null tests observe zero renderable
// candidates.
//
// GRAPHICS-029B will register the concrete TriangleProvider and tighten
// the "unknown selector" semantics (terminate) per GRAPHICS-029
// Decision 7.
// ============================================================

namespace Extrinsic::Runtime
{
    // Owned-handle list value type returned by reference-scene providers.
    // Wraps a single ECS entity handle so future multi-entity providers can
    // attach per-entity metadata without changing the registry contract.
    export struct ReferenceSceneEntity
    {
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
    };

    export struct ReferenceScenePopulation
    {
        std::vector<ReferenceSceneEntity> Entities;
        std::optional<Graphics::CameraViewInput> Camera;
    };

    export class IReferenceSceneProvider
    {
    public:
        virtual ~IReferenceSceneProvider() = default;

        // Populate the given scene with the provider's renderable content.
        // Implementations return the owned-entity list so Engine::Shutdown
        // can route teardown through the same provider that authored the
        // entities (Decision 7).
        [[nodiscard]] virtual ReferenceScenePopulation Populate(ECS::Scene::Registry& scene) = 0;

        // Destroy the entities the provider created. Implementations must be
        // tolerant of entities already destroyed by Scene::Registry::Clear()
        // so shutdown ordering does not impose a strict policy here.
        virtual void Teardown(ECS::Scene::Registry& scene,
                              const std::vector<ReferenceSceneEntity>& entities) = 0;
    };

    // Map of ReferenceSceneSelector → provider. Engine::Initialize() looks
    // up the configured selector; selectors with no explicit registration
    // currently fall back to the no-op default provider exposed by
    // MakeDefaultReferenceSceneRegistry(). GRAPHICS-029B will tighten
    // unknown-selector handling to std::terminate once TriangleProvider is
    // registered for the Triangle selector.
    export class ReferenceSceneRegistry
    {
    public:
        ReferenceSceneRegistry();

        ReferenceSceneRegistry(const ReferenceSceneRegistry&)            = delete;
        ReferenceSceneRegistry& operator=(const ReferenceSceneRegistry&) = delete;
        ReferenceSceneRegistry(ReferenceSceneRegistry&&) noexcept;
        ReferenceSceneRegistry& operator=(ReferenceSceneRegistry&&) noexcept;
        ~ReferenceSceneRegistry();

        // Register a provider for `selector`. A second registration for the
        // same selector fires std::terminate per GRAPHICS-029 Decision 7
        // (no silent shadowing).
        void Register(Core::Config::ReferenceSceneSelector selector,
                      std::unique_ptr<IReferenceSceneProvider> provider);

        // Look up a provider. Returns the no-op default when no explicit
        // provider is registered for `selector`. Never returns nullptr.
        [[nodiscard]] IReferenceSceneProvider& Resolve(
            Core::Config::ReferenceSceneSelector selector);

        // Look up a provider without falling back to the no-op default.
        // Returns nullptr when `selector` has no explicit registration.
        [[nodiscard]] IReferenceSceneProvider* ResolveOrNull(
            Core::Config::ReferenceSceneSelector selector) noexcept;

    private:
        struct Slot
        {
            Core::Config::ReferenceSceneSelector Selector{};
            std::unique_ptr<IReferenceSceneProvider> Provider;
        };

        std::vector<Slot> m_Slots;
        std::unique_ptr<IReferenceSceneProvider> m_Default;
    };

    // Constructs a registry whose unknown-selector resolutions return a
    // no-op provider (empty population, no entities to tear down). The
    // registry is moved into Engine::Initialize() and replaced wholesale by
    // GRAPHICS-029B once TriangleProvider is registered.
    export [[nodiscard]] ReferenceSceneRegistry MakeDefaultReferenceSceneRegistry();
}
