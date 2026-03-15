module;
// TinyGLTF headers
#include <charconv>
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// Optional: enable verbose import-time logging.
// #define INTRINSIC_MODELLOADER_VERBOSE 1

module Graphics:ModelLoader.Impl;

import :ModelLoader;
import :AssetErrors;
import :Model;
import :IORegistry;
import Core.IOBackend;
import Core.Filesystem;
import Core.Logging;
import RHI;
import :Geometry;
import Geometry;

#include "Importers/Graphics.Importers.AttributeVertexKey.hpp"

namespace Graphics
{
    // --- Helpers ---

    inline void RebuildVertexLookupCache(GeometryCollisionData& collision)
    {
        collision.LocalVertexLookupPoints.clear();
        collision.LocalVertexLookupIndices.clear();

        if (collision.SourceMesh)
        {
            collision.LocalVertexLookupPoints.reserve(collision.SourceMesh->VertexCount());
            collision.LocalVertexLookupIndices.reserve(collision.SourceMesh->VertexCount());
            for (std::size_t i = 0; i < collision.SourceMesh->VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                if (!collision.SourceMesh->IsValid(vh) || collision.SourceMesh->IsDeleted(vh))
                    continue;

                collision.LocalVertexLookupPoints.push_back(collision.SourceMesh->Position(vh));
                collision.LocalVertexLookupIndices.push_back(static_cast<uint32_t>(vh.Index));
            }
        }
        else
        {
            collision.LocalVertexLookupPoints = collision.Positions;
            collision.LocalVertexLookupIndices.reserve(collision.Positions.size());
            for (uint32_t i = 0; i < collision.Positions.size(); ++i)
                collision.LocalVertexLookupIndices.push_back(i);
        }

        if (!collision.LocalVertexLookupPoints.empty())
            static_cast<void>(collision.LocalVertexKdTree.BuildFromPoints(collision.LocalVertexLookupPoints));
    }

    using VertexKey = Importers::AttributeVertexKey;
    using VertexKeyHash = Importers::AttributeVertexKeyHash;

    // NOTE: Legacy OBJ helpers (ParseFloat/Split) were removed.
    // The engine now routes parsing through IORegistry importer modules.

    inline void RecalculateNormals(GeometryCpuData& mesh)
    {
        if (mesh.Topology != PrimitiveTopology::Triangles) return;

        Geometry::MeshUtils::CalculateNormals(mesh.Positions, mesh.Indices, mesh.Normals);
#if defined(INTRINSIC_MODELLOADER_VERBOSE)
        Core::Log::Info("Recalculated normals for vertices.");
#endif
    }


    inline void GenerateUVs(GeometryCpuData& mesh)
    {
        auto flatAxis = Geometry::MeshUtils::GenerateUVs(mesh.Positions, mesh.Aux);

        if (flatAxis == -1)
        {
            Core::Log::Warn("Failed to generate UVs: Mesh has no vertices.");
        }
        else
        {
#if defined(INTRINSIC_MODELLOADER_VERBOSE)
            Core::Log::Info("Generated Planar UVs for {} vertices (Axis: {})", mesh.Positions.size(), flatAxis);
#endif
        }
    }

    // --- Bulk Data Loading Helper ---
    // Reads from a generic GLTF accessor into a destination vector.
    // Optimizes for memcpy when types match and stride is packed.
    template <typename DstT, typename SrcT>
    void LoadBuffer(std::vector<DstT>& outBuffer,
                    const tinygltf::Model& model,
                    const tinygltf::Accessor& accessor,
                    size_t count)
    {
        const auto& view = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[view.buffer];

        const uint8_t* srcData = &buffer.data[view.byteOffset + accessor.byteOffset];

        // GLTF spec: byteStride of 0 implies tightly packed.
        // If > 0, it is the byte distance between start of attributes.
        const size_t srcStride = view.byteStride == 0 ? sizeof(SrcT) : view.byteStride;

        outBuffer.resize(count);

        // Optimization: Bulk copy if types match and data is tightly packed.
        if constexpr (std::is_same_v<DstT, SrcT>)
        {
            if (srcStride == sizeof(DstT))
            {
                std::memcpy(outBuffer.data(), srcData, count * sizeof(DstT));
                return;
            }
        }

        // Fallback: Stride-aware loop / type conversion.
        DstT* dstPtr = outBuffer.data();
        for (size_t i = 0; i < count; ++i)
        {
            const SrcT* elem = reinterpret_cast<const SrcT*>(srcData + i * srcStride);
            if constexpr (std::is_same_v<DstT, SrcT>)
            {
                dstPtr[i] = *elem;
            }
            else
            {
                dstPtr[i] = static_cast<DstT>(*elem);
            }
        }
    }

    // Build collision-side acceleration data from imported CPU mesh data.
    [[nodiscard]] std::shared_ptr<GeometryCollisionData> BuildCollisionData(const GeometryCpuData& cpu);

    // =====================================================================
    // ModelLoader API
    // =====================================================================

    std::expected<ModelLoadResult, AssetError> ModelLoader::LoadAsync(
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::TransferManager& transferManager,
        GeometryPool& geometryStorage,
        const std::string& filepath,
        const IORegistry& registry,
        Core::IO::IIOBackend& backend)
    {
        if (!device)
            return std::unexpected(AssetError::InvalidData);

        // 1) Import CPU geometry via IORegistry (format chosen by extension)
        auto importExp = registry.Import(filepath, backend, ImportOptions{.Hint = ImportHint::MeshOnly});
        if (!importExp)
            return std::unexpected(importExp.error());

        const MeshImportData* meshImport = std::get_if<MeshImportData>(&(*importExp));
        if (!meshImport || meshImport->Meshes.empty())
            return std::unexpected(AssetError::InvalidData);

        // 2) Create model + upload geometry async via transfer manager
        auto outModel = std::make_unique<Model>(geometryStorage, device);

        RHI::TransferToken lastToken{};

        for (auto cpu : meshImport->Meshes)
        {
            // Hygiene: if the asset has no normals/uvs, generate deterministic defaults.
            if (cpu.Normals.empty())
                RecalculateNormals(cpu);
            if (cpu.Aux.empty())
                GenerateUVs(cpu);

            GeometryUploadRequest upload{};
            upload.Positions = cpu.Positions;
            upload.Indices = cpu.Indices;
            upload.Normals = cpu.Normals;
            upload.Aux = cpu.Aux;
            upload.Topology = cpu.Topology;
            upload.UploadMode = GeometryUploadMode::Staged;

            auto [gpuData, token] = GeometryGpuData::CreateAsync(device, transferManager, upload, &geometryStorage);
            lastToken = token;

            auto geomHandle = geometryStorage.Add(std::move(gpuData));

            auto seg = std::make_shared<MeshSegment>();
            seg->Handle = geomHandle;
            seg->CollisionGeometry = BuildCollisionData(cpu);
            outModel->Meshes.push_back(std::move(seg));
        }

        return ModelLoadResult{.ModelData = std::move(outModel), .Token = lastToken};
    }

    // Build collision-side acceleration data from imported CPU mesh data.
    [[nodiscard]] std::shared_ptr<GeometryCollisionData> BuildCollisionData(const GeometryCpuData& cpu)
    {
        auto collision = std::make_shared<GeometryCollisionData>();
        collision->Positions = cpu.Positions;
        collision->Aux = cpu.Aux;
        collision->Indices = cpu.Indices;

        if (cpu.Topology == PrimitiveTopology::Triangles && !cpu.Positions.empty() && !cpu.Indices.empty())
        {
            Geometry::MeshUtils::TriangleSoupBuildParams buildParams;
            buildParams.WeldVertices = true;
            buildParams.WeldEpsilon = 1e-6f;

            if (auto mesh = Geometry::MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(cpu.Positions, cpu.Indices, cpu.Aux, buildParams))
            {
                collision->SourceMesh = std::make_shared<Geometry::Halfedge::Mesh>(std::move(*mesh));
            }
        }

        if (!collision->Positions.empty())
        {
            collision->LocalAABB = Geometry::Union(Geometry::Convert(collision->Positions));
        }

        if (cpu.Topology == PrimitiveTopology::Triangles && collision->Indices.size() >= 3)
        {
            std::vector<Geometry::AABB> primitiveBounds;
            primitiveBounds.reserve(collision->Indices.size() / 3);

            for (size_t i = 0; i + 2 < collision->Indices.size(); i += 3)
            {
                const uint32_t i0 = collision->Indices[i + 0];
                const uint32_t i1 = collision->Indices[i + 1];
                const uint32_t i2 = collision->Indices[i + 2];

                if (i0 >= collision->Positions.size() ||
                    i1 >= collision->Positions.size() ||
                    i2 >= collision->Positions.size())
                    continue;

                auto triAabb = Geometry::AABB{collision->Positions[i0], collision->Positions[i0]};
                triAabb = Geometry::Union(triAabb, collision->Positions[i1]);
                triAabb = Geometry::Union(triAabb, collision->Positions[i2]);
                primitiveBounds.push_back(triAabb);
            }

            if (!primitiveBounds.empty())
            {
                static_cast<void>(collision->LocalOctree.Build(
                    primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));
            }
        }

        RebuildVertexLookupCache(*collision);

        return collision;
    }
}
