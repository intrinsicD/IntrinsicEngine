module;

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

module Extrinsic.Runtime.ReferenceScene;

import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;

namespace Extrinsic::Runtime
{
    namespace
    {
        // GRAPHICS-029 Decision 4 camera defaults — see task body for
        // rationale. The seed leaves View/Projection at identity; the
        // viewport-aware finalisation happens in
        // BuildReferenceCameraViewInput at frame build time so the aspect
        // ratio follows the current framebuffer extent.
        constexpr glm::vec3 kReferenceCameraPosition{0.0f, 0.0f, 3.0f};
        constexpr glm::vec3 kReferenceCameraForward{0.0f, 0.0f, -1.0f};
        constexpr glm::vec3 kReferenceCameraUp{0.0f, 1.0f, 0.0f};
        constexpr float     kReferenceCameraNear = 0.1f;
        constexpr float     kReferenceCameraFar  = 100.0f;
        constexpr float     kReferenceCameraFovY = glm::radians(45.0f);

        [[nodiscard]] Graphics::CameraViewInput MakeReferenceCameraSeed() noexcept
        {
            Graphics::CameraViewInput seed{};
            seed.Position  = kReferenceCameraPosition;
            seed.Forward   = kReferenceCameraForward;
            seed.Up        = kReferenceCameraUp;
            seed.NearPlane = kReferenceCameraNear;
            seed.FarPlane  = kReferenceCameraFar;
            seed.Valid     = true;
            return seed;
        }
    }

    ReferenceScenePopulation TriangleProvider::Populate(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "ReferenceTriangle");

        auto& raw = scene.Raw();
        raw.emplace<Graphics::Components::RenderSurface>(entity,
            Graphics::Components::RenderSurface{
                .Domain = Graphics::Components::RenderSurface::SourceDomain::Vertex,
            });
        raw.emplace<ECS::Components::ProceduralGeometryRef>(entity,
            ECS::Components::ProceduralGeometryRef{
                .Kind = ECS::Components::ProceduralGeometryKind::Triangle,
            });

        ReferenceScenePopulation population;
        population.Entities.push_back(ReferenceSceneEntity{entity});
        population.Camera = MakeReferenceCameraSeed();
        return population;
    }

    void TriangleProvider::Teardown(ECS::Scene::Registry& scene,
                                    const std::vector<ReferenceSceneEntity>& entities)
    {
        for (const auto& owned : entities)
        {
            if (scene.IsValid(owned.Entity))
                scene.Destroy(owned.Entity);
        }
    }

    ReferenceSceneRegistry::ReferenceSceneRegistry() = default;

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
        // GRAPHICS-029B (per GRAPHICS-029A Transition notice): resolve must
        // terminate on unregistered selectors. Engine::Initialize() funnels
        // every enabled selector through RegisterDefaultReferenceProvidersIfAbsent
        // first so legitimate production paths cannot reach this branch.
        std::terminate();
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
        ReferenceSceneRegistry registry;
        RegisterDefaultReferenceProvidersIfAbsent(registry);
        return registry;
    }

    void RegisterDefaultReferenceProvidersIfAbsent(ReferenceSceneRegistry& registry)
    {
        if (registry.ResolveOrNull(Core::Config::ReferenceSceneSelector::Triangle) == nullptr)
        {
            registry.Register(Core::Config::ReferenceSceneSelector::Triangle,
                              std::make_unique<TriangleProvider>());
        }
    }

    Graphics::CameraViewInput BuildReferenceCameraViewInput(
        const Graphics::CameraViewInput& seed,
        int viewportWidth,
        int viewportHeight) noexcept
    {
        Graphics::CameraViewInput finalized = seed;

        const float width  = static_cast<float>(viewportWidth > 0 ? viewportWidth : 1);
        const float height = static_cast<float>(viewportHeight > 0 ? viewportHeight : 1);
        const float aspect = width / height;

        const glm::vec3 target = seed.Position + seed.Forward;
        finalized.View       = glm::lookAt(seed.Position, target, seed.Up);
        finalized.Projection = glm::perspective(kReferenceCameraFovY,
                                                aspect,
                                                seed.NearPlane,
                                                seed.FarPlane);
        // Vulkan clip-space Y is inverted relative to glm::perspective's
        // OpenGL-oriented output; the promoted renderer's camera UBO consumes
        // Projection directly, so the Y row must be flipped here for parity
        // with the legacy CameraComponent path
        // (src/legacy/Graphics/Graphics.Camera.cpp:34-39). Without this, the
        // reference triangle renders vertically inverted and any screen-space
        // derivations from the resulting CameraViewSnapshot use the wrong Y
        // convention.
        finalized.Projection[1][1] *= -1.0f;
        return finalized;
    }
}
