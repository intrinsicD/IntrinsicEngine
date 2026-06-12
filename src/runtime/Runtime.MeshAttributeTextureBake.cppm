module;

#include <cstdint>
#include <string>

export module Extrinsic.Runtime.MeshAttributeTextureBake;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.HalfedgeMesh;

export namespace Extrinsic::Runtime
{
    enum class MeshAttributeTextureBakeStatus : std::uint8_t
    {
        Success,
        WrongDomain,
        MissingVertexSource,
        MissingHalfedgeTopology,
        MissingFaceTopology,
        EmptyMesh,
        InvalidTopology,
        MissingTexcoords,
        MissingProperty,
        UnsupportedPropertyType,
        MismatchedPropertyCount,
        InvalidResolution,
        NonFiniteTexcoord,
        NonFinitePropertyValue,
        DegenerateAllTriangles,
    };

    struct MeshAttributeTextureBakeOptions
    {
        std::string SourcePropertyName{};
        std::string TexcoordPropertyName{"v:texcoord"};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        std::string DebugName{};
    };

    struct MeshAttributeTextureBakeResult
    {
        MeshAttributeTextureBakeStatus Status{MeshAttributeTextureBakeStatus::Success};
        Assets::AssetTexture2DPayload Payload{};
    };

    [[nodiscard]] const char* DebugNameForMeshAttributeTextureBakeStatus(
        MeshAttributeTextureBakeStatus status) noexcept;

    [[nodiscard]] MeshAttributeTextureBakeResult BakeMeshVertexNormalTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeOptions& options = {});

    [[nodiscard]] MeshAttributeTextureBakeResult BakeMeshVertexNormalTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeOptions& options = {});

    [[nodiscard]] MeshAttributeTextureBakeResult BakeMeshVertexColorTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeOptions& options = {});

    [[nodiscard]] MeshAttributeTextureBakeResult BakeMeshVertexColorTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeOptions& options = {});
}
