module;
#include <functional>
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.ParameterizationOperations;

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Geometry.HalfedgeMesh;
import Geometry.MeshSoup;
import Geometry.Properties;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Mesh.Conversion;
import Geometry.UvAtlas;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"

#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSupport.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSoup.hpp"

namespace Extrinsic::Runtime
{
extern "C++"
{
namespace GeometryProcessingDetail::MeshSupport
{

        constexpr std::string_view kUvRegenerationJobOutputName{
            "uv_regeneration"};

        template <typename T>
        void CopyTypedPropertyByXref(
            const Geometry::ConstPropertySet& source,
            const std::string& name,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            const auto sourceProperty = source.Get<T>(name);
            if (!sourceProperty)
                return;

            auto targetProperty = target.GetOrAdd<T>(name, T{});
            if (!targetProperty)
                return;
            for (std::size_t outputIndex = 0u; outputIndex < xrefs.size(); ++outputIndex)
            {
                const std::uint32_t sourceIndex = xrefs[outputIndex];
                if (sourceIndex < sourceProperty.Vector().size())
                    targetProperty[outputIndex] = sourceProperty[sourceIndex];
            }
        }

        void CopyKnownPropertiesByXref(
            const Geometry::ConstPropertySet& source,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            for (const std::string& name : source.Properties())
            {
                CopyTypedPropertyByXref<float>(source, name, xrefs, target);
                CopyTypedPropertyByXref<double>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::uint32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::uint64_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::int32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<bool>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec2>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec3>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec4>(source, name, xrefs, target);
            }
        }

        // Publish regenerated UVs onto a mesh that kept its own
        // topology, on whichever domain can represent them.
        //
        // A chart split needs two or more UVs at a seam vertex, which a
        // per-vertex property cannot hold; those go on the corner domain. An
        // atlas that needed no seam stays on the vertex domain, so a mesh is
        // not promoted to corner UVs for nothing. Either way exactly one UV
        // authority survives, because the resolution order is corner-over-vertex
        // and a leftover disagreeing property would silently win or lose.
        //
        // Returns false only when the atlas cross-references do not line up
        // with the source mesh, which would otherwise publish UVs mapped to the
        // wrong corners.
        [[nodiscard]] bool PublishUvRegenerationTexcoords(
            const Geometry::UvAtlas::UvAtlasResult& atlas,
            const MeshSoupFromGeometrySourcesResult& soup,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            const auto removeProperty =
                [](Geometry::PropertySet& properties,
                   const std::string_view name)
            {
                if (const auto id = properties.Registry().Find(name))
                    (void)properties.Registry().Remove(*id);
            };

            const auto outputUvs =
                atlas.OutputMesh.VertexProperties().Get<glm::vec2>("v:texcoord");
            if (!outputUvs)
                return false;

            MeshCornerTexcoords corners{};
            if (!GatherSplitMeshCornerTexcoords(
                    atlas.OutputMesh,
                    outputUvs.Vector(),
                    atlas.SourceFaceForOutputFace,
                    atlas.SourceVertexForOutputVertex,
                    soup.Mesh.Faces(),
                    soup.Mesh.VertexCount(),
                    corners))
            {
                return false;
            }

            if (corners.HasSeam &&
                PublishMeshCornerTexcoords(
                    mesh,
                    soup.Mesh.Faces(),
                    soup.Mesh.VertexCount(),
                    corners.CornerUvs))
            {
                removeProperty(mesh.VertexProperties(), "v:texcoord");
                return true;
            }

            // No seam, or the corner publication could not map: the per-vertex
            // representatives are the exact answer in the first case and the
            // best available one in the second.
            if (corners.VertexUvs.size() != mesh.VerticesSize())
                return false;
            removeProperty(
                mesh.HalfedgeProperties(),
                Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
            auto target = mesh.VertexProperties().GetOrAdd<glm::vec2>(
                "v:texcoord",
                glm::vec2{0.0f});
            if (!target || target.Vector().size() != corners.VertexUvs.size())
                return false;
            target.Vector() = corners.VertexUvs;
            return true;
        }

        void CopyUvSourcePropertiesToHalfedgeMesh(
            const Geometry::ConstPropertySet& sourceVertexProperties,
            const bool hasSourceVertexProperties,
            const Geometry::ConstPropertySet& sourceFaceProperties,
            const bool hasSourceFaceProperties,
            const MeshSoupFromGeometrySourcesResult& soup,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (hasSourceVertexProperties)
            {
                std::vector<std::uint32_t> sourceVertexForMeshVertex;
                sourceVertexForMeshVertex.reserve(mesh.VerticesSize());
                for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
                {
                    sourceVertexForMeshVertex.push_back(
                        static_cast<std::uint32_t>(i));
                }
                CopyKnownPropertiesByXref(
                    sourceVertexProperties,
                    sourceVertexForMeshVertex,
                    mesh.VertexProperties());
            }

            if (hasSourceFaceProperties &&
                soup.SourceFaceForSoupFace.size() == mesh.FacesSize())
            {
                CopyKnownPropertiesByXref(
                    sourceFaceProperties,
                    soup.SourceFaceForSoupFace,
                    mesh.FaceProperties());
            }
        }

        // UV regeneration also commits a replacement mesh, but its
        // point is the texcoord property, so the topology-and-positions
        // comparison alone is not enough: a run that rewrote only `v:texcoord`
        // would read as unchanged and be silently dropped. A texcoord present
        // on one side and absent on the other is a change.
        [[nodiscard]] bool SameUvRegenerationOutput(
            const Geometry::HalfedgeMesh::Mesh& before,
            const Geometry::HalfedgeMesh::Mesh& after)
        {
            if (!SameMeshTopologyAndPositions(before, after))
                return false;

            // UVs land on whichever domain can represent them, so
            // a comparison that reads only the vertex domain would call a
            // corner-UV rewrite "unchanged" and publish nothing.
            const auto sameProperty =
                [](const Geometry::ConstPropertySet& beforeSet,
                   const Geometry::ConstPropertySet& afterSet,
                   const std::string_view name)
            {
                const Geometry::ConstProperty<glm::vec2> beforeUvs =
                    beforeSet.Get<glm::vec2>(name);
                const Geometry::ConstProperty<glm::vec2> afterUvs =
                    afterSet.Get<glm::vec2>(name);
                if (static_cast<bool>(beforeUvs) != static_cast<bool>(afterUvs))
                    return false;
                if (!beforeUvs)
                    return true;
                return beforeUvs.Vector() == afterUvs.Vector();
            };

            return sameProperty(
                       Geometry::ConstPropertySet(before.VertexProperties()),
                       Geometry::ConstPropertySet(after.VertexProperties()),
                       "v:texcoord") &&
                   sameProperty(
                       Geometry::ConstPropertySet(before.HalfedgeProperties()),
                       Geometry::ConstPropertySet(after.HalfedgeProperties()),
                       Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
        }
}
} // extern "C++"
using namespace GeometryProcessingDetail::MeshSupport;

    struct EditorUvRegenerationSourceSnapshot
    {
        std::vector<glm::vec3> Positions{};
        std::vector<std::uint32_t> EdgeV0{};
        std::vector<std::uint32_t> EdgeV1{};
        std::vector<std::uint32_t> HalfedgeToVertex{};
        std::vector<std::uint32_t> HalfedgeNext{};
        std::vector<std::uint32_t> HalfedgeFace{};
        std::vector<std::uint32_t> FaceHalfedge{};
    };

    [[nodiscard]] std::optional<std::vector<std::uint32_t>> CopyU32Property(
        const Geometry::PropertySet& properties,
        const std::string_view name)
    {
        const auto property = properties.Get<std::uint32_t>(name);
        if (!property || property.Vector().size() != properties.Size())
            return std::nullopt;
        return property.Vector();
    }

    [[nodiscard]] bool CaptureUvRegenerationSourceSnapshot(
        const GS::ConstSourceView& view,
        EditorUvRegenerationSourceSnapshot& out)
    {
        if (view.VertexSource == nullptr ||
            view.EdgeSource == nullptr ||
            view.HalfedgeSource == nullptr ||
            view.FaceSource == nullptr)
        {
            return false;
        }

        std::optional<std::vector<glm::vec3>> positions =
            CollectFiniteGeometryPositions(view.VertexSource->Properties,
                                           GS::PropertyNames::kPosition);
        std::optional<std::vector<std::uint32_t>> edgeV0 =
            CopyU32Property(view.EdgeSource->Properties,
                            GS::PropertyNames::kEdgeV0);
        std::optional<std::vector<std::uint32_t>> edgeV1 =
            CopyU32Property(view.EdgeSource->Properties,
                            GS::PropertyNames::kEdgeV1);
        std::optional<std::vector<std::uint32_t>> halfedgeToVertex =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeToVertex);
        std::optional<std::vector<std::uint32_t>> halfedgeNext =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeNext);
        std::optional<std::vector<std::uint32_t>> halfedgeFace =
            CopyU32Property(view.HalfedgeSource->Properties,
                            GS::PropertyNames::kHalfedgeFace);
        std::optional<std::vector<std::uint32_t>> faceHalfedge =
            CopyU32Property(view.FaceSource->Properties,
                            GS::PropertyNames::kFaceHalfedge);
        if (!positions.has_value() ||
            !edgeV0.has_value() ||
            !edgeV1.has_value() ||
            !halfedgeToVertex.has_value() ||
            !halfedgeNext.has_value() ||
            !halfedgeFace.has_value() ||
            !faceHalfedge.has_value())
        {
            return false;
        }

        out.Positions = std::move(*positions);
        out.EdgeV0 = std::move(*edgeV0);
        out.EdgeV1 = std::move(*edgeV1);
        out.HalfedgeToVertex = std::move(*halfedgeToVertex);
        out.HalfedgeNext = std::move(*halfedgeNext);
        out.HalfedgeFace = std::move(*halfedgeFace);
        out.FaceHalfedge = std::move(*faceHalfedge);
        return true;
    }

    [[nodiscard]] bool SameUvRegenerationSourceSnapshot(
        const EditorUvRegenerationSourceSnapshot& lhs,
        const EditorUvRegenerationSourceSnapshot& rhs) noexcept
    {
        return SameGeometryPositions(lhs.Positions, rhs.Positions) &&
               lhs.EdgeV0 == rhs.EdgeV0 &&
               lhs.EdgeV1 == rhs.EdgeV1 &&
               lhs.HalfedgeToVertex == rhs.HalfedgeToVertex &&
               lhs.HalfedgeNext == rhs.HalfedgeNext &&
               lhs.HalfedgeFace == rhs.HalfedgeFace &&
               lhs.FaceHalfedge == rhs.FaceHalfedge;
    }

    struct UvMeshKnownPropertyState
    {
        Geometry::PropertySet VertexProperties{};
        Geometry::PropertySet FaceProperties{};
    };

    using UvMeshKnownPropertySnapshot =
        std::shared_ptr<const UvMeshKnownPropertyState>;

    [[nodiscard]] UvMeshKnownPropertySnapshot CaptureUvMeshKnownPropertyState(
        const GS::ConstSourceView& view)
    {
        if (view.VertexSource == nullptr || view.FaceSource == nullptr)
            return nullptr;
        return std::make_shared<UvMeshKnownPropertyState>(
            UvMeshKnownPropertyState{
                .VertexProperties = view.VertexSource->Properties,
                .FaceProperties = view.FaceSource->Properties,
            });
    }

    [[nodiscard]] bool SameUvMeshKnownPropertyState(
        const GS::ConstSourceView& view,
        const UvMeshKnownPropertyState& expected) noexcept
    {
        return view.VertexSource != nullptr &&
               view.FaceSource != nullptr &&
               SameKnownPropertyValues(
                   Geometry::ConstPropertySet(
                       view.VertexSource->Properties),
                   Geometry::ConstPropertySet(
                       expected.VertexProperties)) &&
               SameKnownPropertyValues(
                   Geometry::ConstPropertySet(
                       view.FaceSource->Properties),
                   Geometry::ConstPropertySet(
                       expected.FaceProperties));
    }

    struct UvMeshTopologyMutationGeneration
    {
        std::uint64_t GeometryMetadataSignature{0u};
        EditorUvRegenerationSourceSnapshot Snapshot{};
        UvMeshKnownPropertySnapshot Properties{};
    };

    [[nodiscard]] EditorCommandStatus CommitUvMeshTopologyReplacement(
        const EditorProcessingContext& context,
        const std::uint32_t stableEntityId,
        const char* label,
        const std::uint64_t expectedGeometryMetadataSignature,
        EditorUvRegenerationSourceSnapshot expectedSnapshot,
        Geometry::HalfedgeMesh::Mesh before,
        Geometry::HalfedgeMesh::Mesh after)
    {
        if (before.HasGarbage())
            before.GarbageCollection();
        if (after.HasGarbage())
            after.GarbageCollection();

        if (context.CommandHistory != nullptr)
        {
            if (context.Scene == nullptr)
            {
                return EditorCommandStatus::MissingScene;
            }
            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandStatus::StaleEntity;
            const UvMeshKnownPropertySnapshot beforeProperties =
                CaptureUvMeshKnownPropertyState(
                    GS::BuildConstView(raw, *entity));
            if (beforeProperties == nullptr)
            {
                return EditorCommandStatus::
                    UnsupportedGeometryDomain;
            }

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
                    UvMeshTopologyMutationGeneration{
                        .GeometryMetadataSignature =
                            expectedGeometryMetadataSignature,
                        .Snapshot = std::move(expectedSnapshot),
                        .Properties = beforeProperties,
                    },
                    beforeState,
                    afterState,
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const UvMeshTopologyMutationGeneration& expected,
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
                            return EditorCommandHistoryStatus::StaleEntity;

                        const GS::ConstSourceView view =
                            GS::BuildConstView(raw, *entity);
                        const GS::SourceAvailability availability =
                            GS::BuildSourceAvailability(view);
                        if (availability.ProvenanceDomain != GS::Domain::Mesh)
                        {
                            return EditorCommandHistoryStatus::
                                UnsupportedOperation;
                        }
                        if (expected.Properties == nullptr || target == nullptr)
                        {
                            return EditorCommandHistoryStatus::CommandFailed;
                        }
                        if (GeometryMetadataSignatureForEntity(
                                raw,
                                *entity) !=
                            expected.GeometryMetadataSignature)
                        {
                            return EditorCommandHistoryStatus::StaleEntity;
                        }

                        EditorUvRegenerationSourceSnapshot current{};
                        if (!CaptureUvRegenerationSourceSnapshot(
                                view,
                                current) ||
                            !SameUvRegenerationSourceSnapshot(
                                current,
                                expected.Snapshot) ||
                            !SameUvMeshKnownPropertyState(
                                view,
                                *expected.Properties))
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
                            return EditorCommandHistoryStatus::CommandFailed;
                        return ApplyMeshTopologyState(
                            identity.Scene,
                            identity.StableEntityId,
                            *target);
                    },
                    [](
                        const MeshPropertyMutationIdentity& identity,
                        const UvMeshTopologyMutationGeneration&,
                        const MeshTopologySnapshot&)
                    {
                        entt::registry& raw = identity.Scene->Raw();
                        const std::optional<ECS::EntityHandle> entity =
                            ResolveStableEntity(
                                raw,
                                identity.StableEntityId);
                        EditorUvRegenerationSourceSnapshot current{};
                        if (entity.has_value())
                        {
                            MarkMeshTopologyReplacementDirty(raw, *entity);
                            Dirty::MarkGpuDirty(raw, *entity);
                            (void)CaptureUvRegenerationSourceSnapshot(
                                GS::BuildConstView(raw, *entity),
                                current);
                        }
                        const UvMeshKnownPropertySnapshot properties =
                            entity.has_value()
                                ? CaptureUvMeshKnownPropertyState(
                                      GS::BuildConstView(raw, *entity))
                                : nullptr;
                        return UvMeshTopologyMutationGeneration{
                            .GeometryMetadataSignature =
                                entity.has_value()
                                    ? GeometryMetadataSignatureForEntity(
                                          raw,
                                          *entity)
                                    : 0u,
                            .Snapshot = std::move(current),
                            .Properties = properties,
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
        {
            MarkMeshTopologyReplacementDirty(raw, *entity);
            Dirty::MarkGpuDirty(raw, *entity);
        }
        return EditorCommandStatus::Applied;
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    MakeUvRegenerationResult(
        const EditorCommandStatus status,
        const Geometry::UvAtlas::UvAtlasStatus uvStatus,
        std::string diagnostic)
    {
        return EditorUvRegenerationCommandResult{
            .Status = status,
            .UvStatus = uvStatus,
            .Diagnostic = std::move(diagnostic),
        };
    }

    void CopyUvAtlasCounters(
        const Geometry::UvAtlas::UvAtlasResult& atlas,
        EditorUvRegenerationCommandResult& result)
    {
        result.UvStatus = atlas.Status;
        result.Provenance = atlas.Provenance;
        result.AtlasWidth = atlas.Diagnostics.AtlasWidth;
        result.AtlasHeight = atlas.Diagnostics.AtlasHeight;
        result.ChartCount = atlas.Diagnostics.ChartCount;
        result.SeamSplitVertexCount =
            atlas.Diagnostics.OutputVertexCount >
                    atlas.Diagnostics.InputVertexCount
                ? atlas.Diagnostics.OutputVertexCount -
                      atlas.Diagnostics.InputVertexCount
                : 0u;
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    MakePendingUvRegenerationResult(
        const JobToken handle)
    {
        EditorUvRegenerationCommandResult result{};
        result.Status = EditorCommandStatus::Pending;
        result.Diagnostic = "UV regeneration CPU job queued";
        AppendDerivedJobHandleToMessage(result.Diagnostic, handle);
        result.Diagnostic += ".";
        return result;
    }

    struct EditorUvRegenerationCpuJobState
    {
        // Guarded terminal callback for this submission. Empty when the caller
        // asked for no completion delivery.
        std::function<void(EditorUvRegenerationCommandResult)> Sink{};
        std::uint32_t StableEntityId{0u};
        std::uint64_t GeometryMetadataSignature{0u};
        EditorUvRegenerationSourceSnapshot Snapshot{};
        MeshSoupFromGeometrySourcesResult Soup{};
        Geometry::PropertySet SourceVertexProperties{};
        bool HasSourceVertexProperties{false};
        Geometry::PropertySet SourceFaceProperties{};
        bool HasSourceFaceProperties{false};
        std::vector<glm::vec2> AuthoredTexcoords{};
        Geometry::HalfedgeMesh::Mesh BeforeMesh{};
        Geometry::HalfedgeMesh::Mesh AfterMesh{};
        EditorUvRegenerationCommand Command{};
        EditorUvRegenerationCommandResult Result{};
        // Last answer this job's `ValidateBeforeApply` gave the drain.
        // `FinalizeUnpublishedOnMainThread` takes no arguments, so the reason a
        // completion was refused has to be recorded where it was decided.
        JobApplyValidation LastApplyValidation{JobApplyValidation::Current};
        // Set the moment a terminal result reaches the caller's sink. The
        // publisher returns `Err` for a real failure, which `JobService` treats
        // as unpublished; without this the finalizer below would overwrite that
        // failure with a generic "did not apply" second result.
        bool TerminalResultPublished{false};
    };

    [[nodiscard]] JobApplyValidation
    ValidateUvRegenerationCpuJobApply(
        const EditorProcessingContext& context,
        const EditorUvRegenerationCpuJobState& job)
    {
        if (context.AttachmentActive && !context.AttachmentActive())
            return JobApplyValidation::StaleWorld;
        if (context.Scene == nullptr)
            return JobApplyValidation::MissingTarget;

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, job.StableEntityId);
        if (!entity.has_value())
            return JobApplyValidation::MissingTarget;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::Mesh)
            return JobApplyValidation::StaleGeneration;

        if (GeometryMetadataSignatureForEntity(raw, *entity) !=
            job.GeometryMetadataSignature)
        {
            return JobApplyValidation::StaleGeneration;
        }

        EditorUvRegenerationSourceSnapshot current{};
        if (!CaptureUvRegenerationSourceSnapshot(view, current) ||
            !SameUvRegenerationSourceSnapshot(current, job.Snapshot) ||
            view.VertexSource == nullptr ||
            view.FaceSource == nullptr ||
            !SameKnownPropertyValues(
                Geometry::ConstPropertySet(
                    view.VertexSource->Properties),
                job.BeforeMesh.VertexProperties()) ||
            !SameKnownPropertyValues(
                Geometry::ConstPropertySet(
                    view.FaceSource->Properties),
                job.BeforeMesh.FaceProperties()))
        {
            return JobApplyValidation::StaleGeneration;
        }

        return JobApplyValidation::Current;
    }

    void PublishUvRegenerationResultSink(
        EditorUvRegenerationCpuJobState& job,
        EditorUvRegenerationCommandResult result)
    {
        job.TerminalResultPublished = true;
        if (job.Sink)
            job.Sink(std::move(result));
    }

    // A queued UV job that terminates without publishing — cancelled, stale, or
    // dropped — still owes the editor exactly one terminal result, so the panel
    // does not sit on its submit-time `Pending` state forever. Mirrors
    // `FinalizeUnpublishedMeshCpuJob`.
    void FinalizeUnpublishedUvRegenerationCpuJob(
        EditorUvRegenerationCpuJobState& job)
    {
        if (job.TerminalResultPublished)
            return;
        auto failure = BuildUnpublishedEditorJobFailure(
            job.LastApplyValidation, "UV regeneration");
        PublishUvRegenerationResultSink(
            job,
            MakeUvRegenerationResult(
                failure.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                std::move(failure.Message)));
    }

    [[nodiscard]] JobResultEnvelope RunUvRegenerationCpuWorker(
        const std::shared_ptr<EditorUvRegenerationCpuJobState>& state)
    {
        Geometry::UvAtlas::UvAtlasOptions options{};
        options.PreserveValidAuthoredUvs =
            state->Command.PreserveValidAuthoredUvs;
        options.ForceRegenerate = state->Command.ForceRegenerate;
        options.Resolution = state->Command.Resolution;
        options.Padding = state->Command.Padding;
        options.TexelsPerUnit = state->Command.TexelsPerUnit;
        options.BackendName = "xatlas";

        Geometry::UvAtlas::UvAtlasInput input{};
        input.Positions = state->Soup.Mesh.Positions();
        input.Faces = state->Soup.Mesh.Faces();
        input.AuthoredTexcoords = state->AuthoredTexcoords;
        input.VertexProperties = state->HasSourceVertexProperties
            ? Geometry::ConstPropertySet(state->SourceVertexProperties)
            : Geometry::ConstPropertySet{};
        input.HasVertexProperties = state->HasSourceVertexProperties;

        Geometry::UvAtlas::UvAtlasResult atlas =
            Geometry::UvAtlas::ResolveUvAtlas(input, options, nullptr);
        CopyUvAtlasCounters(atlas, state->Result);

        if (!atlas.Succeeded())
        {
            const bool backendFailure =
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable ||
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput ||
                atlas.Status ==
                    Geometry::UvAtlas::UvAtlasStatus::BackendFailed;
            state->Result.Status = backendFailure
                ? EditorCommandStatus::GeometryProcessingFailed
                : EditorCommandStatus::InvalidProcessingParameters;
            state->Result.Diagnostic =
                atlas.Diagnostics.BackendDetail.empty()
                    ? std::string{Geometry::UvAtlas::ToString(atlas.Status)}
                    : atlas.Diagnostics.BackendDetail;
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = state->Result.Diagnostic,
                });
        }

        // Build the published mesh from the source soup, never
        // `atlas.OutputMesh`. An unwrapper emits a fresh output vertex per
        // (chart, source vertex) pair, so publishing that output would convert
        // a manifold into chart-split triangle soup. The seam is a corner-domain
        // UV fact, and duplication happens once at GPU upload.
        auto converted =
            Geometry::Mesh::Conversion::ToHalfedgeMesh(state->Soup.Mesh);
        if (!converted.Succeeded())
        {
            state->Result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            state->Result.Diagnostic =
                "selected mesh could not be converted back to halfedge topology";
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = state->Result.Diagnostic,
                });
        }

        CopyUvSourcePropertiesToHalfedgeMesh(
            Geometry::ConstPropertySet(state->SourceVertexProperties),
            state->HasSourceVertexProperties,
            Geometry::ConstPropertySet(state->SourceFaceProperties),
            state->HasSourceFaceProperties,
            state->Soup,
            converted.Mesh);

        if (!PublishUvRegenerationTexcoords(atlas, state->Soup, converted.Mesh))
        {
            state->Result.Status =
                EditorCommandStatus::GeometryProcessingFailed;
            state->Result.Diagnostic =
                "generated UVs could not be mapped back onto the selected "
                "mesh's own corners";
            return JobResultEnvelope::Make<EditorJobResult>(
                EditorJobResult{
                    .Diagnostic = state->Result.Diagnostic,
                });
        }
        state->AfterMesh = std::move(converted.Mesh);
        state->Result.Status = EditorCommandStatus::Applied;
        state->Result.Diagnostic = atlas.Diagnostics.BackendDetail;
        return JobResultEnvelope::Make<EditorJobResult>(
            EditorJobResult{
                .Diagnostic = "UV regeneration CPU result ready",
            });
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    CommitUvRegenerationCpuJobResult(
        const EditorProcessingContext& context,
        EditorUvRegenerationCpuJobState& job)
    {
        EditorUvRegenerationCommandResult result = job.Result;
        if (!result.Succeeded())
            return result;

        if (SameUvRegenerationOutput(job.BeforeMesh, job.AfterMesh))
        {
            // The atlas resolved to exactly the UVs and topology already
            // stored, so there is nothing to commit; replacing the mesh with
            // itself would leave a useless undo entry.
            result.Status = EditorCommandStatus::NoChange;
            result.Diagnostic =
                "UV regeneration produced the texcoords and topology already "
                "stored (" +
                std::to_string(result.ChartCount) +
                " charts, " + std::to_string(result.SeamSplitVertexCount) +
                " GPU-side seam-split vertices). Nothing was published and no "
                "undo entry was created.";
            return result;
        }

        const EditorCommandStatus commitStatus =
            CommitUvMeshTopologyReplacement(
                context,
                job.StableEntityId,
                "Regenerate UVs",
                job.GeometryMetadataSignature,
                std::move(job.Snapshot),
                std::move(job.BeforeMesh),
                std::move(job.AfterMesh));
        if (commitStatus != EditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Diagnostic =
                "UV regeneration publication failed during editor history commit.";
            return result;
        }

        result.Status = EditorCommandStatus::Applied;
        InvalidateSelectedModelCache(context);
        return result;
    }

    [[nodiscard]] Core::Result PublishUvRegenerationCpuJob(
        const EditorProcessingContext& context,
        EditorUvRegenerationCpuJobState& job)
    {
        EditorUvRegenerationCommandResult result =
            CommitUvRegenerationCpuJobResult(context, job);
        const bool succeeded = result.Succeeded();
        PublishUvRegenerationResultSink(job, std::move(result));
        return succeeded ? Core::Ok() : Core::Err(Core::ErrorCode::Unknown);
    }

    // Dedup identity omits `SourcePropertyGeneration`; the dedup guard does not
    // compare it, while `ValidateUvRegenerationCpuJobApply` rechecks source and
    // metadata staleness immediately before apply.
    [[nodiscard]] EditorJobIdentity MakeUvRegenerationCpuJobIdentity(
        const std::uint32_t stableEntityId)
    {
        return EditorJobIdentity{
            .EntityId = stableEntityId,
            .Scope = EditorJobScope::MeshSurface,
            .OutputSemantic = GeometryPresentationSlotSemantic::Albedo,
            .OutputName = std::string{kUvRegenerationJobOutputName},
        };
    }

    [[nodiscard]] JobDesc MakeUvRegenerationCpuJobDesc(
        const EditorProcessingContext& context,
        const std::shared_ptr<EditorUvRegenerationCpuJobState>& state)
    {
        return JobDesc{
            .DebugName = "Sandbox.UvRegeneration.CPU",
            .Scope = context.World,
            .Priority = Core::Dag::TaskPriority::Normal,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .EstimatedCost = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(
                    (std::max(state->Soup.Mesh.VertexCount(),
                              state->Soup.Mesh.FaceCount()) +
                     1023u) /
                    1024u)),
            .Work =
                [state](const JobCancellation&) -> JobResultEnvelope
                {
                    return RunUvRegenerationCpuWorker(state);
                },
            .ValidateBeforeApply =
                [context, state]()
                {
                    const JobApplyValidation validation =
                        ValidateUvRegenerationCpuJobApply(context, *state);
                    state->LastApplyValidation = validation;
                    return validation;
                },
            .PublishCompletion =
                [context, state](KernelEventBus&,
                                 const JobResultEnvelope& result) -> bool
                {
                    if (result.TryGet<EditorJobResult>() == nullptr)
                        return false;
                    return PublishUvRegenerationCpuJob(context, *state)
                        .has_value();
                },
            .FinalizeUnpublishedOnMainThread =
                [state]()
                {
                    FinalizeUnpublishedUvRegenerationCpuJob(*state);
                },
        };
    }

    [[nodiscard]] EditorUvRegenerationCommandResult
    SubmitUvRegenerationCpuJob(
        const EditorProcessingContext& context,
        const std::shared_ptr<EditorUvRegenerationCpuJobState>& state)
    {
        const EditorJobIdentity identity =
            MakeUvRegenerationCpuJobIdentity(state->StableEntityId);
        JobDesc desc = MakeUvRegenerationCpuJobDesc(context, state);

        const JobToken handle = context.JobCommands.Submit(
            std::move(desc),
            identity);
        if (!handle.IsValid())
        {
            state->Sink = {};
            return MakeUvRegenerationResult(
                EditorCommandStatus::GeometryProcessingFailed,
                Geometry::UvAtlas::UvAtlasStatus::BackendFailed,
                "UV regeneration CPU job submission was rejected by the runtime job "
                "lane.");
        }

        return MakePendingUvRegenerationResult(handle);
    }

    namespace
    {
    [[nodiscard]] EditorUvRegenerationCommandResult ValidateUvRegenerationRequest(
        const EditorProcessingContext& context,
        const EditorUvRegenerationCommand& command,
        GS::ConstSourceView& view)
    {
        if (context.Scene == nullptr)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::MissingScene,
                Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                "Scene registry is unavailable.");
        }
        if (command.Resolution == 0u || command.Padding >= command.Resolution)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires a positive resolution and padding smaller "
                "than the atlas.");
        }
        if (!std::isfinite(command.TexelsPerUnit) || command.TexelsPerUnit < 0.0f)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires a finite non-negative texel density.");
        }
        if (!command.BackendName.empty() && command.BackendName != "xatlas")
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable,
                "Only the promoted xatlas UV backend is available.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::StaleEntity,
                Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                "Select a live mesh entity for UV regeneration.");
        }

        view = GS::BuildConstView(raw, *entity);
        std::string diagnostic;
        const auto sourceStatus = ValidateMeshSoupSourceMetadata(view, diagnostic);
        if (sourceStatus != EditorCommandStatus::Applied)
            return MakeUvRegenerationResult(
                sourceStatus, Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration cannot use the selected entity: " + diagnostic);

        if (const auto active = context.JobCommands.Available()
                ? FindActiveEditorJob(context, MakeUvRegenerationCpuJobIdentity(command.StableEntityId))
                : std::nullopt)
        {
            auto pending = MakePendingUvRegenerationResult(active->Token);
            pending.Diagnostic = BuildActiveDerivedJobMessage("UV regeneration CPU", *active);
            return pending;
        }
        return MakeUvRegenerationResult(
            EditorCommandStatus::Applied, Geometry::UvAtlas::UvAtlasStatus::Success, {});
    }

    [[nodiscard]] EditorUvRegenerationCommandResult ApplyUvRegenerationChecked(
        const EditorProcessingContext& context,
        const EditorUvRegenerationCommand& command,
        std::function<void(EditorUvRegenerationCommandResult)> onComplete)
    {
        GS::ConstSourceView view{};
        auto admission = ValidateUvRegenerationRequest(context, command, view);
        if (!admission.Succeeded())
            return admission;
        entt::registry& raw = context.Scene->Raw();
        const auto entity = ResolveStableEntity(raw, command.StableEntityId);
        MeshSoupFromGeometrySourcesResult soup =
            BuildMeshSoupFromGeometrySources(view);
        if (!soup.Succeeded())
        {
            // The shared soup builder reports the source defect only; naming
            // the operation is the calling family's job.
            return MakeUvRegenerationResult(
                soup.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration cannot use the selected entity: " +
                    soup.Diagnostic);
        }

        MeshTopologySourceResult topology =
            BuildHalfedgeMeshForTopologyEdit(view, "UV regeneration");
        if (!topology.Succeeded())
        {
            return MakeUvRegenerationResult(
                topology.Status,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                topology.Diagnostic);
        }

        EditorUvRegenerationSourceSnapshot snapshot{};
        if (!CaptureUvRegenerationSourceSnapshot(view, snapshot))
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::InvalidProcessingParameters,
                Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                "UV regeneration requires count-matched mesh position, edge, "
                "halfedge, and face topology properties.");
        }

        std::vector<glm::vec2> authoredTexcoords;
        if (view.VertexSource != nullptr)
        {
            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (texcoords && texcoords.Vector().size() == soup.Mesh.VertexCount())
                authoredTexcoords = texcoords.Vector();
        }

        auto state =
            std::make_shared<EditorUvRegenerationCpuJobState>();
        state->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
        state->StableEntityId = command.StableEntityId;
        state->GeometryMetadataSignature =
            GeometryMetadataSignatureForEntity(raw, *entity);
        state->Snapshot = std::move(snapshot);
        state->Soup = std::move(soup);
        state->BeforeMesh = std::move(topology.Mesh);
        state->Command = command;
        state->AuthoredTexcoords = std::move(authoredTexcoords);
        if (view.VertexSource != nullptr)
        {
            state->SourceVertexProperties = view.VertexSource->Properties;
            state->HasSourceVertexProperties = true;
        }
        if (view.FaceSource != nullptr)
        {
            state->SourceFaceProperties = view.FaceSource->Properties;
            state->HasSourceFaceProperties = true;
        }
        CopyUvSourcePropertiesToHalfedgeMesh(
            Geometry::ConstPropertySet(state->SourceVertexProperties),
            state->HasSourceVertexProperties,
            Geometry::ConstPropertySet(state->SourceFaceProperties),
            state->HasSourceFaceProperties,
            state->Soup,
            state->BeforeMesh);
        // The before-state needs the mesh's existing corner UVs too,
        // both so a genuine no-op is recognised as one and so undo restores
        // them. Vertex-domain properties came across above; halfedge numbering
        // does not survive the round trip, so these go through the corner walk.
        (void)CopyStoredCornerTexcoordsToScratchMesh(view, state->BeforeMesh);

        if (context.JobCommands.Available())
            return SubmitUvRegenerationCpuJob(context, state);

        const JobResultEnvelope worker =
            RunUvRegenerationCpuWorker(state);
        if (worker.TryGet<EditorJobResult>() == nullptr)
        {
            return MakeUvRegenerationResult(
                EditorCommandStatus::GeometryProcessingFailed,
                Geometry::UvAtlas::UvAtlasStatus::BackendFailed,
                "UV regeneration CPU worker failed.");
        }

        return CommitUvRegenerationCpuJobResult(context, *state);
    }
    } // namespace

    ActionReadiness PreviewEditorUvRegenerationCommand(
        const EditorProcessingCommands& commands, const EditorUvRegenerationCommand& command)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        GS::ConstSourceView view{};
        auto result = ValidateUvRegenerationRequest(context, command, view);
        if (result.Succeeded())
        {
            std::string diagnostic;
            const auto entity = ResolveStableEntity(context.Scene->Raw(), command.StableEntityId);
            if (!PrepareMeshSoupFaceRings(context, *entity, BuildGeometryAvailability(view), diagnostic))
                return {false, "UV regeneration cannot use the selected entity: " + diagnostic};
            if (ValidateMeshVertexDeletionMaskMetadata(view, diagnostic) != EditorCommandStatus::Applied)
                return {false, "UV regeneration: " + diagnostic};
        }
        return {result.Succeeded(), std::move(result.Diagnostic)};
    }

    EditorUvRegenerationCommandResult ApplyEditorUvRegenerationCommand(
        const EditorProcessingCommands& commands, const EditorUvRegenerationCommand& command,
        std::function<void(EditorUvRegenerationCommandResult)> onComplete)
    {
        return ApplyUvRegenerationChecked(
            EditorProcessingCommandsAccess::Resolve(commands), command, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

namespace Extrinsic::Runtime
{
const char *DebugNameForEditorUvAtlasStatus(
    Geometry::UvAtlas::UvAtlasStatus status) noexcept {
  return Geometry::UvAtlas::ToString(status);
}

const char *DebugNameForEditorUvAtlasProvenance(
    Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept {
  return Geometry::UvAtlas::ToString(provenance);
}

}
