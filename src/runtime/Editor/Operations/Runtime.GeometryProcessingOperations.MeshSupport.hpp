// Mesh source snapshots and mesh-specific helpers shared by geometry operation
// implementation units. Included after their runtime/geometry imports, which must
// cover halfedge-mesh, mesh-soup, command-history and job-projection types; no
// public module surface is added.
#pragma once
#include "Runtime.GeometryProcessingOperations.MeshSources.hpp"
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

        struct MeshSoupFromGeometrySourcesResult
        {
            Geometry::MeshSoup::IndexedMesh Mesh{};
            std::vector<std::uint32_t> SourceFaceForSoupFace{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshSoupFromGeometrySourcesResult BuildMeshSoupFromGeometrySources(
            const GS::ConstSourceView& view,
            std::string_view positionProperty = GS::PropertyNames::kPosition);


        template <typename T>
        [[nodiscard]] bool SameKnownPropertyValue(
            const T& lhs,
            const T& rhs) noexcept
        {
            return lhs == rhs;
        }

        template <>
        [[nodiscard]] inline bool SameKnownPropertyValue<float>(
            const float& lhs,
            const float& rhs) noexcept
        {
            return std::bit_cast<std::uint32_t>(lhs) ==
                   std::bit_cast<std::uint32_t>(rhs);
        }

        template <>
        [[nodiscard]] inline bool SameKnownPropertyValue<double>(
            const double& lhs,
            const double& rhs) noexcept
        {
            return std::bit_cast<std::uint64_t>(lhs) ==
                   std::bit_cast<std::uint64_t>(rhs);
        }

        template <>
        [[nodiscard]] inline bool SameKnownPropertyValue<glm::vec2>(
            const glm::vec2& lhs,
            const glm::vec2& rhs) noexcept
        {
            return SameKnownPropertyValue(lhs.x, rhs.x) &&
                   SameKnownPropertyValue(lhs.y, rhs.y);
        }

        template <>
        [[nodiscard]] inline bool SameKnownPropertyValue<glm::vec3>(
            const glm::vec3& lhs,
            const glm::vec3& rhs) noexcept
        {
            return SameKnownPropertyValue(lhs.x, rhs.x) &&
                   SameKnownPropertyValue(lhs.y, rhs.y) &&
                   SameKnownPropertyValue(lhs.z, rhs.z);
        }

        template <>
        [[nodiscard]] inline bool SameKnownPropertyValue<glm::vec4>(
            const glm::vec4& lhs,
            const glm::vec4& rhs) noexcept
        {
            return SameKnownPropertyValue(lhs.x, rhs.x) &&
                   SameKnownPropertyValue(lhs.y, rhs.y) &&
                   SameKnownPropertyValue(lhs.z, rhs.z) &&
                   SameKnownPropertyValue(lhs.w, rhs.w);
        }

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
            if (currentProperty.Vector().size() !=
                expectedProperty.Vector().size())
            {
                return false;
            }
            for (std::size_t i = 0u;
                 i < expectedProperty.Vector().size();
                 ++i)
            {
                const T currentValue = currentProperty[i];
                const T expectedValue = expectedProperty[i];
                if (!SameKnownPropertyValue(
                        currentValue,
                        expectedValue))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameKnownPropertyValues(
            const Geometry::ConstPropertySet& current,
            const Geometry::ConstPropertySet& expected) noexcept;

        // `std::nullopt` means the stored topology could not be read at all, so
        // no two readings may be treated as equal.
        [[nodiscard]] std::optional<std::uint64_t> MeshTopologyValueSignature(
            const GS::ConstSourceView& view);

        [[nodiscard]] std::optional<std::uint64_t>
        StoredMeshTopologySignatureForEntity(
            entt::registry& raw,
            const std::uint32_t stableEntityId);


        [[nodiscard]] bool SameGeometryPositions(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept;


        void AppendDerivedJobHandleToMessage(
            std::string& message,
            const JobToken handle);


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

        [[nodiscard]] bool IsPositiveFinite(const double value) noexcept;


        struct MeshDenoiseSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> BeforePositions{};
            std::vector<bool> DeletedVertices{};
            std::vector<std::uint32_t> SourceFaceForMeshFace{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshDenoiseSourceResult BuildHalfedgeMeshForDenoise(
            const GS::ConstSourceView& view,
            std::string_view positionProperty = GS::PropertyNames::kPosition);


        [[nodiscard]] std::vector<glm::vec3> ExtractMeshPositions(
            const Geometry::HalfedgeMesh::Mesh& mesh);


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

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh);


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


        [[nodiscard]] Core::ErrorCode ResultErrorOrUnknown(
            const Core::ErrorCode error) noexcept;


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
