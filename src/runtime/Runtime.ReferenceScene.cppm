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
// GRAPHICS-029A/B — Reference scene seam.
//
// Provider interface + registry that Runtime::Engine invokes once
// (when EngineConfig::ReferenceScene::Enabled is true) to populate a
// deterministic, opt-in renderable.
//
// GRAPHICS-029A landed the skeleton and config plumbing; GRAPHICS-029B
// adds the concrete TriangleProvider, registers it for the Triangle
// selector in MakeDefaultReferenceSceneRegistry(), and tightens the
// resolve path: unregistered selectors now std::terminate (matching
// GRAPHICS-029 Decision 7's double-install guard policy applied to
// the resolve path). The previous no-op fallback is retired.
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

    // Concrete provider that bootstraps a single "ReferenceTriangle" entity:
    //   - MetaData{EntityName = "ReferenceTriangle"}
    //   - Transform::Component + Transform::WorldMatrix (identity TRS)
    //   - Hierarchy::Component (no parent / no children)
    //   - Graphics::Components::RenderSurface{Domain = Vertex} so
    //     Runtime::RenderExtractionCache observes it as a renderable
    //     candidate and the renderer allocates a GPU instance.
    //   - ECS::Components::ProceduralGeometryRef{Kind = Triangle} so
    //     GRAPHICS-030B can later wire procedural geometry residency.
    // Populate also returns a CameraViewInput seed (Position, Forward, Up,
    // NearPlane, FarPlane, Valid=true) consumed by
    // BuildReferenceCameraViewInput() to derive the runtime-aware
    // View/Projection at frame build time.
    export class TriangleProvider final : public IReferenceSceneProvider
    {
    public:
        [[nodiscard]] ReferenceScenePopulation Populate(ECS::Scene::Registry& scene) override;
        void Teardown(ECS::Scene::Registry& scene,
                      const std::vector<ReferenceSceneEntity>& entities) override;
    };

    // Map of ReferenceSceneSelector → provider. Engine::Initialize() looks
    // up the configured selector after RegisterDefaultReferenceProvidersIfAbsent
    // has filled any unregistered defaults. After GRAPHICS-029B the resolve
    // path is strict: Resolve(selector_with_no_provider) terminates.
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

        // Look up a provider. Selectors with no registered provider fire
        // std::terminate (GRAPHICS-029 Decision 7 applied to resolve).
        [[nodiscard]] IReferenceSceneProvider& Resolve(
            Core::Config::ReferenceSceneSelector selector);

        // Look up a provider without terminating. Returns nullptr when
        // `selector` has no explicit registration. Used by
        // RegisterDefaultReferenceProvidersIfAbsent and shutdown teardown.
        [[nodiscard]] IReferenceSceneProvider* ResolveOrNull(
            Core::Config::ReferenceSceneSelector selector) noexcept;

    private:
        struct Slot
        {
            Core::Config::ReferenceSceneSelector Selector{};
            std::unique_ptr<IReferenceSceneProvider> Provider;
        };

        std::vector<Slot> m_Slots;
    };

    // Constructs a registry pre-populated with the production default
    // provider set (currently: TriangleProvider for Triangle). Engine
    // composes this via RegisterDefaultReferenceProvidersIfAbsent so test
    // pre-registration keeps working alongside the strict double-install
    // guard.
    export [[nodiscard]] ReferenceSceneRegistry MakeDefaultReferenceSceneRegistry();

    // Idempotently install the default provider for each selector that
    // currently has no explicit registration. Engine::Initialize() invokes
    // this after constructing the registry so the strict double-install
    // guard does not collide with test-side Register() calls.
    export void RegisterDefaultReferenceProvidersIfAbsent(ReferenceSceneRegistry& registry);

    // Finalise a runtime-aware CameraViewInput from a provider seed and the
    // current viewport (width, height pair to keep this module free of any
    // Extrinsic.Platform.* import per the GRAPHICS-029B Forbidden rule).
    // Computes View via glm::lookAt(seed.Position, target, seed.Up) and
    // Projection via glm::perspective(45° fovY, aspect, near, far) where
    // target = seed.Position + seed.Forward and aspect = width/height.
    // Mirrors the GRAPHICS-002 sanitizer expectation so
    // Graphics::BuildCameraViewSnapshot reports `Valid = true` for the
    // result. RUNTIME-081 will retire the renderer/runtime call site that
    // routes this output directly into RenderFrameInput::Camera once camera
    // controllers consume the seed as initial state.
    export [[nodiscard]] Graphics::CameraViewInput BuildReferenceCameraViewInput(
        const Graphics::CameraViewInput& seed,
        int viewportWidth,
        int viewportHeight) noexcept;
}
