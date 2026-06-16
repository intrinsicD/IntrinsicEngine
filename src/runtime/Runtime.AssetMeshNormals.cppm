module;

#include <cstddef>
#include <cstdint>

export module Extrinsic.Runtime.AssetMeshNormals;

import Extrinsic.Core.Error;
export import Geometry.HalfedgeMesh;
export import Geometry.HalfedgeMesh.IO;
export import Geometry.UvAtlas;

export namespace Extrinsic::Runtime
{
    enum class RuntimeMeshResolvedUvProvenance : std::uint8_t
    {
        None = 0,
        AuthoredPreserved,
        GeneratedAtlas,
    };

    enum class RuntimeMeshUvFailurePolicy : std::uint8_t
    {
        Required = 0,
        Optional,
    };

    struct RuntimeMeshUvResolutionOptions
    {
        bool PreserveValidAuthoredUvs{true};
        bool ForceRegenerate{false};
        RuntimeMeshUvFailurePolicy FailurePolicy{RuntimeMeshUvFailurePolicy::Required};
        std::uint32_t Resolution{1024u};
        std::uint32_t Padding{2u};
        float TexelsPerUnit{0.0f};
        const Geometry::UvAtlas::UvAtlasBackend* Backend{nullptr};
    };

    struct RuntimeMeshMaterializationDiagnostics
    {
        RuntimeMeshResolvedUvProvenance TexcoordProvenance{RuntimeMeshResolvedUvProvenance::None};
        Geometry::UvAtlas::UvAtlasStatus UvAtlasStatus{Geometry::UvAtlas::UvAtlasStatus::EmptyInput};
        bool AuthoredTexcoordsValid{false};
        bool AuthoredTexcoordsRejected{false};
        bool ResolvedTexcoordsValid{false};
        std::size_t SourceVertexCount{0};
        std::size_t SourceFaceCount{0};
        std::size_t ResolvedVertexCount{0};
        std::size_t ResolvedFaceCount{0};
        std::size_t SeamSplitVertexCount{0};
        std::uint32_t ChartCount{0};
        std::uint32_t AtlasWidth{0};
        std::uint32_t AtlasHeight{0};
    };

    struct RuntimeMeshMaterializationResult
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        RuntimeMeshMaterializationDiagnostics Diagnostics{};
    };

    struct RuntimeMeshMaterializationOptions
    {
        bool AllowDisconnectedRenderableFallback{false};
        RuntimeMeshUvResolutionOptions UvResolution{};
    };

    [[nodiscard]] bool MeshPayloadHasValidVertexTexcoords(
        const Geometry::MeshIO::MeshIOResult& meshPayload) noexcept;

    [[nodiscard]] Core::Expected<RuntimeMeshMaterializationResult>
    BuildRuntimeHalfedgeMeshMaterialization(
        const Geometry::MeshIO::MeshIOResult& meshPayload,
        RuntimeMeshMaterializationOptions options = {});

    [[nodiscard]] Core::Expected<Geometry::HalfedgeMesh::Mesh>
    BuildRuntimeHalfedgeMeshWithNormals(
        const Geometry::MeshIO::MeshIOResult& meshPayload,
        RuntimeMeshMaterializationOptions options = {});
}
