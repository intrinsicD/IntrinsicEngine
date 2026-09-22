// Mesh source snapshots, topology fingerprints, job messages and topology
// publication shared by several geometry-operation families. Compiled once as an
// ordinary translation unit so no family depends on another family module.
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.WorldHandle;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSupport.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSoup.hpp"

namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
        using EditorFeatureDetail::kEditorSignatureOffset;
        using EditorFeatureDetail::MixSignature;
        using EditorFeatureDetail::MixSignatureString;

        [[nodiscard]] EditorCommandStatus ValidateMeshPositionSourceMetadata(
            const GS::ConstSourceView& view, std::string& diagnostic,
            const std::string_view positionProperty)
        {
            diagnostic.clear();
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                diagnostic = "selected entity has no mesh GeometrySources";
                return EditorCommandStatus::UnsupportedGeometryDomain;
            }

            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(positionProperty);
            if (!positions || positions.Vector().empty())
            {
                diagnostic = "selected mesh requires a non-empty vertex position property: " +
                             std::string{positionProperty};
                return EditorCommandStatus::InvalidProcessingParameters;
            }
            if (positions.Vector().size() != view.VertexSource->Properties.Size())
            {
                diagnostic = "selected mesh requires a count-matched vertex position property: " +
                             std::string{positionProperty};
                return EditorCommandStatus::InvalidProcessingParameters;
            }
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] EditorCommandStatus ValidateMeshVertexDeletionMaskMetadata(
            const GS::ConstSourceView& view, std::string& diagnostic,
            const std::string_view positionProperty)
        {
            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(positionProperty);
            const auto deleted = view.VertexSource->Properties.Get<bool>("v:deleted");
            if (deleted && deleted.Vector().size() != positions.Vector().size())
            {
                diagnostic = "v:deleted must match the bound position property: " +
                             std::string{positionProperty};
                return EditorCommandStatus::InvalidProcessingParameters;
            }
            return EditorCommandStatus::Applied;
        }

        [[nodiscard]] EditorCommandStatus ValidateMeshSoupSourceMetadata(
            const GS::ConstSourceView& view, std::string& diagnostic,
            std::string_view positionProperty)
        {
            const auto status = ValidateMeshPositionSourceMetadata(view, diagnostic, positionProperty);
            if (status != EditorCommandStatus::Applied)
                return status;
            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(positionProperty);
            if (positions.Vector().size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                diagnostic = "selected mesh has too many vertices to index";
                return EditorCommandStatus::InvalidProcessingParameters;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces || !faceHalfedges ||
                toVertices.Vector().size() != view.HalfedgeSource->Properties.Size() ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().size() != view.FaceSource->Properties.Size() ||
                faceHalfedges.Vector().empty())
            {
                diagnostic = "selected mesh has invalid halfedge/face topology";
                return EditorCommandStatus::InvalidProcessingParameters;
            }

            return EditorCommandStatus::Applied;
        }

    namespace
    {
        // A null output validates rings without materializing positions or triangles.
        [[nodiscard]] EditorCommandStatus WalkMeshSoupFaces(
            const GS::ConstSourceView& view, std::string_view positionProperty,
            std::string& diagnostic, MeshSoupFromGeometrySourcesResult* output)
        {
            const auto status = ValidateMeshSoupSourceMetadata(view, diagnostic, positionProperty);
            if (status != EditorCommandStatus::Applied) return status;
            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(positionProperty);
            const auto toVertices = view.HalfedgeSource->Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges = view.HalfedgeSource->Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces = view.HalfedgeSource->Properties.Get<std::uint32_t>(GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges = view.FaceSource->Properties.Get<std::uint32_t>(GS::PropertyNames::kFaceHalfedge);

            if (output)
                for (const glm::vec3 position : positions.Vector())
                    (void)output->Mesh.AddVertex(position);
            bool hasFaces = false;

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    diagnostic = "selected mesh has a face ring that is not a valid polygon";
                    return EditorCommandStatus::InvalidProcessingParameters;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                hasFaces = true;
                if (!output) continue;
                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    (void)output->Mesh.AddTriangle(ring[0u], ring[i], ring[i + 1u]);
                    output->SourceFaceForSoupFace.push_back(
                        static_cast<std::uint32_t>(faceIndex));
                }
            }

            if (!hasFaces)
            {
                diagnostic = "selected mesh has no valid surface faces";
                return EditorCommandStatus::InvalidProcessingParameters;
            }

            return EditorCommandStatus::Applied;
        }
    }

        EditorCommandStatus ValidateMeshSoupFaceRings(
            const GS::ConstSourceView& view, std::string& diagnostic,
            std::string_view positionProperty)
        {
            return WalkMeshSoupFaces(view, positionProperty, diagnostic, nullptr);
        }

        MeshSoupFromGeometrySourcesResult BuildMeshSoupFromGeometrySources(
            const GS::ConstSourceView& view, std::string_view positionProperty)
        {
            MeshSoupFromGeometrySourcesResult result{};
            result.Status = WalkMeshSoupFaces(view, positionProperty, result.Diagnostic, &result);
            return result;
        }

    namespace
    {
        template <typename T>
        [[nodiscard]] std::optional<bool> SameTypedPropertyValues(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected,
            const std::string_view name) noexcept
        {
            const auto expectedProperty = expected.Get<T>(name);
            if (!expectedProperty)
                return std::nullopt;

            const auto currentProperty = current.Get<T>(name);
            if (!currentProperty)
                return std::nullopt;
            return GeometryValueComparison::BitEqual(
                currentProperty.Vector(), expectedProperty.Vector());
        }

    }

        [[nodiscard]] bool SameKnownPropertyValues(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected) noexcept
        {
            for (const std::string& name : expected.Properties())
            {
                const auto matches =
                    [&]<typename T>()
                    {
                        const std::optional<bool> same =
                            SameTypedPropertyValues<T>(
                                current,
                                expected,
                                name);
                        return !same.has_value() || *same;
                    };
                if (!matches.template operator()<float>() ||
                    !matches.template operator()<double>() ||
                    !matches.template operator()<std::uint32_t>() ||
                    !matches.template operator()<std::uint64_t>() ||
                    !matches.template operator()<std::int32_t>() ||
                    !matches.template operator()<bool>() ||
                    !matches.template operator()<glm::vec2>() ||
                    !matches.template operator()<glm::vec3>() ||
                    !matches.template operator()<glm::vec4>())
                {
                    return false;
                }
            }
            return true;
        }
        void InvalidateSelectedModelCache(const EditorProcessingContext& context)
        {
            if (context.InvalidateWorkspaceSnapshotCache)
                context.InvalidateWorkspaceSnapshotCache();
        }

        // A topology edit's apply gate fingerprints the stored halfedge arrays
        // at both submit and apply. Re-deriving one side through a triangle
        // soup can renumber topology after an earlier edit and falsely mark
        // unchanged sources stale. Direct stored-array fingerprints detect
        // topology changes; positions are compared separately.
        [[nodiscard]] bool AppendTopologyValueSignature(
            std::uint64_t& signature,
            const Geometry::PropertySet* properties,
            const std::size_t deletedCount,
            const std::uint64_t domainTag,
            const std::span<const std::string_view> propertyNames,
            const std::string_view deletionProperty)
        {
            MixSignature(signature, domainTag);
            if (properties == nullptr)
                return false;

            MixSignature(signature,
                         static_cast<std::uint64_t>(properties->Size()));
            MixSignature(signature,
                         static_cast<std::uint64_t>(deletedCount));
            // Equal deletion counts can still select different source rows.
            const bool hasMask = properties->Exists(deletionProperty);
            MixSignature(signature, hasMask ? 1u : 0u);
            if (hasMask)
            {
                const auto mask = properties->Get<bool>(deletionProperty);
                if (!mask || mask.Size() != properties->Size())
                    return false;
                for (const bool deleted : mask.Vector())
                    MixSignature(signature, deleted ? 1u : 0u);
            }
            for (const std::string_view name : propertyNames)
            {
                const auto values = properties->Get<std::uint32_t>(name);
                if (!values)
                    return false;
                MixSignatureString(signature, name);
                MixSignature(
                    signature,
                    static_cast<std::uint64_t>(values.Vector().size()));
                for (const std::uint32_t value : values.Vector())
                    MixSignature(signature, static_cast<std::uint64_t>(value));
            }
            return true;
        }

        // `std::nullopt` means the stored topology could not be read at all, so
        // no two readings may be treated as equal.
        [[nodiscard]] std::optional<std::uint64_t> MeshTopologyValueSignature(
            const GS::ConstSourceView& view)
        {
            static constexpr std::array<std::string_view, 2> kEdgeNames{
                GS::PropertyNames::kEdgeV0,
                GS::PropertyNames::kEdgeV1,
            };
            static constexpr std::array<std::string_view, 3> kHalfedgeNames{
                GS::PropertyNames::kHalfedgeToVertex,
                GS::PropertyNames::kHalfedgeNext,
                GS::PropertyNames::kHalfedgeFace,
            };
            static constexpr std::array<std::string_view, 1> kFaceNames{
                GS::PropertyNames::kFaceHalfedge,
            };

            std::uint64_t signature = kEditorSignatureOffset;
            if (!AppendTopologyValueSignature(
                    signature,
                    view.EdgeSource != nullptr ? &view.EdgeSource->Properties
                                               : nullptr,
                    view.EdgeSource != nullptr ? view.EdgeSource->NumDeleted
                                               : 0u,
                    1u,
                    kEdgeNames, "e:deleted") ||
                !AppendTopologyValueSignature(
                    signature,
                    view.HalfedgeSource != nullptr
                        ? &view.HalfedgeSource->Properties
                        : nullptr,
                    0u,
                    2u,
                    kHalfedgeNames, "h:deleted") ||
                !AppendTopologyValueSignature(
                    signature,
                    view.FaceSource != nullptr ? &view.FaceSource->Properties
                                               : nullptr,
                    view.FaceSource != nullptr ? view.FaceSource->NumDeleted
                                               : 0u,
                    3u,
                    kFaceNames, "f:deleted") ||
                !AppendTopologyValueSignature(
                    signature,
                    view.VertexSource != nullptr ? &view.VertexSource->Properties
                                                 : nullptr,
                    view.VertexSource != nullptr ? view.VertexSource->NumDeleted
                                                 : 0u,
                    4u, {}, "v:deleted"))
            {
                return std::nullopt;
            }
            return signature;
        }

        [[nodiscard]] std::optional<std::uint64_t>
        StoredMeshTopologySignatureForEntity(
            entt::registry& raw,
            const std::uint32_t stableEntityId)
        {
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return std::nullopt;
            return MeshTopologyValueSignature(
                GS::BuildConstView(raw, *entity));
        }

        [[nodiscard]] bool SameGeometryPositions(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z)
                {
                    return false;
                }
            }
            return true;
        }

        // The guard refuses a duplicate submission when the same entity+output
        // already has a non-terminal `JobService` job. Identity stays with the
        // editor session and is resolved through its active-output query.
        [[nodiscard]] std::optional<EditorJobRecord>
        FindActiveEditorJob(
            const EditorProcessingContext& context,
            const EditorJobIdentity& identity)
        {
            if (!context.JobCommands.FindActive)
                return std::nullopt;
            return context.JobCommands.FindActive(identity);
        }

        void AppendDerivedJobHandleToMessage(
            std::string& message,
            const JobToken handle)
        {
            if (!handle.IsValid())
                return;

            message += " (job ";
            message += std::to_string(handle.Index);
            message += ":";
            message += std::to_string(handle.Generation);
            message += ")";
        }

        [[nodiscard]] std::string BuildActiveDerivedJobMessage(
            const std::string_view label,
            const EditorJobRecord& job)
        {
            std::string message{label};
            message += " already has an active ";
            message += std::string{ToString(job.State)};
            message += " job";
            AppendDerivedJobHandleToMessage(message, job.Token);
            message += ".";
            return message;
        }

    namespace
    {
        std::string_view QueuedCpuJobUnpublishedReason(
            const JobApplyValidation validation) noexcept
        {
            switch (validation)
            {
            case JobApplyValidation::Cancelled:
                return "the job was cancelled before its result could be "
                       "applied";
            case JobApplyValidation::StaleWorld:
                return "the world it was submitted against is no longer "
                       "active";
            case JobApplyValidation::StaleGeneration:
                return "the source geometry changed after the job was queued, "
                       "so the result no longer matches the geometry it was "
                       "computed from";
            case JobApplyValidation::MissingTarget:
                return "the target entity no longer exists";
            case JobApplyValidation::Current:
                break;
            }
            return "it terminated without publishing a result";
        }
    }

        UnpublishedEditorJobFailure BuildUnpublishedEditorJobFailure(
            const JobApplyValidation validation,
            const std::string_view label,
            const std::string_view detail)
        {
            UnpublishedEditorJobFailure failure{};
            if (validation == JobApplyValidation::MissingTarget ||
                validation == JobApplyValidation::StaleGeneration ||
                validation == JobApplyValidation::StaleWorld)
            {
                failure.Status = EditorCommandStatus::StaleEntity;
                failure.Error = Core::ErrorCode::InvalidState;
            }
            failure.Message = label;
            failure.Message += " did not apply: ";
            failure.Message += QueuedCpuJobUnpublishedReason(validation);
            if (validation == JobApplyValidation::Current && !detail.empty())
            {
                failure.Message += " (";
                failure.Message += detail;
                failure.Message += ")";
            }
            failure.Message += ".";
            return failure;
        }

        JobApplyValidation ValidateMeshCpuJobSource(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
            const std::uint64_t geometryMetadataSignature,
            const std::vector<glm::vec3>& snapshotPositions,
            const std::string_view positionProperty)
        {
            if (context.AttachmentActive && !context.AttachmentActive())
                return JobApplyValidation::StaleWorld;
            if (context.Scene == nullptr)
                return JobApplyValidation::MissingTarget;

            entt::registry& raw = context.Scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return JobApplyValidation::MissingTarget;

            const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh)
                return JobApplyValidation::StaleGeneration;

            if (GeometryMetadataSignatureForEntity(raw, *entity) !=
                geometryMetadataSignature)
            {
                return JobApplyValidation::StaleGeneration;
            }

            if (view.VertexSource == nullptr)
                return JobApplyValidation::StaleGeneration;

            const std::optional<std::vector<glm::vec3>> current =
                CollectFiniteGeometryPositions(view.VertexSource->Properties,
                                               positionProperty);
            if (!current.has_value() ||
                !SameGeometryPositions(*current, snapshotPositions))
            {
                return JobApplyValidation::StaleGeneration;
            }

            return JobApplyValidation::Current;
        }

        std::vector<glm::vec3> ExtractMeshPositions(
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            std::vector<glm::vec3> positions(mesh.VerticesSize());
            for (std::size_t i = 0u; i < positions.size(); ++i)
            {
                positions[i] = mesh.Position(
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(i)});
            }
            return positions;
        }

        [[nodiscard]] MeshProcessingSourceResult BuildHalfedgeMeshForProcessing(
            const GS::ConstSourceView& view,
            const std::string_view operationName,
            const std::string_view positionProperty)
        {
            MeshProcessingSourceResult result{};
            const auto fail = [&](const EditorCommandStatus status, std::string diagnostic)
            {
                result.Status = status;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic = std::string{operationName} + ": " + diagnostic;
            };
            std::string diagnostic;
            const auto status = ValidateMeshPositionSourceMetadata(view, diagnostic, positionProperty);
            if (status != EditorCommandStatus::Applied)
            {
                fail(status, std::move(diagnostic));
                return result;
            }

            const auto positions = view.VertexSource->Properties.Get<glm::vec3>(positionProperty);
            result.BeforePositions = positions.Vector();
            result.DeletedVertices.assign(result.BeforePositions.size(), false);
            const auto maskStatus = ValidateMeshVertexDeletionMaskMetadata(view, diagnostic, positionProperty);
            if (maskStatus != EditorCommandStatus::Applied)
            {
                fail(maskStatus, std::move(diagnostic));
                return result;
            }
            if (const auto deleted = view.VertexSource->Properties.Get<bool>("v:deleted"))
            {
                for (std::size_t i = 0u; i < deleted.Vector().size(); ++i)
                    result.DeletedVertices[i] = deleted.Vector()[i];
            }

            MeshSoupFromGeometrySourcesResult soup = BuildMeshSoupFromGeometrySources(view, positionProperty);
            if (!soup.Succeeded())
            {
                fail(soup.Status, std::move(soup.Diagnostic));
                return result;
            }

            auto converted = Geometry::Mesh::Conversion::ToHalfedgeMesh(soup.Mesh);
            if (!converted.Succeeded())
            {
                fail(EditorCommandStatus::GeometryProcessingFailed,
                     "could not convert selected GeometrySources to halfedge topology.");
                return result;
            }
            if (converted.Mesh.VerticesSize() != result.BeforePositions.size())
            {
                fail(EditorCommandStatus::GeometryProcessingFailed,
                     "conversion changed the vertex slot count.");
                return result;
            }
            if (converted.Mesh.FacesSize() != soup.SourceFaceForSoupFace.size())
            {
                fail(EditorCommandStatus::GeometryProcessingFailed,
                     "conversion changed the face slot count.");
                return result;
            }

            result.Mesh = std::move(converted.Mesh);
            result.SourceFaceForMeshFace = std::move(soup.SourceFaceForSoupFace);
            result.Status = EditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] MeshTopologySourceResult BuildHalfedgeMeshForTopologyEdit(
            const GS::ConstSourceView& view,
            std::string_view operationName)
        {
            auto source = BuildHalfedgeMeshForProcessing(view, operationName);
            return {
                .Mesh = std::move(source.Mesh),
                .Status = source.Status,
                .Error = source.Error,
                .Diagnostic = std::move(source.Diagnostic),
            };
        }

        void MarkMeshTopologyReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkEdgeTopologyDirty(raw, entity);
            Dirty::MarkFaceTopologyDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            Geometry::HalfedgeMesh::Mesh published = mesh;
            if (published.HasGarbage())
                published.GarbageCollection();
            GS::PopulateFromMesh(raw, *entity, published);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] bool SameMeshTopologyAndPositions(
            const Geometry::HalfedgeMesh::Mesh& before,
            const Geometry::HalfedgeMesh::Mesh& after) noexcept
        {
            if (before.VerticesSize() != after.VerticesSize() ||
                before.HalfedgesSize() != after.HalfedgesSize() ||
                before.FacesSize() != after.FacesSize() ||
                before.DeletedVertexCount() != after.DeletedVertexCount() ||
                before.DeletedEdgeCount() != after.DeletedEdgeCount() ||
                before.DeletedFaceCount() != after.DeletedFaceCount())
            {
                return false;
            }

            for (std::size_t i = 0u; i < before.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (before.Position(vertex) != after.Position(vertex))
                    return false;
            }
            for (std::size_t i = 0u; i < before.HalfedgesSize(); ++i)
            {
                const Geometry::HalfedgeHandle halfedge{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (before.ToVertex(halfedge) != after.ToVertex(halfedge) ||
                    before.NextHalfedge(halfedge) !=
                        after.NextHalfedge(halfedge) ||
                    before.Face(halfedge) != after.Face(halfedge))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Core::ErrorCode ResultErrorOrUnknown(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : error;
        }

        [[nodiscard]] bool CopyStoredCornerTexcoordsToScratchMesh(
            const GS::ConstSourceView& view,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (view.HalfedgeSource == nullptr || view.VertexSource == nullptr)
                return false;

            const Geometry::PropertySet& halfedgeProperties =
                view.HalfedgeSource->Properties;
            const auto storedCorners = halfedgeProperties.Get<glm::vec2>(
                Geometry::MeshUtils::kHalfedgeTexcoordPropertyName);
            if (!storedCorners ||
                storedCorners.Vector().size() != halfedgeProperties.Size())
            {
                return false;
            }
            for (const glm::vec2 uv : storedCorners.Vector())
            {
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                    return false;
            }

            std::vector<std::uint32_t> surfaceIndices{};
            std::vector<std::uint32_t> triangleFaces{};
            std::vector<std::uint32_t> cornerHalfedges{};
            if (BuildMeshSurfaceTriangleCornerTopology(
                    view,
                    surfaceIndices,
                    triangleFaces,
                    cornerHalfedges) != MeshSurfaceTopologyStatus::Success)
            {
                return false;
            }
            if (cornerHalfedges.size() != surfaceIndices.size() ||
                surfaceIndices.size() % 3u != 0u)
            {
                return false;
            }

            // The corner walk and the soup builder fan every face the same way
            // — `(ring[0], ring[i], ring[i+1])`, faces in index order, skipping
            // the same rings — so triangle `t` here is soup face `t`, which
            // `ToHalfedgeMesh` added as scratch face `t`.
            std::vector<Geometry::MeshSoup::PolygonFace> faces{};
            faces.reserve(surfaceIndices.size() / 3u);
            std::vector<glm::vec2> cornerUvs{};
            cornerUvs.reserve(surfaceIndices.size());
            for (std::size_t i = 0u; i + 2u < surfaceIndices.size(); i += 3u)
            {
                faces.push_back(Geometry::MeshSoup::PolygonFace{
                    .Indices = {surfaceIndices[i],
                                surfaceIndices[i + 1u],
                                surfaceIndices[i + 2u]},
                });
                for (std::size_t k = 0u; k < 3u; ++k)
                {
                    const std::uint32_t halfedge = cornerHalfedges[i + k];
                    if (halfedge >= storedCorners.Vector().size())
                        return false;
                    cornerUvs.push_back(storedCorners.Vector()[halfedge]);
                }
            }

            return PublishMeshCornerTexcoords(
                mesh,
                faces,
                view.VertexSource->Properties.Size(),
                cornerUvs);
        }
}
