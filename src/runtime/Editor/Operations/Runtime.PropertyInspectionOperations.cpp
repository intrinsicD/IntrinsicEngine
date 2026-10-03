module;
#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PropertyInspectionOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.EngineConfigControl;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        void Fail(std::vector<EditorDiagnostic>& diagnostics, std::string message,
                  EditorDiagnosticCode code = EditorDiagnosticCode::InvalidVisualizationProperty)
        {
            diagnostics.push_back({code, std::move(message)});
        }

        std::optional<GeometryEntityAvailability> ResolveEntity(
            const EditorProcessingCommands& commands, std::uint32_t stableId,
            std::vector<EditorDiagnostic>& diagnostics)
        {
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            if (!context.Scene)
            {
                Fail(diagnostics, "Property inspection requires an attached scene.", EditorDiagnosticCode::MissingScene);
                return std::nullopt;
            }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), stableId);
            if (!entity)
            {
                Fail(diagnostics, "Property inspection entity does not exist: " + std::to_string(stableId),
                     EditorDiagnosticCode::NoSelectedEntity);
                return std::nullopt;
            }
            return BuildGeometryAvailability(context.Scene->Raw(), *entity);
        }

        const Geometry::PropertySet* ResolveProperty(
            const GeometryEntityAvailability& availability, GeometryPropertyRef& ref,
            std::vector<EditorDiagnostic>& diagnostics)
        {
            const auto* properties = ResolveGeometryPropertySet(availability, ref.Domain);
            if (!properties)
            {
                Fail(diagnostics, "Unavailable property domain: " + std::string{ToString(ref.Domain)},
                     EditorDiagnosticCode::UnsupportedGeometryDomain);
                return nullptr;
            }
            const auto descriptors = properties->Descriptors();
            const auto descriptor = std::ranges::find_if(descriptors, [&](const auto& d) { return d.Name == ref.Name; });
            const auto label = std::string{ToString(ref.Domain)} + "/" + ref.Name;
            if (ref.Name.empty() || descriptor == descriptors.end())
            {
                Fail(diagnostics, "Property not found: " + label);
                return nullptr;
            }
            if (ref.ValueKind != Geometry::PropertyValueKind::Unknown && ref.ValueKind != descriptor->ValueKind)
            {
                Fail(diagnostics, "Property kind mismatch for " + label + ": requested " +
                     DebugNameForGeometryPropertyValueKind(ref.ValueKind) + ", stored " +
                     DebugNameForGeometryPropertyValueKind(descriptor->ValueKind));
                return nullptr;
            }
            ref.ValueKind = descriptor->ValueKind;
            if (ref.ValueKind == Geometry::PropertyValueKind::Unknown)
            {
                Fail(diagnostics, "Unsupported property kind: " + label);
                return nullptr;
            }
            if (descriptor->ElementCount != properties->Size())
            {
                Fail(diagnostics, "Property row count does not match its domain: " + label);
                return nullptr;
            }
            return properties;
        }

        bool AcceptStatisticsStatus(Geometry::PropertyStatisticsStatus status)
        {
            using S = Geometry::PropertyStatisticsStatus;
            return status == S::Ok || status == S::Empty || status == S::NoFiniteValues;
        }

        template<typename T>
        EditorPropertyValue CopyValue(T value)
        {
            if constexpr (std::is_same_v<T, bool>) return value;
            else if constexpr (std::is_same_v<T, std::uint64_t>) return std::to_string(value);
            else if constexpr (std::is_integral_v<T>) return static_cast<std::int64_t>(value);
            else
            {
                if (std::isnan(value)) return std::string{"NaN"};
                if (std::isinf(value)) return std::string{value < 0 ? "-Infinity" : "+Infinity"};
                return static_cast<double>(value);
            }
        }

        template<typename T>
        void CopyRows(const Geometry::ConstPropertySet& properties, const Geometry::PropertyDeletionMasks& deleted,
                      std::size_t count, EditorPropertyValuesResult& result)
        {
            const auto property = properties.Get<T>(result.Property.Name);
            for (std::size_t local = 0; local < count; ++local)
            {
                const auto index = result.Offset + local;
                EditorPropertyValueRow row{.Index = index, .Deleted = deleted.IsDeleted(index)};
                const T value = property[index];
                if constexpr (std::is_arithmetic_v<T>) row.Components.push_back(CopyValue(value));
                else
                    for (glm::length_t component = 0; component < T::length(); ++component)
                        row.Components.push_back(CopyValue(value[component]));
                result.Rows.push_back(std::move(row));
            }
        }
    }

    EditorPropertyCatalogResult GetEditorPropertyCatalog(const EditorProcessingCommands& commands, std::uint32_t stableId)
    {
        EditorPropertyCatalogResult result{};
        const auto availability = ResolveEntity(commands, stableId, result.Diagnostics);
        if (!availability) return result;
        result.Catalog = BuildGeometryPropertyCatalogSnapshot(*availability, stableId);
        auto& generation = result.Catalog.SourceGeneration;
        generation = EditorFeatureDetail::kEditorSignatureOffset;
        for (auto& entry : result.Catalog.Entries)
        {
            const auto* properties = ResolveGeometryPropertySet(*availability, entry.Ref.Domain);
            entry.PropertyGeneration = properties->FindPropertyRevision(entry.Ref.Name).value_or(0);
            EditorFeatureDetail::MixSignature(generation, static_cast<std::uint64_t>(entry.Ref.Domain));
            EditorFeatureDetail::MixSignatureString(generation, entry.Ref.Name);
            EditorFeatureDetail::MixSignature(generation, properties->Revision());
            EditorFeatureDetail::MixSignature(generation, entry.PropertyGeneration);
        }
        result.Success = true;
        return result;
    }

    EditorPropertyStatisticsResult GetEditorPropertyStatistics(const EditorProcessingCommands& commands,
        std::uint32_t stableId, const GeometryPropertyRef& ref, std::size_t bins)
    {
        EditorPropertyStatisticsResult result{.Property = ref};
        const auto availability = ResolveEntity(commands, stableId, result.Diagnostics);
        if (!availability) return result;
        const auto* properties = ResolveProperty(*availability, result.Property, result.Diagnostics);
        if (!properties) return result;
        result.Statistics = Geometry::ComputePropertyStatistics(Geometry::ConstPropertySet{*properties}, ref.Name,
                                                               {.Bins = bins});
        result.Success = AcceptStatisticsStatus(result.Statistics.Status);
        if (!result.Success) Fail(result.Diagnostics, result.Statistics.Diagnostic);
        return result;
    }

    EditorPropertyComparisonResult CompareEditorProperties(const EditorProcessingCommands& commands,
        std::uint32_t stableId, const GeometryPropertyRef& a, const GeometryPropertyRef& b)
    {
        EditorPropertyComparisonResult result{.A = a, .B = b};
        const auto availability = ResolveEntity(commands, stableId, result.Diagnostics);
        if (!availability) return result;
        const auto* propertiesA = ResolveProperty(*availability, result.A, result.Diagnostics);
        const auto* propertiesB = ResolveProperty(*availability, result.B, result.Diagnostics);
        if (!propertiesA || !propertiesB) return result;
        if (a.Domain != b.Domain)
        {
            Fail(result.Diagnostics, "Property comparison requires the same element domain.");
            return result;
        }
        result.Comparison = Geometry::ComparePropertyValues(Geometry::ConstPropertySet{*propertiesA}, a.Name,
                                                            Geometry::ConstPropertySet{*propertiesB}, b.Name);
        result.Success = AcceptStatisticsStatus(result.Comparison.Status);
        if (!result.Success) Fail(result.Diagnostics, result.Comparison.Diagnostic);
        return result;
    }

    EditorPropertyValuesResult ReadEditorPropertyValues(const EditorProcessingCommands& commands,
        std::uint32_t stableId, const GeometryPropertyRef& ref, std::size_t offset, std::size_t limit)
    {
        EditorPropertyValuesResult result{.Property = ref, .Offset = offset};
        if (limit == 0 || limit > MaxEditorPropertyValues)
        {
            Fail(result.Diagnostics, "Property values limit must be between 1 and 65536.");
            return result;
        }
        const auto availability = ResolveEntity(commands, stableId, result.Diagnostics);
        if (!availability) return result;
        const auto* properties = ResolveProperty(*availability, result.Property, result.Diagnostics);
        if (!properties) return result;
        const Geometry::ConstPropertySet view{*properties};
        const auto deleted = Geometry::ResolvePropertyDeletionMasks(view);
        if (deleted.Status != Geometry::PropertyStatisticsStatus::Ok)
        {
            Fail(result.Diagnostics, deleted.Diagnostic);
            return result;
        }
        result.TotalCount = properties->Size();
        const auto remaining = offset >= result.TotalCount ? 0 : result.TotalCount - offset;
        const auto count = std::min(limit, remaining);
        result.HasMore = count < remaining;
        result.Rows.reserve(count);
        using K = Geometry::PropertyValueKind;
        switch (result.Property.ValueKind)
        {
        case K::Bool: CopyRows<bool>(view, deleted, count, result); break;
        case K::Int32: CopyRows<std::int32_t>(view, deleted, count, result); break;
        case K::UInt32: CopyRows<std::uint32_t>(view, deleted, count, result); break;
        case K::UInt64: CopyRows<std::uint64_t>(view, deleted, count, result); break;
        case K::Float: CopyRows<float>(view, deleted, count, result); break;
        case K::Double: CopyRows<double>(view, deleted, count, result); break;
        case K::Vec2: CopyRows<glm::vec2>(view, deleted, count, result); break;
        case K::Vec3: CopyRows<glm::vec3>(view, deleted, count, result); break;
        case K::Vec4: CopyRows<glm::vec4>(view, deleted, count, result); break;
        case K::Unknown: return result;
        }
        result.Success = true;
        return result;
    }
}
