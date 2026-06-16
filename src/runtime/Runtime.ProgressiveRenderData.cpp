module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.ProgressiveRenderData;

import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = Extrinsic::ECS::Components::GeometrySources;

        template <typename EnumT, std::size_t N>
        [[nodiscard]] std::string_view EnumToString(
            const EnumT value,
            const std::pair<EnumT, std::string_view> (&table)[N],
            const std::string_view fallback) noexcept
        {
            for (const auto& [candidate, name] : table)
            {
                if (candidate == value)
                    return name;
            }
            return fallback;
        }

        template <typename EnumT, std::size_t N>
        [[nodiscard]] bool TryEnumFromString(
            const std::string_view value,
            const std::pair<EnumT, std::string_view> (&table)[N],
            EnumT& out) noexcept
        {
            for (const auto& [candidate, name] : table)
            {
                if (name == value)
                {
                    out = candidate;
                    return true;
                }
            }
            return false;
        }

        constexpr std::pair<ProgressiveEntityShape, std::string_view> kEntityShapes[]{
            {ProgressiveEntityShape::Unknown, "Unknown"},
            {ProgressiveEntityShape::Composition, "Composition"},
            {ProgressiveEntityShape::MeshLeaf, "MeshLeaf"},
            {ProgressiveEntityShape::GraphLeaf, "GraphLeaf"},
            {ProgressiveEntityShape::PointCloudLeaf, "PointCloudLeaf"},
        };

        constexpr std::pair<ProgressiveGeometryDomain, std::string_view> kGeometryDomains[]{
            {ProgressiveGeometryDomain::Unknown, "Unknown"},
            {ProgressiveGeometryDomain::MeshVertex, "MeshVertex"},
            {ProgressiveGeometryDomain::MeshEdge, "MeshEdge"},
            {ProgressiveGeometryDomain::MeshHalfedge, "MeshHalfedge"},
            {ProgressiveGeometryDomain::MeshFace, "MeshFace"},
            {ProgressiveGeometryDomain::MeshSurface, "MeshSurface"},
            {ProgressiveGeometryDomain::GraphVertex, "GraphVertex"},
            {ProgressiveGeometryDomain::GraphEdge, "GraphEdge"},
            {ProgressiveGeometryDomain::Point, "Point"},
        };

        constexpr std::pair<ProgressiveRenderLane, std::string_view> kRenderLanes[]{
            {ProgressiveRenderLane::Surface, "Surface"},
            {ProgressiveRenderLane::Edges, "Edges"},
            {ProgressiveRenderLane::Points, "Points"},
        };

        constexpr std::pair<ProgressivePropertyValueKind, std::string_view> kValueKinds[]{
            {ProgressivePropertyValueKind::Any, "Any"},
            {ProgressivePropertyValueKind::Unknown, "Unknown"},
            {ProgressivePropertyValueKind::ScalarFloat, "ScalarFloat"},
            {ProgressivePropertyValueKind::ScalarDouble, "ScalarDouble"},
            {ProgressivePropertyValueKind::UInt32, "UInt32"},
            {ProgressivePropertyValueKind::Vec2, "Vec2"},
            {ProgressivePropertyValueKind::Vec3, "Vec3"},
            {ProgressivePropertyValueKind::Vec4, "Vec4"},
        };

        constexpr std::pair<ProgressivePresentationKind, std::string_view> kPresentationKinds[]{
            {ProgressivePresentationKind::SurfaceMaterial, "SurfaceMaterial"},
            {ProgressivePresentationKind::PointPresentation, "PointPresentation"},
            {ProgressivePresentationKind::LinePresentation, "LinePresentation"},
        };

        constexpr std::pair<ProgressiveSlotSemantic, std::string_view> kSlotSemantics[]{
            {ProgressiveSlotSemantic::Albedo, "Albedo"},
            {ProgressiveSlotSemantic::Normal, "Normal"},
            {ProgressiveSlotSemantic::Roughness, "Roughness"},
            {ProgressiveSlotSemantic::Metallic, "Metallic"},
            {ProgressiveSlotSemantic::ScalarField, "ScalarField"},
            {ProgressiveSlotSemantic::Displacement, "Displacement"},
            {ProgressiveSlotSemantic::PointColor, "PointColor"},
            {ProgressiveSlotSemantic::PointScalarField, "PointScalarField"},
            {ProgressiveSlotSemantic::PointSize, "PointSize"},
            {ProgressiveSlotSemantic::PointNormalOrientation, "PointNormalOrientation"},
            {ProgressiveSlotSemantic::LineColor, "LineColor"},
            {ProgressiveSlotSemantic::LineScalarField, "LineScalarField"},
            {ProgressiveSlotSemantic::LineWidth, "LineWidth"},
        };

        constexpr std::pair<ProgressiveSlotSourceKind, std::string_view> kSlotSourceKinds[]{
            {ProgressiveSlotSourceKind::UniformDefault, "UniformDefault"},
            {ProgressiveSlotSourceKind::AuthoredTextureAsset, "AuthoredTextureAsset"},
            {ProgressiveSlotSourceKind::GeneratedTextureAsset, "GeneratedTextureAsset"},
            {ProgressiveSlotSourceKind::PropertyBake, "PropertyBake"},
            {ProgressiveSlotSourceKind::PropertyBuffer, "PropertyBuffer"},
        };

        constexpr std::pair<ProgressiveReadinessState, std::string_view> kReadinessStates[]{
            {ProgressiveReadinessState::Unset, "Unset"},
            {ProgressiveReadinessState::DefaultValue, "DefaultValue"},
            {ProgressiveReadinessState::Pending, "Pending"},
            {ProgressiveReadinessState::Ready, "Ready"},
            {ProgressiveReadinessState::Failed, "Failed"},
            {ProgressiveReadinessState::Unsupported, "Unsupported"},
            {ProgressiveReadinessState::Stale, "Stale"},
        };

        constexpr std::pair<ProgressiveGeneratedOutputPolicy, std::string_view> kGeneratedPolicies[]{
            {ProgressiveGeneratedOutputPolicy::SessionCache, "SessionCache"},
            {ProgressiveGeneratedOutputPolicy::DeterministicChildAsset, "DeterministicChildAsset"},
            {ProgressiveGeneratedOutputPolicy::PersistOnSave, "PersistOnSave"},
        };

        constexpr std::pair<ProgressiveGeneratedOutputProvenance, std::string_view> kGeneratedProvenance[]{
            {ProgressiveGeneratedOutputProvenance::None, "None"},
            {ProgressiveGeneratedOutputProvenance::AuthoredAsset, "AuthoredAsset"},
            {ProgressiveGeneratedOutputProvenance::GeneratedTextureAsset, "GeneratedTextureAsset"},
            {ProgressiveGeneratedOutputProvenance::PropertyBuffer, "PropertyBuffer"},
            {ProgressiveGeneratedOutputProvenance::UniformDefault, "UniformDefault"},
            {ProgressiveGeneratedOutputProvenance::PropertyBinding, "PropertyBinding"},
        };

        constexpr std::pair<ProgressiveJobDomain, std::string_view> kJobDomains[]{
            {ProgressiveJobDomain::Cpu, "Cpu"},
            {ProgressiveJobDomain::GpuCompute, "GpuCompute"},
            {ProgressiveJobDomain::GpuGraphics, "GpuGraphics"},
            {ProgressiveJobDomain::Auto, "Auto"},
        };

        constexpr std::pair<ProgressivePropertyResolutionStatus, std::string_view> kResolutionStatuses[]{
            {ProgressivePropertyResolutionStatus::Compatible, "Compatible"},
            {ProgressivePropertyResolutionStatus::MissingProperty, "MissingProperty"},
            {ProgressivePropertyResolutionStatus::TypeMismatch, "TypeMismatch"},
            {ProgressivePropertyResolutionStatus::CountMismatch, "CountMismatch"},
            {ProgressivePropertyResolutionStatus::DomainUnavailable, "DomainUnavailable"},
            {ProgressivePropertyResolutionStatus::UnsupportedDomain, "UnsupportedDomain"},
            {ProgressivePropertyResolutionStatus::UnsupportedType, "UnsupportedType"},
            {ProgressivePropertyResolutionStatus::StaleGeneration, "StaleGeneration"},
        };

        [[nodiscard]] bool KindMatches(const ProgressivePropertyValueKind expected,
                                       const ProgressivePropertyValueKind actual) noexcept
        {
            return expected == ProgressivePropertyValueKind::Any || expected == actual;
        }

        [[nodiscard]] ProgressivePropertyResolution MakeResolution(
            const ProgressivePropertyResolutionStatus status,
            const ProgressivePropertyValueKind actual,
            const std::size_t count,
            const std::uint64_t observedGeneration,
            std::string diagnostic)
        {
            return ProgressivePropertyResolution{
                .Status = status,
                .ActualValueKind = actual,
                .ElementCount = count,
                .ObservedSourceGeneration = observedGeneration,
                .Diagnostic = std::move(diagnostic),
            };
        }
    }

    std::string_view ToString(const ProgressiveEntityShape value) noexcept
    {
        return EnumToString(value, kEntityShapes, "Unknown");
    }

    std::string_view ToString(const ProgressiveGeometryDomain value) noexcept
    {
        return EnumToString(value, kGeometryDomains, "Unknown");
    }

    std::string_view ToString(const ProgressiveRenderLane value) noexcept
    {
        return EnumToString(value, kRenderLanes, "Surface");
    }

    std::string_view ToString(const ProgressivePropertyValueKind value) noexcept
    {
        return EnumToString(value, kValueKinds, "Unknown");
    }

    std::string_view ToString(const ProgressivePresentationKind value) noexcept
    {
        return EnumToString(value, kPresentationKinds, "SurfaceMaterial");
    }

    std::string_view ToString(const ProgressiveSlotSemantic value) noexcept
    {
        return EnumToString(value, kSlotSemantics, "Albedo");
    }

    std::string_view ToString(const ProgressiveSlotSourceKind value) noexcept
    {
        return EnumToString(value, kSlotSourceKinds, "UniformDefault");
    }

    std::string_view ToString(const ProgressiveReadinessState value) noexcept
    {
        return EnumToString(value, kReadinessStates, "Unset");
    }

    std::string_view ToString(const ProgressiveGeneratedOutputPolicy value) noexcept
    {
        return EnumToString(value, kGeneratedPolicies, "SessionCache");
    }

    std::string_view ToString(const ProgressiveGeneratedOutputProvenance value) noexcept
    {
        return EnumToString(value, kGeneratedProvenance, "None");
    }

    std::string_view ToString(const ProgressiveJobDomain value) noexcept
    {
        return EnumToString(value, kJobDomains, "Cpu");
    }

    std::string_view ToString(const ProgressivePropertyResolutionStatus value) noexcept
    {
        return EnumToString(value, kResolutionStatuses, "DomainUnavailable");
    }

    bool TryParseProgressiveEntityShape(const std::string_view value,
                                        ProgressiveEntityShape& out) noexcept
    {
        return TryEnumFromString(value, kEntityShapes, out);
    }

    bool TryParseProgressiveGeometryDomain(const std::string_view value,
                                           ProgressiveGeometryDomain& out) noexcept
    {
        return TryEnumFromString(value, kGeometryDomains, out);
    }

    bool TryParseProgressiveRenderLane(const std::string_view value,
                                       ProgressiveRenderLane& out) noexcept
    {
        return TryEnumFromString(value, kRenderLanes, out);
    }

    bool TryParseProgressivePropertyValueKind(const std::string_view value,
                                              ProgressivePropertyValueKind& out) noexcept
    {
        return TryEnumFromString(value, kValueKinds, out);
    }

    bool TryParseProgressivePresentationKind(const std::string_view value,
                                             ProgressivePresentationKind& out) noexcept
    {
        return TryEnumFromString(value, kPresentationKinds, out);
    }

    bool TryParseProgressiveSlotSemantic(const std::string_view value,
                                         ProgressiveSlotSemantic& out) noexcept
    {
        return TryEnumFromString(value, kSlotSemantics, out);
    }

    bool TryParseProgressiveSlotSourceKind(const std::string_view value,
                                           ProgressiveSlotSourceKind& out) noexcept
    {
        return TryEnumFromString(value, kSlotSourceKinds, out);
    }

    bool TryParseProgressiveGeneratedOutputPolicy(const std::string_view value,
                                                  ProgressiveGeneratedOutputPolicy& out) noexcept
    {
        return TryEnumFromString(value, kGeneratedPolicies, out);
    }

    bool TryParseProgressiveGeneratedOutputProvenance(const std::string_view value,
                                                      ProgressiveGeneratedOutputProvenance& out) noexcept
    {
        return TryEnumFromString(value, kGeneratedProvenance, out);
    }

    ProgressiveGeneratedOutputPolicy DefaultGeneratedOutputPolicyFor(
        const ProgressiveSlotSourceKind sourceKind) noexcept
    {
        switch (sourceKind)
        {
        case ProgressiveSlotSourceKind::GeneratedTextureAsset:
        case ProgressiveSlotSourceKind::PropertyBake:
            return ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
        case ProgressiveSlotSourceKind::PropertyBuffer:
            return ProgressiveGeneratedOutputPolicy::SessionCache;
        case ProgressiveSlotSourceKind::UniformDefault:
        case ProgressiveSlotSourceKind::AuthoredTextureAsset:
            return ProgressiveGeneratedOutputPolicy::SessionCache;
        }
        return ProgressiveGeneratedOutputPolicy::SessionCache;
    }

    const Geometry::PropertySet* ResolvePropertySet(
        const GS::ConstSourceView& view,
        const ProgressiveGeometryDomain domain) noexcept
    {
        switch (domain)
        {
        case ProgressiveGeometryDomain::MeshVertex:
            return view.ActiveDomain == GS::Domain::Mesh && view.VertexSource ? &view.VertexSource->Properties : nullptr;
        case ProgressiveGeometryDomain::MeshEdge:
            return view.ActiveDomain == GS::Domain::Mesh && view.EdgeSource ? &view.EdgeSource->Properties : nullptr;
        case ProgressiveGeometryDomain::MeshHalfedge:
            return view.ActiveDomain == GS::Domain::Mesh && view.HalfedgeSource ? &view.HalfedgeSource->Properties : nullptr;
        case ProgressiveGeometryDomain::MeshFace:
            return view.ActiveDomain == GS::Domain::Mesh && view.FaceSource ? &view.FaceSource->Properties : nullptr;
        case ProgressiveGeometryDomain::GraphVertex:
            return view.ActiveDomain == GS::Domain::Graph && view.NodeSource ? &view.NodeSource->Properties : nullptr;
        case ProgressiveGeometryDomain::GraphEdge:
            return view.ActiveDomain == GS::Domain::Graph && view.EdgeSource ? &view.EdgeSource->Properties : nullptr;
        case ProgressiveGeometryDomain::Point:
            return view.ActiveDomain == GS::Domain::PointCloud && view.VertexSource ? &view.VertexSource->Properties : nullptr;
        case ProgressiveGeometryDomain::MeshSurface:
        case ProgressiveGeometryDomain::Unknown:
            return nullptr;
        }
        return nullptr;
    }

    std::size_t ResolvePropertyElementCount(
        const GS::ConstSourceView& view,
        const ProgressiveGeometryDomain domain) noexcept
    {
        const Geometry::PropertySet* set = ResolvePropertySet(view, domain);
        return set ? set->Size() : 0u;
    }

    ProgressivePropertyValueKind DetectPropertyValueKind(
        const Geometry::PropertySet& properties,
        const std::string_view propertyName)
    {
        if (properties.Get<float>(propertyName).IsValid())
            return ProgressivePropertyValueKind::ScalarFloat;
        if (properties.Get<double>(propertyName).IsValid())
            return ProgressivePropertyValueKind::ScalarDouble;
        if (properties.Get<std::uint32_t>(propertyName).IsValid())
            return ProgressivePropertyValueKind::UInt32;
        if (properties.Get<glm::vec2>(propertyName).IsValid())
            return ProgressivePropertyValueKind::Vec2;
        if (properties.Get<glm::vec3>(propertyName).IsValid())
            return ProgressivePropertyValueKind::Vec3;
        if (properties.Get<glm::vec4>(propertyName).IsValid())
            return ProgressivePropertyValueKind::Vec4;
        return ProgressivePropertyValueKind::Unknown;
    }

    ProgressivePropertyResolution ResolvePropertyBinding(
        const GS::ConstSourceView& view,
        const ProgressivePropertyBindingDescriptor& descriptor,
        const std::uint64_t observedSourceGeneration)
    {
        if (descriptor.Domain == ProgressiveGeometryDomain::Unknown ||
            descriptor.Domain == ProgressiveGeometryDomain::MeshSurface)
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::UnsupportedDomain,
                                  ProgressivePropertyValueKind::Unknown,
                                  0u,
                                  observedSourceGeneration,
                                  "unsupported property domain");
        }

        const Geometry::PropertySet* properties = ResolvePropertySet(view, descriptor.Domain);
        if (properties == nullptr)
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::DomainUnavailable,
                                  ProgressivePropertyValueKind::Unknown,
                                  0u,
                                  observedSourceGeneration,
                                  "geometry domain is unavailable on this entity");
        }

        if (descriptor.SourceGeneration != 0u &&
            observedSourceGeneration != 0u &&
            descriptor.SourceGeneration != observedSourceGeneration)
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::StaleGeneration,
                                  ProgressivePropertyValueKind::Unknown,
                                  properties->Size(),
                                  observedSourceGeneration,
                                  "source generation is stale");
        }

        if (!properties->Exists(descriptor.PropertyName))
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::MissingProperty,
                                  ProgressivePropertyValueKind::Unknown,
                                  properties->Size(),
                                  observedSourceGeneration,
                                  "property is missing");
        }

        const ProgressivePropertyValueKind actual =
            DetectPropertyValueKind(*properties, descriptor.PropertyName);
        if (actual == ProgressivePropertyValueKind::Unknown)
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::UnsupportedType,
                                  actual,
                                  properties->Size(),
                                  observedSourceGeneration,
                                  "property type is unsupported");
        }

        if (!KindMatches(descriptor.ExpectedValueKind, actual))
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::TypeMismatch,
                                  actual,
                                  properties->Size(),
                                  observedSourceGeneration,
                                  "property type is incompatible with the slot");
        }

        if (descriptor.ExpectedElementCount != 0u &&
            descriptor.ExpectedElementCount != properties->Size())
        {
            return MakeResolution(ProgressivePropertyResolutionStatus::CountMismatch,
                                  actual,
                                  properties->Size(),
                                  observedSourceGeneration,
                                  "property element count does not match the binding");
        }

        return MakeResolution(ProgressivePropertyResolutionStatus::Compatible,
                              actual,
                              properties->Size(),
                              observedSourceGeneration,
                              {});
    }

    std::vector<ProgressivePropertyOption> EnumeratePropertyOptions(
        const GS::ConstSourceView& view,
        const ProgressiveGeometryDomain domain,
        const ProgressivePropertyValueKind expectedValueKind,
        const std::size_t expectedElementCount,
        const std::uint64_t observedSourceGeneration)
    {
        const Geometry::PropertySet* properties = ResolvePropertySet(view, domain);
        if (properties == nullptr)
            return {};

        std::vector<ProgressivePropertyOption> out;
        for (const std::string& name : properties->Properties())
        {
            ProgressivePropertyBindingDescriptor descriptor{
                .Domain = domain,
                .PropertyName = name,
                .ExpectedValueKind = expectedValueKind,
                .ExpectedElementCount = expectedElementCount,
                .SourceGeneration = observedSourceGeneration,
            };
            ProgressivePropertyResolution resolution =
                ResolvePropertyBinding(view, descriptor, observedSourceGeneration);
            out.push_back(ProgressivePropertyOption{
                .Descriptor = std::move(descriptor),
                .ActualValueKind = resolution.ActualValueKind,
                .ElementCount = resolution.ElementCount,
                .Compatible = resolution.Compatible(),
                .DisabledReason = resolution.Diagnostic,
            });
        }

        std::stable_sort(out.begin(),
                         out.end(),
                         [](const ProgressivePropertyOption& lhs,
                            const ProgressivePropertyOption& rhs)
                         {
                             if (lhs.Compatible != rhs.Compatible)
                                 return lhs.Compatible;
                             return lhs.Descriptor.PropertyName < rhs.Descriptor.PropertyName;
                         });
        return out;
    }

    ProgressiveRenderLaneBinding* FindLaneBinding(
        ProgressivePresentationBindings& bindings,
        const ProgressiveRenderLane lane) noexcept
    {
        for (ProgressiveRenderLaneBinding& binding : bindings.Lanes)
        {
            if (binding.Lane == lane)
                return &binding;
        }
        return nullptr;
    }

    const ProgressiveRenderLaneBinding* FindLaneBinding(
        const ProgressivePresentationBindings& bindings,
        const ProgressiveRenderLane lane) noexcept
    {
        for (const ProgressiveRenderLaneBinding& binding : bindings.Lanes)
        {
            if (binding.Lane == lane)
                return &binding;
        }
        return nullptr;
    }

    ProgressivePresentationBinding* FindPresentationBinding(
        ProgressivePresentationBindings& bindings,
        const std::string_view key) noexcept
    {
        for (ProgressivePresentationBinding& presentation : bindings.Presentations)
        {
            if (presentation.Key == key)
                return &presentation;
        }
        return nullptr;
    }

    const ProgressivePresentationBinding* FindPresentationBinding(
        const ProgressivePresentationBindings& bindings,
        const std::string_view key) noexcept
    {
        for (const ProgressivePresentationBinding& presentation : bindings.Presentations)
        {
            if (presentation.Key == key)
                return &presentation;
        }
        return nullptr;
    }

    ProgressiveSlotBinding* FindSlotBinding(
        ProgressivePresentationBinding& presentation,
        const ProgressiveSlotSemantic semantic) noexcept
    {
        for (ProgressiveSlotBinding& slot : presentation.Slots)
        {
            if (slot.Semantic == semantic)
                return &slot;
        }
        return nullptr;
    }

    const ProgressiveSlotBinding* FindSlotBinding(
        const ProgressivePresentationBinding& presentation,
        const ProgressiveSlotSemantic semantic) noexcept
    {
        for (const ProgressiveSlotBinding& slot : presentation.Slots)
        {
            if (slot.Semantic == semantic)
                return &slot;
        }
        return nullptr;
    }
}
