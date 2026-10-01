module;
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Scene.Registry;

namespace Extrinsic::Runtime
{
    Graphics::GpuPropertyKey MakeGpuPropertyKey(const WorldHandle world, const entt::entity entity,
                                                const GeometryPropertyRef& ref)
    {
        return {.Scope = (std::uint64_t(world.Generation) << 32u) | world.Index,
                .Owner = std::uint64_t(entt::to_integral(entity)),
                .Domain = std::uint32_t(ref.Domain),
                .ValueKind = std::uint32_t(ref.ValueKind),
                .Name = ref.Name};
    }

    std::optional<Graphics::GpuPropertyLayout> MakeGpuPropertyLayout(const Geometry::PropertyValueKind kind,
                                                                     const std::uint32_t count)
    {
        using Geometry::PropertyValueKind;
        using Graphics::GpuScalarType;
        Graphics::GpuPropertyLayout layout{.Count = count};
        switch (kind)
        {
        case PropertyValueKind::Int32: layout.Scalar = GpuScalarType::Int32; break;
        case PropertyValueKind::UInt32: layout.Scalar = GpuScalarType::UInt32; break;
        case PropertyValueKind::UInt64: layout.Scalar = GpuScalarType::UInt64; break;
        case PropertyValueKind::Float: layout.Scalar = GpuScalarType::Float32; break;
        case PropertyValueKind::Double: layout.Scalar = GpuScalarType::Float64; break;
        case PropertyValueKind::Vec2: layout.Channels = 2u; break;
        case PropertyValueKind::Vec3: layout.Channels = 3u; break;
        case PropertyValueKind::Vec4: layout.Channels = 4u; break;
        case PropertyValueKind::Bool:
        case PropertyValueKind::Unknown: return std::nullopt;
        }
        if (!layout.Valid()) return std::nullopt;
        return layout;
    }

    namespace
    {
        template <typename T>
        std::optional<Graphics::GpuPropertyView> Acquire(Graphics::GpuPropertyResidency& residency,
                                                         const Graphics::GpuPropertyKey& key,
                                                         const Geometry::PropertySet& properties,
                                                         const GeometryPropertyRef& ref, const std::uint32_t count)
        {
            const auto property = properties.Get<T>(ref.Name);
            if (!property || property.Size() != count) return std::nullopt;
            const auto layout = MakeGpuPropertyLayout(ref.ValueKind, count);
            if (!layout || layout->Bytes() != std::uint64_t(sizeof(T)) * count) return std::nullopt;
            return residency.AcquireInput(key, property.Revision(), *layout, [&](std::span<std::byte> out) {
                std::memcpy(out.data(), property.Span().data(), out.size());
            });
        }
    }

    std::optional<Graphics::GpuPropertyView> ResolveGpuPropertyInput(
        Graphics::GpuPropertyResidency& residency, WorldRegistry& worlds, const WorldHandle world,
        const entt::entity entity, const GeometryPropertyRef& ref)
    {
        auto* scene = worlds.Get(world);
        if (!scene) return std::nullopt;
        return ResolveGpuPropertyInput(residency, *scene, world, entity, ref);
    }

    std::optional<Graphics::GpuPropertyView> ResolveGpuPropertyInput(
        Graphics::GpuPropertyResidency& residency, ECS::Scene::Registry& scene, const WorldHandle world,
        const entt::entity entity, const GeometryPropertyRef& ref)
    {
        if (!scene.IsValid(entity)) return std::nullopt;
        const auto available = BuildGeometryAvailability(scene.Raw(), entity);
        const auto* properties = ResolveGeometryPropertySet(available, ref.Domain);
        if (!properties || properties->Size() == 0u || properties->Size() > 0xffffffffu) return std::nullopt;
        const auto resolution = ResolveGeometryProperty(available, ref, properties->Size(), false);
        if (!resolution.Resolved() || resolution.ResolvedValueKind != ref.ValueKind) return std::nullopt;
        const auto key = MakeGpuPropertyKey(world, entity, ref);
        const auto count = std::uint32_t(properties->Size());
        using Geometry::PropertyValueKind;
        switch (ref.ValueKind)
        {
        case PropertyValueKind::Int32: return Acquire<std::int32_t>(residency, key, *properties, ref, count);
        case PropertyValueKind::UInt32: return Acquire<std::uint32_t>(residency, key, *properties, ref, count);
        case PropertyValueKind::UInt64: return Acquire<std::uint64_t>(residency, key, *properties, ref, count);
        case PropertyValueKind::Float: return Acquire<float>(residency, key, *properties, ref, count);
        case PropertyValueKind::Double: return Acquire<double>(residency, key, *properties, ref, count);
        case PropertyValueKind::Vec2: return Acquire<glm::vec2>(residency, key, *properties, ref, count);
        case PropertyValueKind::Vec3: return Acquire<glm::vec3>(residency, key, *properties, ref, count);
        case PropertyValueKind::Vec4: return Acquire<glm::vec4>(residency, key, *properties, ref, count);
        case PropertyValueKind::Bool:
        case PropertyValueKind::Unknown: break;
        }
        return std::nullopt;
    }

    GeometryPropertyRef GpuPropertyPresentationRef(const GeometryPropertyRef& ref)
    {
        return {.Domain = ref.Domain, .Name = ref.Name, .ValueKind = Geometry::PropertyValueKind::Float};
    }

    std::optional<GpuPropertyObservation> ObserveGpuPropertyFront(
        Graphics::GpuPropertyResidency& residency, ECS::Scene::Registry& scene, const WorldHandle world,
        const entt::entity entity, const GeometryPropertyRef& ref)
    {
        const bool positions = ref.ValueKind == Geometry::PropertyValueKind::Vec3;
        const auto key = MakeGpuPropertyKey(world, entity, positions ? ref : GpuPropertyPresentationRef(ref));
        if (!residency.HasRing(key) || !scene.IsValid(entity)) return std::nullopt;
        const auto front = residency.Front(key);
        const std::uint32_t channels = positions ? 3u : 1u;
        if (!front || front->Layout.Scalar != Graphics::GpuScalarType::Float32 || front->Layout.Channels != channels ||
            front->Layout.Stride != 0u)
            return std::nullopt;
        const auto* properties = ResolveGeometryPropertySet(BuildGeometryAvailability(scene.Raw(), entity), ref.Domain);
        if (!properties || properties->Size() != front->Layout.Count) return std::nullopt;
        residency.MarkObserved(key);
        return GpuPropertyObservation{.Buffer = front->Buffer, .Address = front->Address, .Bytes = front->Bytes,
                                      .Count = front->Layout.Count, .Stamp = GpuPropertyObservationStamp(*front),
                                      .ScalarRange = front->ScalarRange};
    }

    std::uint64_t GpuPropertyObservationStamp(const Graphics::GpuPropertyView& front) noexcept
    {
        return front.Publication;
    }

    std::optional<Graphics::GpuPropertyView> AcquireGpuPropertyOutput(
        Graphics::GpuPropertyResidency& residency, const WorldHandle world, const entt::entity entity,
        const GeometryPropertyRef& ref, const std::uint32_t count, const std::uint32_t depth)
    {
        const auto layout = MakeGpuPropertyLayout(ref.ValueKind, count);
        if (!layout) return std::nullopt;
        return residency.AcquireBack(MakeGpuPropertyKey(world, entity, ref), *layout, depth);
    }
}
