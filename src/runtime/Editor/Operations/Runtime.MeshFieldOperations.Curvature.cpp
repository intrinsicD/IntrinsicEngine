// Mesh curvature and curvature segmentation: vertex/face/edge fields published
// onto the mesh that produced them. Topology is never replaced here.
module;
#include <functional>
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshFieldOperations;

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Curvature;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.CurvatureSegmentation;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Features;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut;
import Geometry.Properties;

#include "Config/internal/Runtime.CurvatureSegmentationParams.hpp"

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSupport.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.Properties.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"

namespace Extrinsic::Runtime::MeshFieldDetail
{
    template <typename T>
    [[nodiscard]] bool CaptureCurvatureProperty(
        Geometry::PropertySet& properties,
        const std::string_view name,
        const std::size_t expectedCount,
        bool& hadProperty,
        std::vector<T>& values,
        std::string& diagnostic)
    {
        hadProperty = false;
        values.clear();
        if (!properties.Exists(name))
            return true;

        auto property = properties.Get<T>(name);
        if (!property || property.Vector().size() != expectedCount)
        {
            diagnostic = "existing curvature property has an incompatible type or count: ";
            diagnostic += std::string{name};
            return false;
        }

        hadProperty = true;
        values = property.Vector();
        return true;
    }

    template <typename T>
    [[nodiscard]] bool ApplyCurvatureProperty(
        Geometry::PropertySet& properties,
        const std::string_view name,
        const bool hasProperty,
        const std::vector<T>& values,
        const T& defaultValue)
    {
        if (!hasProperty)
        {
            auto property = properties.Get<T>(name);
            if (property)
            {
                properties.Remove(property);
                return true;
            }
            return !properties.Exists(name);
        }

        auto property =
            properties.GetOrAdd<T>(std::string{name}, defaultValue);
        if (!property || property.Vector().size() != values.size())
            return false;
        property.Vector() = values;
        return true;
    }

    // Geodesics shares these instantiations; other field types are local to this unit.
    template bool CaptureCurvatureProperty<double>(
        Geometry::PropertySet&, std::string_view, std::size_t, bool&,
        std::vector<double>&, std::string&);
    template bool ApplyCurvatureProperty<double>(
        Geometry::PropertySet&, std::string_view, bool,
        const std::vector<double>&, const double&);
    template bool CaptureCurvatureProperty<bool>(
        Geometry::PropertySet&, std::string_view, std::size_t, bool&,
        std::vector<bool>&, std::string&);
    template bool ApplyCurvatureProperty<bool>(
        Geometry::PropertySet&, std::string_view, bool,
        const std::vector<bool>&, const bool&);

    namespace
    {
        // Counts how many published values differ from the ones already
        // stored. A property the previous run did not have counts as changed in
        // every slot, because every value is newly authored.
        template <typename T>
        [[nodiscard]] std::size_t CountChangedValues(
            const bool hadProperty,
            const std::vector<T>& before,
            const std::vector<T>& after) noexcept
        {
            if (!hadProperty || before.size() != after.size())
                return after.size();

            std::size_t changed = 0u;
            for (std::size_t i = 0u; i < after.size(); ++i)
            {
                if (after[i] != before[i])
                    ++changed;
            }
            return changed;
        }
    }

        using namespace GeometryProcessingDetail::MeshSupport;
        namespace Curv = Geometry::Curvature;
        namespace CurvSeg = Geometry::CurvatureSegmentation;
        inline constexpr std::array<EditorMeshCurvatureOutput, 4>
            kMeshCurvatureOutputs{{
                EditorMeshCurvatureOutput::All,
                EditorMeshCurvatureOutput::Mean,
                EditorMeshCurvatureOutput::Gaussian,
                EditorMeshCurvatureOutput::PrincipalDirections,
            }};

        struct MeshCurvaturePropertyState
        {
            MeshCurvatureConfig Bindings{};
            bool HadMean{false};
            bool HadGaussian{false};
            bool HadMinPrincipal{false};
            bool HadMaxPrincipal{false};
            bool HadDir1{false};
            bool HadDir2{false};
            std::vector<double> Mean{};
            std::vector<double> Gaussian{};
            std::vector<double> MinPrincipal{};
            std::vector<double> MaxPrincipal{};
            std::vector<glm::vec3> Dir1{};
            std::vector<glm::vec3> Dir2{};
        };

        using MeshCurvaturePropertySnapshot =
            std::shared_ptr<const MeshCurvaturePropertyState>;

        [[nodiscard]] std::size_t CountChangedCurvatureValues(
            const MeshCurvaturePropertyState& before,
            const MeshCurvaturePropertyState& after) noexcept
        {
            return CountChangedValues(before.HadMean, before.Mean, after.Mean) +
                   CountChangedValues(
                       before.HadGaussian, before.Gaussian, after.Gaussian) +
                   CountChangedValues(
                       before.HadMinPrincipal,
                       before.MinPrincipal,
                       after.MinPrincipal) +
                   CountChangedValues(
                       before.HadMaxPrincipal,
                       before.MaxPrincipal,
                       after.MaxPrincipal) +
                   CountChangedValues(before.HadDir1, before.Dir1, after.Dir1) +
                   CountChangedValues(before.HadDir2, before.Dir2, after.Dir2);
        }

        [[nodiscard]] std::string BuildMeshCurvatureNoChangeMessage(
            const EditorMeshCurvatureResult& result)
        {
            std::string message =
                "Mesh curvature is already up to date (";
            message += std::to_string(result.ScalarWrittenCount);
            message += " scalar and ";
            message += std::to_string(result.DirectionWrittenCount);
            message += " direction values recomputed, support=";
            message += std::to_string(result.SupportedVertexCount);
            message += ", nonzero=";
            message += std::to_string(result.NonZeroPrincipalVertexCount);
            message += ", degenerate-faces=";
            message += std::to_string(result.DegenerateFaceCount);
            message += ", ill-conditioned-faces=";
            message += std::to_string(result.IllConditionedFaceCount);
            message += ", 0 changed). Nothing was "
                       "published and no undo entry was created.";
            return message;
        }

        void CopyMeshCurvatureDiagnostics(
            const Curv::CurvatureField& curvature,
            EditorMeshCurvatureResult& result) noexcept
        {
            result.SupportedVertexCount =
                curvature.Diagnostics.SupportedVertexCount;
            result.NonZeroPrincipalVertexCount =
                curvature.Diagnostics.NonZeroPrincipalVertexCount;
            result.MinimumPrincipalValue =
                curvature.Diagnostics.MinimumPrincipalValue;
            result.MaximumPrincipalValue =
                curvature.Diagnostics.MaximumPrincipalValue;
            result.DegenerateFaceCount =
                curvature.Diagnostics.DegenerateFaceCount;
            result.IllConditionedFaceCount =
                curvature.Diagnostics.IllConditionedFaceCount;
            result.UnsupportedFaceCount =
                curvature.Diagnostics.UnsupportedFaceCount;
            result.MinimumTriangleQuality =
                curvature.Diagnostics.MinimumTriangleQuality;
            result.TriangleQualityThreshold =
                curvature.Diagnostics.TriangleQualityThreshold;
        }

        struct MeshCurvatureMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            std::size_t VertexSlotCount{0u};
            MeshPositionState Positions{};
            MeshCurvaturePropertySnapshot Properties{};
        };

        [[nodiscard]] bool SameMeshCurvaturePropertyState(
            const MeshCurvaturePropertyState& lhs,
            const MeshCurvaturePropertyState& rhs) noexcept
        {
            return lhs.HadMean == rhs.HadMean &&
                   lhs.HadGaussian == rhs.HadGaussian &&
                   lhs.HadMinPrincipal == rhs.HadMinPrincipal &&
                   lhs.HadMaxPrincipal == rhs.HadMaxPrincipal &&
                   lhs.HadDir1 == rhs.HadDir1 &&
                   lhs.HadDir2 == rhs.HadDir2 &&
                   lhs.Mean == rhs.Mean &&
                   lhs.Gaussian == rhs.Gaussian &&
                   lhs.MinPrincipal == rhs.MinPrincipal &&
                   lhs.MaxPrincipal == rhs.MaxPrincipal &&
                   lhs.Dir1 == rhs.Dir1 &&
                   lhs.Dir2 == rhs.Dir2;
        }

        [[nodiscard]] bool MeshCurvaturePropertyStateMatchesCount(
            const MeshCurvaturePropertyState& state,
            const std::size_t expectedCount) noexcept
        {
            return (state.HadMean
                        ? state.Mean.size() == expectedCount
                        : state.Mean.empty()) &&
                   (state.HadGaussian
                        ? state.Gaussian.size() == expectedCount
                        : state.Gaussian.empty()) &&
                   (state.HadMinPrincipal
                        ? state.MinPrincipal.size() == expectedCount
                        : state.MinPrincipal.empty()) &&
                   (state.HadMaxPrincipal
                        ? state.MaxPrincipal.size() == expectedCount
                        : state.MaxPrincipal.empty()) &&
                   (state.HadDir1
                        ? state.Dir1.size() == expectedCount
                        : state.Dir1.empty()) &&
                   (state.HadDir2
                        ? state.Dir2.size() == expectedCount
                        : state.Dir2.empty());
        }

        [[nodiscard]] bool CaptureMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const std::size_t expectedCount,
            MeshCurvaturePropertyState& out,
            std::string& diagnostic,
            const MeshCurvatureConfig& bindings)
        {
            out.Bindings = bindings;
            return CaptureCurvatureProperty<double>(
                       properties,
                       bindings.Mean.Name,
                       expectedCount,
                       out.HadMean,
                       out.Mean,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       properties,
                       bindings.Gaussian.Name,
                       expectedCount,
                       out.HadGaussian,
                       out.Gaussian,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       properties,
                       bindings.MinPrincipal.Name,
                       expectedCount,
                       out.HadMinPrincipal,
                       out.MinPrincipal,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       properties,
                       bindings.MaxPrincipal.Name,
                       expectedCount,
                       out.HadMaxPrincipal,
                       out.MaxPrincipal,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       bindings.Direction1.Name,
                       expectedCount,
                       out.HadDir1,
                       out.Dir1,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       bindings.Direction2.Name,
                       expectedCount,
                       out.HadDir2,
                       out.Dir2,
                       diagnostic);
        }

        [[nodiscard]] bool ApplyMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const MeshCurvaturePropertyState& state)
        {
            return ApplyCurvatureProperty<double>(
                       properties,
                       state.Bindings.Mean.Name,
                       state.HadMean,
                       state.Mean,
                       0.0) &&
                   ApplyCurvatureProperty<double>(
                       properties,
                       state.Bindings.Gaussian.Name,
                       state.HadGaussian,
                       state.Gaussian,
                       0.0) &&
                   ApplyCurvatureProperty<double>(
                       properties,
                       state.Bindings.MinPrincipal.Name,
                       state.HadMinPrincipal,
                       state.MinPrincipal,
                       0.0) &&
                   ApplyCurvatureProperty<double>(
                       properties,
                       state.Bindings.MaxPrincipal.Name,
                       state.HadMaxPrincipal,
                       state.MaxPrincipal,
                       0.0) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       state.Bindings.Direction1.Name,
                       state.HadDir1,
                       state.Dir1,
                       glm::vec3{0.0f}) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       state.Bindings.Direction2.Name,
                       state.HadDir2,
                       state.Dir2,
                       glm::vec3{0.0f});
        }

        [[nodiscard]] std::size_t CountNonFiniteScalars(
            const std::span<const double> values) noexcept
        {
            std::size_t count = 0u;
            for (const double value : values)
            {
                if (!std::isfinite(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] std::size_t CountNonFiniteVectors(
            const std::span<const glm::vec3> values) noexcept
        {
            std::size_t count = 0u;
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteGeometryPosition(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] bool CurvatureOutputRequestsDirections(
            const EditorMeshCurvatureOutput output) noexcept
        {
            return output == EditorMeshCurvatureOutput::All ||
                   output == EditorMeshCurvatureOutput::PrincipalDirections;
        }

        // Mean and Gaussian curvature are derived from the principal values,
        // so all four scalar fields publish as one transaction. Validity checks
        // must short-circuit before Vector() accesses an invalid property.
        [[nodiscard]] bool MeshCurvatureScalarsUsable(
            const Curv::CurvatureField& curvature,
            const std::size_t expectedCount) noexcept
        {
            return curvature.MeanCurvatureProperty &&
                   curvature.GaussianCurvatureProperty &&
                   curvature.MinPrincipalCurvatureProperty &&
                   curvature.MaxPrincipalCurvatureProperty &&
                   curvature.MeanCurvatureProperty.Vector().size() ==
                       expectedCount &&
                   curvature.GaussianCurvatureProperty.Vector().size() ==
                       expectedCount &&
                   curvature.MinPrincipalCurvatureProperty.Vector().size() ==
                       expectedCount &&
                   curvature.MaxPrincipalCurvatureProperty.Vector().size() ==
                       expectedCount;
        }

        [[nodiscard]] std::size_t CountNonFiniteMeshCurvatureScalars(
            const Curv::CurvatureField& curvature) noexcept
        {
            const auto count = [](const std::vector<double>& values) noexcept
            {
                return CountNonFiniteScalars(
                    std::span<const double>{values.data(), values.size()});
            };
            return count(curvature.MeanCurvatureProperty.Vector()) +
                   count(curvature.GaussianCurvatureProperty.Vector()) +
                   count(curvature.MinPrincipalCurvatureProperty.Vector()) +
                   count(curvature.MaxPrincipalCurvatureProperty.Vector());
        }

        void StageMeshCurvatureScalars(
            const Curv::CurvatureField& curvature,
            MeshCurvaturePropertyState& after,
            EditorMeshCurvatureResult& result)
        {
            const std::vector<double>& mean =
                curvature.MeanCurvatureProperty.Vector();
            const std::vector<double>& gaussian =
                curvature.GaussianCurvatureProperty.Vector();
            const std::vector<double>& minPrincipal =
                curvature.MinPrincipalCurvatureProperty.Vector();
            const std::vector<double>& maxPrincipal =
                curvature.MaxPrincipalCurvatureProperty.Vector();

            after.HadMean = true;
            after.Mean = mean;
            after.HadGaussian = true;
            after.Gaussian = gaussian;
            after.HadMinPrincipal = true;
            after.MinPrincipal = minPrincipal;
            after.HadMaxPrincipal = true;
            after.MaxPrincipal = maxPrincipal;

            result.ScalarPropertyCount = 4u;
            result.ScalarWrittenCount = mean.size() + gaussian.size() +
                                        minPrincipal.size() +
                                        maxPrincipal.size();
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshCurvatureState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const MeshCurvaturePropertyState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            if (!ApplyMeshCurvaturePropertyState(
                    view.VertexSource->Properties,
                    state))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            return EditorCommandHistoryStatus::Applied;
        }

        void StampMeshCurvaturePropertyDirty(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            Dirty::MarkVertexAttributesDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandStatus CommitMeshCurvatureProperties(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> positions,
            MeshCurvaturePropertyState before,
            MeshCurvaturePropertyState after)
        {
            if (context.CommandHistory != nullptr)
            {
                if (context.Scene == nullptr)
                    return EditorCommandStatus::MissingScene;
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableEntityId);
                if (entity == ECS::InvalidEntityHandle ||
                    !context.Scene->Raw().valid(entity))
                {
                    return EditorCommandStatus::StaleEntity;
                }

                const MeshPositionState positionState =
                    std::make_shared<std::vector<glm::vec3>>(
                        std::move(positions));
                const MeshCurvaturePropertySnapshot beforeState =
                    std::make_shared<MeshCurvaturePropertyState>(
                        std::move(before));
                const MeshCurvaturePropertySnapshot afterState =
                    std::make_shared<MeshCurvaturePropertyState>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        "Compute mesh curvature",
                        MeshPropertyMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        MeshCurvatureMutationGeneration{
                            .GeometryMetadataSignature =
                                GeometryMetadataSignatureForEntity(
                                    context.Scene->Raw(),
                                    entity),
                            .VertexSlotCount = positionState->size(),
                            .Positions = positionState,
                            .Properties = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshCurvatureMutationGeneration& expected,
                            const MeshCurvaturePropertySnapshot& target)
                        {
                            if (identity.Scene == nullptr ||
                                !identity.World.IsValid())
                            {
                                return EditorCommandHistoryStatus::MissingScene;
                            }

                            entt::registry& raw = identity.Scene->Raw();
                            const ECS::EntityHandle entity =
                                SelectionController::ToEntityHandle(
                                    identity.StableEntityId);
                            if (entity == ECS::InvalidEntityHandle ||
                                !raw.valid(entity))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            GS::MutableSourceView view =
                                GS::BuildMutableView(raw, entity);
                            const GS::SourceAvailability availability =
                                GS::BuildSourceAvailability(view);
                            if (availability.ProvenanceDomain !=
                                    GS::Domain::Mesh ||
                                view.VertexSource == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (expected.Positions == nullptr ||
                                expected.Properties == nullptr ||
                                target == nullptr ||
                                !MeshCurvaturePropertyStateMatchesCount(
                                    *target,
                                    expected.VertexSlotCount))
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }
                            if (GeometryMetadataSignatureForEntity(raw, entity) !=
                                expected.GeometryMetadataSignature)
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const auto currentPositions =
                                view.VertexSource->Properties.Get<glm::vec3>(
                                    expected.Properties->Bindings.Positions.Name);
                            if (!currentPositions ||
                                !SameGeometryPositions(
                                    currentPositions.Vector(),
                                    *expected.Positions))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            MeshCurvaturePropertyState currentProperties{};
                            std::string diagnostic{};
                            if (!CaptureMeshCurvaturePropertyState(
                                    view.VertexSource->Properties,
                                    expected.VertexSlotCount,
                                    currentProperties,
                                    diagnostic,
                                    expected.Properties->Bindings) ||
                                !SameMeshCurvaturePropertyState(
                                    currentProperties,
                                    *expected.Properties))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshCurvaturePropertySnapshot& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyMeshCurvatureState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshCurvatureMutationGeneration& expected,
                            const MeshCurvaturePropertySnapshot& target)
                        {
                            StampMeshCurvaturePropertyDirty(
                                *identity.Scene,
                                identity.StableEntityId);
                            const ECS::EntityHandle entity =
                                SelectionController::ToEntityHandle(
                                    identity.StableEntityId);
                            return MeshCurvatureMutationGeneration{
                                .GeometryMetadataSignature =
                                    GeometryMetadataSignatureForEntity(
                                        identity.Scene->Raw(),
                                        entity),
                                .VertexSlotCount = expected.VertexSlotCount,
                                .Positions = expected.Positions,
                                .Properties = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyMeshCurvatureState(context.Scene, stableEntityId, after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            StampMeshCurvaturePropertyDirty(
                *context.Scene,
                stableEntityId);
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] std::string BuildMeshCurvatureSuccessMessage(
            const EditorMeshCurvatureResult& result)
        {
            std::string message = "Mesh curvature computed (vertices=";
            message += std::to_string(result.VertexSlotCount);
            message += ", scalars=";
            message += std::to_string(result.ScalarWrittenCount);
            message += ", changed=";
            message += std::to_string(result.ChangedValueCount);
            message += ", support=";
            message += std::to_string(result.SupportedVertexCount);
            message += ", nonzero=";
            message += std::to_string(result.NonZeroPrincipalVertexCount);
            message += ", degenerate-faces=";
            message += std::to_string(result.DegenerateFaceCount);
            message += ", ill-conditioned-faces=";
            message += std::to_string(result.IllConditionedFaceCount);
            message += ", principal-range=[";
            message += std::to_string(result.MinimumPrincipalValue);
            message += ", ";
            message += std::to_string(result.MaximumPrincipalValue);
            message += "]";
            message += ", directions=";
            message += result.DirectionsPublished ? "published" : "not published";
            message += ").";
            if (result.DirectionsRequested && !result.DirectionsPublished)
                message += " Principal directions were not published for this run.";
            return message;
        }

        struct MeshCurvatureSegmentationSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> SourcePositions{};
            std::vector<std::uint32_t> SourceFaceForMeshFace{};
            std::vector<std::uint32_t> SourceEdgeForMeshEdge{};
            std::size_t FaceSlotCount{0u};
            std::size_t EdgeSlotCount{0u};
            EditorCommandStatus Status{EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] std::uint64_t UndirectedEdgeKey(
            const std::uint32_t vertex0,
            const std::uint32_t vertex1) noexcept
        {
            const std::uint32_t lower = std::min(vertex0, vertex1);
            const std::uint32_t upper = std::max(vertex0, vertex1);
            return (static_cast<std::uint64_t>(lower) << 32u) |
                   static_cast<std::uint64_t>(upper);
        }

        [[nodiscard]] EditorCommandStatus ValidateSegmentationSourceMetadata(
            const GS::ConstSourceView& view, const std::string_view positionProperty,
            std::string& diagnostic)
        {
            if (!view.EdgeSource || !view.FaceSource)
            {
                diagnostic = "Curvature segmentation requires mesh face and edge sources.";
                return EditorCommandStatus::UnsupportedGeometryDomain;
            }
            const auto status = ValidateMeshSoupSourceMetadata(view, diagnostic, positionProperty);
            if (status != EditorCommandStatus::Applied) return status;
            const auto& edges = view.EdgeSource->Properties;
            const auto v0 = edges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV0);
            const auto v1 = edges.Get<std::uint32_t>(GS::PropertyNames::kEdgeV1);
            const auto deleted = edges.Get<bool>("e:deleted");
            if (!v0 || !v1 || v0.Vector().size() != edges.Size() ||
                v1.Vector().size() != edges.Size() ||
                (deleted && deleted.Vector().size() != edges.Size()))
            {
                diagnostic = "Curvature segmentation requires count-matched canonical edge endpoints.";
                return EditorCommandStatus::InvalidProcessingParameters;
            }
            const auto maskStatus = ValidateMeshVertexDeletionMaskMetadata(view, diagnostic, positionProperty);
            if (maskStatus != EditorCommandStatus::Applied)
                diagnostic = "Curvature segmentation: " + diagnostic;
            return maskStatus;
        }

        [[nodiscard]] MeshCurvatureSegmentationSourceResult
        BuildHalfedgeMeshForCurvatureSegmentation(
            const GS::ConstSourceView& view, const std::string_view positionProperty)
        {
            MeshCurvatureSegmentationSourceResult result{};
            result.Status = ValidateSegmentationSourceMetadata(view, positionProperty, result.Diagnostic);
            if (!result.Succeeded())
            {
                result.Error = Core::ErrorCode::InvalidArgument;
                return result;
            }

            MeshProcessingSourceResult source =
                BuildHalfedgeMeshForProcessing(view, "Curvature segmentation", positionProperty);
            if (!source.Succeeded())
            {
                result.Status = source.Status;
                result.Error = source.Error;
                result.Diagnostic = std::move(source.Diagnostic);
                return result;
            }

            result.FaceSlotCount = view.FaceSource->Properties.Size();
            result.EdgeSlotCount = view.EdgeSource->Properties.Size();
            result.SourcePositions = std::move(source.BeforePositions);
            result.SourceFaceForMeshFace =
                std::move(source.SourceFaceForMeshFace);
            result.Mesh = std::move(source.Mesh);
            if (result.SourceFaceForMeshFace.size() !=
                result.Mesh.FacesSize())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Diagnostic =
                    "Curvature segmentation face cross-references do not match the detached mesh.";
                return result;
            }

            std::vector<bool> seenFaces(result.FaceSlotCount, false);
            for (const std::uint32_t sourceFace :
                 result.SourceFaceForMeshFace)
            {
                if (sourceFace >= seenFaces.size() || seenFaces[sourceFace])
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "Curvature segmentation currently requires triangle source faces; polygon triangulation is not published as source-face labels.";
                    return result;
                }
                seenFaces[sourceFace] = true;
            }

            const auto sourceV0 =
                view.EdgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV0);
            const auto sourceV1 =
                view.EdgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV1);
            const auto sourceDeleted =
                view.EdgeSource->Properties.Get<bool>("e:deleted");
            std::unordered_map<std::uint64_t, std::uint32_t>
                sourceEdgeByVertices{};
            sourceEdgeByVertices.reserve(result.EdgeSlotCount);
            for (std::size_t edge = 0u;
                 edge < result.EdgeSlotCount;
                 ++edge)
            {
                if (sourceDeleted && sourceDeleted.Vector()[edge])
                    continue;
                const std::uint32_t v0 = sourceV0.Vector()[edge];
                const std::uint32_t v1 = sourceV1.Vector()[edge];
                if (v0 >= result.SourcePositions.size() ||
                    v1 >= result.SourcePositions.size() || v0 == v1)
                {
                    continue;
                }
                const auto [iterator, inserted] =
                    sourceEdgeByVertices.emplace(
                        UndirectedEdgeKey(v0, v1),
                        static_cast<std::uint32_t>(edge));
                (void)iterator;
                if (!inserted)
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "Curvature segmentation found ambiguous source edges with identical endpoints.";
                    return result;
                }
            }

            result.SourceEdgeForMeshEdge.assign(
                result.Mesh.EdgesSize(),
                CurvSeg::kInvalidLabel);
            for (const Geometry::EdgeHandle edge :
                 result.Mesh.LiveEdges())
            {
                const Geometry::HalfedgeHandle halfedge =
                    result.Mesh.Halfedge(edge, 0u);
                const std::uint32_t v0 = static_cast<std::uint32_t>(
                    result.Mesh.FromVertex(halfedge).Index);
                const std::uint32_t v1 = static_cast<std::uint32_t>(
                    result.Mesh.ToVertex(halfedge).Index);
                const auto sourceEdge = sourceEdgeByVertices.find(
                    UndirectedEdgeKey(v0, v1));
                if (sourceEdge == sourceEdgeByVertices.end())
                {
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "Curvature segmentation could not map a detached edge to the authoritative mesh edge source.";
                    return result;
                }
                result.SourceEdgeForMeshEdge[edge.Index] =
                    sourceEdge->second;
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] bool ValidateSegmentationFeatureMetadata(
            const GS::ConstSourceView& view, const CurvatureSegmentationConfig& config,
            std::string& diagnostic)
        {
            const auto availability = BuildGeometryAvailability(view);
            for (const auto& ref : config.Features)
            {
                const auto count = ref.Domain == GeometryElementDomain::MeshVertex
                    ? view.VertexSource->Properties.Size() : view.FaceSource->Properties.Size();
                const auto resolution = ResolveGeometryProperty(availability, ref, count);
                if (!resolution.Resolved())
                {
                    diagnostic = "Segmentation feature '" + ref.Name +
                        "' must exist with its declared numeric type and domain slot count.";
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool CaptureSegmentationFaceFeatures(
            const GS::ConstSourceView& view, const CurvatureSegmentationConfig& config,
            const MeshCurvatureSegmentationSourceResult& source,
            std::vector<glm::dvec3>& features, std::uint32_t& dimension,
            std::string& diagnostic)
        {
            for (const auto face : source.Mesh.LiveFaces())
                for (const auto vertex : source.Mesh.VerticesAroundFace(face))
                    if (!GeometryProcessingDetail::FinitePosition(source.Mesh.Position(vertex)))
                    {
                        diagnostic = "Segmentation requires finite positions on participating face vertices.";
                        return false;
                    }
            features.assign(source.Mesh.FacesSize(), glm::dvec3{0.0});
            dimension = 0u;
            for (const auto& ref : config.Features)
            {
                const bool vertexInput = ref.Domain == GeometryElementDomain::MeshVertex;
                const auto& properties = vertexInput ? view.VertexSource->Properties : view.FaceSource->Properties;
                const auto width = GeometryPropertyComponentCount(ref.ValueKind);
                const auto capture = [&]<typename T>()
                {
                    const auto property = properties.Get<T>(ref.Name);
                    const auto add = [&](std::size_t row, glm::dvec3& sum)
                    {
                        const T value = property.Vector()[row];
                        glm::dvec3 numeric{0.0};
                        if constexpr (std::is_arithmetic_v<T>)
                        {
                            if constexpr (std::is_same_v<T, std::uint64_t>)
                            {
                                // Reject integer precision loss instead of merging distinct features.
                                const auto bits = std::bit_width(value);
                                if (bits > std::numeric_limits<double>::digits &&
                                    (value & ((std::uint64_t{1} << (bits - std::numeric_limits<double>::digits)) - 1u)))
                                    return false;
                            }
                            numeric.x = static_cast<double>(value);
                        }
                        else
                            for (std::uint32_t c = 0; c < width; ++c) numeric[c] = value[c];
                        for (std::uint32_t c = 0; c < width; ++c)
                        {
                            if (!std::isfinite(numeric[c])) return false;
                            sum[c] += numeric[c];
                        }
                        return true;
                    };
                    for (const auto face : source.Mesh.LiveFaces())
                    {
                        glm::dvec3 sample{0.0};
                        if (vertexInput)
                        {
                            std::size_t count = 0;
                            for (const auto vertex : source.Mesh.VerticesAroundFace(face))
                            {
                                if (!add(vertex.Index, sample)) return false;
                                ++count;
                            }
                            sample /= static_cast<double>(count);
                        }
                        else if (!add(source.SourceFaceForMeshFace[face.Index], sample)) return false;
                        for (std::uint32_t c = 0; c < width; ++c)
                        {
                            if (!std::isfinite(sample[c])) return false;
                            features[face.Index][dimension + c] = sample[c];
                        }
                    }
                    return true;
                };
                bool captured = false;
                using K = Geometry::PropertyValueKind;
                switch (ref.ValueKind)
                {
                case K::Bool: captured = capture.template operator()<bool>(); break;
                case K::Int32: captured = capture.template operator()<std::int32_t>(); break;
                case K::UInt32: captured = capture.template operator()<std::uint32_t>(); break;
                case K::UInt64: captured = capture.template operator()<std::uint64_t>(); break;
                case K::Float: captured = capture.template operator()<float>(); break;
                case K::Double: captured = capture.template operator()<double>(); break;
                case K::Vec2: captured = capture.template operator()<glm::vec2>(); break;
                case K::Vec3: captured = capture.template operator()<glm::vec3>(); break;
                case K::Unknown: case K::Vec4: break;
                }
                if (!captured)
                {
                    diagnostic = "Segmentation feature '" + ref.Name +
                        "' must contain finite values representable without integer precision loss.";
                    return false;
                }
                dimension += width;
            }
            return true;
        }

        [[nodiscard]] CurvSeg::FeatureEvidenceParams
        MakeFeatureEvidenceParams(
            const CurvatureSegmentationConfig& config)
        {
            return CurvSeg::FeatureEvidenceParams{
                .BoundaryIsHardFeature = false,
                .HardDihedralThresholdDegrees =
                    config.HardDihedralThresholdDegrees,
                .BaseRadiusRatio = config.FeatureBaseRadiusRatio,
            };
        }

        [[nodiscard]] CurvSeg::CurvaturePatchParams
        MakeCurvaturePatchParams(
            const CurvatureSegmentationConfig& config)
        {
            CurvSeg::CurvaturePatchParams params{};
            params.Mixture = MakeCurvatureSegmentationParams(config);
            params.BaseRadiusRatio = config.FeatureBaseRadiusRatio;
            params.PatchComplexityCost = config.PatchComplexityCost;
            return params;
        }

        struct MeshCurvatureSegmentationPropertyState
        {
            CurvatureSegmentationConfig Bindings{};
            bool HadComponent{false};
            bool HadRegion{false};
            bool HadRegionColor{false};
            bool HadBoundary{false};
            bool HadBoundaryColor{false};
            bool HadHardFeature{false};
            bool HadSoftFeatureConfidence{false};
            bool HadBoundaryRole{false};
            bool HadFeaturePatchColor{false};
            std::vector<std::uint32_t> Components{};
            std::vector<std::uint32_t> Regions{};
            std::vector<glm::vec4> RegionColors{};
            std::vector<bool> Boundaries{};
            std::vector<glm::vec4> BoundaryColors{};
            std::vector<bool> HardFeatures{};
            std::vector<double> SoftFeatureConfidences{};
            std::vector<std::uint32_t> BoundaryRoles{};
            std::vector<glm::vec4> FeaturePatchColors{};
        };

        using MeshCurvatureSegmentationPropertySnapshot =
            std::shared_ptr<
                const MeshCurvatureSegmentationPropertyState>;

        [[nodiscard]] bool SameMeshCurvatureSegmentationPropertyState(
            const MeshCurvatureSegmentationPropertyState& lhs,
            const MeshCurvatureSegmentationPropertyState& rhs) noexcept
        {
            return lhs.HadComponent == rhs.HadComponent &&
                   lhs.HadRegion == rhs.HadRegion &&
                   lhs.HadRegionColor == rhs.HadRegionColor &&
                   lhs.HadBoundary == rhs.HadBoundary &&
                   lhs.HadBoundaryColor == rhs.HadBoundaryColor &&
                   lhs.HadHardFeature == rhs.HadHardFeature &&
                   lhs.HadSoftFeatureConfidence ==
                       rhs.HadSoftFeatureConfidence &&
                   lhs.HadBoundaryRole == rhs.HadBoundaryRole &&
                   lhs.HadFeaturePatchColor ==
                       rhs.HadFeaturePatchColor &&
                   lhs.Components == rhs.Components &&
                   lhs.Regions == rhs.Regions &&
                   lhs.RegionColors == rhs.RegionColors &&
                   lhs.Boundaries == rhs.Boundaries &&
                   lhs.BoundaryColors == rhs.BoundaryColors &&
                   lhs.HardFeatures == rhs.HardFeatures &&
                   lhs.SoftFeatureConfidences ==
                       rhs.SoftFeatureConfidences &&
                   lhs.BoundaryRoles == rhs.BoundaryRoles &&
                   lhs.FeaturePatchColors == rhs.FeaturePatchColors;
        }

        [[nodiscard]] bool
        MeshCurvatureSegmentationPropertyStateMatchesCounts(
            const MeshCurvatureSegmentationPropertyState& state,
            const std::size_t faceCount,
            const std::size_t edgeCount) noexcept
        {
            return (state.HadComponent
                        ? state.Components.size() == faceCount
                        : state.Components.empty()) &&
                   (state.HadRegion
                        ? state.Regions.size() == faceCount
                        : state.Regions.empty()) &&
                   (state.HadRegionColor
                        ? state.RegionColors.size() == faceCount
                        : state.RegionColors.empty()) &&
                   (state.HadBoundary
                        ? state.Boundaries.size() == edgeCount
                        : state.Boundaries.empty()) &&
                   (state.HadBoundaryColor
                        ? state.BoundaryColors.size() == edgeCount
                        : state.BoundaryColors.empty()) &&
                   (state.HadHardFeature
                        ? state.HardFeatures.size() == edgeCount
                        : state.HardFeatures.empty()) &&
                   (state.HadSoftFeatureConfidence
                        ? state.SoftFeatureConfidences.size() == edgeCount
                        : state.SoftFeatureConfidences.empty()) &&
                   (state.HadBoundaryRole
                        ? state.BoundaryRoles.size() == edgeCount
                        : state.BoundaryRoles.empty()) &&
                   (state.HadFeaturePatchColor
                        ? state.FeaturePatchColors.size() == edgeCount
                        : state.FeaturePatchColors.empty());
        }

        [[nodiscard]] bool CaptureMeshCurvatureSegmentationPropertyState(
            Geometry::PropertySet& faceProperties,
            Geometry::PropertySet& edgeProperties,
            const std::size_t faceCount,
            const std::size_t edgeCount,
            MeshCurvatureSegmentationPropertyState& out,
            std::string& diagnostic, const CurvatureSegmentationConfig& bindings)
        {
            out.Bindings = bindings;
            return CaptureCurvatureProperty<std::uint32_t>(
                       faceProperties,
                       bindings.Components.Name,
                       faceCount,
                       out.HadComponent,
                       out.Components,
                       diagnostic) &&
                   CaptureCurvatureProperty<std::uint32_t>(
                       faceProperties,
                       bindings.Regions.Name,
                       faceCount,
                       out.HadRegion,
                       out.Regions,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec4>(
                       faceProperties,
                       bindings.RegionColors.Name,
                       faceCount,
                       out.HadRegionColor,
                       out.RegionColors,
                       diagnostic) &&
                   CaptureCurvatureProperty<bool>(
                       edgeProperties,
                       bindings.Boundaries.Name,
                       edgeCount,
                       out.HadBoundary,
                       out.Boundaries,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec4>(
                       edgeProperties,
                       bindings.BoundaryColors.Name,
                       edgeCount,
                       out.HadBoundaryColor,
                       out.BoundaryColors,
                       diagnostic) &&
                   CaptureCurvatureProperty<bool>(
                       edgeProperties,
                       bindings.HardFeatures.Name,
                       edgeCount,
                       out.HadHardFeature,
                       out.HardFeatures,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       edgeProperties,
                       bindings.FeatureConfidence.Name,
                       edgeCount,
                       out.HadSoftFeatureConfidence,
                       out.SoftFeatureConfidences,
                       diagnostic) &&
                   CaptureCurvatureProperty<std::uint32_t>(
                       edgeProperties,
                       bindings.BoundaryRoles.Name,
                       edgeCount,
                       out.HadBoundaryRole,
                       out.BoundaryRoles,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec4>(
                       edgeProperties,
                       bindings.FeatureColors.Name,
                       edgeCount,
                       out.HadFeaturePatchColor,
                       out.FeaturePatchColors,
                       diagnostic);
        }

        [[nodiscard]] bool ApplyMeshCurvatureSegmentationPropertyState(
            Geometry::PropertySet& faceProperties,
            Geometry::PropertySet& edgeProperties,
            const MeshCurvatureSegmentationPropertyState& state)
        {
            return ApplyCurvatureProperty<std::uint32_t>(
                       faceProperties,
                       state.Bindings.Components.Name,
                       state.HadComponent,
                       state.Components,
                       CurvSeg::kInvalidLabel) &&
                   ApplyCurvatureProperty<std::uint32_t>(
                       faceProperties,
                       state.Bindings.Regions.Name,
                       state.HadRegion,
                       state.Regions,
                       CurvSeg::kInvalidLabel) &&
                   ApplyCurvatureProperty<glm::vec4>(
                       faceProperties,
                       state.Bindings.RegionColors.Name,
                       state.HadRegionColor,
                       state.RegionColors,
                       glm::vec4{0.0f}) &&
                   ApplyCurvatureProperty<bool>(
                       edgeProperties,
                       state.Bindings.Boundaries.Name,
                       state.HadBoundary,
                       state.Boundaries,
                       false) &&
                   ApplyCurvatureProperty<glm::vec4>(
                       edgeProperties,
                       state.Bindings.BoundaryColors.Name,
                       state.HadBoundaryColor,
                       state.BoundaryColors,
                       glm::vec4{0.0f}) &&
                   ApplyCurvatureProperty<bool>(
                       edgeProperties,
                       state.Bindings.HardFeatures.Name,
                       state.HadHardFeature,
                       state.HardFeatures,
                       false) &&
                   ApplyCurvatureProperty<double>(
                       edgeProperties,
                       state.Bindings.FeatureConfidence.Name,
                       state.HadSoftFeatureConfidence,
                       state.SoftFeatureConfidences,
                       0.0) &&
                   ApplyCurvatureProperty<std::uint32_t>(
                       edgeProperties,
                       state.Bindings.BoundaryRoles.Name,
                       state.HadBoundaryRole,
                       state.BoundaryRoles,
                       0u) &&
                   ApplyCurvatureProperty<glm::vec4>(
                       edgeProperties,
                       state.Bindings.FeatureColors.Name,
                       state.HadFeaturePatchColor,
                       state.FeaturePatchColors,
                       glm::vec4{0.0f});
        }

        [[nodiscard]] std::size_t
        CountChangedCurvatureSegmentationValues(
            const MeshCurvatureSegmentationPropertyState& before,
            const MeshCurvatureSegmentationPropertyState& after) noexcept
        {
            return (before.HadComponent && !after.HadComponent
                        ? before.Components.size()
                        : CountChangedValues(before.HadComponent,
                                             before.Components, after.Components)) +
                   CountChangedValues(
                       before.HadRegion,
                       before.Regions,
                       after.Regions) +
                   CountChangedValues(
                       before.HadRegionColor,
                       before.RegionColors,
                       after.RegionColors) +
                   CountChangedValues(
                       before.HadBoundary,
                       before.Boundaries,
                       after.Boundaries) +
                   CountChangedValues(
                       before.HadBoundaryColor,
                       before.BoundaryColors,
                       after.BoundaryColors) +
                   CountChangedValues(
                       before.HadHardFeature,
                       before.HardFeatures,
                       after.HardFeatures) +
                   CountChangedValues(
                       before.HadSoftFeatureConfidence,
                       before.SoftFeatureConfidences,
                       after.SoftFeatureConfidences) +
                   CountChangedValues(
                       before.HadBoundaryRole,
                       before.BoundaryRoles,
                       after.BoundaryRoles) +
                   CountChangedValues(
                       before.HadFeaturePatchColor,
                       before.FeaturePatchColors,
                       after.FeaturePatchColors);
        }

        struct MeshCurvatureSegmentationMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            std::optional<std::uint64_t> TopologySignature{};
            std::size_t FaceSlotCount{0u};
            std::size_t EdgeSlotCount{0u};
            MeshPositionState Positions{};
            MeshCurvatureSegmentationPropertySnapshot Properties{};
        };

        [[nodiscard]] EditorCommandHistoryStatus
        ApplyMeshCurvatureSegmentationState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const MeshCurvatureSegmentationPropertyState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;
            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;
            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.FaceSource == nullptr || view.EdgeSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }
            if (!ApplyMeshCurvatureSegmentationPropertyState(
                    view.FaceSource->Properties,
                    view.EdgeSource->Properties,
                    state))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }
            return EditorCommandHistoryStatus::Applied;
        }

        void StampMeshCurvatureSegmentationDirty(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                Dirty::MarkGpuDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandStatus
        CommitMeshCurvatureSegmentationProperties(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> positions,
            const std::size_t faceSlotCount,
            const std::size_t edgeSlotCount,
            MeshCurvatureSegmentationPropertyState before,
            MeshCurvatureSegmentationPropertyState after)
        {
            if (context.CommandHistory == nullptr)
            {
                const EditorCommandHistoryStatus applied =
                    ApplyMeshCurvatureSegmentationState(
                        context.Scene, stableEntityId, after);
                if (applied != EditorCommandHistoryStatus::Applied)
                    return ToEditorCommandStatus(applied);
                StampMeshCurvatureSegmentationDirty(
                    *context.Scene, stableEntityId);
                return EditorCommandStatus::Applied;
            }
            if (context.Scene == nullptr)
                return EditorCommandStatus::MissingScene;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandStatus::StaleEntity;

            const MeshPositionState positionState =
                std::make_shared<std::vector<glm::vec3>>(
                    std::move(positions));
            const MeshCurvatureSegmentationPropertySnapshot beforeState =
                std::make_shared<
                    MeshCurvatureSegmentationPropertyState>(
                        std::move(before));
            const MeshCurvatureSegmentationPropertySnapshot afterState =
                std::make_shared<
                    MeshCurvatureSegmentationPropertyState>(
                        std::move(after));
            const EditorCommandHistoryResult history =
                Internal::ExecuteUndoableEntityMutation(
                    *context.CommandHistory,
                    "Segment mesh by signed curvature",
                    MeshPropertyMutationIdentity{
                        .Scene = context.Scene,
                        .World = context.World,
                        .StableEntityId = stableEntityId,
                    },
                    MeshCurvatureSegmentationMutationGeneration{
                        .GeometryMetadataSignature =
                            GeometryMetadataSignatureForEntity(raw, *entity),
                        .TopologySignature =
                            StoredMeshTopologySignatureForEntity(
                                raw, stableEntityId),
                        .FaceSlotCount = faceSlotCount,
                        .EdgeSlotCount = edgeSlotCount,
                        .Positions = positionState,
                        .Properties = beforeState,
                    },
                    beforeState,
                    afterState,
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const MeshCurvatureSegmentationMutationGeneration& expected,
                        const MeshCurvatureSegmentationPropertySnapshot& target)
                    {
                        if (identity.Scene == nullptr ||
                            !identity.World.IsValid() || target == nullptr ||
                            expected.Positions == nullptr ||
                            expected.Properties == nullptr ||
                            !MeshCurvatureSegmentationPropertyStateMatchesCounts(
                                *target,
                                expected.FaceSlotCount,
                                expected.EdgeSlotCount))
                        {
                            return EditorCommandHistoryStatus::CommandFailed;
                        }
                        entt::registry& raw = identity.Scene->Raw();
                        const std::optional<ECS::EntityHandle> entity =
                            ResolveStableEntity(
                                raw, identity.StableEntityId);
                        if (!entity.has_value())
                            return EditorCommandHistoryStatus::StaleEntity;
                        GS::MutableSourceView view =
                            GS::BuildMutableView(raw, *entity);
                        if (view.VertexSource == nullptr ||
                            view.FaceSource == nullptr ||
                            view.EdgeSource == nullptr ||
                            view.FaceSource->Properties.Size() !=
                                expected.FaceSlotCount ||
                            view.EdgeSource->Properties.Size() !=
                                expected.EdgeSlotCount ||
                            GeometryMetadataSignatureForEntity(raw, *entity) !=
                                expected.GeometryMetadataSignature ||
                            MeshTopologyValueSignature(
                                GS::BuildConstView(raw, *entity)) !=
                                expected.TopologySignature)
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }
                        const auto currentPositions =
                            view.VertexSource->Properties.Get<glm::vec3>(
                                expected.Properties->Bindings.Positions.Name);
                        if (!currentPositions ||
                            !SameGeometryPositions(
                                currentPositions.Vector(),
                                *expected.Positions))
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }
                        MeshCurvatureSegmentationPropertyState current{};
                        std::string diagnostic{};
                        if (!CaptureMeshCurvatureSegmentationPropertyState(
                                view.FaceSource->Properties,
                                view.EdgeSource->Properties,
                                expected.FaceSlotCount,
                                expected.EdgeSlotCount,
                                current,
                                diagnostic, expected.Properties->Bindings) ||
                            !SameMeshCurvatureSegmentationPropertyState(
                                current, *expected.Properties))
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }
                        return EditorCommandHistoryStatus::Applied;
                    },
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const MeshCurvatureSegmentationPropertySnapshot& target)
                    {
                        if (target == nullptr)
                            return EditorCommandHistoryStatus::CommandFailed;
                        return ApplyMeshCurvatureSegmentationState(
                            identity.Scene,
                            identity.StableEntityId,
                            *target);
                    },
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const MeshCurvatureSegmentationMutationGeneration& expected,
                        const MeshCurvatureSegmentationPropertySnapshot& target)
                    {
                        StampMeshCurvatureSegmentationDirty(
                            *identity.Scene,
                            identity.StableEntityId);
                        entt::registry& raw = identity.Scene->Raw();
                        const std::optional<ECS::EntityHandle> entity =
                            ResolveStableEntity(
                                raw, identity.StableEntityId);
                        return MeshCurvatureSegmentationMutationGeneration{
                            .GeometryMetadataSignature = entity.has_value()
                                ? GeometryMetadataSignatureForEntity(
                                      raw, *entity)
                                : 0u,
                            .TopologySignature =
                                StoredMeshTopologySignatureForEntity(
                                    raw, identity.StableEntityId),
                            .FaceSlotCount = expected.FaceSlotCount,
                            .EdgeSlotCount = expected.EdgeSlotCount,
                            .Positions = expected.Positions,
                            .Properties = target,
                        };
                    });
            return ToEditorCommandStatus(history.Status);
        }

        [[nodiscard]] EditorMeshCurvatureResult
        MakeMeshCurvatureBaseResult(
            const EditorMeshCurvatureCommand& command,
            const bool directionsAvailable)
        {
            return EditorMeshCurvatureResult{
                .Status = EditorCommandStatus::NoChange,
                .Output = command.Output,
                .DirectionsRequested =
                    command.PublishPrincipalDirections &&
                    CurvatureOutputRequestsDirections(command.Output),
                .DirectionsAvailable = directionsAvailable,
                .Error = Core::ErrorCode::Success,
            };
        }


        [[nodiscard]] EditorMeshCurvatureResult
        MakePendingMeshCurvatureResult(
            const EditorMeshCurvatureCommand& command,
            const bool directionsAvailable,
            const std::size_t vertexSlotCount,
            const JobToken handle)
        {
            EditorMeshCurvatureResult result =
                MakeMeshCurvatureBaseResult(command, directionsAvailable);
            result.Status = EditorCommandStatus::Pending;
            result.VertexSlotCount = vertexSlotCount;
            result.Message = "Mesh curvature CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }



        inline constexpr const char* kMeshCurvatureJobName =
            "Sandbox.MeshCurvature.CPU";

        // Curvature is this family's only queued method, so the job carries one
        // command and one result instead of a kind discriminator. The terminal
        // callback the caller supplied is guarded at submit and owned here, so
        // every path that ends the job still delivers exactly one result.
        struct EditorMeshCurvatureJobState
        {
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            MeshCurvaturePropertyState CurvatureBefore{};
            MeshCurvaturePropertyState CurvatureAfter{};
            EditorMeshCurvatureCommand CurvatureCommand{};
            EditorMeshCurvatureResult CurvatureResult{};
            std::function<void(EditorMeshCurvatureResult)> Sink{};
            bool TerminalResultPublished{false};
            // Last answer this job's `ValidateBeforeApply` gave the drain.
            // `FinalizeUnpublishedOnMainThread` takes no arguments, so the
            // reason a completion was refused has to be recorded where it was
            // decided; without it an unpublished job can only say "did not
            // apply". `Current` means the gate never rejected the result, so
            // the job ended for another reason — cancellation, or a publisher
            // that refused the envelope.
            JobApplyValidation LastApplyValidation{
                JobApplyValidation::Current};
        };

        void PublishMeshCurvatureResultSink(
            EditorMeshCurvatureJobState& job,
            EditorMeshCurvatureResult result)
        {
            job.TerminalResultPublished = true;
            if (job.Sink)
                job.Sink(std::move(result));
        }

        [[nodiscard]] JobApplyValidation ValidateMeshCurvatureJobApply(
            const EditorProcessingContext& context,
            const EditorMeshCurvatureJobState& job)
        {
            const JobApplyValidation source = ValidateMeshCpuJobSource(
                context,
                job.StableEntityId,
                job.GeometryMetadataSignature,
                job.SnapshotPositions,
                job.CurvatureCommand.Positions.Name);
            if (source != JobApplyValidation::Current)
                return source;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, job.StableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            GS::MutableSourceView mutableView =
                GS::BuildMutableView(raw, *entity);
            if (!mutableView.Valid() ||
                mutableView.VertexSource == nullptr)
            {
                return JobApplyValidation::StaleGeneration;
            }

            MeshCurvaturePropertyState currentCurvature{};
            std::string diagnostic{};
            if (!CaptureMeshCurvaturePropertyState(
                    mutableView.VertexSource->Properties,
                    job.SnapshotPositions.size(),
                    currentCurvature,
                    diagnostic,
                    job.CurvatureCommand) ||
                !SameMeshCurvaturePropertyState(
                    currentCurvature,
                    job.CurvatureBefore))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }
        [[nodiscard]] JobResultEnvelope RunMeshCurvatureCpuWorker(
            const std::shared_ptr<EditorMeshCurvatureJobState>& state)
        {
            EditorMeshCurvatureResult& result = state->CurvatureResult;
            result.VertexSlotCount = state->SnapshotPositions.size();

            Curv::CurvatureField curvature =
                Curv::ComputeCurvature(state->Mesh);
            if (!MeshCurvatureScalarsUsable(curvature, result.VertexSlotCount))
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.Curvature produced missing or count-mismatched "
                                 "scalar properties.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            CopyMeshCurvatureDiagnostics(curvature, result);
            if (result.SupportedVertexCount == 0u)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature found no reliable curvature support "
                    "(degenerate faces=" +
                    std::to_string(result.DegenerateFaceCount) +
                    ", ill-conditioned faces=" +
                    std::to_string(result.IllConditionedFaceCount) +
                    ", unsupported faces=" +
                    std::to_string(result.UnsupportedFaceCount) + ").";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            result.NonFiniteScalarCount =
                CountNonFiniteMeshCurvatureScalars(curvature);
            if (result.NonFiniteScalarCount != 0u)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite scalar curvature values.";
                return JobResultEnvelope::Make<EditorJobResult>(
                    EditorJobResult{
                        .Diagnostic = result.Message,
                    });
            }

            state->CurvatureAfter = state->CurvatureBefore;
            StageMeshCurvatureScalars(
                curvature,
                state->CurvatureAfter,
                result);

            if (result.DirectionsRequested &&
                result.DirectionsAvailable)
            {
                if (!curvature.PrincipalDir1Property ||
                    !curvature.PrincipalDir2Property ||
                    curvature.PrincipalDir1Property.Vector().size() !=
                        result.VertexSlotCount ||
                    curvature.PrincipalDir2Property.Vector().size() !=
                        result.VertexSlotCount)
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.Curvature produced missing or "
                                     "count-mismatched principal-direction properties.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }

                const std::vector<glm::vec3>& dir1 =
                    curvature.PrincipalDir1Property.Vector();
                const std::vector<glm::vec3>& dir2 =
                    curvature.PrincipalDir2Property.Vector();
                result.NonFiniteDirectionCount =
                    CountNonFiniteVectors(
                        std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                    CountNonFiniteVectors(
                        std::span<const glm::vec3>{dir2.data(), dir2.size()});
                if (result.NonFiniteDirectionCount != 0u)
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Geometry.Curvature produced non-finite principal directions.";
                    return JobResultEnvelope::Make<EditorJobResult>(
                        EditorJobResult{
                            .Diagnostic = result.Message,
                        });
                }

                state->CurvatureAfter.HadDir1 = true;
                state->CurvatureAfter.Dir1 = dir1;
                state->CurvatureAfter.HadDir2 = true;
                state->CurvatureAfter.Dir2 = dir2;
                result.DirectionPropertyCount = 2u;
                result.DirectionWrittenCount = dir1.size() + dir2.size();
            }

            result.Status = EditorCommandStatus::Applied;
            result.DirectionsPublished =
                result.DirectionPropertyCount == 2u &&
                result.DirectionWrittenCount == result.VertexSlotCount * 2u;
            result.Error = Core::ErrorCode::Success;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = "Mesh curvature CPU result ready",
                });
        }

        [[nodiscard]] Core::Result PublishMeshCurvatureCpuJob(
            const EditorProcessingContext& context,
            EditorMeshCurvatureJobState& job)
        {
            EditorMeshCurvatureResult result = job.CurvatureResult;
            if (!result.Succeeded())
            {
                PublishMeshCurvatureResultSink(job, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            result.ChangedValueCount = CountChangedCurvatureValues(
                job.CurvatureBefore, job.CurvatureAfter);
            if (result.ChangedValueCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshCurvatureNoChangeMessage(result);
                PublishMeshCurvatureResultSink(job, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshCurvatureProperties(
                    context,
                    job.StableEntityId,
                    std::move(job.SnapshotPositions),
                    std::move(job.CurvatureBefore),
                    std::move(job.CurvatureAfter));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Mesh curvature property publication failed during editor "
                                 "history commit.";
                PublishMeshCurvatureResultSink(job, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.DirectionsPublished =
                result.DirectionPropertyCount == 2u &&
                result.DirectionWrittenCount == result.VertexSlotCount * 2u;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshCurvatureSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshCurvatureResultSink(job, result);
            return Core::Ok();
        }

        // A curvature job that terminates without publishing — cancelled, stale,
        // or dropped — owes the editor exactly one terminal result. This hook
        // replaces the submit-time `Pending` state with that result, matching
        // the reconciliation contract of the other queued runtime owners.
        void FinalizeUnpublishedMeshCurvatureJob(
            EditorMeshCurvatureJobState& job)
        {
            if (job.TerminalResultPublished)
                return;
            auto failure = BuildUnpublishedEditorJobFailure(
                job.LastApplyValidation, kMeshCurvatureJobName);
            EditorMeshCurvatureResult result = job.CurvatureResult;
            result.Status = failure.Status;
            result.Error = failure.Error;
            result.Message = std::move(failure.Message);
            PublishMeshCurvatureResultSink(job, std::move(result));
        }

        // Dedup identity omits `SourcePropertyGeneration`; the dedup guard does
        // not compare it, while `ValidateMeshCurvatureJobApply` rechecks source
        // staleness immediately before apply.
        [[nodiscard]] EditorJobIdentity MakeMeshCurvatureJobIdentity(
            const EditorMeshCurvatureJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.StableEntityId,
                .Scope = EditorJobScope::MeshSurface,
                .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                .OutputName = SerializeMeshCurvatureConfig(state.CurvatureCommand),
            };
        }

        [[nodiscard]] JobDesc MakeMeshCurvatureJobDesc(
            const EditorProcessingContext& context,
            const std::shared_ptr<EditorMeshCurvatureJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SnapshotPositions.size(),
                                  state->Mesh.FaceCount()) +
                         1023u) /
                        1024u));
            return JobDesc{
                .DebugName = kMeshCurvatureJobName,
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = estimatedCost,
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunMeshCurvatureCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        const JobApplyValidation validation =
                            ValidateMeshCurvatureJobApply(context, *state);
                        state->LastApplyValidation = validation;
                        return validation;
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishMeshCurvatureCpuJob(context, *state).has_value();
                    },
                .FinalizeUnpublishedOnMainThread =
                    [state]()
                    {
                        FinalizeUnpublishedMeshCurvatureJob(*state);
                    },
            };
        }

        [[nodiscard]] EditorMeshCurvatureResult
        SubmitMeshCurvatureCpuJob(
            const EditorProcessingContext& context,
            const EditorMeshCurvatureCommand& command,
            MeshProcessingSourceResult source,
            MeshCurvaturePropertyState before,
            const std::uint64_t geometryMetadataSignature,
            std::function<void(EditorMeshCurvatureResult)> onComplete)
        {
            const auto vertexSlotCount = source.BeforePositions.size();
            auto state = std::make_shared<EditorMeshCurvatureJobState>();
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions =
                std::move(source.BeforePositions);
            state->Mesh = std::move(source.Mesh);
            state->CurvatureBefore = std::move(before);
            state->CurvatureAfter = state->CurvatureBefore;
            state->CurvatureCommand = command;
            state->CurvatureResult = MakeMeshCurvatureBaseResult(
                command,
                context.MeshCurvatureDirectionsAvailable);
            state->CurvatureResult.VertexSlotCount = vertexSlotCount;

            const EditorJobIdentity identity =
                MakeMeshCurvatureJobIdentity(*state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                // The active job already owns the callback that will deliver
                // this output's terminal result; a duplicate request adds none.
                EditorMeshCurvatureResult pending =
                    MakePendingMeshCurvatureResult(
                        command,
                        context.MeshCurvatureDirectionsAvailable,
                        vertexSlotCount,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh curvature CPU", *active);
                return pending;
            }

            state->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc = MakeMeshCurvatureJobDesc(context, state);

            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshCurvatureResult result =
                    MakeMeshCurvatureBaseResult(
                        command,
                        context.MeshCurvatureDirectionsAvailable);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.VertexSlotCount = vertexSlotCount;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message         = "Mesh curvature CPU job submission was rejected by the "
                                         "runtime job lane.";
                return result;
            }

            return MakePendingMeshCurvatureResult(
                command,
                context.MeshCurvatureDirectionsAvailable,
                vertexSlotCount,
                handle);
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveMeshFieldCommandTarget(
            const EditorProcessingContext& context, const EditorMeshCurvatureCommand& command,
            EditorMeshCurvatureResult& result)
        {
            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Scene registry is unavailable for mesh curvature.";
                return std::nullopt;
            }
            if (!context.MeshCurvatureKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Geometry.Curvature mesh curvature is unavailable in this "
                                 "runtime configuration.";
                return std::nullopt;
            }

            const bool validOutput =
                std::find(kMeshCurvatureOutputs.begin(),
                          kMeshCurvatureOutputs.end(),
                          command.Output) != kMeshCurvatureOutputs.end();
            if (!validOutput)
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Mesh curvature requires a valid output mode.";
                return std::nullopt;
            }

            if (!IsValidMeshCurvaturePropertyBindings(command))
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Curvature requires distinct typed mesh vertex input/output properties.";
                return std::nullopt;
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message = command.StableEntityId == 0u
                    ? "Choose a mesh entity to compute curvature."
                    : "Mesh curvature target entity is stale or no longer live.";
                return std::nullopt;
            }

            const auto view = GS::BuildConstView(raw, *entity);
            auto status = ValidateMeshSoupSourceMetadata(view, result.Message, command.Positions.Name);
            if (status == EditorCommandStatus::Applied)
            {
                status = ValidateMeshVertexDeletionMaskMetadata(view, result.Message, command.Positions.Name);
                if (status != EditorCommandStatus::Applied)
                    result.Message = "Mesh curvature: " + result.Message;
            }
            if (status != EditorCommandStatus::Applied)
            {
                if (status == EditorCommandStatus::InvalidProcessingParameters)
                    if (const auto positions = view.VertexSource->Properties.Get<glm::vec3>(command.Positions.Name))
                        result.VertexSlotCount = positions.Vector().size();
                result.Status = status;
                result.Error = Core::ErrorCode::InvalidArgument;
                return std::nullopt;
            }
            return entity;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveMeshFieldCommandTarget(
            const EditorProcessingContext& context, const EditorCurvatureSegmentationCommand& command,
            EditorCurvatureSegmentationResult& result)
        {
            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Scene registry is unavailable for curvature segmentation.";
                return std::nullopt;
            }
            if (!context.CurvatureSegmentationKernelAvailable)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Geometry curvature segmentation is unavailable in this runtime configuration.";
                return std::nullopt;
            }
            if (!IsValidCurvatureSegmentationConfig(command.Config))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Segmentation requires valid ranges, distinct typed outputs, and at most three numeric feature channels. Feature-curve methods require computed curvature.";
                return std::nullopt;
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message = command.StableEntityId == 0u
                    ? "Choose a mesh entity to run segmentation."
                    : "Curvature segmentation target entity is stale or no longer live.";
                return std::nullopt;
            }

            const auto view = GS::BuildConstView(raw, *entity);
            const auto status = ValidateSegmentationSourceMetadata(view, command.Config.Positions.Name, result.Message);
            if (status != EditorCommandStatus::Applied)
            {
                result.Status = status;
                result.Error = Core::ErrorCode::InvalidArgument;
                return std::nullopt;
            }
            if (!ValidateSegmentationFeatureMetadata(view, command.Config, result.Message))
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                return std::nullopt;
            }
            return entity;
        }

    [[nodiscard]] bool PrepareBoundMeshFields(
        const EditorProcessingContext& context, entt::entity entity,
        const GeometryPropertyRef& positions, const CurvatureSegmentationConfig* segmentation,
        std::string& diagnostic)
    {
        using namespace GeometryProcessingDetail;
        using D = GeometryElementDomain;
        const auto availability = BuildGeometryAvailability(context.Scene->Raw(), entity);
        if (!PrepareMeshSoupFaceRings(context, entity, availability, diagnostic, positions))
        {
            diagnostic = std::string{segmentation ? "Curvature segmentation: " : "Mesh curvature: "} + diagnostic;
            return false;
        }
        std::vector<PointPropertyWatch> inputs;
        const auto watch = [&](D domain, std::string_view name) {
            inputs.push_back(ObserveGeometryProperty(availability, domain, std::string{name}));
        };
        watch(D::MeshVertex, positions.Name);
        watch(D::MeshVertex, "v:deleted");
        watch(D::MeshHalfedge, GS::PropertyNames::kHalfedgeToVertex);
        watch(D::MeshHalfedge, GS::PropertyNames::kHalfedgeNext);
        watch(D::MeshHalfedge, GS::PropertyNames::kHalfedgeFace);
        watch(D::MeshFace, GS::PropertyNames::kFaceHalfedge);
        std::vector<GeometryPropertyRef> bindings;
        if (segmentation)
        {
            bindings = segmentation->Features;
            watch(D::MeshEdge, GS::PropertyNames::kEdgeV0);
            watch(D::MeshEdge, GS::PropertyNames::kEdgeV1);
            watch(D::MeshEdge, "e:deleted");
            for (const auto& ref : bindings) watch(ref.Domain, ref.Name);
        }
        const auto validate = [positions, config = segmentation
            ? std::optional<CurvatureSegmentationConfig>{*segmentation} : std::nullopt](
                const GeometryEntityAvailability& current, std::string& why) {
            if (!config)
            {
                auto source = BuildHalfedgeMeshForProcessing(current.SourceView, "Mesh curvature", positions.Name);
                why = std::move(source.Diagnostic);
                return source.Succeeded();
            }
            auto source = BuildHalfedgeMeshForCurvatureSegmentation(current.SourceView, positions.Name);
            if (!source.Succeeded()) { why = std::move(source.Diagnostic); return false; }
            if (config->Features.empty()) return true;
            std::vector<glm::dvec3> features;
            std::uint32_t dimension{};
            return CaptureSegmentationFaceFeatures(current.SourceView, *config, source, features, dimension, why);
        };
        return PrepareMeshFieldInput(context, entity, availability, positions, segmentation != nullptr, std::move(bindings),
                                     std::move(inputs), validate, diagnostic);
    }

} // namespace Extrinsic::Runtime::MeshFieldDetail

namespace Extrinsic::Runtime
{
    using namespace MeshFieldDetail;

    ActionReadiness PreviewEditorMeshCurvatureCommand(
        const EditorProcessingCommands& commands, const EditorMeshCurvatureCommand& command)
    {
        EditorMeshCurvatureResult result{};
        const auto entity = ResolveMeshFieldCommandTarget(
            EditorProcessingCommandsAccess::Resolve(commands), command, result);
        const bool ready = entity && PrepareBoundMeshFields(
            EditorProcessingCommandsAccess::Resolve(commands), *entity, command.Positions, nullptr, result.Message);
        return {ready, std::move(result.Message)};
    }

    ActionReadiness PreviewEditorCurvatureSegmentationCommand(
        const EditorProcessingCommands& commands, const EditorCurvatureSegmentationCommand& command)
    {
        EditorCurvatureSegmentationResult result{};
        const auto entity = ResolveMeshFieldCommandTarget(
            EditorProcessingCommandsAccess::Resolve(commands), command, result);
        const bool ready = entity && PrepareBoundMeshFields(
            EditorProcessingCommandsAccess::Resolve(commands), *entity, command.Config.Positions, &command.Config, result.Message);
        return {ready, std::move(result.Message)};
    }

    EditorMeshCurvatureResult
ApplyEditorMeshCurvatureCommand(
        const EditorProcessingCommands& commands,
        const EditorMeshCurvatureCommand& command,
        std::function<void(EditorMeshCurvatureResult)> onComplete)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorMeshCurvatureResult result =
            MakeMeshCurvatureBaseResult(
                command,
                context.MeshCurvatureDirectionsAvailable);

        const auto entity = ResolveMeshFieldCommandTarget(context, command, result);
        if (!entity) return result;
        entt::registry& raw = context.Scene->Raw();

        const GS::ConstSourceView constView = GS::BuildConstView(raw, *entity);
        MeshProcessingSourceResult source =
            BuildHalfedgeMeshForProcessing(constView, "Mesh curvature", command.Positions.Name);
        result.VertexSlotCount = source.BeforePositions.size();
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        GS::MutableSourceView publishView = GS::BuildMutableView(raw, *entity);
        if (!publishView.Valid() || publishView.VertexSource == nullptr)
        {
            result.Status = EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh curvature target has no writable vertex GeometrySources.";
            return result;
        }

        MeshCurvaturePropertyState before{};
        std::string captureDiagnostic{};
        if (!CaptureMeshCurvaturePropertyState(
                publishView.VertexSource->Properties,
                result.VertexSlotCount,
                before,
                captureDiagnostic,
                command))
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message = captureDiagnostic;
            return result;
        }

        if (context.JobCommands.Available())
        {
            return SubmitMeshCurvatureCpuJob(
                context,
                command,
                std::move(source),
                std::move(before),
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(onComplete));
        }

        Curv::CurvatureField curvature = Curv::ComputeCurvature(source.Mesh);
        if (!MeshCurvatureScalarsUsable(curvature, result.VertexSlotCount))
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Geometry.Curvature produced missing or count-mismatched "
                             "scalar properties.";
            return result;
        }

        CopyMeshCurvatureDiagnostics(curvature, result);
        if (result.SupportedVertexCount == 0u)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature found no reliable curvature support "
                "(degenerate faces=" +
                std::to_string(result.DegenerateFaceCount) +
                ", ill-conditioned faces=" +
                std::to_string(result.IllConditionedFaceCount) +
                ", unsupported faces=" +
                std::to_string(result.UnsupportedFaceCount) + ").";
            return result;
        }

        result.NonFiniteScalarCount =
            CountNonFiniteMeshCurvatureScalars(curvature);
        if (result.NonFiniteScalarCount != 0u)
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature produced non-finite scalar curvature values.";
            return result;
        }

        MeshCurvaturePropertyState after = before;
        StageMeshCurvatureScalars(curvature, after, result);

        if (result.DirectionsRequested &&
            result.DirectionsAvailable)
        {
            if (!curvature.PrincipalDir1Property ||
                !curvature.PrincipalDir2Property ||
                curvature.PrincipalDir1Property.Vector().size() !=
                    result.VertexSlotCount ||
                curvature.PrincipalDir2Property.Vector().size() !=
                    result.VertexSlotCount)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Geometry.Curvature produced missing or "
                                 "count-mismatched principal-direction properties.";
                return result;
            }

            const std::vector<glm::vec3>& dir1 =
                curvature.PrincipalDir1Property.Vector();
            const std::vector<glm::vec3>& dir2 =
                curvature.PrincipalDir2Property.Vector();
            result.NonFiniteDirectionCount =
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir2.data(), dir2.size()});
            if (result.NonFiniteDirectionCount != 0u)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite principal directions.";
                return result;
            }

            after.HadDir1 = true;
            after.Dir1 = dir1;
            after.HadDir2 = true;
            after.Dir2 = dir2;
            result.DirectionPropertyCount = 2u;
            result.DirectionWrittenCount = dir1.size() + dir2.size();
        }

        result.ChangedValueCount = CountChangedCurvatureValues(before, after);
        if (result.ChangedValueCount == 0u)
        {
            // Nothing differs, so there is nothing to commit; publishing an
            // identity edit would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshCurvatureNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshCurvatureProperties(
                context,
                command.StableEntityId,
                std::move(source.BeforePositions),
                std::move(before),
                std::move(after));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Mesh curvature property publication failed during editor "
                             "history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.DirectionsPublished =
            result.DirectionPropertyCount == 2u &&
            result.DirectionWrittenCount == result.VertexSlotCount * 2u;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshCurvatureSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorCurvatureSegmentationResult
    ApplyEditorCurvatureSegmentationCommand(
        const EditorProcessingCommands& commands,
        const EditorCurvatureSegmentationCommand& command)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorCurvatureSegmentationResult result{
            .Config = command.Config,
            .RequestedMethod = command.Config.Method,
            .ActualMethod = command.Config.Method,
        };
        const auto entity = ResolveMeshFieldCommandTarget(context, command, result);
        if (!entity) return result;
        entt::registry& raw = context.Scene->Raw();

        const GS::ConstSourceView constView =
            GS::BuildConstView(raw, *entity);
        MeshCurvatureSegmentationSourceResult source =
            BuildHalfedgeMeshForCurvatureSegmentation(constView, command.Config.Positions.Name);
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = std::move(source.Diagnostic);
            return result;
        }

        GS::MutableSourceView publishView =
            GS::BuildMutableView(raw, *entity);
        if (publishView.FaceSource == nullptr ||
            publishView.EdgeSource == nullptr)
        {
            result.Status =
                EditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Curvature segmentation target has no writable face and edge GeometrySources.";
            return result;
        }

        MeshCurvatureSegmentationPropertyState before{};
        std::string captureDiagnostic{};
        if (!CaptureMeshCurvatureSegmentationPropertyState(
                publishView.FaceSource->Properties,
                publishView.EdgeSource->Properties,
                source.FaceSlotCount,
                source.EdgeSlotCount,
                before,
                captureDiagnostic, command.Config))
        {
            result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message = std::move(captureDiagnostic);
            return result;
        }

        std::vector<std::uint32_t> faceComponents{};
        std::vector<std::uint32_t> faceRegions{};
        std::vector<glm::vec4> faceRegionColors{};
        std::vector<std::uint8_t> edgeBoundaries{};
        std::vector<glm::vec4> edgeBoundaryColors{};
        std::vector<std::uint8_t> hardFeatureMask(
            source.Mesh.EdgesSize(), 0u);
        std::vector<double> softFeatureConfidence(
            source.Mesh.EdgesSize(), 0.0);
        std::vector<std::uint32_t> edgeBoundaryRoles(
            source.Mesh.EdgesSize(), 0u);
        std::vector<glm::vec4> featurePatchColors(
            source.Mesh.EdgesSize(), glm::vec4{0.0f});

        if (command.Config.Method ==
            CurvatureSegmentationMethod::CurvatureGmm)
        {
            CurvSeg::CurvatureSegmentationResult segmented;
            if (command.Config.Features.empty())
                segmented = CurvSeg::ComputeAndSegment(source.Mesh, MakeCurvatureSegmentationParams(command.Config));
            else
            {
                std::vector<glm::dvec3> features;
                std::uint32_t dimension{};
                if (!CaptureSegmentationFaceFeatures(constView, command.Config, source, features, dimension, result.Message))
                {
                    result.Status = EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    return result;
                }
                segmented = CurvSeg::SegmentFaceFeatures(source.Mesh, features, dimension,
                    MakeCurvatureSegmentationParams(command.Config));
            }
            result.Diagnostics = segmented.Diagnostics;
            if (!segmented.Succeeded())
            {
                switch (segmented.Diagnostics.Status)
                {
                case CurvSeg::SegmentationStatus::EmptyMesh:
                case CurvSeg::SegmentationStatus::UnsupportedSubmeshView:
                    result.Status =
                        EditorCommandStatus::UnsupportedGeometryDomain;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    break;
                case CurvSeg::SegmentationStatus::InvalidParameters:
                case CurvSeg::SegmentationStatus::FeatureCountMismatch:
                case CurvSeg::SegmentationStatus::NonTriangleFace:
                case CurvSeg::SegmentationStatus::NonFinitePosition:
                case CurvSeg::SegmentationStatus::DegenerateFace:
                case CurvSeg::SegmentationStatus::NonFiniteFeature:
                    result.Status =
                        EditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    break;
                case CurvSeg::SegmentationStatus::GaussianMixtureFitFailed:
                case CurvSeg::SegmentationStatus::PosteriorEvaluationFailed:
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::Unknown;
                    break;
                case CurvSeg::SegmentationStatus::Success:
                    break;
                }
                result.Message = "Property GMM segmentation failed: ";
                result.Message +=
                    CurvSeg::ToString(segmented.Diagnostics.Status);
                result.Message += ".";
                return result;
            }
            faceComponents = std::move(segmented.FaceComponents);
            faceRegions = std::move(segmented.FaceRegions);
            faceRegionColors = std::move(segmented.FaceRegionColors);
            edgeBoundaries = std::move(segmented.EdgeBoundaries);
            edgeBoundaryColors =
                std::move(segmented.EdgeBoundaryColors);
            for (std::size_t edge = 0u;
                 edge < edgeBoundaries.size(); ++edge)
            {
                if (edgeBoundaries[edge] != 0u)
                {
                    edgeBoundaryRoles[edge] = static_cast<std::uint32_t>(
                        CurvSeg::PatchBoundaryRole::CurvatureClosure);
                    featurePatchColors[edge] =
                        edgeBoundaryColors[edge];
                }
            }
        }
        else
        {
            Curv::CurvatureField curvature =
                Curv::ComputeCurvature(source.Mesh);
            if (!MeshCurvatureScalarsUsable(
                    curvature, source.Mesh.VerticesSize()) ||
                curvature.Diagnostics.SupportedVertexCount == 0u ||
                CountNonFiniteMeshCurvatureScalars(curvature) != 0u)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Feature-aligned segmentation could not compute a finite, supported curvature field.";
                return result;
            }

            CurvSeg::FeatureEvidenceResult featureEvidence =
                CurvSeg::DetectFeatureEvidence(
                    source.Mesh,
                    curvature.MaxPrincipalCurvatureProperty.Vector(),
                    curvature.MinPrincipalCurvatureProperty.Vector(),
                    MakeFeatureEvidenceParams(command.Config));
            result.FeatureDiagnostics = featureEvidence.Diagnostics;
            if (!featureEvidence.Succeeded())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Feature evidence detection failed: ";
                result.Message +=
                    CurvSeg::ToString(featureEvidence.Diagnostics.Status);
                result.Message += ".";
                return result;
            }

            if (command.Config.Method == CurvatureSegmentationMethod::FeatureBoundaryCurves)
            {
                auto partition = CurvSeg::PartitionFeatureBoundaries(
                    source.Mesh, featureEvidence.View(),
                    CurvSeg::BoundaryCurveCoverageProfileV1(),
                    curvature.MaxPrincipalCurvatureProperty.Vector(),
                    curvature.MinPrincipalCurvatureProperty.Vector());
                result.BoundaryDiagnostics = partition.Diagnostics;
                if (!partition.Succeeded())
                {
                    result.Status = EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Experimental METHOD-040 failed: ";
                    result.Message += CurvSeg::ToString(partition.Diagnostics.Status);
                    return result;
                }
                const auto& diagnostic = partition.Diagnostics;
                result.Diagnostics.ConnectedRegionCount =
                    static_cast<std::uint32_t>(diagnostic.RegionCount);
                result.Diagnostics.BoundaryEdgeCount = diagnostic.BoundaryCount;
                faceRegions = std::move(partition.FaceRegions);
                faceComponents.assign(source.Mesh.FacesSize(), CurvSeg::kInvalidLabel);
                faceRegionColors.resize(faceRegions.size());
                for (std::size_t face = 0; face < faceRegions.size(); ++face)
                {
                    if (faceRegions[face] == CurvSeg::kInvalidLabel)
                        continue;
                    const float phase = 2.39996323f * static_cast<float>(faceRegions[face]);
                    faceRegionColors[face] = glm::vec4{
                        0.6f + 0.35f * std::cos(phase),
                        0.6f + 0.35f * std::cos(phase + 2.0943951f),
                        0.6f + 0.35f * std::cos(phase + 4.1887902f), 1.0f};
                }
                edgeBoundaries = std::move(partition.EdgeBoundaries);
                edgeBoundaryColors.assign(edgeBoundaries.size(), glm::vec4{0.0f});
                hardFeatureMask = std::move(featureEvidence.HardEdgeMask);
                softFeatureConfidence = std::move(featureEvidence.SoftEdgeConfidence);
                for (std::size_t edge = 0; edge < edgeBoundaries.size(); ++edge)
                {
                    if (edgeBoundaries[edge] == 0u)
                        continue;
                    const auto role = hardFeatureMask[edge]
                        ? CurvSeg::PatchBoundaryRole::HardFeature
                        : softFeatureConfidence[edge] > 0.0
                            ? CurvSeg::PatchBoundaryRole::SoftFeatureSupported
                            : CurvSeg::PatchBoundaryRole::CurvatureClosure;
                    edgeBoundaryRoles[edge] = static_cast<std::uint32_t>(role);
                    const glm::vec4 color = hardFeatureMask[edge]
                        ? glm::vec4{1.0f, 0.15f, 0.1f, 1.0f}
                        : softFeatureConfidence[edge] > 0.0
                            ? glm::vec4{1.0f, 0.75f, 0.1f, 1.0f}
                            : glm::vec4{0.15f, 0.45f, 1.0f, 1.0f};
                    edgeBoundaryColors[edge] = color;
                    featurePatchColors[edge] = color;
                }
            }
            else
            {
                CurvSeg::CurvaturePatchResult patches =
                    CurvSeg::SegmentFeatureAlignedPatches(
                        source.Mesh,
                        curvature.MaxPrincipalCurvatureProperty.Vector(),
                        curvature.MinPrincipalCurvatureProperty.Vector(),
                        featureEvidence.View(),
                        MakeCurvaturePatchParams(command.Config));
                result.PatchDiagnostics = patches.Diagnostics;
                if (!patches.Succeeded())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message =
                        "Feature-aligned patch segmentation failed: ";
                    result.Message +=
                        CurvSeg::ToString(patches.Diagnostics.Status);
                    result.Message += ".";
                    return result;
                }

                result.Diagnostics.Status =
                    CurvSeg::SegmentationStatus::Success;
                result.Diagnostics.FaceSlotCount =
                    patches.Diagnostics.FaceSlotCount;
                result.Diagnostics.LiveFaceCount =
                    patches.Diagnostics.LiveFaceCount;
                result.Diagnostics.EdgeSlotCount =
                    patches.Diagnostics.EdgeSlotCount;
                result.Diagnostics.LiveEdgeCount =
                    patches.Diagnostics.LiveEdgeCount;
                result.Diagnostics.SelectedComponentCount =
                    patches.Diagnostics.SelectedComponentCount;
                result.Diagnostics.ActiveComponentCount =
                    static_cast<std::uint32_t>(
                        patches.Diagnostics.Components.size());
                result.Diagnostics.ConnectedRegionCount =
                    static_cast<std::uint32_t>(
                        patches.Diagnostics.FinalRegionCount);
                result.Diagnostics.BoundaryEdgeCount =
                    patches.Diagnostics.FinalBoundaryEdgeCount;
                result.Diagnostics.InitialEnergy =
                    patches.Diagnostics.InitialEnergy;
                result.Diagnostics.FinalEnergy =
                    patches.Diagnostics.FinalEnergy;
                result.Diagnostics.Candidates = patches.Diagnostics.Candidates;
                result.Diagnostics.Components = patches.Diagnostics.Components;

                faceComponents = std::move(patches.FaceComponents);
                faceRegions = std::move(patches.FaceRegions);
                faceRegionColors = std::move(patches.FaceRegionColors);
                edgeBoundaries = std::move(patches.EdgeBoundaries);
                edgeBoundaryColors =
                    std::move(patches.EdgeBoundaryColors);
                hardFeatureMask = std::move(featureEvidence.HardEdgeMask);
                softFeatureConfidence =
                    std::move(featureEvidence.SoftEdgeConfidence);
                for (std::size_t edge = 0u;
                     edge < patches.EdgeBoundaryRoles.size(); ++edge)
                {
                    edgeBoundaryRoles[edge] = static_cast<std::uint32_t>(
                        patches.EdgeBoundaryRoles[edge]);
                    if (edgeBoundaries[edge] != 0u)
                    {
                        featurePatchColors[edge] = edgeBoundaryColors[edge];
                    }
                }
            }
        }

        if (faceComponents.size() != source.Mesh.FacesSize() ||
            faceRegions.size() != source.Mesh.FacesSize() ||
            faceRegionColors.size() != source.Mesh.FacesSize() ||
            edgeBoundaries.size() != source.Mesh.EdgesSize() ||
            edgeBoundaryColors.size() != source.Mesh.EdgesSize() ||
            hardFeatureMask.size() != source.Mesh.EdgesSize() ||
            softFeatureConfidence.size() != source.Mesh.EdgesSize() ||
            edgeBoundaryRoles.size() != source.Mesh.EdgesSize() ||
            featurePatchColors.size() != source.Mesh.EdgesSize())
        {
            result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Curvature segmentation returned outputs that do not match detached mesh cardinality.";
            return result;
        }

        MeshCurvatureSegmentationPropertyState after = before;
        after.HadComponent =
            command.Config.Method != CurvatureSegmentationMethod::FeatureBoundaryCurves;
        after.HadRegion = true;
        after.HadRegionColor = true;
        after.HadBoundary = true;
        after.HadBoundaryColor = true;
        after.HadHardFeature = true;
        after.HadSoftFeatureConfidence = true;
        after.HadBoundaryRole = true;
        after.HadFeaturePatchColor = true;
        // A boundary partition has no fitted curvature components. Remove an
        // earlier method-owned component field in the same undo transaction.
        after.Components.clear();
        if (after.HadComponent)
            after.Components.assign(source.FaceSlotCount, CurvSeg::kInvalidLabel);
        after.Regions.assign(
            source.FaceSlotCount, CurvSeg::kInvalidLabel);
        after.RegionColors.assign(
            source.FaceSlotCount, glm::vec4{0.0f});
        after.Boundaries.assign(source.EdgeSlotCount, false);
        after.BoundaryColors.assign(
            source.EdgeSlotCount, glm::vec4{0.0f});
        after.HardFeatures.assign(source.EdgeSlotCount, false);
        after.SoftFeatureConfidences.assign(
            source.EdgeSlotCount, 0.0);
        after.BoundaryRoles.assign(source.EdgeSlotCount, 0u);
        after.FeaturePatchColors.assign(
            source.EdgeSlotCount, glm::vec4{0.0f});

        for (std::size_t meshFace = 0u;
             meshFace < source.SourceFaceForMeshFace.size();
             ++meshFace)
        {
            const std::uint32_t sourceFace =
                source.SourceFaceForMeshFace[meshFace];
            if (sourceFace >= source.FaceSlotCount)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Curvature segmentation produced an invalid source-face cross-reference.";
                return result;
            }
            if (after.HadComponent)
                after.Components[sourceFace] = faceComponents[meshFace];
            after.Regions[sourceFace] =
                faceRegions[meshFace];
            after.RegionColors[sourceFace] =
                faceRegionColors[meshFace];
        }
        for (std::size_t meshEdge = 0u;
             meshEdge < source.SourceEdgeForMeshEdge.size();
             ++meshEdge)
        {
            const std::uint32_t sourceEdge =
                source.SourceEdgeForMeshEdge[meshEdge];
            if (sourceEdge == CurvSeg::kInvalidLabel)
                continue;
            if (sourceEdge >= source.EdgeSlotCount)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Curvature segmentation produced an invalid source-edge cross-reference.";
                return result;
            }
            after.Boundaries[sourceEdge] =
                edgeBoundaries[meshEdge] != 0u;
            after.BoundaryColors[sourceEdge] =
                edgeBoundaryColors[meshEdge];
            after.HardFeatures[sourceEdge] =
                hardFeatureMask[meshEdge] != 0u;
            after.SoftFeatureConfidences[sourceEdge] =
                softFeatureConfidence[meshEdge];
            after.BoundaryRoles[sourceEdge] =
                edgeBoundaryRoles[meshEdge];
            after.FeaturePatchColors[sourceEdge] =
                featurePatchColors[meshEdge];
        }

        result.ChangedValueCount =
            CountChangedCurvatureSegmentationValues(before, after);
        if (result.ChangedValueCount == 0u)
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message =
                "Curvature segmentation properties are already up to date; no undo entry was created.";
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshCurvatureSegmentationProperties(
                context,
                command.StableEntityId,
                std::move(source.SourcePositions),
                source.FaceSlotCount,
                source.EdgeSlotCount,
                std::move(before),
                std::move(after));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Curvature segmentation publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = DebugNameForCurvatureSegmentationMethod(
            result.ActualMethod);
        result.Message += " published ";
        result.Message += std::to_string(
            result.Diagnostics.ConnectedRegionCount);
        result.Message += " connected regions and ";
        result.Message += std::to_string(
            result.Diagnostics.BoundaryEdgeCount);
        result.Message += " internal boundary edges";
        if (result.BoundaryDiagnostics.has_value())
        {
            result.Message += "; experimental curves_v1, adoption oracle not passed";
        }
        else
        {
            result.Message += " (GMM components=";
            result.Message += std::to_string(result.Diagnostics.SelectedComponentCount);
            result.Message += ")";
        }
        if (result.PatchDiagnostics.has_value() &&
            result.FeatureDiagnostics.has_value())
        {
            result.Message += "; features=";
            result.Message += std::to_string(
                result.FeatureDiagnostics->HardFeatureEdgeCount);
            result.Message += " hard + ";
            result.Message += std::to_string(
                result.FeatureDiagnostics->RetainedSoftEdgeCount);
            result.Message += " soft, boundary roles=";
            result.Message += std::to_string(
                result.PatchDiagnostics->HardBoundaryEdgeCount);
            result.Message += "/";
            result.Message += std::to_string(
                result.PatchDiagnostics->SoftBoundaryEdgeCount);
            result.Message += "/";
            result.Message += std::to_string(
                result.PatchDiagnostics->ClosureBoundaryEdgeCount);
            result.Message += " hard/soft/closure";
        }
        result.Message += ".";
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorCurvatureSegmentationResult
    ApplyEditorConfiguredCurvatureSegmentationCommand(
        const EditorProcessingCommands& commands,
        const std::uint32_t stableEntityId)
    {
        const std::optional<CurvatureSegmentationConfig> config =
            GetEditorCurvatureSegmentationConfig(commands);
        if (!config.has_value())
        {
            EditorCurvatureSegmentationResult result{};
            result.Status =
                EditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "No validated curvature-segmentation config is active.";
            return result;
        }
        return ApplyEditorCurvatureSegmentationCommand(
            commands,
            EditorCurvatureSegmentationCommand{
                .StableEntityId = stableEntityId,
                .Config = *config,
            });
    }

} // namespace Extrinsic::Runtime
