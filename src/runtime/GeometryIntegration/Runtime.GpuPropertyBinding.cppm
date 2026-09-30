// Binds live ECS geometry properties to the ECS-blind GPU property residency (ADR 0030): the
// key and typed layout of an entity's property, its canonical input slot at the current CPU
// revision, a write slot of its output ring, and the ring front the renderer observes. Free
// functions; SpatialIndexCache owns the residency and the per-run state stays in the caller's
// job.
module;
#include <cstdint>
#include <optional>
#include <entt/entity/entity.hpp>
export module Extrinsic.Runtime.GpuPropertyBinding;
export import Extrinsic.Graphics.GpuPropertyResidency;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.WorldHandle;

extern "C++" { namespace Extrinsic::Runtime { class WorldRegistry; } }
extern "C++" { namespace Extrinsic::ECS::Scene { class Registry; } }

export namespace Extrinsic::Runtime
{
    [[nodiscard]] Graphics::GpuPropertyKey MakeGpuPropertyKey(WorldHandle world, entt::entity entity,
                                                              const GeometryPropertyRef& ref);
    // The property's own scalar type and channels, tightly packed over `count` rows. Bool and
    // Unknown have no GPU layout.
    [[nodiscard]] std::optional<Graphics::GpuPropertyLayout> MakeGpuPropertyLayout(
        Geometry::PropertyValueKind kind, std::uint32_t count);
    // The canonical slot of the entity's property at its current CPU revision: one upload per
    // revision, shared by every GPU user. Empty when the property does not resolve on a live
    // entity, has no GPU layout, or the device refuses. Device-owner thread only.
    [[nodiscard]] std::optional<Graphics::GpuPropertyView> ResolveGpuPropertyInput(
        Graphics::GpuPropertyResidency& residency, WorldRegistry& worlds, WorldHandle world, entt::entity entity,
        const GeometryPropertyRef& ref);
    [[nodiscard]] std::optional<Graphics::GpuPropertyView> ResolveGpuPropertyInput(
        Graphics::GpuPropertyResidency& residency, ECS::Scene::Registry& scene, WorldHandle world, entt::entity entity,
        const GeometryPropertyRef& ref);
    // The float ring a scalar property shows while a GPU method writes it: a float property's
    // own ring, or the float presentation ring a double property's method publishes beside its
    // typed ring (the renderer reads float scalars). The observation keys the property by name
    // and domain with ValueKind Float, which is why a presentation ring uses that key.
    [[nodiscard]] GeometryPropertyRef GpuPropertyPresentationRef(const GeometryPropertyRef& ref);
    struct GpuPropertyObservation
    {
        std::uint64_t Address{};
        std::uint32_t Count{};
        std::uint64_t Stamp{}; // changes with the observed slot
    };
    // The ring front of the entity's scalar property when a method is writing it and the front
    // covers every property row (ADR 0030 decision 5); marks it observed this frame. Empty
    // otherwise: the renderer then uploads the CPU property as usual. Render thread only.
    [[nodiscard]] std::optional<GpuPropertyObservation> ObserveGpuPropertyFront(
        Graphics::GpuPropertyResidency& residency, ECS::Scene::Registry& scene, WorldHandle world, entt::entity entity,
        const GeometryPropertyRef& ref);
    // A write slot of the property's output ring, in the property's own layout over `count`
    // rows with `depth` slots (ADR 0030 decision 4). Empty when the ring is exhausted (a
    // dropped preview) or the device refuses.
    [[nodiscard]] std::optional<Graphics::GpuPropertyView> AcquireGpuPropertyOutput(
        Graphics::GpuPropertyResidency& residency, WorldHandle world, entt::entity entity,
        const GeometryPropertyRef& ref, std::uint32_t count, std::uint32_t depth);
}
