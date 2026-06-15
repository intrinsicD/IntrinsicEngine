module;

#include <cstdint>
#include <string>
#include <string_view>

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
        UnsupportedDomain,
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
        InvalidRange,
        NonFiniteTexcoord,
        NonFinitePropertyValue,
        DegenerateAllTriangles,
        DegenerateUvTriangles,
        ZeroCoverageBake,
    };

    enum class MeshAttributeTextureBakeSourceDomain : std::uint8_t
    {
        Vertex,
        Face,
        Edge,
        Halfedge,
    };

    enum class MeshAttributeTextureBakeValueKind : std::uint8_t
    {
        Auto,
        Scalar,
        Label,
        Vector2,
        Vector3,
        Vector4,
    };

    enum class MeshAttributeTextureBakeEncoder : std::uint8_t
    {
        Auto,
        ScalarColormap,
        LinearScalar,
        LabelPalette,
        Vector2,
        Vector3,
        Normal,
        RgbaColor,
    };

    enum class MeshAttributeTextureBakeRangePolicy : std::uint8_t
    {
        AutoFinite,
        Manual,
    };

    struct MeshAttributeTextureBakeOptions
    {
        std::string SourcePropertyName{};
        std::string TexcoordPropertyName{"v:texcoord"};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        std::string DebugName{};
    };

    struct MeshAttributeTextureBakeRequest
    {
        std::string SourcePropertyName{};
        MeshAttributeTextureBakeSourceDomain SourceDomain{
            MeshAttributeTextureBakeSourceDomain::Vertex};
        MeshAttributeTextureBakeValueKind ValueKind{
            MeshAttributeTextureBakeValueKind::Auto};
        std::string TargetSemantic{"attribute"};
        MeshAttributeTextureBakeEncoder Encoder{
            MeshAttributeTextureBakeEncoder::Auto};
        std::string TexcoordPropertyName{"v:texcoord"};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        Assets::AssetTextureColorSpace ColorSpace{
            Assets::AssetTextureColorSpace::Unknown};
        Assets::AssetTexturePixelFormat PixelFormat{
            Assets::AssetTexturePixelFormat::Unknown};
        MeshAttributeTextureBakeRangePolicy RangePolicy{
            MeshAttributeTextureBakeRangePolicy::AutoFinite};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint64_t DirtyStamp{0u};
        std::string DebugName{};
    };

    struct MeshAttributeTextureBakeDiagnostics
    {
        MeshAttributeTextureBakeSourceDomain SourceDomain{
            MeshAttributeTextureBakeSourceDomain::Vertex};
        MeshAttributeTextureBakeValueKind ValueKind{
            MeshAttributeTextureBakeValueKind::Auto};
        MeshAttributeTextureBakeEncoder Encoder{
            MeshAttributeTextureBakeEncoder::Auto};
        std::uint32_t ExpectedValueCount{0u};
        std::uint32_t SourceValueCount{0u};
        std::uint32_t SurfaceTriangleCount{0u};
        std::uint32_t DegenerateUvTriangleCount{0u};
        std::uint32_t CoveredPixelCount{0u};
        std::uint64_t DirtyStamp{0u};
    };

    struct MeshAttributeTextureBakeResult
    {
        MeshAttributeTextureBakeStatus Status{MeshAttributeTextureBakeStatus::Success};
        MeshAttributeTextureBakeDiagnostics Diagnostics{};
        Assets::AssetTexture2DPayload Payload{};
    };

    [[nodiscard]] const char* DebugNameForMeshAttributeTextureBakeStatus(
        MeshAttributeTextureBakeStatus status) noexcept;

    [[nodiscard]] std::string BuildMeshAttributeTextureBakeAssetPath(
        std::string_view sourceKey,
        const MeshAttributeTextureBakeRequest& request);

    [[nodiscard]] MeshAttributeTextureBakeResult BakeMeshAttributeTexture(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const MeshAttributeTextureBakeRequest& request);

    [[nodiscard]] MeshAttributeTextureBakeResult BakeMeshAttributeTexture(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const MeshAttributeTextureBakeRequest& request);

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
