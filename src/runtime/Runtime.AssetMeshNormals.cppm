module;

#include <cstdint>

export module Extrinsic.Runtime.AssetMeshNormals;

import Extrinsic.Core.Error;
export import Geometry.HalfedgeMesh;
export import Geometry.HalfedgeMesh.IO;

export namespace Extrinsic::Runtime
{
    struct RuntimeMeshMaterializationOptions
    {
        bool AllowDisconnectedRenderableFallback{false};
    };

    [[nodiscard]] Core::Expected<Geometry::HalfedgeMesh::Mesh>
    BuildRuntimeHalfedgeMeshWithNormals(
        const Geometry::MeshIO::MeshIOResult& meshPayload,
        RuntimeMeshMaterializationOptions options = {});
}
