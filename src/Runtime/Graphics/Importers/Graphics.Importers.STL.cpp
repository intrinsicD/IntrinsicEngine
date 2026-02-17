module;
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

module Graphics:Importers.STL.Impl;
import :Importers.STL;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Geometry;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".stl" };

        // Spatial hash for vertex deduplication
        struct VertexKey
        {
            glm::vec3 Position;

            bool operator==(const VertexKey& other) const
            {
                // Quantize-based comparison (~10 micron tolerance for unit-scale models)
                auto qi = [](float v) { return static_cast<int32_t>(v * 1e5f); };
                return qi(Position.x) == qi(other.Position.x)
                    && qi(Position.y) == qi(other.Position.y)
                    && qi(Position.z) == qi(other.Position.z);
            }
        };

        struct VertexKeyHash
        {
            std::size_t operator()(const VertexKey& k) const
            {
                auto qi = [](float v) { return static_cast<int32_t>(v * 1e5f); };
                std::size_t h = std::hash<int32_t>()(qi(k.Position.x));
                h ^= std::hash<int32_t>()(qi(k.Position.y)) * 2654435761u;
                h ^= std::hash<int32_t>()(qi(k.Position.z)) * 40503u;
                return h;
            }
        };

        bool IsBinarySTL(std::span<const std::byte> data)
        {
            // Binary STL: 80-byte header + 4-byte triangle count + 50*N bytes
            if (data.size() < 84) return false;

            uint32_t triCount = 0;
            std::memcpy(&triCount, data.data() + 80, sizeof(uint32_t));

            std::size_t expectedSize = 80 + 4 + static_cast<std::size_t>(triCount) * 50;
            if (expectedSize == data.size())
                return true;

            // Fallback: check if file starts with "solid" AND contains "facet" → ASCII
            std::string_view text(reinterpret_cast<const char*>(data.data()),
                                  std::min(data.size(), static_cast<std::size_t>(1024)));

            // Skip whitespace
            auto start = text.find_first_not_of(" \t\r\n");
            if (start == std::string_view::npos) return false;

            bool startsSolid = text.substr(start, 5) == "solid";
            bool hasFacet = text.find("facet") != std::string_view::npos;

            // If it starts with "solid" and has "facet", it's ASCII
            if (startsSolid && hasFacet) return false;

            // Otherwise assume binary
            return data.size() >= 84;
        }

        std::expected<GeometryCpuData, AssetError> ParseBinary(std::span<const std::byte> data)
        {
            if (data.size() < 84)
                return std::unexpected(AssetError::InvalidData);

            uint32_t triCount = 0;
            std::memcpy(&triCount, data.data() + 80, sizeof(uint32_t));

            if (triCount == 0)
                return std::unexpected(AssetError::InvalidData);

            std::size_t expectedSize = 80 + 4 + static_cast<std::size_t>(triCount) * 50;
            if (data.size() < expectedSize)
                return std::unexpected(AssetError::DecodeFailed);

            GeometryCpuData outData;
            outData.Topology = PrimitiveTopology::Triangles;
            outData.Positions.reserve(triCount * 3);
            outData.Indices.reserve(triCount * 3);

            std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVerts;
            uniqueVerts.reserve(triCount * 2);

            bool allNormalsZero = true;
            std::vector<glm::vec3> faceNormals;
            faceNormals.reserve(triCount);

            const std::byte* ptr = data.data() + 84;

            for (uint32_t t = 0; t < triCount; ++t)
            {
                // Read normal (3 floats)
                float nx, ny, nz;
                std::memcpy(&nx, ptr + 0, 4);
                std::memcpy(&ny, ptr + 4, 4);
                std::memcpy(&nz, ptr + 8, 4);
                glm::vec3 faceNormal(nx, ny, nz);
                faceNormals.push_back(faceNormal);
                if (glm::length(faceNormal) > 1e-6f) allNormalsZero = false;

                // Read 3 vertices (each 3 floats)
                for (int v = 0; v < 3; ++v)
                {
                    float vx, vy, vz;
                    std::memcpy(&vx, ptr + 12 + v * 12 + 0, 4);
                    std::memcpy(&vy, ptr + 12 + v * 12 + 4, 4);
                    std::memcpy(&vz, ptr + 12 + v * 12 + 8, 4);
                    glm::vec3 pos(vx, vy, vz);

                    VertexKey key{pos};
                    auto it = uniqueVerts.find(key);
                    if (it == uniqueVerts.end())
                    {
                        auto idx = static_cast<uint32_t>(outData.Positions.size());
                        uniqueVerts[key] = idx;
                        outData.Positions.push_back(pos);
                        outData.Indices.push_back(idx);
                    }
                    else
                    {
                        outData.Indices.push_back(it->second);
                    }
                }

                ptr += 50; // 12 (normal) + 36 (3 vertices) + 2 (attribute byte count)
            }

            // Normals: recompute from geometry if all-zero
            outData.Normals.resize(outData.Positions.size(), glm::vec3(0, 0, 0));
            Geometry::MeshUtils::CalculateNormals(outData.Positions, outData.Indices, outData.Normals);

            // Generate UVs
            Geometry::MeshUtils::GenerateUVs(outData.Positions, outData.Aux);

            return outData;
        }

        std::expected<GeometryCpuData, AssetError> ParseAscii(std::span<const std::byte> data)
        {
            std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
            std::istringstream stream{std::string{text}};

            GeometryCpuData outData;
            outData.Topology = PrimitiveTopology::Triangles;

            std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVerts;
            std::string line;
            std::string token;

            while (std::getline(stream, line))
            {
                // Trim leading whitespace
                auto start = line.find_first_not_of(" \t");
                if (start == std::string::npos) continue;
                std::string_view trimmed(line.data() + start, line.size() - start);

                if (trimmed.starts_with("vertex"))
                {
                    std::string lineStr{trimmed};
                    std::stringstream ss{lineStr};
                    ss >> token; // "vertex"
                    float x = 0, y = 0, z = 0;
                    ss >> x >> y >> z;
                    glm::vec3 pos(x, y, z);

                    VertexKey key{pos};
                    auto it = uniqueVerts.find(key);
                    if (it == uniqueVerts.end())
                    {
                        auto idx = static_cast<uint32_t>(outData.Positions.size());
                        uniqueVerts[key] = idx;
                        outData.Positions.push_back(pos);
                        outData.Indices.push_back(idx);
                    }
                    else
                    {
                        outData.Indices.push_back(it->second);
                    }
                }
            }

            if (outData.Positions.empty())
                return std::unexpected(AssetError::InvalidData);

            // Compute normals from geometry
            outData.Normals.resize(outData.Positions.size(), glm::vec3(0, 0, 0));
            Geometry::MeshUtils::CalculateNormals(outData.Positions, outData.Indices, outData.Normals);

            // Generate UVs
            Geometry::MeshUtils::GenerateUVs(outData.Positions, outData.Aux);

            return outData;
        }
    }

    std::span<const std::string_view> STLLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> STLLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        if (data.empty())
            return std::unexpected(AssetError::InvalidData);

        auto result = IsBinarySTL(data) ? ParseBinary(data) : ParseAscii(data);
        if (!result)
            return std::unexpected(result.error());

        MeshImportData importData;
        importData.Meshes.push_back(std::move(*result));
        return ImportResult{std::move(importData)};
    }
}
