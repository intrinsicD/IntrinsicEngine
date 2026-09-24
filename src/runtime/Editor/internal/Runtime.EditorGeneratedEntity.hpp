// Undoable publication of a new root-level mesh or graph entity derived from a
// source entity. Include after editor/common, ECS registry, Geometry.HalfedgeMesh
// and Geometry.Graph imports; the global module fragment supplies entt, glm,
// integer, optional and string types.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    struct EditorGeneratedEntityRequest
    {
        entt::entity Source{entt::null};
        std::string Name{};
        // Exactly one of Mesh or Graph, in world coordinates.
        std::optional<Geometry::HalfedgeMesh::Mesh> Mesh{};
        std::optional<Geometry::Graph::Graph> Graph{};
        // StableId.High reserved for this producer; Low is allocated past the
        // largest existing id in that range.
        std::uint64_t IdentityHigh{};
        std::string Label{};
    };
    struct EditorGeneratedEntityPublication
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::uint32_t OutputEntityId{};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };
    // Creates the entity with import-parity render/selection authoring and
    // culling bounds, selects it, and records create/destroy in the command
    // history. Undo refuses (StaleEntity) once the entity was edited.
    // Transform matrices composed up the hierarchy; nullopt for cycles, broken
    // parents or non-finite results.
    [[nodiscard]] std::optional<glm::mat4> ComposeEditorWorldMatrix(const entt::registry& raw,
                                                                    entt::entity entity);
    [[nodiscard]] EditorGeneratedEntityPublication PublishEditorGeneratedEntity(
        const EditorProcessingContext& context, EditorGeneratedEntityRequest request);
}
}
