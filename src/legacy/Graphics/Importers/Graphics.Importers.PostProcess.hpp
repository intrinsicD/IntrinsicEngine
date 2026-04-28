#pragma once

namespace Graphics::Importers
{
    struct GeometryImportPostProcessPolicy
    {
        bool RequirePositions = true;
        bool RequireIndicesForTriangles = false;
        bool GenerateNormalsForTrianglesIfMissing = true;
        bool GenerateUVsIfMissing = true;
    };

    template <typename MeshData, typename ComputeNormalsFn, typename GenerateUVsFn>
    bool ApplyGeometryImportPostProcess(
        MeshData& mesh,
        bool hasNormals,
        bool hasUVs,
        ComputeNormalsFn&& computeNormals,
        GenerateUVsFn&& generateUVs,
        const GeometryImportPostProcessPolicy& policy = {})
    {
        if (policy.RequirePositions && mesh.Positions().empty())
            return false;

        if (policy.RequireIndicesForTriangles && mesh.Topology == PrimitiveTopology::Triangles && mesh.Indices.empty())
            return false;

        if (policy.GenerateNormalsForTrianglesIfMissing && !hasNormals && mesh.Topology == PrimitiveTopology::Triangles)
            computeNormals(mesh.Positions(), mesh.Indices, mesh.Normals());

        if (policy.GenerateUVsIfMissing && !hasUVs)
        {
            auto& attrs = mesh.Attrs();
            attrs.resize(mesh.Positions().size(), glm::vec4(0.0f));
            generateUVs(mesh.Positions(), attrs);
        }

        return true;
    }
}
