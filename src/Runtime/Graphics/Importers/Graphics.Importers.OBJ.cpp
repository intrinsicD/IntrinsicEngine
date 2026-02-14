module;
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

module Graphics:Importers.OBJ.Impl;
import :Importers.OBJ;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Geometry;

namespace Graphics
{
    namespace
    {
        struct VertexKey
        {
            int p = -1, n = -1, t = -1;
            bool operator==(const VertexKey& other) const { return p == other.p && n == other.n && t == other.t; }
        };

        struct VertexKeyHash
        {
            size_t operator()(const VertexKey& k) const
            {
                return std::hash<int>()(k.p) ^ (std::hash<int>()(k.n) << 1) ^ (std::hash<int>()(k.t) << 2);
            }
        };

        static constexpr std::string_view s_Extensions[] = { ".obj" };
    }

    std::span<const std::string_view> OBJLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> OBJLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
        std::istringstream stream{std::string{text}};

        GeometryCpuData outData;
        std::vector<glm::vec3> tempPos;
        std::vector<glm::vec3> tempNorm;
        std::vector<glm::vec2> tempUV;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVertices;

        outData.Topology = PrimitiveTopology::Triangles;
        std::string line;
        bool hasNormals = false;
        bool hasUVs = false;

        while (std::getline(stream, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string type;
            ss >> type;

            if (type == "v")
            {
                glm::vec3 v;
                ss >> v.x >> v.y >> v.z;
                tempPos.push_back(v);
            }
            else if (type == "vn")
            {
                glm::vec3 vn;
                ss >> vn.x >> vn.y >> vn.z;
                tempNorm.push_back(vn);
                hasNormals = true;
            }
            else if (type == "vt")
            {
                glm::vec2 vt;
                ss >> vt.x >> vt.y;
                tempUV.push_back(vt);
                hasUVs = true;
            }
            else if (type == "f")
            {
                std::string vertexStr;
                std::vector<uint32_t> faceIndices;

                while (ss >> vertexStr)
                {
                    VertexKey key;
                    size_t s1 = vertexStr.find('/');
                    size_t s2 = vertexStr.find('/', s1 + 1);

                    std::from_chars(vertexStr.data(),
                                    vertexStr.data() + (s1 == std::string::npos ? vertexStr.size() : s1), key.p);
                    key.p--;

                    if (s1 != std::string::npos)
                    {
                        if (s2 != std::string::npos && s2 - s1 > 1)
                        {
                            std::from_chars(vertexStr.data() + s1 + 1, vertexStr.data() + s2, key.t);
                            key.t--;
                        }
                        if (s2 != std::string::npos && s2 + 1 < vertexStr.size())
                        {
                            std::from_chars(vertexStr.data() + s2 + 1, vertexStr.data() + vertexStr.size(), key.n);
                            key.n--;
                        }
                    }

                    if (uniqueVertices.find(key) == uniqueVertices.end())
                    {
                        auto idx = static_cast<uint32_t>(outData.Positions.size());
                        uniqueVertices[key] = idx;

                        outData.Positions.push_back(tempPos[key.p]);
                        outData.Normals.push_back((key.n >= 0 && static_cast<size_t>(key.n) < tempNorm.size())
                                                      ? tempNorm[key.n]
                                                      : glm::vec3(0, 1, 0));
                        glm::vec2 uv = (key.t >= 0 && static_cast<size_t>(key.t) < tempUV.size()) ? tempUV[key.t] : glm::vec2(0, 0);
                        outData.Aux.emplace_back(uv.x, uv.y, 0, 0);
                    }
                    faceIndices.push_back(uniqueVertices[key]);
                }

                for (size_t i = 1; i < faceIndices.size() - 1; ++i)
                {
                    outData.Indices.push_back(faceIndices[0]);
                    outData.Indices.push_back(faceIndices[i]);
                    outData.Indices.push_back(faceIndices[i + 1]);
                }
            }
            else if (type == "l")
            {
                outData.Topology = PrimitiveTopology::Lines;
            }
        }

        if (outData.Positions.empty())
            return std::unexpected(AssetError::InvalidData);

        if (!hasNormals && outData.Topology == PrimitiveTopology::Triangles)
        {
            Geometry::MeshUtils::CalculateNormals(outData.Positions, outData.Indices, outData.Normals);
        }
        if (!hasUVs)
        {
            Geometry::MeshUtils::GenerateUVs(outData.Positions, outData.Aux);
        }

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
