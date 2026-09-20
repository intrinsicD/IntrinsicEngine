// Mesh admission and deferred ring verdicts without owning mesh/soup dependencies.
// Include after EditorProcessing, GeometryAvailability and GeometrySources imports.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
    namespace GS = ECS::Components::GeometrySources;
    // Checks property presence/cardinality without traversing or copying buffers.
    [[nodiscard]] EditorCommandStatus ValidateMeshPositionSourceMetadata(
        const GS::ConstSourceView& view, std::string& diagnostic,
        std::string_view positionProperty = GS::PropertyNames::kPosition);

    // Requires successful position metadata validation; callers retain error priority.
    [[nodiscard]] EditorCommandStatus ValidateMeshVertexDeletionMaskMetadata(
        const GS::ConstSourceView& view, std::string& diagnostic,
        std::string_view positionProperty = GS::PropertyNames::kPosition);

    [[nodiscard]] EditorCommandStatus ValidateMeshSoupSourceMetadata(
        const GS::ConstSourceView& view, std::string& diagnostic,
        std::string_view positionProperty = GS::PropertyNames::kPosition);

    [[nodiscard]] EditorCommandStatus ValidateMeshSoupFaceRings(
        const GS::ConstSourceView&, std::string& diagnostic,
        std::string_view positionProperty);

    [[nodiscard]] bool PrepareMeshSoupFaceRings(
        const EditorProcessingContext&, entt::entity,
        const GeometryEntityAvailability&, std::string& diagnostic);
}
}
