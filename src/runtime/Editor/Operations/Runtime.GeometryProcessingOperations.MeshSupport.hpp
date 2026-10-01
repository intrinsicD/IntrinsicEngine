// Mesh source snapshots and mesh-specific helpers shared by geometry operation
// implementation units. Included after their runtime/geometry imports, which must
// cover halfedge-mesh, command-history and job-projection types.
// No public module surface is added.
#pragma once
#include "Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Runtime.GeometryProcessingOperations.MeshReadiness.hpp"
// Job envelope and the shared job/cache/finite-position helpers are declared by
// the point-field header; this one adds the mesh-snapshot surface on top.
#include "Runtime.GeometryProcessingOperations.PointFields.hpp"

// C++ linkage permits the shared type/template definitions in several units.
extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{

        using EditorFeatureDetail::GeometryMetadataSignatureForEntity;
        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::ToEditorCommandStatus;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;

        [[nodiscard]] bool SameKnownPropertyValues(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected) noexcept;

        [[nodiscard]] std::optional<std::uint64_t>
        StoredMeshTopologySignatureForEntity(
            entt::registry& raw,
            const std::uint32_t stableEntityId);




        // Shared apply gate for queued mesh CPU jobs: the entity still exists,
        // still carries mesh provenance, still has the metadata signature the
        // job was submitted against, and still holds the exact positions the
        // job computed from. Families add their own output-specific checks.
        [[nodiscard]] JobApplyValidation ValidateMeshCpuJobSource(
            const EditorProcessingContext& context,
            const std::uint32_t stableEntityId,
            const std::uint64_t geometryMetadataSignature,
            const std::vector<glm::vec3>& snapshotPositions,
            const std::string_view positionProperty);






        // Immutable vertex-position snapshot captured for an undo/redo
        // generation. Shared ownership keeps one copy alive for as long as any
        // history record still references it; the pointee is never mutated.
        using MeshPositionState =
            std::shared_ptr<const std::vector<glm::vec3>>;


        // What an undo/redo record needs to find its entity again. The scene is
        // borrowed, not owned: a stored record never outlives the registry,
        // because `SceneDocumentModule` clears the undo/redo stacks from its
        // `WorldWillBeDestroyed` handler and at shutdown, and `WorldRegistry`
        // frees the scene only on a later maintenance epoch than the one that
        // announced the destruction. Validators still re-check the pointer and
        // the entity before every replay.
        struct MeshPropertyMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
        };

        struct MeshTopologySourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshTopologySourceResult BuildHalfedgeMeshForTopologyEdit(
            const GS::ConstSourceView& view,
            std::string_view operationName);


        void MarkMeshTopologyReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity);


        using MeshTopologySnapshot =
            std::shared_ptr<const Geometry::HalfedgeMesh::Mesh>;

        // Exact copy of the four stored mesh source components and their
        // provenance markers. A topology edit replaces all of them, and the
        // halfedge mesh it is rebuilt from is a re-derivation (halfedge, edge
        // and face numbering do not survive the triangle-soup round trip, and
        // user properties never entered it), so the undo side of the edit is
        // this snapshot rather than a mesh.
        struct MeshStoredSourceSnapshot
        {
            GS::Vertices Vertices{};
            GS::Edges Edges{};
            GS::Halfedges Halfedges{};
            GS::Faces Faces{};
            bool HasMeshTopology{false};
            bool HasGraphTopology{false};
        };
        using MeshStoredSourceState =
            std::shared_ptr<const MeshStoredSourceSnapshot>;

        // Null when the view lacks any of the four mesh domains.
        [[nodiscard]] MeshStoredSourceState CaptureMeshStoredSources(
            const GS::ConstSourceView& view);

        // Publishes `mesh` through `GS::PopulateFromMesh`, or, when `stored`
        // is given, restores that snapshot instead (the undo side).
        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const MeshStoredSourceSnapshot* stored = nullptr);

        // Names of the user-authored properties in one stored or scratch
        // property set: everything except what the halfedge mesh owns itself,
        // what `GS::PopulateFromMesh` regenerates, derived geometry the
        // engine recomputes (normals, curvature), and UVs, which
        // `EditorMeshTexcoordOutcome` reports separately.
        [[nodiscard]] std::vector<std::string> MeshUserPropertyNames(
            const Geometry::PropertySet& properties);

        // Carries the stored vertex-domain user properties into a scratch mesh
        // that shares its vertex numbering with the stored mesh (simplify).
        // Mesh garbage collection later moves them with their vertices.
        void ForwardMeshUserVertexProperties(
            const GS::ConstSourceView& stored,
            Geometry::HalfedgeMesh::Mesh& scratch);

        // "<domain>:<name>" for every stored user property that `after` does
        // not carry, i.e. what publishing `after` removes from the entity.
        [[nodiscard]] std::vector<std::string> DroppedMeshUserProperties(
            const GS::ConstSourceView& stored,
            const Geometry::HalfedgeMesh::Mesh& after);


        // A topology operation commits a whole replacement mesh, so
        // the honest change signal is whether that mesh differs from the one it
        // replaced. Counts decide neither direction on their own: an edge flip
        // and a tangential relaxation pass each change the mesh while leaving
        // vertex and face counts identical, and reporting `NoChange` for either
        // would silently drop the user's edit. Storage sizes are compared
        // rather than live counts, so a mesh still carrying garbage reads as
        // different — the conservative direction, which reports `Applied`.
        [[nodiscard]] bool SameMeshTopologyAndPositions(
            const Geometry::HalfedgeMesh::Mesh& before,
            const Geometry::HalfedgeMesh::Mesh& after) noexcept;

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
        [[nodiscard]] bool CopyStoredCornerTexcoordsToScratchMesh(
            const GS::ConstSourceView& view,
            Geometry::HalfedgeMesh::Mesh& mesh);

}

} // extern "C++"
