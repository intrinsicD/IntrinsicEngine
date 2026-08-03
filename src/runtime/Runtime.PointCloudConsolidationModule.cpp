module;

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
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.PointCloud;
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

        struct PointCloudConsolidationSnapshot
        {
            PointCloudConsolidationRequest Request{};
            WorldHandle World{};
            CommandCorrelationId Correlation{};
            GS::Vertices Before{};
            std::vector<glm::vec3> Positions{};
            std::vector<glm::vec3> Normals{};
            Consolidation::Params Params{};
        };

        struct PointCloudConsolidationJobResult
        {
            PointCloudConsolidationSnapshot Snapshot{};
            PointCloudConsolidationResult Completion{};
            std::optional<GS::Vertices> After{};
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

        [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
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
            if (!params.has_value())
            {
                failure = MakeCompletion(
                    request,
                    world,
                    correlation,
                    PointCloudConsolidationRunStatus::
                        InvalidProcessingParameters,
                    Core::ErrorCode::InvalidArgument,
                    "Point-cloud consolidation parameters are outside the validated runtime control surface.");
                return std::nullopt;
            }

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
                    "Point-cloud consolidation target is stale or no longer live.");
                return std::nullopt;
            }

            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            if (!view.Valid() || view.ActiveDomain != GS::Domain::PointCloud ||
                view.VertexSource == nullptr ||
                view.VertexSource->NumDeleted != 0u ||
                !SupportsExactSnapshot(view.VertexSource->Properties))
            {
                failure = MakeCompletion(
                    request,
                    world,
                    correlation,
                    PointCloudConsolidationRunStatus::UnsupportedPointCloud,
                    Core::ErrorCode::InvalidArgument,
                    "Selected entity must expose a contiguous point-cloud GeometrySources vertex domain with supported typed properties.");
                return std::nullopt;
            }

            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(
                GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().size() < 2u ||
                positions.Vector().size() > kMaximumPointCount ||
                positions.Vector().size() !=
                    view.VertexSource->Properties.Size())
            {
                failure = MakeCompletion(
                    request,
                    world,
                    correlation,
                    PointCloudConsolidationRunStatus::UnsupportedPointCloud,
                    Core::ErrorCode::InvalidArgument,
                    "Point-cloud consolidation requires between two and one million canonical positions.");
                return std::nullopt;
            }
            for (const glm::vec3& position : positions.Vector())
            {
                if (!IsFinite(position))
                {
                    failure = MakeCompletion(
                        request,
                        world,
                        correlation,
                        PointCloudConsolidationRunStatus::
                            UnsupportedPointCloud,
                        Core::ErrorCode::InvalidArgument,
                        "Point-cloud consolidation requires finite canonical positions.");
                    return std::nullopt;
                }
            }

            std::vector<glm::vec3> normals{};
            if (view.VertexSource->Properties.Exists(
                    GS::PropertyNames::kNormal))
            {
                const auto normalProperty =
                    view.VertexSource->Properties.Get<glm::vec3>(
                        GS::PropertyNames::kNormal);
                if (!normalProperty ||
                    normalProperty.Vector().size() !=
                        positions.Vector().size())
                {
                    failure = MakeCompletion(
                        request,
                        world,
                        correlation,
                        PointCloudConsolidationRunStatus::
                            UnsupportedPointCloud,
                        Core::ErrorCode::TypeMismatch,
                        "Canonical point-cloud normals must be a count-matched vec3 property.");
                    return std::nullopt;
                }
                normals = normalProperty.Vector();
            }

            return PointCloudConsolidationSnapshot{
                .Request = request,
                .World = world,
                .Correlation = correlation,
                .Before = *view.VertexSource,
                .Positions = positions.Vector(),
                .Normals = std::move(normals),
                .Params = *params,
            };
        }

        [[nodiscard]] std::optional<GS::Vertices> BuildPublishedVertices(
            const Consolidation::Result& consolidated)
        {
            if (consolidated.Positions.empty() ||
                (!consolidated.Normals.empty() &&
                 consolidated.Normals.size() !=
                     consolidated.Positions.size()))
            {
                return std::nullopt;
            }

            Geometry::PointCloud::Cloud cloud{};
            cloud.Reserve(consolidated.Positions.size());
            const bool publishNormals = !consolidated.Normals.empty();
            if (publishNormals)
                cloud.EnableNormals();
            for (std::size_t index = 0u;
                 index < consolidated.Positions.size();
                 ++index)
            {
                const Geometry::VertexHandle point =
                    cloud.AddPoint(consolidated.Positions[index]);
                if (publishNormals)
                    cloud.Normal(point) = consolidated.Normals[index];
            }

            entt::registry staged{};
            const entt::entity stagedEntity = staged.create();
            GS::PopulateFromCloud(staged, stagedEntity, cloud);
            const GS::Vertices* vertices =
                staged.try_get<GS::Vertices>(stagedEntity);
            return vertices != nullptr
                ? std::optional<GS::Vertices>{*vertices}
                : std::nullopt;
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
                    "Point-cloud consolidation was cancelled before execution.");
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
                    "Point-cloud consolidation was cancelled before publication.";
                return result;
            }

            result.After = BuildPublishedVertices(consolidated);
            if (!result.After.has_value())
            {
                result.Completion.Status =
                    PointCloudConsolidationRunStatus::
                        GeometryProcessingFailed;
                result.Completion.Error = Core::ErrorCode::TypeMismatch;
                result.Completion.Message =
                    "Consolidated positions could not be materialized through GeometrySources::PopulateFromCloud.";
            }
            return result;
        }

        struct PointCloudMutationIdentity
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        using PointCloudVerticesSnapshot =
            std::shared_ptr<const GS::Vertices>;

        [[nodiscard]] EditorCommandHistoryStatus ApplyVertices(
            const PointCloudMutationIdentity& identity,
            const GS::Vertices& vertices)
        {
            if (identity.Scene == nullptr || !identity.World.IsValid())
                return EditorCommandHistoryStatus::MissingScene;
            entt::registry& raw = identity.Scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, identity.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            if (!view.Valid() || view.ActiveDomain != GS::Domain::PointCloud)
                return EditorCommandHistoryStatus::UnsupportedOperation;
            raw.emplace_or_replace<GS::Vertices>(entity, vertices);
            return EditorCommandHistoryStatus::Applied;
        }

        void StampGeometryMutation(const PointCloudMutationIdentity& identity)
        {
            if (identity.Scene == nullptr)
                return;
            entt::registry& raw = identity.Scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, identity.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return;
            Dirty::MarkGpuDirty(raw, entity);
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkVertexNormalsDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus CommitVertices(
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            EditorCommandHistory* history,
            const std::uint32_t stableEntityId,
            const GS::Vertices& before,
            const GS::Vertices& after)
        {
            const PointCloudMutationIdentity identity{
                .Scene = scene,
                .World = world,
                .StableEntityId = stableEntityId,
            };
            const PointCloudVerticesSnapshot beforeState =
                std::make_shared<GS::Vertices>(before);
            const PointCloudVerticesSnapshot afterState =
                std::make_shared<GS::Vertices>(after);

            const auto validate = [](
                const PointCloudMutationIdentity& candidate,
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
                const PointCloudMutationIdentity& candidate,
                const PointCloudVerticesSnapshot& target)
            {
                return target != nullptr
                    ? ApplyVertices(candidate, *target)
                    : EditorCommandHistoryStatus::CommandFailed;
            };
            const auto stamp = [](
                const PointCloudMutationIdentity& candidate,
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
                    "Point-cloud consolidation result was dropped because its world is no longer active.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            ECS::Scene::Registry* scene = worlds->Get(job.Snapshot.World);
            if (scene == nullptr || !job.After.has_value())
            {
                stats.CommitsDropped += 1u;
                PointCloudConsolidationResult dropped = job.Completion;
                dropped.Status =
                    PointCloudConsolidationRunStatus::MissingScene;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Scene registry is unavailable for consolidated point-cloud publication.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity = ResolveEntity(
                raw, job.Snapshot.Request.StableEntityId);
            const GS::ConstSourceView view = entity != ECS::InvalidEntityHandle
                ? GS::BuildConstView(raw, entity)
                : GS::ConstSourceView{};
            if (entity == ECS::InvalidEntityHandle || !view.Valid() ||
                view.ActiveDomain != GS::Domain::PointCloud ||
                view.VertexSource == nullptr ||
                !SameVertices(*view.VertexSource, job.Snapshot.Before))
            {
                stats.CommitsDropped += 1u;
                PointCloudConsolidationResult dropped = job.Completion;
                dropped.Status = entity == ECS::InvalidEntityHandle
                    ? PointCloudConsolidationRunStatus::StaleEntity
                    : PointCloudConsolidationRunStatus::StaleSource;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Point-cloud consolidation result was dropped because the source entity changed before commit.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            const EditorCommandHistoryStatus commit = CommitVertices(
                scene,
                job.Snapshot.World,
                history,
                job.Snapshot.Request.StableEntityId,
                job.Snapshot.Before,
                *job.After);
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
                    "Point-cloud consolidation publication failed during the editor mutation transaction.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            PointCloudConsolidationResult completed = job.Completion;
            completed.Message = "Point-cloud consolidation (" +
                completed.StrategyToken + ", " +
                completed.ImplementationId + ") applied " +
                std::to_string(completed.OutputPointCount) +
                " points after " + std::to_string(completed.Iterations) +
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
                            "Point-cloud consolidation was cancelled before its result could be committed."));
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
                        "Point-cloud consolidation job submission was rejected by JobService."));
                return CommandOutcome::Fail(
                    "Point-cloud consolidation job submission was rejected by JobService.");
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
                    "Point-cloud consolidation requires JobService, WorldRegistry, and KernelEventBus.");
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
                        "Active world is unavailable for point-cloud consolidation."));
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
        case PointCloudConsolidationRunStatus::UnsupportedPointCloud:
            return "UnsupportedPointCloud";
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
