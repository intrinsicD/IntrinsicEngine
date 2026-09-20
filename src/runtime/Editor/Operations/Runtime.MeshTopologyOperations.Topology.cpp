// Denoise, remesh, subdivide and simplify: the four operations that rebuild the
// entity's halfedge mesh. Their shared scratch-mesh source, UV-preservation
// reporting and replacement commit are what make them one family.
module;
#include <functional>
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <expected>
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

module Extrinsic.Runtime.MeshTopologyOperations;

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
import Geometry.CatmullClark;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.AdaptiveRemeshing;
import Geometry.HalfedgeMesh.SubdivisionSqrt3;
import Geometry.HalfedgeMesh.Utils;
import Geometry.MeshOperator;
import Geometry.Properties;
import Geometry.Remeshing;
import Geometry.Simplification;
import Geometry.Smoothing;
import Geometry.Subdivision;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSupport.hpp"

namespace Extrinsic::Runtime::MeshTopologyDetail
{
    namespace
    {
        bool IsPositiveFinite(const double value) noexcept
        {
            return std::isfinite(value) && value > 0.0;
        }
    }

        using namespace GeometryProcessingDetail::MeshSupport;
        namespace Smooth = Geometry::Smoothing;
        namespace Remesh = Geometry::Remeshing;
        namespace AdaptiveRemesh = Geometry::AdaptiveRemeshing;
        namespace LoopSubdivide = Geometry::Subdivision;
        namespace CatmullClark = Geometry::CatmullClark;
        namespace Sqrt3Subdivide = Geometry::SubdivisionSqrt3;
        namespace Simpl = Geometry::Simplification;
        inline constexpr std::array<EditorMeshRemeshMode, 2>
            kMeshRemeshModes{{
                EditorMeshRemeshMode::Uniform,
                EditorMeshRemeshMode::Adaptive,
            }};
        inline constexpr std::array<EditorMeshDenoiseStage, 1>
            kMeshDenoiseStages{{
                EditorMeshDenoiseStage::FullBilateral,
            }};

        inline constexpr std::array<EditorMeshRemeshSizingLaw, 2>
            kMeshRemeshSizingLaws{{
                EditorMeshRemeshSizingLaw::MeanCurvature,
                EditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
            }};
        inline constexpr std::array<EditorMeshSimplifyMetric, 2>
            kMeshSimplifyMetrics{{
                EditorMeshSimplifyMetric::ClassicalQEM,
                EditorMeshSimplifyMetric::FA_QEM,
            }};
        inline constexpr std::array<EditorMeshSubdivideOperator, 3>
            kMeshSubdivideOperators{{
                EditorMeshSubdivideOperator::Loop,
                EditorMeshSubdivideOperator::CatmullClark,
                EditorMeshSubdivideOperator::Sqrt3,
            }};

        [[nodiscard]] Core::ErrorCode ErrorForDenoiseStatus(
            const Smooth::DenoiseStatus status) noexcept
        {
            switch (status)
            {
            case Smooth::DenoiseStatus::Success:
                return Core::ErrorCode::Success;
            case Smooth::DenoiseStatus::EmptyMesh:
                return Core::ErrorCode::ResourceNotFound;
            case Smooth::DenoiseStatus::NonManifoldInput:
            case Smooth::DenoiseStatus::DegenerateGeometry:
            case Smooth::DenoiseStatus::NonFiniteInput:
            case Smooth::DenoiseStatus::InvalidParams:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] bool AllFiniteVec3(
            const std::span<const glm::vec3> values) noexcept
        {
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteGeometryPosition(value))
                    return false;
            }
            return true;
        }

        struct MeshDenoiseMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            MeshPositionState Positions{};
        };

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshDenoisePositionState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::vector<glm::vec3>& positions)
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

            auto currentPositions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!currentPositions ||
                currentPositions.Vector().size() != positions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    positions.data(),
                    positions.size()}))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            currentPositions.Vector() = positions;
            return EditorCommandHistoryStatus::Applied;
        }

        void StampMeshDenoisePositionDirty(
            ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId)
        {
            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandStatus CommitMeshDenoisePositions(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> before,
            std::vector<glm::vec3> after)
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

                const MeshPositionState beforeState =
                    std::make_shared<std::vector<glm::vec3>>(
                        std::move(before));
                const MeshPositionState afterState =
                    std::make_shared<std::vector<glm::vec3>>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        "Denoise mesh vertices",
                        MeshPropertyMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        MeshDenoiseMutationGeneration{
                            .GeometryMetadataSignature =
                                GeometryMetadataSignatureForEntity(
                                    context.Scene->Raw(),
                                    entity),
                            .Positions = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshDenoiseMutationGeneration& expected,
                            const MeshPositionState&)
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

                            const GS::ConstSourceView view =
                                GS::BuildConstView(raw, entity);
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
                                GeometryMetadataSignatureForEntity(raw, entity) !=
                                    expected.GeometryMetadataSignature)
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const auto positions =
                                view.VertexSource->Properties.Get<glm::vec3>(
                                    GS::PropertyNames::kPosition);
                            if (!positions ||
                                !SameGeometryPositions(
                                    positions.Vector(),
                                    *expected.Positions))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshPositionState& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyMeshDenoisePositionState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshDenoiseMutationGeneration&,
                            const MeshPositionState& target)
                        {
                            StampMeshDenoisePositionDirty(
                                *identity.Scene,
                                identity.StableEntityId);
                            const ECS::EntityHandle entity =
                                SelectionController::ToEntityHandle(
                                    identity.StableEntityId);
                            return MeshDenoiseMutationGeneration{
                                .GeometryMetadataSignature =
                                    GeometryMetadataSignatureForEntity(
                                        identity.Scene->Raw(),
                                        entity),
                                .Positions = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyMeshDenoisePositionState(
                    context.Scene,
                    stableEntityId,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            StampMeshDenoisePositionDirty(
                *context.Scene,
                stableEntityId);
            return EditorCommandStatus::Applied;
        }

        void CopyMeshDenoiseCounters(
            const Smooth::BilateralDenoiseResult& source,
            EditorMeshDenoiseResult& target)
        {
            // SkippedDeletedVertexCount belongs to source capture, not the kernel result.
            target.NormalIterations =
                static_cast<std::uint32_t>(
                    source.NormalIterationsPerformed);
            target.VertexIterations =
                static_cast<std::uint32_t>(
                    source.VertexIterationsPerformed);
            target.VertexSlotCount = source.VertexCount;
            target.MovedVertexCount = source.MovedVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.PinnedBoundaryVertexCount =
                source.PinnedBoundaryVertexCount;
            target.SigmaSpatialUsed = source.SigmaSpatialUsed;
            target.SigmaRangeUsed = source.SigmaRangeUsed;
        }

        [[nodiscard]] std::string BuildMeshDenoiseSuccessMessage(
            const EditorMeshDenoiseResult& result)
        {
            std::string message = "Mesh denoise completed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", moved=";
            message += std::to_string(result.MovedVertexCount);
            message += ", sigmaSpatial=";
            message += std::to_string(result.SigmaSpatialUsed);
            message += ", sigmaRange=";
            message += std::to_string(result.SigmaRangeUsed);
            message += ").";
            return message;
        }

        // A run that writes every slot but moves nothing is not a success. The
        // written count is slot-derived, so it stays non-zero whenever the
        // kernel ran at all and cannot distinguish the two.
        [[nodiscard]] std::string BuildMeshDenoiseNoChangeMessage(
            const EditorMeshDenoiseResult& result)
        {
            const std::size_t live =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;
            std::string message = "Mesh denoise moved no vertices; the mesh is unchanged";
            if (result.PinnedBoundaryVertexCount >= live && live > 0u)
            {
                message += " because all ";
                message += std::to_string(result.PinnedBoundaryVertexCount);
                message += " vertices are pinned as boundary, leaving nothing to smooth";
            }
            else if (result.PinnedBoundaryVertexCount > 0u)
            {
                message += " (pinned boundary vertices: ";
                message += std::to_string(result.PinnedBoundaryVertexCount);
                message += " of ";
                message += std::to_string(live);
                message += ")";
            }
            else
            {
                message += "; no vertex had an interior neighborhood that changed it";
            }
            message += ". Written=";
            message += std::to_string(result.WrittenCount);
            message += ", moved=0.";
            return message;
        }

        [[nodiscard]] bool ValidMeshRemeshMode(
            const EditorMeshRemeshMode mode) noexcept
        {
            return std::find(kMeshRemeshModes.begin(),
                             kMeshRemeshModes.end(),
                             mode) != kMeshRemeshModes.end();
        }

        [[nodiscard]] bool ValidMeshRemeshSizingLaw(
            const EditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            return std::find(kMeshRemeshSizingLaws.begin(),
                             kMeshRemeshSizingLaws.end(),
                             sizingLaw) != kMeshRemeshSizingLaws.end();
        }

        [[nodiscard]] bool ValidMeshSubdivideOperator(
            const EditorMeshSubdivideOperator op) noexcept
        {
            return std::find(kMeshSubdivideOperators.begin(),
                             kMeshSubdivideOperators.end(),
                             op) != kMeshSubdivideOperators.end();
        }

        [[nodiscard]] bool ValidMeshSimplifyMetric(
            const EditorMeshSimplifyMetric metric) noexcept
        {
            return std::find(kMeshSimplifyMetrics.begin(),
                             kMeshSimplifyMetrics.end(),
                             metric) != kMeshSimplifyMetrics.end();
        }


        struct MeshTopologyMutationGeneration
        {
            std::uint64_t GeometryMetadataSignature{0u};
            // `std::nullopt` means the stored topology could not be read, which
            // no later reading may match. See `MeshTopologyValueSignature`.
            std::optional<std::uint64_t> TopologySignature{};
            MeshTopologySnapshot Mesh{};
        };

        [[nodiscard]] bool SameStoredMeshTopology(
            const GS::ConstSourceView& view,
            const std::optional<std::uint64_t>& expected)
        {
            const std::optional<std::uint64_t> current =
                MeshTopologyValueSignature(view);
            return current.has_value() && expected.has_value() &&
                   *current == *expected;
        }

        // Compares stored vertex slots with a job or history mesh. Vertex
        // numbering survives the GeometrySources -> triangle-soup -> halfedge
        // round-trip, so this comparison is representation-faithful. Edge,
        // halfedge, and face state uses `MeshTopologyValueSignature`, which
        // fingerprints the stored arrays directly.
        [[nodiscard]] bool SameMeshVertexState(
            const GS::ConstSourceView& view,
            const Geometry::HalfedgeMesh::Mesh& mesh) noexcept
        {
            if (view.VertexSource == nullptr ||
                view.EdgeSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr ||
                view.VertexSource->Properties.Size() != mesh.VerticesSize() ||
                view.VertexSource->NumDeleted != mesh.DeletedVertexCount())
            {
                return false;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions)
                return false;

            if (!SameKnownPropertyValues(
                    Geometry::ConstPropertySet(
                        view.VertexSource->Properties),
                    mesh.VertexProperties()))
            {
                return false;
            }

            for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
            {
                const glm::vec3& current = positions.Vector()[i];
                const glm::vec3& expected =
                    mesh.Position(Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(i)});
                if (current.x != expected.x ||
                    current.y != expected.y ||
                    current.z != expected.z)
                {
                    return false;
                }
            }
            return true;
        }


        [[nodiscard]] EditorCommandStatus CommitMeshTopologyReplacement(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            const std::uint64_t expectedGeometryMetadataSignature,
            Geometry::HalfedgeMesh::Mesh before,
            Geometry::HalfedgeMesh::Mesh after)
        {
            if (before.HasGarbage())
                before.GarbageCollection();
            if (after.HasGarbage())
                after.GarbageCollection();

            if (context.CommandHistory != nullptr)
            {
                // Read the stored topology before the apply rewrites it: this
                // is the "what we expect to still be there" side of the first
                // undo transition, and it comes from the sources rather than
                // from `before`, which is a re-derivation of them.
                const std::optional<std::uint64_t> beforeTopology =
                    context.Scene != nullptr
                        ? StoredMeshTopologySignatureForEntity(
                              context.Scene->Raw(),
                              stableEntityId)
                        : std::nullopt;
                const MeshTopologySnapshot beforeState =
                    std::make_shared<Geometry::HalfedgeMesh::Mesh>(
                        std::move(before));
                const MeshTopologySnapshot afterState =
                    std::make_shared<Geometry::HalfedgeMesh::Mesh>(
                        std::move(after));
                const EditorCommandHistoryResult history =
                    Internal::ExecuteUndoableEntityMutation(
                        *context.CommandHistory,
                        label,
                        MeshPropertyMutationIdentity{
                            .Scene = context.Scene,
                            .World = context.World,
                            .StableEntityId = stableEntityId,
                        },
                        MeshTopologyMutationGeneration{
                            .GeometryMetadataSignature =
                                expectedGeometryMetadataSignature,
                            .TopologySignature = beforeTopology,
                            .Mesh = beforeState,
                        },
                        beforeState,
                        afterState,
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshTopologyMutationGeneration& expected,
                            const MeshTopologySnapshot& target)
                        {
                            if (identity.Scene == nullptr ||
                                !identity.World.IsValid())
                            {
                                return EditorCommandHistoryStatus::MissingScene;
                            }

                            entt::registry& raw = identity.Scene->Raw();
                            const std::optional<ECS::EntityHandle> entity =
                                ResolveStableEntity(
                                    raw,
                                    identity.StableEntityId);
                            if (!entity.has_value())
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }

                            const GS::ConstSourceView view =
                                GS::BuildConstView(raw, *entity);
                            const GS::SourceAvailability availability =
                                GS::BuildSourceAvailability(view);
                            if (availability.ProvenanceDomain !=
                                GS::Domain::Mesh)
                            {
                                return EditorCommandHistoryStatus::
                                    UnsupportedOperation;
                            }
                            if (expected.Mesh == nullptr || target == nullptr)
                            {
                                return EditorCommandHistoryStatus::CommandFailed;
                            }
                            if (GeometryMetadataSignatureForEntity(
                                    raw,
                                    *entity) !=
                                    expected.GeometryMetadataSignature ||
                                !SameMeshVertexState(view, *expected.Mesh) ||
                                !SameStoredMeshTopology(
                                    view,
                                    expected.TopologySignature))
                            {
                                return EditorCommandHistoryStatus::StaleEntity;
                            }
                            return EditorCommandHistoryStatus::Applied;
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshTopologySnapshot& target)
                        {
                            if (target == nullptr)
                            {
                                return EditorCommandHistoryStatus::
                                    CommandFailed;
                            }
                            return ApplyMeshTopologyState(
                                identity.Scene,
                                identity.StableEntityId,
                                *target);
                        },
                        [](
                            const MeshPropertyMutationIdentity& identity,
                            const MeshTopologyMutationGeneration&,
                            const MeshTopologySnapshot& target)
                        {
                            entt::registry& raw = identity.Scene->Raw();
                            const std::optional<ECS::EntityHandle> entity =
                                ResolveStableEntity(
                                    raw,
                                    identity.StableEntityId);
                            if (entity.has_value())
                                MarkMeshTopologyReplacementDirty(raw, *entity);
                            return MeshTopologyMutationGeneration{
                                .GeometryMetadataSignature =
                                    entity.has_value()
                                        ? GeometryMetadataSignatureForEntity(
                                              raw,
                                              *entity)
                                        : 0u,
                                // Re-read from the sources the apply just
                                // wrote, so the next undo/redo transition
                                // compares against the numbering that is
                                // actually stored rather than a re-derivation
                                // of it.
                                .TopologySignature =
                                    entity.has_value()
                                        ? MeshTopologyValueSignature(
                                              GS::BuildConstView(raw, *entity))
                                        : std::nullopt,
                                .Mesh = target,
                            };
                        });
                return ToEditorCommandStatus(history.Status);
            }

            const EditorCommandHistoryStatus applied =
                ApplyMeshTopologyState(
                    context.Scene,
                    stableEntityId,
                    after);
            if (applied != EditorCommandHistoryStatus::Applied)
                return ToEditorCommandStatus(applied);
            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (entity.has_value())
                MarkMeshTopologyReplacementDirty(raw, *entity);
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] AdaptiveRemesh::SizingLaw ToAdaptiveSizingLaw(
            const EditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            switch (sizingLaw)
            {
            case EditorMeshRemeshSizingLaw::MeanCurvature:
                return AdaptiveRemesh::SizingLaw::MeanCurvature;
            case EditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
                return AdaptiveRemesh::SizingLaw::ErrorBoundedTaubin;
            }
            return AdaptiveRemesh::SizingLaw::MeanCurvature;
        }

        void CopyRemeshCounters(
            const Geometry::RemeshingOperationResult& source,
            EditorMeshRemeshResult& target)
        {
            target.IterationsPerformed =
                static_cast<std::uint32_t>(source.IterationsPerformed);
            target.OutputVertexCount = source.FinalVertexCount;
            target.OutputFaceCount = source.FinalFaceCount;
            target.SplitCount = source.SplitCount;
            target.CollapseCount = source.CollapseCount;
            target.FlipCount = source.FlipCount;
        }

        // A topology operation commits a whole replacement mesh, so
        // the honest change signal is whether that mesh differs from the one it
        // replaced. Counts decide neither direction on their own: an edge flip
        // and a tangential relaxation pass each change the mesh while leaving
        // vertex and face counts identical, and reporting `NoChange` for either
        // would silently drop the user's edit. Storage sizes are compared
        // rather than live counts, so a mesh still carrying garbage reads as
        // different — the conservative direction, which reports `Applied`.

        [[nodiscard]] std::string BuildMeshTopologyNoChangeMessage(
            const char* const operation,
            const std::size_t vertexCount,
            const std::size_t faceCount,
            const std::string& work)
        {
            std::string message = "Mesh ";
            message += operation;
            message += " left the mesh unchanged (";
            message += std::to_string(vertexCount);
            message += " vertices, ";
            message += std::to_string(faceCount);
            message += " faces, ";
            message += work;
            message += "). Nothing was published and no undo entry was "
                       "created.";
            return message;
        }

        [[nodiscard]] std::string BuildMeshRemeshNoChangeMessage(
            const EditorMeshRemeshResult& result)
        {
            std::string work = std::to_string(result.IterationsPerformed);
            work += " iterations, ";
            work += std::to_string(result.SplitCount);
            work += " splits, ";
            work += std::to_string(result.CollapseCount);
            work += " collapses, ";
            work += std::to_string(result.FlipCount);
            work += " flips";
            return BuildMeshTopologyNoChangeMessage(
                "remesh",
                result.OutputVertexCount,
                result.OutputFaceCount,
                work);
        }

        [[nodiscard]] std::string BuildMeshSimplifyNoChangeMessage(
            const EditorMeshSimplifyResult& result)
        {
            std::string work = std::to_string(result.CollapseCount);
            work += " collapses, ";
            work += std::to_string(result.CollapsesRejectedTopology);
            work += " rejected on topology, ";
            work += std::to_string(result.CollapsesRejectedQuality);
            work += " rejected on quality";
            return BuildMeshTopologyNoChangeMessage(
                "simplify",
                result.OutputVertexCount,
                result.OutputFaceCount,
                work);
        }

        // A discarded parameterization is a user-visible loss, so it
        // is named in the message and not left to the enum alone.
        [[nodiscard]] std::string AppendedTexcoordDiscardSentence(
            const EditorMeshTexcoordOutcome outcome)
        {
            if (outcome != EditorMeshTexcoordOutcome::Discarded)
                return {};
            return " The mesh's UV parameterization was discarded: this "
                   "operation replaces the topology and cannot resample UVs "
                   "onto it. Re-parameterize the result if you need UVs.";
        }

        [[nodiscard]] std::string BuildMeshRemeshSuccessMessage(
            const EditorMeshRemeshResult& result)
        {
            std::string message = "Mesh remesh completed (mode=";
            message += DebugNameForEditorMeshRemeshMode(result.Mode);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", ";
            message += DebugNameForEditorMeshTexcoordOutcome(
                result.TexcoordOutcome);
            message += ").";
            message += AppendedTexcoordDiscardSentence(result.TexcoordOutcome);
            return message;
        }

        [[nodiscard]] std::string BuildMeshSubdivideSuccessMessage(
            const EditorMeshSubdivideResult& result)
        {
            std::string message = "Mesh subdivide completed (operator=";
            message += DebugNameForEditorMeshSubdivideOperator(
                result.Operator);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ", ";
            message += DebugNameForEditorMeshTexcoordOutcome(
                result.TexcoordOutcome);
            message += ").";
            message += AppendedTexcoordDiscardSentence(result.TexcoordOutcome);
            return message;
        }

        [[nodiscard]] std::string BuildMeshSimplifySuccessMessage(
            const EditorMeshSimplifyResult& result)
        {
            std::string message = "Mesh simplify completed (metric=";
            message += DebugNameForEditorMeshSimplifyMetric(
                result.Metric);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", collapses=";
            message += std::to_string(result.CollapseCount);
            message += ", ";
            message += DebugNameForEditorMeshTexcoordOutcome(
                result.TexcoordOutcome);
            message += ").";
            message += AppendedTexcoordDiscardSentence(result.TexcoordOutcome);
            return message;
        }

        [[nodiscard]] EditorMeshDenoiseResult MakeMeshDenoiseBaseResult(
            const EditorMeshDenoiseCommand& command)
        {
            return EditorMeshDenoiseResult{
                .Status = EditorCommandStatus::NoChange,
                .DenoiseStatus = Smooth::DenoiseStatus::Success,
                .Stage = command.Stage,
                .NormalIterations = command.NormalIterations,
                .VertexIterations = command.VertexIterations,
                .SigmaSpatial = command.SigmaSpatial,
                .SigmaRange = command.SigmaRange,
                .PreserveBoundary = command.PreserveBoundary,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshRemeshResult MakeMeshRemeshBaseResult(
            const EditorMeshRemeshCommand& command)
        {
            return EditorMeshRemeshResult{
                .Status = EditorCommandStatus::NoChange,
                .Mode = command.Mode,
                .SizingLaw = command.SizingLaw,
                .IterationsRequested = command.Iterations,
                .TargetEdgeLength = command.TargetEdgeLength,
                .ProjectToSurface = command.ProjectToSurface,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshSubdivideResult
        MakeMeshSubdivideBaseResult(
            const EditorMeshSubdivideCommand& command)
        {
            return EditorMeshSubdivideResult{
                .Status = EditorCommandStatus::NoChange,
                .Operator = command.Operator,
                .IterationsRequested = command.Iterations,
                .PreserveLoopFeatureEdges = command.PreserveLoopFeatureEdges,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshSimplifyResult MakeMeshSimplifyBaseResult(
            const EditorMeshSimplifyCommand& command)
        {
            return EditorMeshSimplifyResult{
                .Status = EditorCommandStatus::NoChange,
                .Metric = command.Metric,
                .TargetFaces = command.TargetFaces,
                .MaxError = command.MaxError,
                .Error = Core::ErrorCode::Success,
            };
        }

        [[nodiscard]] EditorMeshDenoiseResult MakePendingMeshDenoiseResult(
            const EditorMeshDenoiseCommand& command,
            const MeshProcessingSourceResult& source,
            const JobToken handle)
        {
            EditorMeshDenoiseResult result =
                MakeMeshDenoiseBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.VertexSlotCount = source.BeforePositions.size();
            result.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(source.DeletedVertices.begin(),
                               source.DeletedVertices.end(),
                               true));
            result.WrittenCount =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;
            result.Message = "Mesh denoise CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshRemeshResult MakePendingMeshRemeshResult(
            const EditorMeshRemeshCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const JobToken handle)
        {
            EditorMeshRemeshResult result =
                MakeMeshRemeshBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh remesh CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshSubdivideResult
        MakePendingMeshSubdivideResult(
            const EditorMeshSubdivideCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const JobToken handle)
        {
            EditorMeshSubdivideResult result =
                MakeMeshSubdivideBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh subdivide CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        [[nodiscard]] EditorMeshSimplifyResult
        MakePendingMeshSimplifyResult(
            const EditorMeshSimplifyCommand& command,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const JobToken handle)
        {
            EditorMeshSimplifyResult result =
                MakeMeshSimplifyBaseResult(command);
            result.Status = EditorCommandStatus::Pending;
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();
            result.Message = "Mesh simplify CPU job queued";
            AppendDerivedJobHandleToMessage(result.Message, handle);
            result.Message += ".";
            return result;
        }

        enum class EditorMeshCpuJobKind : std::uint8_t
        {
            Denoise,
            Remesh,
            Subdivide,
            Simplify,
        };

        [[nodiscard]] const char* MeshCpuJobName(
            const EditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorMeshCpuJobKind::Denoise:
                return "Sandbox.MeshDenoise.CPU";
            case EditorMeshCpuJobKind::Remesh:
                return "Sandbox.MeshRemesh.CPU";
            case EditorMeshCpuJobKind::Subdivide:
                return "Sandbox.MeshSubdivide.CPU";
            case EditorMeshCpuJobKind::Simplify:
                return "Sandbox.MeshSimplify.CPU";
            }
            return "Sandbox.MeshProcessing.CPU";
        }

        [[nodiscard]] const char* MeshCpuJobOutputName(
            const EditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorMeshCpuJobKind::Denoise:
                return "mesh_denoise_positions";
            case EditorMeshCpuJobKind::Remesh:
                return "mesh_remesh_topology";
            case EditorMeshCpuJobKind::Subdivide:
                return "mesh_subdivide_topology";
            case EditorMeshCpuJobKind::Simplify:
                return "mesh_simplify_topology";
            }
            return "mesh_processing";
        }

        [[nodiscard]] GeometryPresentationSlotSemantic MeshCpuJobOutputSemantic(
            const EditorMeshCpuJobKind kind) noexcept
        {
            switch (kind)
            {
            case EditorMeshCpuJobKind::Denoise:
            case EditorMeshCpuJobKind::Remesh:
            case EditorMeshCpuJobKind::Subdivide:
            case EditorMeshCpuJobKind::Simplify:
                return GeometryPresentationSlotSemantic::Displacement;
            }
            return GeometryPresentationSlotSemantic::Displacement;
        }

        // Forward the mesh's corner UVs into a scratch halfedge mesh.
        //
        // A scratch mesh is rebuilt from a triangle soup and starts with no
        // properties at all, and `ApplyMeshTopologyState` publishes its
        // halfedge properties wholesale — so anything not forwarded here is not
        // merely stale afterwards, it is removed from the entity. Vertex
        // numbering survives the round trip; halfedge numbering does not, so
        // corner UVs go through the canonical corner walk rather than a copy.
        //
        // Used by simplify, which preserves rather than resamples (an edge
        // collapse removes corners and the survivors keep their own UVs), and
        // by UV regeneration's before-state, which needs the mesh's existing
        // corner UVs to tell a genuine no-op from a change.
        // Operations that *create* corners (remesh, subdivide) have no source
        // UV for them and must not use this path.

        // Determines whether a mesh carries UVs a topology edit could destroy.
        // Follows the canonical corner-over-vertex order, and ignores a
        // property whose size does not match its domain, exactly as every
        // reader does.
        [[nodiscard]] bool MeshHasResolvableTexcoords(
            const GS::ConstSourceView& view) noexcept
        {
            if (view.HalfedgeSource != nullptr)
            {
                const Geometry::PropertySet& corners =
                    view.HalfedgeSource->Properties;
                if (const auto uvs = corners.Get<glm::vec2>(
                        Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
                    uvs && uvs.Vector().size() == corners.Size())
                {
                    return true;
                }
            }
            if (view.VertexSource != nullptr)
            {
                const Geometry::PropertySet& vertices =
                    view.VertexSource->Properties;
                if (const auto uvs = vertices.Get<glm::vec2>("v:texcoord");
                    uvs && uvs.Vector().size() == vertices.Size())
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] EditorMeshTexcoordOutcome
        CopyMeshSimplifyAuxiliaryProperties(
            const GS::ConstSourceView& view,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (view.VertexSource == nullptr)
                return EditorMeshTexcoordOutcome::None;

            const bool hadTexcoords = MeshHasResolvableTexcoords(view);
            bool carried = CopyStoredCornerTexcoordsToScratchMesh(view, mesh);

            const auto sourceTexcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (sourceTexcoords &&
                sourceTexcoords.Vector().size() == mesh.VerticesSize())
            {
                auto meshTexcoords = mesh.VertexProperties().GetOrAdd<glm::vec2>(
                    "v:texcoord",
                    glm::vec2{0.0f});
                for (std::size_t i = 0u;
                     i < sourceTexcoords.Vector().size();
                     ++i)
                {
                    meshTexcoords[i] = sourceTexcoords.Vector()[i];
                }
                carried = true;
            }

            if (!hadTexcoords)
                return EditorMeshTexcoordOutcome::None;
            return carried
                ? EditorMeshTexcoordOutcome::Preserved
                : EditorMeshTexcoordOutcome::Discarded;
        }

        struct EditorMeshCpuJobState
        {
            EditorMeshCpuJobKind Kind{
                EditorMeshCpuJobKind::Denoise};
            std::uint32_t StableEntityId{0u};
            std::uint64_t GeometryMetadataSignature{0u};
            std::vector<glm::vec3> SnapshotPositions{};
            std::vector<bool> DeletedVertices{};
            // Stored-topology fingerprint taken at submit. `std::nullopt` means
            // the topology could not be read, which no later reading may match.
            // See `MeshTopologyValueSignature`.
            std::optional<std::uint64_t> TopologySignature{};
            Geometry::HalfedgeMesh::Mesh BeforeMesh{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> DenoiseAfterPositions{};
            EditorMeshDenoiseCommand DenoiseCommand{};
            EditorMeshRemeshCommand RemeshCommand{};
            EditorMeshSubdivideCommand SubdivideCommand{};
            EditorMeshSimplifyCommand SimplifyCommand{};
            EditorMeshDenoiseResult DenoiseResult{};
            EditorMeshRemeshResult RemeshResult{};
            EditorMeshSubdivideResult SubdivideResult{};
            EditorMeshSimplifyResult SimplifyResult{};
            // Terminal callback the caller supplied, guarded at submit. Only
            // the member matching `Kind` is set; every path that ends the job
            // delivers exactly one result through it.
            std::function<void(EditorMeshDenoiseResult)> DenoiseSink{};
            std::function<void(EditorMeshRemeshResult)> RemeshSink{};
            std::function<void(EditorMeshSubdivideResult)> SubdivideSink{};
            std::function<void(EditorMeshSimplifyResult)> SimplifySink{};
            // Last answer this job's `ValidateBeforeApply` gave the drain.
            // `FinalizeUnpublishedOnMainThread` takes no arguments, so the
            // reason a completion was refused has to be recorded where it was
            // decided; without it an unpublished job can only say "did not
            // apply". `Current` means the gate never rejected the result, so
            // the job ended for another reason — cancellation, or a publisher
            // that refused the envelope.
            JobApplyValidation LastApplyValidation{
                JobApplyValidation::Current};
            // Set the moment a terminal result reaches the caller's sink. A
            // publisher that reports a real failure still returns `Err`, which
            // `JobService` treats as unpublished and answers with the
            // unpublished finalizer; without this the finalizer would overwrite
            // that failure with a generic "did not apply" second result.
            bool TerminalResultPublished{false};
        };

        [[nodiscard]] JobApplyValidation ValidateMeshCpuJobApply(
            const EditorProcessingContext& context,
            const EditorMeshCpuJobState& job)
        {
            const JobApplyValidation source = ValidateMeshCpuJobSource(
                context,
                job.StableEntityId,
                job.GeometryMetadataSignature,
                job.SnapshotPositions,
                GS::PropertyNames::kPosition);
            if (source != JobApplyValidation::Current)
                return source;

            if (job.Kind == EditorMeshCpuJobKind::Remesh ||
                job.Kind == EditorMeshCpuJobKind::Subdivide ||
                job.Kind == EditorMeshCpuJobKind::Simplify)
            {
                entt::registry& raw = context.Scene->Raw();
                const std::optional<ECS::EntityHandle> entity =
                    ResolveStableEntity(raw, job.StableEntityId);
                if (!entity.has_value())
                    return JobApplyValidation::MissingTarget;

                const std::optional<std::uint64_t> current =
                    MeshTopologyValueSignature(GS::BuildConstView(raw, *entity));
                if (!current.has_value() ||
                    !job.TopologySignature.has_value() ||
                    *current != *job.TopologySignature)
                {
                    return JobApplyValidation::StaleGeneration;
                }
            }

            return JobApplyValidation::Current;
        }

        void PublishMeshDenoiseResultSink(
            EditorMeshCpuJobState& job,
            EditorMeshDenoiseResult result)
        {
            job.TerminalResultPublished = true;
            if (job.DenoiseSink)
                job.DenoiseSink(std::move(result));
        }

        void PublishMeshRemeshResultSink(
            EditorMeshCpuJobState& job,
            EditorMeshRemeshResult result)
        {
            job.TerminalResultPublished = true;
            if (job.RemeshSink)
                job.RemeshSink(std::move(result));
        }

        void PublishMeshSubdivideResultSink(
            EditorMeshCpuJobState& job,
            EditorMeshSubdivideResult result)
        {
            job.TerminalResultPublished = true;
            if (job.SubdivideSink)
                job.SubdivideSink(std::move(result));
        }

        void PublishMeshSimplifyResultSink(
            EditorMeshCpuJobState& job,
            EditorMeshSimplifyResult result)
        {
            job.TerminalResultPublished = true;
            if (job.SimplifySink)
                job.SimplifySink(std::move(result));
        }

        [[nodiscard]] bool ComputeMeshDenoise(
            const EditorMeshDenoiseCommand& command,
            Geometry::HalfedgeMesh::Mesh& mesh,
            const std::vector<glm::vec3>& snapshotPositions,
            const std::vector<bool>& deletedVertices,
            EditorMeshDenoiseResult& result,
            std::vector<glm::vec3>& outputPositions)
        {
            Smooth::BilateralDenoiseParams params{};
            params.NormalIterations = command.NormalIterations;
            params.VertexIterations = command.VertexIterations;
            params.SigmaSpatial = command.SigmaSpatial;
            params.SigmaRange = command.SigmaRange;
            params.PreserveBoundary = command.PreserveBoundary;
            params.DegenerateNormalLengthEpsilon =
                command.DegenerateNormalLengthEpsilon;

            const Smooth::BilateralDenoiseResult denoise =
                Smooth::DenoiseBilateral(mesh, params);
            result.DenoiseStatus = denoise.Status;
            result.Error = ErrorForDenoiseStatus(denoise.Status);
            CopyMeshDenoiseCounters(denoise, result);
            result.VertexSlotCount = snapshotPositions.size();
            result.WrittenCount =
                result.VertexSlotCount - result.SkippedDeletedVertexCount;

            if (denoise.Status != Smooth::DenoiseStatus::Success)
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Geometry.Smoothing denoise failed with ";
                result.Message += std::string(Smooth::DebugName(denoise.Status));
                result.Message += ".";
                return false;
            }

            std::vector<glm::vec3> afterPositions =
                ExtractMeshPositions(mesh);
            if (afterPositions.size() != snapshotPositions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    afterPositions.data(),
                    afterPositions.size()}))
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::NonFiniteInput;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message       = "Geometry.Smoothing denoise produced invalid or "
                                       "count-mismatched positions.";
                return false;
            }

            std::size_t movedPublishedVertices = 0u;
            for (std::size_t i = 0u; i < afterPositions.size(); ++i)
            {
                if (i < deletedVertices.size() &&
                    deletedVertices[i])
                {
                    afterPositions[i] = snapshotPositions[i];
                    continue;
                }
                if (afterPositions[i] != snapshotPositions[i])
                    ++movedPublishedVertices;
            }
            result.MovedVertexCount = movedPublishedVertices;
            result.Status = movedPublishedVertices == 0u
                ? EditorCommandStatus::NoChange
                : EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            outputPositions = std::move(afterPositions);
            return true;
        }

        [[nodiscard]] bool ComputeMeshRemesh(
            const EditorMeshRemeshCommand& command,
            Geometry::HalfedgeMesh::Mesh& mesh,
            EditorMeshRemeshResult& result)
        {
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();

            std::optional<Geometry::RemeshingOperationResult> remeshResult{};
            if (command.Mode == EditorMeshRemeshMode::Uniform)
            {
                Remesh::RemeshingParams params{};
                params.TargetLength = command.TargetEdgeLength;
                params.Iterations = command.Iterations;
                params.Lambda = command.Lambda;
                params.PreserveBoundary =
                    command.PreserveBoundary;
                params.ProjectToSurface =
                    command.ProjectToSurface;
                params.ReferenceProjectionK =
                    command.ReferenceProjectionK;
                params.MaxReferenceProjectionDistance =
                    command.MaxReferenceProjectionDistance;
                remeshResult = Remesh::Remesh(mesh, params);
            }
            else
            {
                AdaptiveRemesh::AdaptiveRemeshingParams params{};
                if (command.TargetEdgeLength > 0.0)
                {
                    params.MinEdgeLength =
                        command.TargetEdgeLength * 0.5;
                    params.MaxEdgeLength =
                        command.TargetEdgeLength * 2.0;
                }
                params.CurvatureAdaptation =
                    command.CurvatureAdaptation;
                params.Sizing =
                    ToAdaptiveSizingLaw(command.SizingLaw);
                params.ApproximationError =
                    command.ApproximationError;
                params.Iterations = command.Iterations;
                params.Lambda = command.Lambda;
                params.PreserveBoundary =
                    command.PreserveBoundary;
                params.EnableReferenceProjection =
                    command.ProjectToSurface;
                params.ReferenceProjectionK =
                    command.ReferenceProjectionK;
                params.MaxReferenceProjectionDistance =
                    command.MaxReferenceProjectionDistance;
                remeshResult = AdaptiveRemesh::AdaptiveRemesh(mesh, params);
            }

            if (!remeshResult.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry remeshing failed for the selected mesh and parameters.";
                return false;
            }

            if (mesh.HasGarbage())
                mesh.GarbageCollection();
            CopyRemeshCounters(*remeshResult, result);
            result.OutputVertexCount = mesh.VertexCount();
            result.OutputFaceCount = mesh.FaceCount();
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return true;
        }

        [[nodiscard]] bool ComputeMeshSubdivide(
            const EditorMeshSubdivideCommand& command,
            Geometry::HalfedgeMesh::Mesh& mesh,
            EditorMeshSubdivideResult& result)
        {
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();

            Geometry::HalfedgeMesh::Mesh output{};
            if (command.Operator ==
                EditorMeshSubdivideOperator::Loop)
            {
                LoopSubdivide::SubdivisionParams params{};
                params.Iterations = command.Iterations;
                params.MaxOutputFaces =
                    command.MaxOutputFaces;
                params.PreserveFeatureEdges =
                    command.PreserveLoopFeatureEdges;
                params.FeatureEdgePropertyName =
                    command.FeatureEdgePropertyName;
                const std::optional<LoopSubdivide::SubdivisionResult>
                    subdivision =
                        LoopSubdivide::Subdivide(mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.Subdivision Loop subdivision failed for the "
                                     "selected mesh and parameters.";
                    return false;
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }
            else if (command.Operator ==
                     EditorMeshSubdivideOperator::CatmullClark)
            {
                CatmullClark::SubdivisionParams params{};
                params.Iterations = command.Iterations;
                const std::optional<CatmullClark::SubdivisionResult>
                    subdivision =
                        CatmullClark::Subdivide(mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.CatmullClark subdivision failed for the "
                                     "selected mesh and parameters.";
                    return false;
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }
            else
            {
                Sqrt3Subdivide::Sqrt3Params params{};
                params.Iterations = command.Iterations;
                params.MaxOutputFaces =
                    command.MaxOutputFaces;
                const std::optional<Sqrt3Subdivide::Sqrt3Result>
                    subdivision =
                        Sqrt3Subdivide::Subdivide(mesh, output, params);
                if (!subdivision.has_value())
                {
                    result.Status =
                        EditorCommandStatus::GeometryProcessingFailed;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Message = "Geometry.HalfedgeMesh.SubdivisionSqrt3 failed for the "
                                     "selected mesh and parameters.";
                    return false;
                }
                result.IterationsPerformed =
                    static_cast<std::uint32_t>(
                        subdivision->IterationsPerformed);
                result.OutputVertexCount = subdivision->FinalVertexCount;
                result.OutputFaceCount = subdivision->FinalFaceCount;
            }

            if (output.HasGarbage())
                output.GarbageCollection();
            result.OutputVertexCount = output.VertexCount();
            result.OutputFaceCount = output.FaceCount();
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            mesh = std::move(output);
            return true;
        }

        [[nodiscard]] bool ComputeMeshSimplify(
            const EditorMeshSimplifyCommand& command,
            Geometry::HalfedgeMesh::Mesh& mesh,
            EditorMeshSimplifyResult& result)
        {
            result.InputVertexCount = mesh.VertexCount();
            result.InputFaceCount = mesh.FaceCount();

            Simpl::Params params{};
            params.Metric =
                command.Metric ==
                        EditorMeshSimplifyMetric::FA_QEM
                    ? Simpl::Metric::FA_QEM
                    : Simpl::Metric::ClassicalQEM;
            params.TargetFaces = command.TargetFaces;
            params.MaxError = command.MaxError > 0.0
                ? command.MaxError
                : 1.0e30;
            params.PreserveBoundary =
                command.PreserveBoundary;
            params.FeatureAngleThresholdDegrees =
                command.FeatureAngleThresholdDegrees;
            params.NormalWeight = command.NormalWeight;
            params.BoundaryWeight = command.BoundaryWeight;
            params.CurvatureWeight = command.CurvatureWeight;
            params.PreserveSharpFeatures =
                command.PreserveSharpFeatures;
            params.PreserveUvSeams =
                command.PreserveUvSeams;

            const std::optional<Simpl::Result> simplification =
                Simpl::Simplify(mesh, params);
            if (!simplification.has_value())
            {
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Simplification failed for the selected mesh and parameters.";
                return false;
            }

            if (mesh.HasGarbage())
                mesh.GarbageCollection();
            result.OutputVertexCount = mesh.VertexCount();
            result.OutputFaceCount = mesh.FaceCount();
            result.CollapseCount = simplification->CollapseCount;
            result.MaxCollapseError = simplification->MaxCollapseError;
            result.CollapsesRejectedTopology =
                simplification->CollapsesRejectedTopology;
            result.CollapsesRejectedQuality =
                simplification->CollapsesRejectedQuality;
            result.SharpFeatureVerticesPinned =
                simplification->SharpFeatureVerticesPinned;
            result.SeamVerticesPinned = simplification->SeamVerticesPinned;
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return true;
        }

        [[nodiscard]] JobResultEnvelope RunMeshCpuWorker(
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            switch (state->Kind)
            {
            case EditorMeshCpuJobKind::Denoise:
                return JobResultEnvelope::Make<EditorJobResult>(EditorJobResult{
                    .Diagnostic = ComputeMeshDenoise(
                        state->DenoiseCommand, state->Mesh,
                        state->SnapshotPositions, state->DeletedVertices,
                        state->DenoiseResult, state->DenoiseAfterPositions)
                        ? "Mesh denoise CPU result ready"
                        : state->DenoiseResult.Message});
            case EditorMeshCpuJobKind::Remesh:
                return JobResultEnvelope::Make<EditorJobResult>(EditorJobResult{
                    .Diagnostic = ComputeMeshRemesh(
                        state->RemeshCommand, state->Mesh, state->RemeshResult)
                        ? "Mesh remesh CPU result ready"
                        : state->RemeshResult.Message});
            case EditorMeshCpuJobKind::Subdivide:
                return JobResultEnvelope::Make<EditorJobResult>(EditorJobResult{
                    .Diagnostic = ComputeMeshSubdivide(
                        state->SubdivideCommand, state->Mesh, state->SubdivideResult)
                        ? "Mesh subdivide CPU result ready"
                        : state->SubdivideResult.Message});
            case EditorMeshCpuJobKind::Simplify:
                return JobResultEnvelope::Make<EditorJobResult>(EditorJobResult{
                    .Diagnostic = ComputeMeshSimplify(
                        state->SimplifyCommand, state->Mesh, state->SimplifyResult)
                        ? "Mesh simplify CPU result ready"
                        : state->SimplifyResult.Message});
            }
            return JobResultEnvelope{};
        }

        [[nodiscard]] Core::Result PublishMeshDenoiseCpuJob(
            const EditorProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshDenoiseResult result = job.DenoiseResult;
            if (!result.Succeeded() && result.Status != EditorCommandStatus::NoChange)
            {
                PublishMeshDenoiseResultSink(job, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (result.MovedVertexCount == 0u)
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshDenoiseNoChangeMessage(result);
                PublishMeshDenoiseResultSink(job, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshDenoisePositions(
                    context,
                    job.StableEntityId,
                    job.SnapshotPositions,
                    job.DenoiseAfterPositions);
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message = "Mesh denoise position publication failed during editor "
                                 "history commit.";
                PublishMeshDenoiseResultSink(job, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshDenoiseSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshDenoiseResultSink(job, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshRemeshCpuJob(
            const EditorProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshRemeshResult result = job.RemeshResult;
            if (!result.Succeeded())
            {
                PublishMeshRemeshResultSink(job, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (SameMeshTopologyAndPositions(job.BeforeMesh, job.Mesh))
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshRemeshNoChangeMessage(result);
                PublishMeshRemeshResultSink(job, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Remesh mesh",
                    job.GeometryMetadataSignature,
                    std::move(job.BeforeMesh),
                    std::move(job.Mesh));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh remesh publication failed during editor history commit.";
                PublishMeshRemeshResultSink(job, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshRemeshSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshRemeshResultSink(job, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshSubdivideCpuJob(
            const EditorProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshSubdivideResult result = job.SubdivideResult;
            if (!result.Succeeded())
            {
                PublishMeshSubdivideResultSink(job, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            // There is no `NoChange` gate here, unlike remesh and simplify.
            // Every implemented subdivision operator either refines — which
            // always raises the face count — or fails closed, including when
            // `MaxOutputFaces` blocks the first iteration. A changed-count gate
            // would be a branch no input can reach.
            const EditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Subdivide mesh",
                    job.GeometryMetadataSignature,
                    std::move(job.BeforeMesh),
                    std::move(job.Mesh));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh subdivide publication failed during editor history commit.";
                PublishMeshSubdivideResultSink(job, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSubdivideSuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshSubdivideResultSink(job, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshSimplifyCpuJob(
            const EditorProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            EditorMeshSimplifyResult result = job.SimplifyResult;
            if (!result.Succeeded())
            {
                PublishMeshSimplifyResultSink(job, result);
                return Core::Err(ResultErrorOrUnknown(result.Error));
            }

            if (SameMeshTopologyAndPositions(job.BeforeMesh, job.Mesh))
            {
                result.Status = EditorCommandStatus::NoChange;
                result.Error = Core::ErrorCode::Success;
                result.Message = BuildMeshSimplifyNoChangeMessage(result);
                PublishMeshSimplifyResultSink(job, result);
                return Core::Ok();
            }

            const EditorCommandStatus commitStatus =
                CommitMeshTopologyReplacement(
                    context,
                    job.StableEntityId,
                    "Simplify mesh",
                    job.GeometryMetadataSignature,
                    std::move(job.BeforeMesh),
                    std::move(job.Mesh));
            if (commitStatus != EditorCommandStatus::Applied)
            {
                result.Status = commitStatus;
                result.Error = Core::ErrorCode::Unknown;
                result.Message =
                    "Mesh simplify publication failed during editor history commit.";
                PublishMeshSimplifyResultSink(job, result);
                return Core::Err(Core::ErrorCode::Unknown);
            }

            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSimplifySuccessMessage(result);
            InvalidateSelectedModelCache(context);
            PublishMeshSimplifyResultSink(job, result);
            return Core::Ok();
        }

        [[nodiscard]] Core::Result PublishMeshCpuJob(
            const EditorProcessingContext& context,
            EditorMeshCpuJobState& job)
        {
            switch (job.Kind)
            {
            case EditorMeshCpuJobKind::Denoise:
                return PublishMeshDenoiseCpuJob(context, job);
            case EditorMeshCpuJobKind::Remesh:
                return PublishMeshRemeshCpuJob(context, job);
            case EditorMeshCpuJobKind::Subdivide:
                return PublishMeshSubdivideCpuJob(context, job);
            case EditorMeshCpuJobKind::Simplify:
                return PublishMeshSimplifyCpuJob(context, job);
            }
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        // A mesh CPU job that terminates without publishing — cancelled, stale,
        // or dropped — owes the editor exactly one terminal result. This hook
        // replaces the submit-time `Pending` state with that result, matching
        // the reconciliation contract of the other queued runtime owners.
        void FinalizeUnpublishedMeshCpuJob(
            EditorMeshCpuJobState& job)
        {
            // A publisher that already answered the caller reports the real
            // outcome; it returns `Err` for a failed commit, which is
            // "unpublished" to `JobService` but not to the editor.
            if (job.TerminalResultPublished)
                return;

            auto failure = BuildUnpublishedEditorJobFailure(
                job.LastApplyValidation, MeshCpuJobName(job.Kind));

            switch (job.Kind)
            {
            case EditorMeshCpuJobKind::Denoise:
            {
                EditorMeshDenoiseResult result = job.DenoiseResult;
                result.Status = failure.Status;
                result.Error = failure.Error;
                result.Message = std::move(failure.Message);
                PublishMeshDenoiseResultSink(job, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Remesh:
            {
                EditorMeshRemeshResult result = job.RemeshResult;
                result.Status = failure.Status;
                result.Error = failure.Error;
                result.Message = std::move(failure.Message);
                PublishMeshRemeshResultSink(job, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Subdivide:
            {
                EditorMeshSubdivideResult result = job.SubdivideResult;
                result.Status = failure.Status;
                result.Error = failure.Error;
                result.Message = std::move(failure.Message);
                PublishMeshSubdivideResultSink(job, std::move(result));
                return;
            }
            case EditorMeshCpuJobKind::Simplify:
            {
                EditorMeshSimplifyResult result = job.SimplifyResult;
                result.Status = failure.Status;
                result.Error = failure.Error;
                result.Message = std::move(failure.Message);
                PublishMeshSimplifyResultSink(job, std::move(result));
                return;
            }
            }
        }

        // Dedup identity omits `SourcePropertyGeneration`; the dedup guard does
        // not compare it, while `ValidateMeshCpuJobApply` rechecks source
        // staleness immediately before apply.
        [[nodiscard]] EditorJobIdentity MakeMeshCpuJobIdentity(
            const EditorMeshCpuJobState& state)
        {
            return EditorJobIdentity{
                .EntityId = state.StableEntityId,
                .Scope = EditorJobScope::MeshSurface,
                .OutputSemantic = MeshCpuJobOutputSemantic(state.Kind),
                .OutputName = std::string{MeshCpuJobOutputName(state.Kind)},
            };
        }

        [[nodiscard]] JobDesc MakeMeshCpuJobDesc(
            const EditorProcessingContext& context,
            const std::shared_ptr<EditorMeshCpuJobState>& state)
        {
            const std::uint32_t estimatedCost =
                std::max<std::uint32_t>(
                    1u,
                    static_cast<std::uint32_t>(
                        (std::max(state->SnapshotPositions.size(),
                                  state->BeforeMesh.FaceCount()) +
                         1023u) /
                        1024u));
            return JobDesc{
                .DebugName = MeshCpuJobName(state->Kind),
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = estimatedCost,
                .Work =
                    [state](const JobCancellation&) -> JobResultEnvelope
                    {
                        return RunMeshCpuWorker(state);
                    },
                .ValidateBeforeApply =
                    [context, state]()
                    {
                        const JobApplyValidation validation =
                            ValidateMeshCpuJobApply(context, *state);
                        state->LastApplyValidation = validation;
                        return validation;
                    },
                .PublishCompletion =
                    [context, state](KernelEventBus&,
                                     const JobResultEnvelope& result) -> bool
                    {
                        if (result.TryGet<EditorJobResult>() == nullptr)
                            return false;
                        return PublishMeshCpuJob(context, *state).has_value();
                    },
                .FinalizeUnpublishedOnMainThread =
                    [state]()
                    {
                        FinalizeUnpublishedMeshCpuJob(*state);
                    },
            };
        }

        [[nodiscard]] EditorMeshDenoiseResult SubmitMeshDenoiseCpuJob(
            const EditorProcessingContext& context,
            const EditorMeshDenoiseCommand& command,
            MeshProcessingSourceResult source,
            const std::uint64_t geometryMetadataSignature,
            std::function<void(EditorMeshDenoiseResult)> onComplete)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Denoise;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = std::move(source.BeforePositions);
            state->DeletedVertices = source.DeletedVertices;
            state->Mesh = std::move(source.Mesh);
            state->DenoiseCommand = command;
            state->DenoiseResult = MakeMeshDenoiseBaseResult(command);
            state->DenoiseResult.VertexSlotCount =
                state->SnapshotPositions.size();
            state->DenoiseResult.SkippedDeletedVertexCount =
                static_cast<std::size_t>(
                    std::count(state->DeletedVertices.begin(),
                               state->DeletedVertices.end(),
                               true));
            state->DenoiseResult.WrittenCount =
                state->DenoiseResult.VertexSlotCount -
                state->DenoiseResult.SkippedDeletedVertexCount;

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                MeshProcessingSourceResult pendingSource{};
                pendingSource.BeforePositions = state->SnapshotPositions;
                pendingSource.DeletedVertices = state->DeletedVertices;
                EditorMeshDenoiseResult pending =
                    MakePendingMeshDenoiseResult(
                        command,
                        pendingSource,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh denoise CPU", *active);
                return pending;
            }

            // The active job already owns the callback that will deliver this
            // output's terminal result; a duplicate request adds none.
            state->DenoiseSink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshDenoiseResult result =
                    MakeMeshDenoiseBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh denoise CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            MeshProcessingSourceResult pendingSource{};
            pendingSource.BeforePositions = state->SnapshotPositions;
            pendingSource.DeletedVertices = state->DeletedVertices;
            return MakePendingMeshDenoiseResult(
                command,
                pendingSource,
                handle);
        }

        [[nodiscard]] EditorMeshRemeshResult SubmitMeshRemeshCpuJob(
            const EditorProcessingContext& context,
            const EditorMeshRemeshCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature,
            const EditorMeshTexcoordOutcome texcoordOutcome,
            std::function<void(EditorMeshRemeshResult)> onComplete)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Remesh;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            // The apply gate compares stored topology
            // against the stored topology, so fingerprint it here rather than
            // re-deriving it from `BeforeMesh` later.
            state->TopologySignature =
                context.Scene != nullptr
                    ? StoredMeshTopologySignatureForEntity(
                          context.Scene->Raw(),
                          command.StableEntityId)
                    : std::nullopt;
            state->Mesh = std::move(source.Mesh);
            state->RemeshCommand = command;
            state->RemeshResult = MakeMeshRemeshBaseResult(command);
            state->RemeshResult.TexcoordOutcome = texcoordOutcome;
            state->RemeshResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->RemeshResult.InputFaceCount = state->BeforeMesh.FaceCount();

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshRemeshResult pending =
                    MakePendingMeshRemeshResult(
                        command,
                        state->BeforeMesh,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh remesh CPU", *active);
                return pending;
            }

            // The active job already owns the callback that will deliver this
            // output's terminal result; a duplicate request adds none.
            state->RemeshSink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshRemeshResult result =
                    MakeMeshRemeshBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Mesh remesh CPU job submission was rejected by the runtime job lane.";
                return result;
            }

            return MakePendingMeshRemeshResult(command, state->BeforeMesh, handle);
        }

        [[nodiscard]] EditorMeshSubdivideResult
        SubmitMeshSubdivideCpuJob(
            const EditorProcessingContext& context,
            const EditorMeshSubdivideCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature,
            const EditorMeshTexcoordOutcome texcoordOutcome,
            std::function<void(EditorMeshSubdivideResult)> onComplete)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Subdivide;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            // The apply gate compares stored topology
            // against the stored topology, so fingerprint it here rather than
            // re-deriving it from `BeforeMesh` later.
            state->TopologySignature =
                context.Scene != nullptr
                    ? StoredMeshTopologySignatureForEntity(
                          context.Scene->Raw(),
                          command.StableEntityId)
                    : std::nullopt;
            state->Mesh = std::move(source.Mesh);
            state->SubdivideCommand = command;
            state->SubdivideResult = MakeMeshSubdivideBaseResult(command);
            state->SubdivideResult.TexcoordOutcome = texcoordOutcome;
            state->SubdivideResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->SubdivideResult.InputFaceCount =
                state->BeforeMesh.FaceCount();

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshSubdivideResult pending =
                    MakePendingMeshSubdivideResult(
                        command,
                        state->BeforeMesh,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh subdivide CPU", *active);
                return pending;
            }

            // The active job already owns the callback that will deliver this
            // output's terminal result; a duplicate request adds none.
            state->SubdivideSink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshSubdivideResult result =
                    MakeMeshSubdivideBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Mesh subdivide CPU job submission was rejected by the "
                                 "runtime job lane.";
                return result;
            }

            return MakePendingMeshSubdivideResult(
                command,
                state->BeforeMesh,
                handle);
        }

        [[nodiscard]] EditorMeshSimplifyResult SubmitMeshSimplifyCpuJob(
            const EditorProcessingContext& context,
            const EditorMeshSimplifyCommand& command,
            MeshTopologySourceResult source,
            const std::uint64_t geometryMetadataSignature,
            const EditorMeshTexcoordOutcome texcoordOutcome,
            std::function<void(EditorMeshSimplifyResult)> onComplete)
        {
            auto state = std::make_shared<EditorMeshCpuJobState>();
            state->Kind = EditorMeshCpuJobKind::Simplify;
            state->StableEntityId = command.StableEntityId;
            state->GeometryMetadataSignature = geometryMetadataSignature;
            state->SnapshotPositions = ExtractMeshPositions(source.Mesh);
            state->BeforeMesh = source.Mesh;
            // The apply gate compares stored topology
            // against the stored topology, so fingerprint it here rather than
            // re-deriving it from `BeforeMesh` later.
            state->TopologySignature =
                context.Scene != nullptr
                    ? StoredMeshTopologySignatureForEntity(
                          context.Scene->Raw(),
                          command.StableEntityId)
                    : std::nullopt;
            state->Mesh = std::move(source.Mesh);
            state->SimplifyCommand = command;
            state->SimplifyResult = MakeMeshSimplifyBaseResult(command);
            state->SimplifyResult.TexcoordOutcome = texcoordOutcome;
            state->SimplifyResult.InputVertexCount =
                state->BeforeMesh.VertexCount();
            state->SimplifyResult.InputFaceCount = state->BeforeMesh.FaceCount();

            const EditorJobIdentity identity =
                MakeMeshCpuJobIdentity(*state);
            if (const std::optional<EditorJobRecord> active =
                    FindActiveEditorJob(context, identity))
            {
                EditorMeshSimplifyResult pending =
                    MakePendingMeshSimplifyResult(
                        command,
                        state->BeforeMesh,
                        active->Token);
                pending.Message =
                    BuildActiveDerivedJobMessage("Mesh simplify CPU", *active);
                return pending;
            }

            // The active job already owns the callback that will deliver this
            // output's terminal result; a duplicate request adds none.
            state->SimplifySink = GuardEditorProcessingResult(context, std::move(onComplete));
            JobDesc desc = MakeMeshCpuJobDesc(context, state);
            const JobToken handle = context.JobCommands.Submit(
                std::move(desc),
                identity);
            if (!handle.IsValid())
            {
                EditorMeshSimplifyResult result =
                    MakeMeshSimplifyBaseResult(command);
                result.Status =
                    EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Mesh simplify CPU job submission was rejected by the "
                                 "runtime job lane.";
                return result;
            }

            return MakePendingMeshSimplifyResult(
                command,
                state->BeforeMesh,
                handle);
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveMeshCommandTarget(
            const EditorProcessingContext& context,
            const EditorMeshDenoiseCommand& command,
            EditorMeshDenoiseResult& result)
        {
            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Scene registry is unavailable for mesh denoise.";
                return std::nullopt;
            }
            if (!context.MeshDenoiseKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message       = "Geometry.Smoothing mesh denoiser is unavailable in this "
                                       "runtime configuration.";
                return std::nullopt;
            }

            const bool validStage =
                std::find(kMeshDenoiseStages.begin(),
                          kMeshDenoiseStages.end(),
                          command.Stage) != kMeshDenoiseStages.end();
            if (!validStage ||
                command.NormalIterations == 0u ||
                command.VertexIterations == 0u ||
                !std::isfinite(command.SigmaSpatial) ||
                !std::isfinite(command.SigmaRange) ||
                command.SigmaSpatial < 0.0 ||
                command.SigmaRange < 0.0 ||
                !IsPositiveFinite(command.DegenerateNormalLengthEpsilon))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message       = "Mesh denoise requires a valid stage, positive iteration "
                                       "counts, non-negative finite sigma values, and a positive "
                                       "finite degeneracy epsilon.";
                return std::nullopt;
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Mesh denoise target entity is stale or no longer live.";
                return std::nullopt;
            }

            return entity;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveMeshCommandTarget(
            const EditorProcessingContext& context,
            const EditorMeshSimplifyCommand& command,
            EditorMeshSimplifyResult& result)
        {
            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Scene registry is unavailable for mesh simplify.";
                return std::nullopt;
            }
            const bool hasStopCriterion =
                command.TargetFaces > 0u || command.MaxError > 0.0;
            if (!ValidMeshSimplifyMetric(command.Metric) ||
                !hasStopCriterion ||
                command.NormalWeight < 0.0 ||
                command.BoundaryWeight < 0.0 ||
                command.CurvatureWeight < 0.0 ||
                command.FeatureAngleThresholdDegrees < 0.0 ||
                command.FeatureAngleThresholdDegrees > 180.0)
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Mesh simplify requires a valid metric, a positive target "
                                 "face count or maximum error, non-negative weights, and a "
                                 "feature angle within [0, 180].";
                return std::nullopt;
            }
            if (!context.MeshSimplifyKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Geometry.Simplification is unavailable in this runtime configuration.";
                return std::nullopt;
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Mesh simplify target entity is stale or no longer live.";
                return std::nullopt;
            }

            return entity;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveMeshCommandTarget(
            const EditorProcessingContext& context,
            const EditorMeshRemeshCommand& command,
            EditorMeshRemeshResult& result)
        {
            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Scene registry is unavailable for mesh remesh.";
                return std::nullopt;
            }
            if (!ValidMeshRemeshMode(command.Mode) ||
                !ValidMeshRemeshSizingLaw(command.SizingLaw) ||
                command.Iterations == 0u ||
                !std::isfinite(command.TargetEdgeLength) ||
                command.TargetEdgeLength < 0.0 ||
                !IsPositiveFinite(command.Lambda) ||
                !std::isfinite(command.CurvatureAdaptation) ||
                command.CurvatureAdaptation < 0.0 ||
                !std::isfinite(command.MaxReferenceProjectionDistance) ||
                command.MaxReferenceProjectionDistance < 0.0 ||
                (command.ProjectToSurface && command.ReferenceProjectionK == 0u) ||
                (command.SizingLaw ==
                     EditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
                 !IsPositiveFinite(command.ApproximationError)))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Mesh remesh requires a valid mode, sizing law, positive "
                                 "iteration count, finite non-negative target length, "
                                 "positive lambda, and valid projection/sizing parameters.";
                return std::nullopt;
            }
            if (command.Mode == EditorMeshRemeshMode::Uniform &&
                !context.MeshRemeshUniformKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Geometry.Remeshing uniform remesher is unavailable in "
                                 "this runtime configuration.";
                return std::nullopt;
            }
            if (command.Mode == EditorMeshRemeshMode::Adaptive &&
                !context.MeshRemeshAdaptiveKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Geometry.HalfedgeMesh.AdaptiveRemeshing is unavailable "
                                 "in this runtime configuration.";
                return std::nullopt;
            }
            if (command.ProjectToSurface &&
                !context.MeshRemeshProjectToSurfaceAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Mesh remesh project-to-surface is unavailable in this "
                                 "runtime configuration.";
                return std::nullopt;
            }
            if (command.Mode == EditorMeshRemeshMode::Adaptive &&
                command.SizingLaw == EditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
                !context.MeshRemeshErrorBoundedSizingAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Mesh remesh error-bounded Taubin sizing is unavailable "
                                 "in this runtime configuration.";
                return std::nullopt;
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Mesh remesh target entity is stale or no longer live.";
                return std::nullopt;
            }

            return entity;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveMeshCommandTarget(
            const EditorProcessingContext& context,
            const EditorMeshSubdivideCommand& command,
            EditorMeshSubdivideResult& result)
        {
            if (context.Scene == nullptr)
            {
                result.Status = EditorCommandStatus::MissingScene;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Scene registry is unavailable for mesh subdivide.";
                return std::nullopt;
            }
            if (!ValidMeshSubdivideOperator(command.Operator) ||
                command.Iterations == 0u ||
                (command.PreserveLoopFeatureEdges &&
                 command.FeatureEdgePropertyName.empty()))
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Mesh subdivide requires a valid operator, positive "
                                 "iteration count, and a feature-edge property name when "
                                 "feature preservation is enabled.";
                return std::nullopt;
            }
            if (command.Operator == EditorMeshSubdivideOperator::Loop &&
                !context.MeshSubdivideLoopKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Geometry.Subdivision Loop subdivision is unavailable in "
                                 "this runtime configuration.";
                return std::nullopt;
            }
            if (command.Operator ==
                    EditorMeshSubdivideOperator::CatmullClark &&
                !context.MeshSubdivideCatmullClarkKernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Geometry.CatmullClark subdivision is unavailable in this "
                                 "runtime configuration.";
                return std::nullopt;
            }
            if (command.Operator == EditorMeshSubdivideOperator::Sqrt3 &&
                !context.MeshSubdivideSqrt3KernelAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Geometry.HalfedgeMesh.SubdivisionSqrt3 is unavailable in "
                                 "this runtime configuration.";
                return std::nullopt;
            }
            if (command.PreserveLoopFeatureEdges &&
                command.Operator != EditorMeshSubdivideOperator::Loop)
            {
                result.Status =
                    EditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message = "Loop feature-edge preservation can only be used with the "
                                 "Loop subdivision operator.";
                return std::nullopt;
            }
            if (command.PreserveLoopFeatureEdges &&
                !context.MeshSubdivideLoopFeatureEdgesAvailable)
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message = "Loop subdivision feature-edge preservation is "
                                 "unavailable in this runtime configuration.";
                return std::nullopt;
            }

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, command.StableEntityId);
            if (!entity.has_value())
            {
                result.Status = EditorCommandStatus::StaleEntity;
                result.Error = Core::ErrorCode::ResourceNotFound;
                result.Message =
                    "Mesh subdivide target entity is stale or no longer live.";
                return std::nullopt;
            }

            return entity;
        }

        // Metadata precedes cached ring admission; conversion and numerical
        // validity remain command-time checks before mutation or job submission.
        template <typename Result, typename Command>
        [[nodiscard]] ActionReadiness PreviewMeshCommand(
            const EditorProcessingCommands& commands, const Command& command,
            const std::string_view operationName)
        {
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            Result result{};
            const auto entity = ResolveMeshCommandTarget(context, command, result);
            if (!entity) return {false, std::move(result.Message)};
            std::string diagnostic;
            const auto view = GS::BuildConstView(context.Scene->Raw(), *entity);
            auto status = ValidateMeshPositionSourceMetadata(view, diagnostic);
            // Processing capture checks the mask before soup topology metadata.
            if (status == EditorCommandStatus::Applied)
                status = ValidateMeshVertexDeletionMaskMetadata(view, diagnostic);
            if (status == EditorCommandStatus::Applied)
                status = ValidateMeshSoupSourceMetadata(view, diagnostic);
            if (status == EditorCommandStatus::Applied &&
                !PrepareMeshSoupFaceRings(context, *entity, BuildGeometryAvailability(view), diagnostic))
                return {false, std::string{operationName} + ": " + diagnostic};
            if (status != EditorCommandStatus::Applied)
                diagnostic = std::string{operationName} + ": " + diagnostic;
            return {status == EditorCommandStatus::Applied, std::move(diagnostic)};
        }

} // namespace Extrinsic::Runtime::MeshTopologyDetail

namespace Extrinsic::Runtime
{
    using namespace MeshTopologyDetail;

    ActionReadiness PreviewEditorMeshRemeshCommand(
        const EditorProcessingCommands& commands, const EditorMeshRemeshCommand& command)
    {
        return PreviewMeshCommand<EditorMeshRemeshResult>(commands, command, "Mesh remesh");
    }

    ActionReadiness PreviewEditorMeshSubdivideCommand(
        const EditorProcessingCommands& commands, const EditorMeshSubdivideCommand& command)
    {
        return PreviewMeshCommand<EditorMeshSubdivideResult>(commands, command, "Mesh subdivide");
    }

    ActionReadiness PreviewEditorMeshDenoiseCommand(
        const EditorProcessingCommands& commands, const EditorMeshDenoiseCommand& command)
    {
        return PreviewMeshCommand<EditorMeshDenoiseResult>(commands, command, "Mesh denoise");
    }

    ActionReadiness PreviewEditorMeshSimplifyCommand(
        const EditorProcessingCommands& commands, const EditorMeshSimplifyCommand& command)
    {
        return PreviewMeshCommand<EditorMeshSimplifyResult>(commands, command, "Mesh simplify");
    }

    EditorMeshDenoiseResult
ApplyEditorMeshDenoiseCommand(
        const EditorProcessingCommands& commands,
        const EditorMeshDenoiseCommand& command,
        std::function<void(EditorMeshDenoiseResult)> onComplete)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorMeshDenoiseResult result =
            MakeMeshDenoiseBaseResult(command);

        const auto entity = ResolveMeshCommandTarget(context, command, result);
        if (!entity) return result;
        entt::registry& raw = context.Scene->Raw();

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshProcessingSourceResult source =
            BuildHalfedgeMeshForProcessing(view, "Mesh denoise");
        result.VertexSlotCount = source.BeforePositions.size();
        result.SkippedDeletedVertexCount =
            static_cast<std::size_t>(
                std::count(source.DeletedVertices.begin(),
                           source.DeletedVertices.end(),
                           true));
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.DenoiseStatus =
                source.Status ==
                        EditorCommandStatus::UnsupportedGeometryDomain
                    ? Smooth::DenoiseStatus::EmptyMesh
                    : Smooth::DenoiseStatus::InvalidParams;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        if (context.JobCommands.Available())
        {
            return SubmitMeshDenoiseCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(onComplete));
        }

        std::vector<glm::vec3> afterPositions;
        if (!ComputeMeshDenoise(command, source.Mesh, source.BeforePositions,
                source.DeletedVertices, result, afterPositions))
            return result;

        if (result.MovedVertexCount == 0u)
        {
            // Nothing changed, so there is nothing to commit; publishing an
            // identity edit would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshDenoiseNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshDenoisePositions(
                context,
                command.StableEntityId,
                std::move(source.BeforePositions),
                std::move(afterPositions));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message = "Mesh denoise position publication failed during editor "
                             "history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshDenoiseSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshRemeshResult
ApplyEditorMeshRemeshCommand(
        const EditorProcessingCommands& commands,
        const EditorMeshRemeshCommand& command,
        std::function<void(EditorMeshRemeshResult)> onComplete)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorMeshRemeshResult result =
            MakeMeshRemeshBaseResult(command);

        const auto entity = ResolveMeshCommandTarget(context, command, result);
        if (!entity) return result;
        entt::registry& raw = context.Scene->Raw();

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh remesh");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // Remesh and subdivide replace the topology with one whose
        // corners have no source UV, so they cannot carry the parameterization
        // and the publish step removes it. Report that instead of leaving the
        // loss silent; resampling UVs onto a re-tessellated surface is a
        // separate capability, not a side effect of this command.
        const EditorMeshTexcoordOutcome texcoordOutcome =
            MeshHasResolvableTexcoords(view)
                ? EditorMeshTexcoordOutcome::Discarded
                : EditorMeshTexcoordOutcome::None;

        if (context.JobCommands.Available())
        {
            return SubmitMeshRemeshCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                texcoordOutcome,
                std::move(onComplete));
        }
        result.TexcoordOutcome = texcoordOutcome;

        Geometry::HalfedgeMesh::Mesh before = source.Mesh;
        if (!ComputeMeshRemesh(command, source.Mesh, result))
            return result;

        if (SameMeshTopologyAndPositions(before, source.Mesh))
        {
            // Nothing differs, so there is nothing to commit; replacing the
            // mesh with itself would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshRemeshNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Remesh mesh",
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh remesh publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshRemeshSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshSubdivideResult
ApplyEditorMeshSubdivideCommand(
        const EditorProcessingCommands& commands,
        const EditorMeshSubdivideCommand& command,
        std::function<void(EditorMeshSubdivideResult)> onComplete)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorMeshSubdivideResult result =
            MakeMeshSubdivideBaseResult(command);

        const auto entity = ResolveMeshCommandTarget(context, command, result);
        if (!entity) return result;
        entt::registry& raw = context.Scene->Raw();

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh subdivide");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // Remesh and subdivide replace the topology with one whose
        // corners have no source UV, so they cannot carry the parameterization
        // and the publish step removes it. Report that instead of leaving the
        // loss silent; resampling UVs onto a re-tessellated surface is a
        // separate capability, not a side effect of this command.
        const EditorMeshTexcoordOutcome texcoordOutcome =
            MeshHasResolvableTexcoords(view)
                ? EditorMeshTexcoordOutcome::Discarded
                : EditorMeshTexcoordOutcome::None;

        if (context.JobCommands.Available())
        {
            return SubmitMeshSubdivideCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                texcoordOutcome,
                std::move(onComplete));
        }
        result.TexcoordOutcome = texcoordOutcome;

        Geometry::HalfedgeMesh::Mesh before = source.Mesh;
        if (!ComputeMeshSubdivide(command, source.Mesh, result))
            return result;

        // As in the subdivide job publisher, subdivision cannot both
        // run and leave the mesh unchanged, so there is no gate here.
        const EditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Subdivide mesh",
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh subdivide publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSubdivideSuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

    EditorMeshSimplifyResult
ApplyEditorMeshSimplifyCommand(
        const EditorProcessingCommands& commands,
        const EditorMeshSimplifyCommand& command,
        std::function<void(EditorMeshSimplifyResult)> onComplete)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorMeshSimplifyResult result =
            MakeMeshSimplifyBaseResult(command);

        const auto entity = ResolveMeshCommandTarget(context, command, result);
        if (!entity) return result;
        entt::registry& raw = context.Scene->Raw();

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh simplify");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        // BuildHalfedgeMeshForTopologyEdit carries only positions + topology, so
        // the scratch halfedge mesh has neither v:texcoord nor h:texcoord even
        // when the selected mesh has them. Copy them in so FA_QEM's
        // PreserveUvSeams can actually see a seam, and -- since the publish step
        // replaces the entity's properties wholesale -- so the mesh keeps the
        // UVs it came in with.
        const EditorMeshTexcoordOutcome texcoordOutcome =
            CopyMeshSimplifyAuxiliaryProperties(view, source.Mesh);

        if (context.JobCommands.Available())
        {
            return SubmitMeshSimplifyCpuJob(
                context,
                command,
                std::move(source),
                GeometryMetadataSignatureForEntity(raw, *entity),
                texcoordOutcome,
                std::move(onComplete));
        }
        result.TexcoordOutcome = texcoordOutcome;

        Geometry::HalfedgeMesh::Mesh before = source.Mesh;
        if (!ComputeMeshSimplify(command, source.Mesh, result))
            return result;

        if (SameMeshTopologyAndPositions(before, source.Mesh))
        {
            // Nothing differs, so there is nothing to commit; replacing the
            // mesh with itself would also leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Error = Core::ErrorCode::Success;
            result.Message = BuildMeshSimplifyNoChangeMessage(result);
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Simplify mesh",
                GeometryMetadataSignatureForEntity(raw, *entity),
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh simplify publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSimplifySuccessMessage(result);
        InvalidateSelectedModelCache(context);
        return result;
    }

} // namespace Extrinsic::Runtime
