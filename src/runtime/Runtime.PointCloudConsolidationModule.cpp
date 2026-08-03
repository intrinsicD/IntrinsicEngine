module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.PointCloudConsolidationModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.PointCloud.Consolidation;
import Geometry.Properties;

#include "Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Dirty = ECS::Components::DirtyTags;
        namespace GS = ECS::Components::GeometrySources;
        namespace Consolidation = Geometry::PointCloud::Consolidation;

        constexpr std::size_t kMaximumPointCount = 1'000'000u;

        struct Vec3PropertySnapshot
        {
            std::string Name{};
            bool Exists{false};
            std::vector<glm::vec3> Values{};
        };

        struct ElementDomainPropertyState
        {
            GeometryElementDomain Domain{GeometryElementDomain::Unknown};
            std::size_t ElementCount{0u};
            std::vector<Vec3PropertySnapshot> Properties{};
        };

        struct PointCloudConsolidationSnapshot
        {
            PointCloudConsolidationRequest Request{};
            WorldHandle World{};
            CommandCorrelationId Correlation{};
            ElementDomainPropertyState SourceState{};
            ElementDomainPropertyState BeforeOutputs{};
            std::optional<GS::Vertices> PointCloudReplacementBefore{};
            bool AllowsPointCloudReplacement{false};
            std::vector<glm::vec3> Positions{};
            std::vector<glm::vec3> Normals{};
            Consolidation::Params Params{};
        };

        struct PointCloudConsolidationJobResult
        {
            PointCloudConsolidationSnapshot Snapshot{};
            PointCloudConsolidationResult Completion{};
            std::optional<ElementDomainPropertyState> AfterOutputs{};
            std::optional<GS::Vertices> PointCloudReplacementAfter{};
        };

        struct PointCloudConsolidationJobCompleted
        {
            PointCloudConsolidationJobResult Result{};
        };

        [[nodiscard]] ECS::EntityHandle ResolveEntity(
            entt::registry& registry,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            return entity != ECS::InvalidEntityHandle &&
                    registry.valid(entity)
                ? entity
                : ECS::InvalidEntityHandle;
        }

        [[nodiscard]] bool SameValue(const bool lhs, const bool rhs) noexcept
        {
            return lhs == rhs;
        }

        [[nodiscard]] bool SameValue(
            const std::int32_t lhs,
            const std::int32_t rhs) noexcept
        {
            return lhs == rhs;
        }

        [[nodiscard]] bool SameValue(
            const std::uint32_t lhs,
            const std::uint32_t rhs) noexcept
        {
            return lhs == rhs;
        }

        [[nodiscard]] bool SameValue(
            const std::uint64_t lhs,
            const std::uint64_t rhs) noexcept
        {
            return lhs == rhs;
        }

        [[nodiscard]] bool SameValue(
            const float lhs,
            const float rhs) noexcept
        {
            return std::bit_cast<std::uint32_t>(lhs) ==
                   std::bit_cast<std::uint32_t>(rhs);
        }

        [[nodiscard]] bool SameValue(
            const double lhs,
            const double rhs) noexcept
        {
            return std::bit_cast<std::uint64_t>(lhs) ==
                   std::bit_cast<std::uint64_t>(rhs);
        }

        [[nodiscard]] bool SameValue(
            const glm::vec2& lhs,
            const glm::vec2& rhs) noexcept
        {
            return SameValue(lhs.x, rhs.x) && SameValue(lhs.y, rhs.y);
        }

        [[nodiscard]] bool SameValue(
            const glm::vec3& lhs,
            const glm::vec3& rhs) noexcept
        {
            return SameValue(lhs.x, rhs.x) && SameValue(lhs.y, rhs.y) &&
                   SameValue(lhs.z, rhs.z);
        }

        [[nodiscard]] bool SameValue(
            const glm::vec4& lhs,
            const glm::vec4& rhs) noexcept
        {
            return SameValue(lhs.x, rhs.x) && SameValue(lhs.y, rhs.y) &&
                   SameValue(lhs.z, rhs.z) && SameValue(lhs.w, rhs.w);
        }

        template <typename TValue>
        [[nodiscard]] bool SameProperty(
            const Geometry::PropertySet& lhs,
            const Geometry::PropertySet& rhs,
            const std::string_view name)
        {
            const auto lhsProperty = lhs.Get<TValue>(name);
            const auto rhsProperty = rhs.Get<TValue>(name);
            if (!lhsProperty || !rhsProperty ||
                lhsProperty.Vector().size() != rhsProperty.Vector().size())
            {
                return false;
            }
            for (std::size_t index = 0u;
                 index < lhsProperty.Vector().size();
                 ++index)
            {
                if (!SameValue(
                        static_cast<TValue>(lhsProperty[index]),
                        static_cast<TValue>(rhsProperty[index])))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SamePropertySet(
            const Geometry::PropertySet& lhs,
            const Geometry::PropertySet& rhs)
        {
            if (lhs.Size() != rhs.Size())
                return false;

            const std::vector<Geometry::PropertyDescriptor> lhsDescriptors =
                lhs.Descriptors();
            const std::vector<Geometry::PropertyDescriptor> rhsDescriptors =
                rhs.Descriptors();
            if (lhsDescriptors.size() != rhsDescriptors.size())
                return false;

            for (std::size_t index = 0u;
                 index < lhsDescriptors.size();
                 ++index)
            {
                const Geometry::PropertyDescriptor& a = lhsDescriptors[index];
                const Geometry::PropertyDescriptor& b = rhsDescriptors[index];
                if (a.Name != b.Name || a.ValueKind != b.ValueKind ||
                    a.ElementCount != b.ElementCount)
                {
                    return false;
                }

                bool same = false;
                switch (a.ValueKind)
                {
                case Geometry::PropertyValueKind::Bool:
                    same = SameProperty<bool>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Int32:
                    same = SameProperty<std::int32_t>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::UInt32:
                    same = SameProperty<std::uint32_t>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::UInt64:
                    same = SameProperty<std::uint64_t>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Float:
                    same = SameProperty<float>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Double:
                    same = SameProperty<double>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Vec2:
                    same = SameProperty<glm::vec2>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Vec3:
                    same = SameProperty<glm::vec3>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Vec4:
                    same = SameProperty<glm::vec4>(lhs, rhs, a.Name);
                    break;
                case Geometry::PropertyValueKind::Unknown:
                    return false;
                }
                if (!same)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool SameVertices(
            const GS::Vertices& lhs,
            const GS::Vertices& rhs)
        {
            return lhs.NumDeleted == rhs.NumDeleted &&
                   SamePropertySet(lhs.Properties, rhs.Properties);
        }

        [[nodiscard]] std::optional<Consolidation::NormalSourcePolicy>
        ToNormalSource(
            const PointCloudConsolidationNormalSource source) noexcept
        {
            switch (source)
            {
            case PointCloudConsolidationNormalSource::AuthoredOrEstimate:
                return Consolidation::NormalSourcePolicy::AuthoredOrEstimate;
            case PointCloudConsolidationNormalSource::RequireAuthored:
                return Consolidation::NormalSourcePolicy::RequireAuthored;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Consolidation::Params> BuildParams(
            const PointCloudConsolidationConfig& config) noexcept
        {
            if (!std::isfinite(config.SupportRadius) ||
                config.SupportRadius <= 0.0 ||
                !std::isfinite(config.RepulsionWeight) ||
                config.RepulsionWeight < 0.0 ||
                config.RepulsionWeight >= 0.5 ||
                config.MaxIterations == 0u ||
                config.MaxIterations > 4096u ||
                !std::isfinite(config.ConvergenceTolerance) ||
                config.ConvergenceTolerance < 0.0 ||
                config.TargetPointCount == 1u ||
                config.TargetPointCount > kMaximumPointCount ||
                !std::isfinite(config.NormalAngleRadians) ||
                config.NormalAngleRadians <= 0.0 ||
                config.NormalAngleRadians >= 3.14159265358979323846 ||
                config.NormalRefinementRounds == 0u ||
                config.NormalRefinementRounds > config.MaxIterations ||
                config.ClopMixtureComponentCount == 0u ||
                config.ClopMixtureComponentCount > kMaximumPointCount ||
                config.ClopMixtureMaxIterations == 0u ||
                config.ClopMixtureMaxIterations > 4096u ||
                !std::isfinite(config.ClopMixtureRelativeTolerance) ||
                config.ClopMixtureRelativeTolerance < 0.0 ||
                config.ClopMixtureRelativeTolerance > 1.0 ||
                !std::isfinite(config.ClopCovarianceFloor) ||
                config.ClopCovarianceFloor <= 0.0 ||
                !std::isfinite(config.EarEdgeSensitivity) ||
                config.EarEdgeSensitivity <= 0.0)
            {
                return std::nullopt;
            }

            const std::optional<Consolidation::NormalSourcePolicy>
                normalSource = ToNormalSource(config.NormalSource);
            if (!normalSource.has_value())
                return std::nullopt;

            Consolidation::Params params{
                .SupportRadius = config.SupportRadius,
                .RepulsionWeight = config.RepulsionWeight,
                .MaxIterations = config.MaxIterations,
                .ConvergenceTolerance = config.ConvergenceTolerance,
                .TargetPointCount = config.TargetPointCount,
                .Seed = config.Seed,
                .MaxInputPointCount = kMaximumPointCount,
                .MaxOutputPointCount = kMaximumPointCount,
            };
            switch (config.Strategy)
            {
            case PointCloudConsolidationStrategy::Lop:
                params.Method = Consolidation::LopStrategy{};
                break;
            case PointCloudConsolidationStrategy::Wlop:
                params.Method = Consolidation::WlopStrategy{
                    .Weighting = config.WlopAnisotropic
                        ? Consolidation::WeightingMode::Anisotropic
                        : Consolidation::WeightingMode::Isotropic,
                    .NormalSource = *normalSource,
                    .NormalAngleRadians = config.NormalAngleRadians,
                    .NormalRefinementRounds =
                        config.NormalRefinementRounds,
                };
                break;
            case PointCloudConsolidationStrategy::Clop:
                params.Method = Consolidation::ClopStrategy{
                    .MixtureComponentCount =
                        config.ClopMixtureComponentCount,
                    .MixtureMaxIterations =
                        config.ClopMixtureMaxIterations,
                    .MixtureRelativeTolerance =
                        config.ClopMixtureRelativeTolerance,
                    .CovarianceFloor = config.ClopCovarianceFloor,
                };
                break;
            case PointCloudConsolidationStrategy::Ear:
                params.Method = Consolidation::EarStrategy{
                    .NormalSource = *normalSource,
                    .NormalAngleRadians = config.NormalAngleRadians,
                    .EdgeSensitivity = config.EarEdgeSensitivity,
                    .NormalRefinementRounds =
                        config.NormalRefinementRounds,
                };
                break;
            default: return std::nullopt;
            }
            return params;
        }

        [[nodiscard]] PointCloudConsolidationResult MakeCompletion(
            const PointCloudConsolidationRequest& request,
            const WorldHandle world,
            const CommandCorrelationId correlation,
            const PointCloudConsolidationRunStatus status,
            const Core::ErrorCode error,
            std::string message)
        {
            return PointCloudConsolidationResult{
                .Correlation = correlation,
                .World = world,
                .Status = status,
                .StableEntityId = request.StableEntityId,
                .Properties = request.Properties,
                .Config = request.Config,
                .StrategyToken = std::string{StableToken(
                    request.Config.Strategy)},
                .Error = error,
                .Message = std::move(message),
            };
        }

        void PublishCompletion(
            KernelEventBus* events,
            PointCloudConsolidationResult completion)
        {
            if (events != nullptr)
                events->Publish(std::move(completion));
        }

        [[nodiscard]] bool SupportsExactSnapshot(
            const Geometry::PropertySet& properties) noexcept
        {
            for (const Geometry::PropertyDescriptor& descriptor :
                 properties.Descriptors())
            {
                if (descriptor.ValueKind ==
                    Geometry::PropertyValueKind::Unknown)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] PointCloudConsolidationAvailability
        UnavailableConsolidation(
            const Core::ErrorCode error,
            std::string message,
            const std::size_t inputPointCount = 0u,
            const bool cardinalityChanging = false)
        {
            return PointCloudConsolidationAvailability{
                .Available = false,
                .InputPointCount = inputPointCount,
                .CardinalityChanging = cardinalityChanging,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool HasValidPropertyRefs(
            const PointCloudConsolidationPropertyRefs& refs) noexcept
        {
            const GeometryElementDomain domain = refs.InputPositions.Domain;
            if (domain == GeometryElementDomain::Unknown ||
                !refs.InputPositions.HasName() ||
                refs.InputPositions.ValueKind !=
                    Geometry::PropertyValueKind::Vec3 ||
                refs.OutputPositions.Domain != domain ||
                !refs.OutputPositions.HasName() ||
                refs.OutputPositions.ValueKind !=
                    Geometry::PropertyValueKind::Vec3)
            {
                return false;
            }

            const auto validOptional = [domain](
                const std::optional<GeometryPropertyRef>& ref)
            {
                return !ref.has_value() ||
                       (ref->Domain == domain && ref->HasName() &&
                        ref->ValueKind ==
                            Geometry::PropertyValueKind::Vec3);
            };
            if (!validOptional(refs.InputNormals) ||
                !validOptional(refs.OutputNormals))
            {
                return false;
            }

            if (refs.InputNormals.has_value() &&
                refs.InputNormals->Name == refs.InputPositions.Name)
            {
                return false;
            }
            if (refs.OutputNormals.has_value() &&
                refs.OutputNormals->Name == refs.OutputPositions.Name)
            {
                return false;
            }
            if (refs.OutputNormals.has_value() &&
                refs.OutputNormals->Name == refs.InputPositions.Name)
            {
                return false;
            }
            return !refs.InputNormals.has_value() ||
                   refs.OutputPositions.Name != refs.InputNormals->Name;
        }

        [[nodiscard]] std::size_t DeletedElementCount(
            const GeometryEntityAvailability& availability,
            const GeometryElementDomain domain) noexcept
        {
            const GS::ConstSourceView& view = availability.SourceView;
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::PointCloudPoint:
                return view.VertexSource != nullptr
                    ? view.VertexSource->NumDeleted
                    : 0u;
            case GeometryElementDomain::MeshEdge:
            case GeometryElementDomain::GraphEdge:
                return view.EdgeSource != nullptr
                    ? view.EdgeSource->NumDeleted
                    : 0u;
            case GeometryElementDomain::MeshHalfedge:
            case GeometryElementDomain::GraphHalfedge:
                return 0u;
            case GeometryElementDomain::MeshFace:
                return view.FaceSource != nullptr
                    ? view.FaceSource->NumDeleted
                    : 0u;
            case GeometryElementDomain::Unknown:
                break;
            }
            return 0u;
        }

        [[nodiscard]] bool RequiresAuthoredNormals(
            const PointCloudConsolidationConfig& config) noexcept
        {
            const bool anisotropic =
                (config.Strategy == PointCloudConsolidationStrategy::Wlop &&
                 config.WlopAnisotropic) ||
                config.Strategy == PointCloudConsolidationStrategy::Ear;
            return anisotropic &&
                   config.NormalSource ==
                       PointCloudConsolidationNormalSource::RequireAuthored;
        }

        [[nodiscard]] bool CanWriteVec3Property(
            const Geometry::PropertySet& properties,
            const GeometryPropertyRef& ref) noexcept
        {
            return !properties.Exists(ref.Name) ||
                   DetectGeometryPropertyValueKind(properties, ref.Name) ==
                       Geometry::PropertyValueKind::Vec3;
        }

        [[nodiscard]] PointCloudConsolidationAvailability
        InspectConsolidationSource(
            const GeometryEntityAvailability& availability,
            const PointCloudConsolidationPropertyRefs& refs,
            const PointCloudConsolidationConfig& config)
        {
            if (!BuildParams(config).has_value())
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "Point-set consolidation parameters are outside the validated runtime control surface.");
            }
            if (!HasValidPropertyRefs(refs))
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "Point-set consolidation requires distinct, named vec3 input/output properties on one element domain.");
            }

            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(
                    availability, refs.InputPositions.Domain);
            if (properties == nullptr)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "The selected entity does not expose the requested geometry element domain.");
            }
            if (DeletedElementCount(
                    availability, refs.InputPositions.Domain) != 0u)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "Point-set consolidation requires a compact element domain without deleted slots.");
            }

            const std::size_t elementCount = properties->Size();
            const GeometryPropertyResolution positions =
                ResolveGeometryProperty(
                    availability,
                    refs.InputPositions,
                    elementCount,
                    true);
            if (!positions.Resolved() || elementCount < 2u ||
                elementCount > kMaximumPointCount)
            {
                return UnavailableConsolidation(
                    positions.Status ==
                            GeometryPropertyResolutionStatus::NonFiniteValues
                        ? Core::ErrorCode::InvalidArgument
                        : Core::ErrorCode::TypeMismatch,
                    "Point-set consolidation requires between two and one million finite vec3 values in the selected position property.",
                    elementCount);
            }
            if (config.Strategy ==
                    PointCloudConsolidationStrategy::Clop &&
                config.ClopMixtureComponentCount > elementCount)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "CLOP mixture component count cannot exceed the selected input property's element count.",
                    elementCount);
            }

            const bool authoredNormalsRequired =
                RequiresAuthoredNormals(config);
            if (refs.InputNormals.has_value())
            {
                if (properties->Exists(refs.InputNormals->Name))
                {
                    const GeometryPropertyResolution normals =
                        ResolveGeometryProperty(
                            availability,
                            *refs.InputNormals,
                            elementCount,
                            true);
                    if (!normals.Resolved())
                    {
                        return UnavailableConsolidation(
                            Core::ErrorCode::TypeMismatch,
                            "The selected normal property must contain finite, count-matched vec3 values.",
                            elementCount);
                    }
                }
                else if (authoredNormalsRequired)
                {
                    return UnavailableConsolidation(
                        Core::ErrorCode::ResourceNotFound,
                        "The selected consolidation strategy requires the named authored normal property.",
                        elementCount);
                }
            }
            else if (authoredNormalsRequired)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::ResourceNotFound,
                    "The selected consolidation strategy requires an authored normal property reference.",
                    elementCount);
            }

            if (!CanWriteVec3Property(*properties, refs.OutputPositions) ||
                (refs.OutputNormals.has_value() &&
                 !CanWriteVec3Property(*properties, *refs.OutputNormals)))
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::TypeMismatch,
                    "A selected output property already exists with a non-vec3 value type.",
                    elementCount);
            }

            const std::size_t outputCount = config.TargetPointCount == 0u
                ? elementCount
                : static_cast<std::size_t>(config.TargetPointCount);
            const bool cardinalityChanging = outputCount != elementCount;
            if (config.Strategy != PointCloudConsolidationStrategy::Ear &&
                outputCount > elementCount)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "LOP, WLOP, and CLOP are downsample-only; only EAR permits a larger target count.",
                    elementCount,
                    cardinalityChanging);
            }
            if (cardinalityChanging &&
                refs.InputPositions.Domain !=
                    GeometryElementDomain::PointCloudPoint)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "Point-set consolidation cannot change the cardinality of a topology-bearing element domain; use a cardinality-preserving target or a separate topology-editing operation.",
                    elementCount,
                    true);
            }
            if (cardinalityChanging &&
                refs.OutputPositions.Name != GS::PropertyNames::kPosition)
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::InvalidArgument,
                    "Count-changing point-cloud consolidation must publish the canonical v:position property.",
                    elementCount,
                    true);
            }
            if (cardinalityChanging &&
                !SupportsExactSnapshot(*properties))
            {
                return UnavailableConsolidation(
                    Core::ErrorCode::TypeMismatch,
                    "Count-changing point-cloud consolidation requires an exactly restorable property set.",
                    elementCount,
                    true);
            }

            return PointCloudConsolidationAvailability{
                .Available = true,
                .InputPointCount = elementCount,
                .CardinalityChanging = cardinalityChanging,
                .Error = Core::ErrorCode::Success,
                .Message = "Point-set consolidation input and publication properties are available.",
            };
        }

        void AppendUniquePropertyName(
            std::vector<std::string>& names,
            const GeometryPropertyRef& ref)
        {
            if (std::find(names.begin(), names.end(), ref.Name) == names.end())
                names.push_back(ref.Name);
        }

        [[nodiscard]] ElementDomainPropertyState CapturePropertyState(
            const Geometry::PropertySet& properties,
            const GeometryElementDomain domain,
            const std::vector<std::string>& names)
        {
            ElementDomainPropertyState state{
                .Domain = domain,
                .ElementCount = properties.Size(),
            };
            state.Properties.reserve(names.size());
            for (const std::string& name : names)
            {
                const auto property = properties.Get<glm::vec3>(name);
                state.Properties.push_back(Vec3PropertySnapshot{
                    .Name = name,
                    .Exists = static_cast<bool>(property),
                    .Values = property
                        ? property.Vector()
                        : std::vector<glm::vec3>{},
                });
            }
            return state;
        }

        [[nodiscard]] std::vector<std::string> TrackedSourcePropertyNames(
            const PointCloudConsolidationPropertyRefs& refs)
        {
            std::vector<std::string> names{};
            names.reserve(4u);
            AppendUniquePropertyName(names, refs.InputPositions);
            if (refs.InputNormals.has_value())
                AppendUniquePropertyName(names, *refs.InputNormals);
            AppendUniquePropertyName(names, refs.OutputPositions);
            if (refs.OutputNormals.has_value())
                AppendUniquePropertyName(names, *refs.OutputNormals);
            return names;
        }

        [[nodiscard]] std::vector<std::string> OutputPropertyNames(
            const PointCloudConsolidationPropertyRefs& refs)
        {
            std::vector<std::string> names{};
            names.reserve(2u);
            AppendUniquePropertyName(names, refs.OutputPositions);
            if (refs.OutputNormals.has_value())
                AppendUniquePropertyName(names, *refs.OutputNormals);
            return names;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationSnapshot>
        TryBuildSnapshot(
            ECS::Scene::Registry& scene,
            const WorldHandle world,
            const CommandCorrelationId correlation,
            const PointCloudConsolidationRequest& request,
            PointCloudConsolidationResult& failure)
        {
            const std::optional<Consolidation::Params> params =
                BuildParams(request.Config);
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
            {
                failure = MakeCompletion(
                    request,
                    world,
                    correlation,
                    PointCloudConsolidationRunStatus::StaleEntity,
                    Core::ErrorCode::ResourceNotFound,
                    "Point-set consolidation target is stale or no longer live.");
                return std::nullopt;
            }

            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            const GeometryEntityAvailability geometryAvailability =
                BuildGeometryAvailability(view);
            const PointCloudConsolidationAvailability sourceAvailability =
                InspectConsolidationSource(
                    geometryAvailability,
                    request.Properties,
                    request.Config);
            if (!sourceAvailability.Available || !params.has_value())
            {
                failure = MakeCompletion(
                    request,
                    world,
                    correlation,
                    params.has_value()
                        ? PointCloudConsolidationRunStatus::
                              UnsupportedPropertySource
                        : PointCloudConsolidationRunStatus::
                              InvalidProcessingParameters,
                    sourceAvailability.Error,
                    sourceAvailability.Message);
                return std::nullopt;
            }

            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(
                    geometryAvailability,
                    request.Properties.InputPositions.Domain);
            if (properties == nullptr)
                return std::nullopt;

            const auto positions = properties->Get<glm::vec3>(
                request.Properties.InputPositions.Name);
            if (!positions)
                return std::nullopt;

            std::vector<glm::vec3> normals{};
            if (request.Properties.InputNormals.has_value())
            {
                const auto normalProperty = properties->Get<glm::vec3>(
                    request.Properties.InputNormals->Name);
                if (normalProperty)
                    normals = normalProperty.Vector();
            }

            const GeometryElementDomain domain =
                request.Properties.InputPositions.Domain;
            const std::vector<std::string> trackedNames =
                TrackedSourcePropertyNames(request.Properties);
            const std::vector<std::string> outputNames =
                OutputPropertyNames(request.Properties);

            std::optional<GS::Vertices> replacementBefore{};
            if (sourceAvailability.CardinalityChanging &&
                view.VertexSource != nullptr)
            {
                replacementBefore = *view.VertexSource;
            }

            return PointCloudConsolidationSnapshot{
                .Request = request,
                .World = world,
                .Correlation = correlation,
                .SourceState = CapturePropertyState(
                    *properties, domain, trackedNames),
                .BeforeOutputs = CapturePropertyState(
                    *properties, domain, outputNames),
                .PointCloudReplacementBefore =
                    std::move(replacementBefore),
                .AllowsPointCloudReplacement =
                    sourceAvailability.CardinalityChanging,
                .Positions = positions.Vector(),
                .Normals = std::move(normals),
                .Params = *params,
            };
        }

        [[nodiscard]] std::optional<ElementDomainPropertyState>
        BuildPublishedOutputs(
            const PointCloudConsolidationSnapshot& snapshot,
            const Consolidation::Result& consolidated)
        {
            if (consolidated.Positions.empty() ||
                consolidated.Positions.size() !=
                    snapshot.SourceState.ElementCount ||
                (!consolidated.Normals.empty() &&
                 consolidated.Normals.size() !=
                     consolidated.Positions.size()))
            {
                return std::nullopt;
            }

            ElementDomainPropertyState state{
                .Domain = snapshot.SourceState.Domain,
                .ElementCount = consolidated.Positions.size(),
            };
            state.Properties.push_back(Vec3PropertySnapshot{
                .Name = snapshot.Request.Properties.OutputPositions.Name,
                .Exists = true,
                .Values = consolidated.Positions,
            });
            if (!consolidated.Normals.empty() &&
                snapshot.Request.Properties.OutputNormals.has_value())
            {
                state.Properties.push_back(Vec3PropertySnapshot{
                    .Name =
                        snapshot.Request.Properties.OutputNormals->Name,
                    .Exists = true,
                    .Values = consolidated.Normals,
                });
            }
            return state;
        }

        [[nodiscard]] std::optional<GS::Vertices>
        BuildPointCloudReplacement(
            const PointCloudConsolidationSnapshot& snapshot,
            const Consolidation::Result& consolidated)
        {
            if (!snapshot.AllowsPointCloudReplacement ||
                snapshot.SourceState.Domain !=
                    GeometryElementDomain::PointCloudPoint ||
                consolidated.Positions.empty() ||
                snapshot.Request.Properties.OutputPositions.Name !=
                    GS::PropertyNames::kPosition ||
                (!consolidated.Normals.empty() &&
                 consolidated.Normals.size() !=
                     consolidated.Positions.size()))
            {
                return std::nullopt;
            }

            GS::Vertices replacement{};
            replacement.Properties.Resize(consolidated.Positions.size());
            auto positions = replacement.Properties.GetOrAdd<glm::vec3>(
                std::string{GS::PropertyNames::kPosition},
                glm::vec3{0.0f});
            if (!positions)
                return std::nullopt;
            positions.Vector() = consolidated.Positions;

            if (!consolidated.Normals.empty() &&
                snapshot.Request.Properties.OutputNormals.has_value())
            {
                auto normals = replacement.Properties.GetOrAdd<glm::vec3>(
                    snapshot.Request.Properties.OutputNormals->Name,
                    glm::vec3{0.0f});
                if (!normals)
                    return std::nullopt;
                normals.Vector() = consolidated.Normals;
            }
            return replacement;
        }

        [[nodiscard]] ElementDomainPropertyState SelectPropertyState(
            const ElementDomainPropertyState& source,
            const ElementDomainPropertyState& selection)
        {
            ElementDomainPropertyState selected{
                .Domain = source.Domain,
                .ElementCount = source.ElementCount,
            };
            selected.Properties.reserve(selection.Properties.size());
            for (const Vec3PropertySnapshot& requested :
                 selection.Properties)
            {
                const auto found = std::find_if(
                    source.Properties.begin(),
                    source.Properties.end(),
                    [&requested](const Vec3PropertySnapshot& candidate)
                    {
                        return candidate.Name == requested.Name;
                    });
                if (found != source.Properties.end())
                    selected.Properties.push_back(*found);
            }
            return selected;
        }

        void CopyDiagnostics(
            PointCloudConsolidationResult& completion,
            const Consolidation::Result& consolidated)
        {
            completion.ImplementationId =
                std::string{consolidated.Diagnostics.Implementation};
            completion.StrategyToken =
                std::string{Consolidation::DebugName(
                    consolidated.Diagnostics.Strategy)};
            completion.GeometryStatus = consolidated.State;
            completion.InputPointCount = static_cast<std::uint32_t>(
                consolidated.Diagnostics.InputPointCount);
            completion.OutputPointCount = static_cast<std::uint32_t>(
                consolidated.Diagnostics.OutputPointCount);
            completion.Iterations = consolidated.Diagnostics.Iterations;
            completion.Converged = consolidated.Diagnostics.Converged;
            completion.AverageDisplacement =
                consolidated.Diagnostics.AverageDisplacement;
            completion.MaxDisplacement =
                consolidated.Diagnostics.MaxDisplacement;
            completion.UsedAuthoredNormals =
                consolidated.Diagnostics.UsedAuthoredNormals;
            completion.EstimatedNormals =
                consolidated.Diagnostics.EstimatedNormals;
            completion.NormalRefinementIterations =
                consolidated.Diagnostics.NormalRefinementIterations;
            completion.InsertedPointCount = static_cast<std::uint32_t>(
                consolidated.Diagnostics.InsertedPointCount);
        }

        [[nodiscard]] PointCloudConsolidationJobResult RunWorker(
            PointCloudConsolidationSnapshot snapshot,
            const JobCancellation& cancellation)
        {
            if (cancellation.IsCancelled())
            {
                PointCloudConsolidationJobResult cancelled{};
                cancelled.Snapshot = std::move(snapshot);
                cancelled.Completion = MakeCompletion(
                    cancelled.Snapshot.Request,
                    cancelled.Snapshot.World,
                    cancelled.Snapshot.Correlation,
                    PointCloudConsolidationRunStatus::Cancelled,
                    Core::ErrorCode::InvalidState,
                    "Point-set consolidation was cancelled before execution.");
                return cancelled;
            }

            Consolidation::Result consolidated = snapshot.Normals.empty()
                ? Consolidation::Consolidate(
                      std::span<const glm::vec3>{snapshot.Positions},
                      snapshot.Params)
                : Consolidation::Consolidate(
                      std::span<const glm::vec3>{snapshot.Positions},
                      std::span<const glm::vec3>{snapshot.Normals},
                      snapshot.Params);

            PointCloudConsolidationJobResult result{};
            result.Snapshot = std::move(snapshot);
            result.Completion = MakeCompletion(
                result.Snapshot.Request,
                result.Snapshot.World,
                result.Snapshot.Correlation,
                consolidated.Succeeded()
                    ? PointCloudConsolidationRunStatus::Applied
                    : PointCloudConsolidationRunStatus::
                          GeometryProcessingFailed,
                consolidated.Succeeded()
                    ? Core::ErrorCode::Success
                    : Core::ErrorCode::InvalidState,
                consolidated.Succeeded()
                    ? std::string{}
                    : "Geometry.PointCloud.Consolidation failed with status " +
                          std::string{Consolidation::DebugName(
                              consolidated.State)} +
                          ".");
            CopyDiagnostics(result.Completion, consolidated);

            if (!consolidated.Succeeded())
                return result;
            if (cancellation.IsCancelled())
            {
                result.Completion.Status =
                    PointCloudConsolidationRunStatus::Cancelled;
                result.Completion.Error = Core::ErrorCode::InvalidState;
                result.Completion.Message =
                    "Point-set consolidation was cancelled before publication.";
                return result;
            }

            const bool cardinalityPreserved =
                consolidated.Positions.size() ==
                result.Snapshot.SourceState.ElementCount;
            if (cardinalityPreserved)
            {
                result.AfterOutputs = BuildPublishedOutputs(
                    result.Snapshot, consolidated);
            }
            else
            {
                result.PointCloudReplacementAfter =
                    BuildPointCloudReplacement(
                        result.Snapshot, consolidated);
            }
            if ((cardinalityPreserved &&
                 !result.AfterOutputs.has_value()) ||
                (!cardinalityPreserved &&
                 !result.PointCloudReplacementAfter.has_value()))
            {
                result.Completion.Status =
                    PointCloudConsolidationRunStatus::
                        GeometryProcessingFailed;
                result.Completion.Error = Core::ErrorCode::TypeMismatch;
                result.Completion.Message =
                    "Consolidated positions could not be published to the selected element-domain properties without violating cardinality or type constraints.";
            }
            return result;
        }

        struct ConsolidationMutationIdentity
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            GeometryElementDomain Domain{GeometryElementDomain::Unknown};
            PointCloudConsolidationPropertyRefs Properties{};
        };

        using ElementDomainPropertySnapshot =
            std::shared_ptr<const ElementDomainPropertyState>;
        using PointCloudVerticesSnapshot =
            std::shared_ptr<const GS::Vertices>;

        [[nodiscard]] Geometry::PropertySet* ResolveMutablePropertySet(
            GS::MutableSourceView& view,
            const GeometryElementDomain domain) noexcept
        {
            const GS::SourceAvailability sources =
                GS::BuildSourceAvailability(view);
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                        view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case GeometryElementDomain::MeshEdge:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                        view.EdgeSource != nullptr
                    ? &view.EdgeSource->Properties
                    : nullptr;
            case GeometryElementDomain::MeshHalfedge:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                        view.HalfedgeSource != nullptr
                    ? &view.HalfedgeSource->Properties
                    : nullptr;
            case GeometryElementDomain::MeshFace:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                        view.FaceSource != nullptr
                    ? &view.FaceSource->Properties
                    : nullptr;
            case GeometryElementDomain::GraphNode:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                        view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case GeometryElementDomain::GraphHalfedge:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                        view.HalfedgeSource != nullptr
                    ? &view.HalfedgeSource->Properties
                    : nullptr;
            case GeometryElementDomain::GraphEdge:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                        view.EdgeSource != nullptr
                    ? &view.EdgeSource->Properties
                    : nullptr;
            case GeometryElementDomain::PointCloudPoint:
                return sources.ProvenanceDomain == GS::Domain::PointCloud &&
                        view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case GeometryElementDomain::Unknown:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] bool SamePropertyState(
            const Geometry::PropertySet& current,
            const ElementDomainPropertyState& expected)
        {
            if (current.Size() != expected.ElementCount)
                return false;
            for (const Vec3PropertySnapshot& snapshot : expected.Properties)
            {
                const auto property = current.Get<glm::vec3>(snapshot.Name);
                if (static_cast<bool>(property) != snapshot.Exists)
                    return false;
                if (!snapshot.Exists)
                    continue;
                if (property.Vector().size() != snapshot.Values.size())
                    return false;
                for (std::size_t index = 0u;
                     index < snapshot.Values.size();
                     ++index)
                {
                    if (!SameValue(property[index], snapshot.Values[index]))
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ApplyPropertyState(
            Geometry::PropertySet& current,
            const ElementDomainPropertyState& target)
        {
            if (current.Size() != target.ElementCount)
                return false;

            Geometry::PropertySet staged = current;
            for (const Vec3PropertySnapshot& snapshot : target.Properties)
            {
                if (snapshot.Exists)
                {
                    if (snapshot.Values.size() != target.ElementCount ||
                        (staged.Exists(snapshot.Name) &&
                         DetectGeometryPropertyValueKind(
                             staged, snapshot.Name) !=
                             Geometry::PropertyValueKind::Vec3))
                    {
                        return false;
                    }
                    auto property = staged.GetOrAdd<glm::vec3>(
                        snapshot.Name, glm::vec3{0.0f});
                    if (!property)
                        return false;
                    property.Vector() = snapshot.Values;
                }
                else if (staged.Exists(snapshot.Name))
                {
                    auto property = staged.Get<glm::vec3>(snapshot.Name);
                    if (!property)
                        return false;
                    staged.Remove(property);
                }
            }
            current = std::move(staged);
            return true;
        }

        void StampGeometryMutation(
            const ConsolidationMutationIdentity& identity)
        {
            if (identity.Scene == nullptr)
                return;
            entt::registry& raw = identity.Scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, identity.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return;

            Dirty::MarkGpuDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            const bool vertexLikeDomain =
                identity.Domain == GeometryElementDomain::MeshVertex ||
                identity.Domain == GeometryElementDomain::GraphNode ||
                identity.Domain == GeometryElementDomain::PointCloudPoint;
            if (vertexLikeDomain &&
                identity.Properties.OutputPositions.Name ==
                    GS::PropertyNames::kPosition)
            {
                Dirty::MarkVertexPositionsDirty(raw, entity);
            }
            if (vertexLikeDomain &&
                identity.Properties.OutputNormals.has_value() &&
                identity.Properties.OutputNormals->Name ==
                    GS::PropertyNames::kNormal)
            {
                Dirty::MarkVertexNormalsDirty(raw, entity);
            }
        }

        [[nodiscard]] EditorCommandHistoryStatus CommitPropertyOutputs(
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            EditorCommandHistory* history,
            const std::uint32_t stableEntityId,
            const PointCloudConsolidationPropertyRefs& properties,
            const ElementDomainPropertyState& before,
            const ElementDomainPropertyState& after)
        {
            const ConsolidationMutationIdentity identity{
                .Scene = scene,
                .World = world,
                .StableEntityId = stableEntityId,
                .Domain = before.Domain,
                .Properties = properties,
            };
            const ElementDomainPropertySnapshot beforeState =
                std::make_shared<ElementDomainPropertyState>(before);
            const ElementDomainPropertySnapshot afterState =
                std::make_shared<ElementDomainPropertyState>(after);

            const auto validate = [](
                const ConsolidationMutationIdentity& candidate,
                const ElementDomainPropertySnapshot& expected,
                const ElementDomainPropertySnapshot& target)
            {
                if (candidate.Scene == nullptr ||
                    !candidate.World.IsValid())
                {
                    return EditorCommandHistoryStatus::MissingScene;
                }
                if (expected == nullptr || target == nullptr)
                    return EditorCommandHistoryStatus::CommandFailed;
                entt::registry& raw = candidate.Scene->Raw();
                const ECS::EntityHandle entity =
                    ResolveEntity(raw, candidate.StableEntityId);
                if (entity == ECS::InvalidEntityHandle)
                    return EditorCommandHistoryStatus::StaleEntity;
                GS::MutableSourceView view =
                    GS::BuildMutableView(raw, entity);
                Geometry::PropertySet* current =
                    ResolveMutablePropertySet(view, candidate.Domain);
                if (current == nullptr)
                    return EditorCommandHistoryStatus::UnsupportedOperation;
                return SamePropertyState(*current, *expected)
                    ? EditorCommandHistoryStatus::Applied
                    : EditorCommandHistoryStatus::StaleEntity;
            };
            const auto apply = [](
                const ConsolidationMutationIdentity& candidate,
                const ElementDomainPropertySnapshot& target)
            {
                if (candidate.Scene == nullptr || target == nullptr)
                    return EditorCommandHistoryStatus::CommandFailed;
                entt::registry& raw = candidate.Scene->Raw();
                const ECS::EntityHandle entity =
                    ResolveEntity(raw, candidate.StableEntityId);
                if (entity == ECS::InvalidEntityHandle)
                    return EditorCommandHistoryStatus::StaleEntity;
                GS::MutableSourceView view =
                    GS::BuildMutableView(raw, entity);
                Geometry::PropertySet* current =
                    ResolveMutablePropertySet(view, candidate.Domain);
                if (current == nullptr)
                    return EditorCommandHistoryStatus::UnsupportedOperation;
                return ApplyPropertyState(*current, *target)
                    ? EditorCommandHistoryStatus::Applied
                    : EditorCommandHistoryStatus::CommandFailed;
            };
            const auto stamp = [](
                const ConsolidationMutationIdentity& candidate,
                const ElementDomainPropertySnapshot&,
                const ElementDomainPropertySnapshot& target)
            {
                StampGeometryMutation(candidate);
                return target;
            };

            const std::string label = before.Domain ==
                    GeometryElementDomain::PointCloudPoint
                ? "Consolidate point cloud"
                : "Consolidate point set";
            if (history != nullptr)
            {
                return Internal::ExecuteUndoableEntityMutation(
                           *history,
                           label,
                           identity,
                           beforeState,
                           beforeState,
                           afterState,
                           validate,
                           apply,
                           stamp)
                    .Status;
            }

            const EditorCommandHistoryStatus validation =
                validate(identity, beforeState, afterState);
            if (validation != EditorCommandHistoryStatus::Applied)
                return validation;
            const EditorCommandHistoryStatus applied =
                apply(identity, afterState);
            if (applied == EditorCommandHistoryStatus::Applied)
                (void)stamp(identity, beforeState, afterState);
            return applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPointCloudVertices(
            const ConsolidationMutationIdentity& identity,
            const GS::Vertices& vertices)
        {
            if (identity.Scene == nullptr || !identity.World.IsValid())
                return EditorCommandHistoryStatus::MissingScene;
            entt::registry& raw = identity.Scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, identity.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;
            GS::MutableSourceView view = GS::BuildMutableView(raw, entity);
            if (ResolveMutablePropertySet(
                    view, GeometryElementDomain::PointCloudPoint) == nullptr ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }
            *view.VertexSource = vertices;
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus
        CommitPointCloudReplacement(
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            EditorCommandHistory* history,
            const std::uint32_t stableEntityId,
            const PointCloudConsolidationPropertyRefs& properties,
            const GS::Vertices& before,
            const GS::Vertices& after)
        {
            const ConsolidationMutationIdentity identity{
                .Scene = scene,
                .World = world,
                .StableEntityId = stableEntityId,
                .Domain = GeometryElementDomain::PointCloudPoint,
                .Properties = properties,
            };
            const PointCloudVerticesSnapshot beforeState =
                std::make_shared<GS::Vertices>(before);
            const PointCloudVerticesSnapshot afterState =
                std::make_shared<GS::Vertices>(after);

            const auto validate = [](
                const ConsolidationMutationIdentity& candidate,
                const PointCloudVerticesSnapshot& expected,
                const PointCloudVerticesSnapshot& target)
            {
                if (candidate.Scene == nullptr ||
                    !candidate.World.IsValid())
                {
                    return EditorCommandHistoryStatus::MissingScene;
                }
                if (expected == nullptr || target == nullptr)
                    return EditorCommandHistoryStatus::CommandFailed;
                entt::registry& raw = candidate.Scene->Raw();
                const ECS::EntityHandle entity =
                    ResolveEntity(raw, candidate.StableEntityId);
                if (entity == ECS::InvalidEntityHandle)
                    return EditorCommandHistoryStatus::StaleEntity;
                const GS::ConstSourceView view =
                    GS::BuildConstView(raw, entity);
                if (!view.Valid() ||
                    view.ActiveDomain != GS::Domain::PointCloud ||
                    view.VertexSource == nullptr)
                {
                    return EditorCommandHistoryStatus::UnsupportedOperation;
                }
                return SameVertices(*view.VertexSource, *expected)
                    ? EditorCommandHistoryStatus::Applied
                    : EditorCommandHistoryStatus::StaleEntity;
            };
            const auto apply = [](
                const ConsolidationMutationIdentity& candidate,
                const PointCloudVerticesSnapshot& target)
            {
                return target != nullptr
                    ? ApplyPointCloudVertices(candidate, *target)
                    : EditorCommandHistoryStatus::CommandFailed;
            };
            const auto stamp = [](
                const ConsolidationMutationIdentity& candidate,
                const PointCloudVerticesSnapshot&,
                const PointCloudVerticesSnapshot& target)
            {
                StampGeometryMutation(candidate);
                return target;
            };

            if (history != nullptr)
            {
                return Internal::ExecuteUndoableEntityMutation(
                           *history,
                           "Consolidate point cloud",
                           identity,
                           beforeState,
                           beforeState,
                           afterState,
                           validate,
                           apply,
                           stamp)
                    .Status;
            }

            const EditorCommandHistoryStatus validation =
                validate(identity, beforeState, afterState);
            if (validation != EditorCommandHistoryStatus::Applied)
                return validation;
            const EditorCommandHistoryStatus applied =
                apply(identity, afterState);
            if (applied == EditorCommandHistoryStatus::Applied)
                (void)stamp(identity, beforeState, afterState);
            return applied;
        }

        void HandleJobCompleted(
            const PointCloudConsolidationJobCompleted& event,
            WorldRegistry* worlds,
            KernelEventBus* events,
            EditorCommandHistory* history,
            PointCloudConsolidationModuleStats& stats)
        {
            stats.CompletionEvents += 1u;
            const PointCloudConsolidationJobResult& job = event.Result;
            if (!job.Completion.Succeeded())
            {
                stats.CommitsDropped += 1u;
                PublishCompletion(events, job.Completion);
                return;
            }

            if (worlds == nullptr || !job.Snapshot.World.IsValid() ||
                !worlds->Contains(job.Snapshot.World) ||
                worlds->ActiveWorld() != job.Snapshot.World)
            {
                stats.CommitsDropped += 1u;
                PointCloudConsolidationResult dropped = job.Completion;
                dropped.Status = PointCloudConsolidationRunStatus::StaleWorld;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Point-set consolidation result was dropped because its world is no longer active.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            ECS::Scene::Registry* scene = worlds->Get(job.Snapshot.World);
            const bool hasPropertyPublication =
                job.AfterOutputs.has_value();
            const bool hasPointCloudReplacement =
                job.PointCloudReplacementAfter.has_value();
            if (scene == nullptr ||
                hasPropertyPublication == hasPointCloudReplacement)
            {
                stats.CommitsDropped += 1u;
                PointCloudConsolidationResult dropped = job.Completion;
                dropped.Status =
                    PointCloudConsolidationRunStatus::MissingScene;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Scene registry or the typed consolidation publication payload is unavailable.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity = ResolveEntity(
                raw, job.Snapshot.Request.StableEntityId);
            const GS::ConstSourceView view = entity != ECS::InvalidEntityHandle
                ? GS::BuildConstView(raw, entity)
                : GS::ConstSourceView{};
            const GeometryEntityAvailability geometryAvailability =
                BuildGeometryAvailability(view);
            const Geometry::PropertySet* currentProperties =
                ResolveGeometryPropertySet(
                    geometryAvailability,
                    job.Snapshot.SourceState.Domain);
            const bool sourceUnchanged = hasPointCloudReplacement
                ? view.VertexSource != nullptr &&
                      job.Snapshot.PointCloudReplacementBefore.has_value() &&
                      SameVertices(
                          *view.VertexSource,
                          *job.Snapshot.PointCloudReplacementBefore)
                : currentProperties != nullptr &&
                      SamePropertyState(
                          *currentProperties,
                          job.Snapshot.SourceState);
            if (entity == ECS::InvalidEntityHandle || !view.Valid() ||
                !sourceUnchanged)
            {
                stats.CommitsDropped += 1u;
                PointCloudConsolidationResult dropped = job.Completion;
                dropped.Status = entity == ECS::InvalidEntityHandle
                    ? PointCloudConsolidationRunStatus::StaleEntity
                    : PointCloudConsolidationRunStatus::StaleSource;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Point-set consolidation result was dropped because its selected element-domain properties changed before commit.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            EditorCommandHistoryStatus commit =
                EditorCommandHistoryStatus::CommandFailed;
            if (hasPropertyPublication)
            {
                const ElementDomainPropertyState beforeOutputs =
                    SelectPropertyState(
                        job.Snapshot.BeforeOutputs,
                        *job.AfterOutputs);
                commit = CommitPropertyOutputs(
                    scene,
                    job.Snapshot.World,
                    history,
                    job.Snapshot.Request.StableEntityId,
                    job.Snapshot.Request.Properties,
                    beforeOutputs,
                    *job.AfterOutputs);
            }
            else
            {
                commit = CommitPointCloudReplacement(
                    scene,
                    job.Snapshot.World,
                    history,
                    job.Snapshot.Request.StableEntityId,
                    job.Snapshot.Request.Properties,
                    *job.Snapshot.PointCloudReplacementBefore,
                    *job.PointCloudReplacementAfter);
            }
            if (commit != EditorCommandHistoryStatus::Applied)
            {
                stats.CommitsDropped += 1u;
                PointCloudConsolidationResult dropped = job.Completion;
                dropped.Status = commit ==
                        EditorCommandHistoryStatus::StaleEntity
                    ? PointCloudConsolidationRunStatus::StaleSource
                    : PointCloudConsolidationRunStatus::
                          GeometryProcessingFailed;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Point-set consolidation publication failed during the editor mutation transaction.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            PointCloudConsolidationResult completed = job.Completion;
            completed.Message = "Point-set consolidation (" +
                completed.StrategyToken + ", " +
                completed.ImplementationId + ") applied " +
                std::to_string(completed.OutputPointCount) +
                " samples to " +
                std::string{ToString(
                    completed.Properties.OutputPositions.Domain)} +
                ":" + completed.Properties.OutputPositions.Name +
                " after " + std::to_string(completed.Iterations) +
                " iterations.";
            stats.ResultsCommitted += 1u;
            PublishCompletion(events, std::move(completed));
        }

        [[nodiscard]] CommandOutcome SubmitSnapshot(
            JobService& jobs,
            KernelEventBus* events,
            PointCloudConsolidationSnapshot snapshot,
            PointCloudConsolidationModuleStats& stats)
        {
            const PointCloudConsolidationRequest request = snapshot.Request;
            const WorldHandle world = snapshot.World;
            const CommandCorrelationId correlation = snapshot.Correlation;
            JobDesc job = MakeCpuJobDesc<PointCloudConsolidationJobResult>(
                "Runtime.PointCloudConsolidation.CPU",
                world,
                [snapshot = std::move(snapshot)](
                    const JobCancellation& cancellation) mutable
                {
                    return RunWorker(std::move(snapshot), cancellation);
                },
                [](const PointCloudConsolidationJobResult& result)
                {
                    return PointCloudConsolidationJobCompleted{
                        .Result = result};
                });
            job.FinalizeUnpublishedOnMainThread =
                [events, request, world, correlation]() mutable
                {
                    PublishCompletion(
                        events,
                        MakeCompletion(
                            request,
                            world,
                            correlation,
                            PointCloudConsolidationRunStatus::Cancelled,
                            Core::ErrorCode::InvalidState,
                            "Point-set consolidation was cancelled before its result could be committed."));
                };
            const JobToken token = jobs.Submit(std::move(job));
            if (!token.IsValid())
            {
                stats.JobSubmissionFailures += 1u;
                PublishCompletion(
                    events,
                    MakeCompletion(
                        request,
                        world,
                        correlation,
                        PointCloudConsolidationRunStatus::
                            GeometryProcessingFailed,
                        Core::ErrorCode::InvalidState,
                        "Point-set consolidation job submission was rejected by JobService."));
                return CommandOutcome::Fail(
                    "Point-set consolidation job submission was rejected by JobService.");
            }
            stats.JobsSubmitted += 1u;
            return CommandOutcome::Ok();
        }

        [[nodiscard]] CommandOutcome HandleRunCommand(
            CommandContext& context,
            const PointCloudConsolidationRequest& request,
            PointCloudConsolidationModuleStats& stats)
        {
            stats.CommandsHandled += 1u;
            if (context.Jobs == nullptr || context.Worlds == nullptr ||
                context.Events == nullptr)
            {
                return CommandOutcome::Fail(
                    "Point-set consolidation requires JobService, WorldRegistry, and KernelEventBus.");
            }

            const WorldHandle world = context.Worlds->ActiveWorld();
            if (!world.IsValid() ||
                context.Worlds->Get(world) != &context.ActiveWorld)
            {
                PublishCompletion(
                    context.Events,
                    MakeCompletion(
                        request,
                        world,
                        context.Correlation,
                        PointCloudConsolidationRunStatus::MissingScene,
                        Core::ErrorCode::InvalidState,
                        "Active world is unavailable for point-set consolidation."));
                return CommandOutcome::Fail("Active world is unavailable.");
            }

            PointCloudConsolidationResult failure{};
            std::optional<PointCloudConsolidationSnapshot> snapshot =
                TryBuildSnapshot(
                    context.ActiveWorld,
                    world,
                    context.Correlation,
                    request,
                    failure);
            if (!snapshot.has_value())
            {
                std::string message = failure.Message;
                PublishCompletion(context.Events, std::move(failure));
                return CommandOutcome::Fail(std::move(message));
            }
            return SubmitSnapshot(
                *context.Jobs,
                context.Events,
                std::move(*snapshot),
                stats);
        }
    }

    PointCloudConsolidationPropertyRefs
    MakePointCloudConsolidationPropertyRefs(
        const GeometryElementDomain domain,
        std::string positionPropertyName,
        std::optional<std::string> normalPropertyName)
    {
        PointCloudConsolidationPropertyRefs refs{
            .InputPositions = GeometryPropertyRef{
                .Domain = domain,
                .Name = positionPropertyName,
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
            .OutputPositions = GeometryPropertyRef{
                .Domain = domain,
                .Name = std::move(positionPropertyName),
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
        };
        if (normalPropertyName.has_value())
        {
            refs.InputNormals = GeometryPropertyRef{
                .Domain = domain,
                .Name = *normalPropertyName,
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            };
            refs.OutputNormals = GeometryPropertyRef{
                .Domain = domain,
                .Name = std::move(*normalPropertyName),
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            };
        }
        else
        {
            refs.InputNormals.reset();
            refs.OutputNormals.reset();
        }
        return refs;
    }

    PointCloudConsolidationAvailability
    ResolvePointCloudConsolidationAvailability(
        const GeometryEntityAvailability& availability,
        const PointCloudConsolidationPropertyRefs& properties,
        const PointCloudConsolidationConfig& config)
    {
        return InspectConsolidationSource(
            availability, properties, config);
    }

    std::string_view ToString(
        const PointCloudConsolidationRunStatus status) noexcept
    {
        switch (status)
        {
        case PointCloudConsolidationRunStatus::Queued: return "Queued";
        case PointCloudConsolidationRunStatus::Applied: return "Applied";
        case PointCloudConsolidationRunStatus::MissingScene:
            return "MissingScene";
        case PointCloudConsolidationRunStatus::InvalidProcessingParameters:
            return "InvalidProcessingParameters";
        case PointCloudConsolidationRunStatus::StaleEntity:
            return "StaleEntity";
        case PointCloudConsolidationRunStatus::UnsupportedPropertySource:
            return "UnsupportedPropertySource";
        case PointCloudConsolidationRunStatus::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        case PointCloudConsolidationRunStatus::Cancelled: return "Cancelled";
        case PointCloudConsolidationRunStatus::StaleSource:
            return "StaleSource";
        case PointCloudConsolidationRunStatus::StaleWorld:
            return "StaleWorld";
        case PointCloudConsolidationRunStatus::ModuleUnavailable:
            return "ModuleUnavailable";
        }
        return "Unknown";
    }

    bool PointCloudConsolidationService::Available() const noexcept
    {
        return m_Commands != nullptr && m_Events != nullptr;
    }

    CommandCorrelationId PointCloudConsolidationService::Run(
        PointCloudConsolidationRequest request)
    {
        return m_Commands != nullptr
            ? m_Commands->Enqueue(std::move(request))
            : CommandCorrelationId{};
    }

    KernelEventSubscription
    PointCloudConsolidationService::SubscribeCompleted(
        std::function<void(const PointCloudConsolidationResult&)> listener)
    {
        return m_Events != nullptr && listener
            ? m_Events->Subscribe<PointCloudConsolidationResult>(
                  std::move(listener))
            : KernelEventSubscription{};
    }

    void PointCloudConsolidationService::Unsubscribe(
        const KernelEventSubscription subscription)
    {
        if (m_Events != nullptr && subscription.IsValid())
            m_Events->Unsubscribe(subscription);
    }

    PointCloudConsolidationModuleStats
    PointCloudConsolidationService::Stats() const noexcept
    {
        return m_Stats != nullptr
            ? *m_Stats
            : PointCloudConsolidationModuleStats{};
    }

    void PointCloudConsolidationService::Bind(
        CommandBus* commands,
        KernelEventBus* events,
        const PointCloudConsolidationModuleStats* stats) noexcept
    {
        m_Commands = commands;
        m_Events = events;
        m_Stats = stats;
    }

    std::string_view PointCloudConsolidationModule::Name() const noexcept
    {
        return "Runtime.PointCloudConsolidationModule";
    }

    Core::Result PointCloudConsolidationModule::OnRegister(EngineSetup& setup)
    {
        m_Events = &setup.Events();
        m_Jobs = &setup.Jobs();
        m_Worlds = &setup.Worlds();
        m_Service.Bind(&setup.Commands(), m_Events, &m_Stats);

        if (Core::Result provided =
                setup.Services().Provide<PointCloudConsolidationService>(
                    m_Service, Name());
            !provided.has_value())
        {
            return provided;
        }

        setup.RegisterCommandHandler<PointCloudConsolidationRequest>(
            [this](
                CommandContext& context,
                const PointCloudConsolidationRequest& request)
            {
                return HandleRunCommand(context, request, m_Stats);
            });
        m_JobCompletedSubscription =
            setup.Subscribe<PointCloudConsolidationJobCompleted>(
                [this](const PointCloudConsolidationJobCompleted& event)
                {
                    HandleJobCompleted(
                        event,
                        m_Worlds,
                        m_Events,
                        m_History,
                        m_Stats);
                });
        return Core::Ok();
    }

    Core::Result PointCloudConsolidationModule::OnResolve(EngineSetup& setup)
    {
        m_History = setup.Services().Find<EditorCommandHistory>();
        return Core::Ok();
    }

    void PointCloudConsolidationModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        if (m_JobCompletedSubscription.IsValid())
            context.Events.Unsubscribe(m_JobCompletedSubscription);
        m_JobCompletedSubscription = {};
        m_Service.Bind(nullptr, nullptr, nullptr);
        m_Events = nullptr;
        m_Jobs = nullptr;
        m_Worlds = nullptr;
        m_History = nullptr;
    }
}
