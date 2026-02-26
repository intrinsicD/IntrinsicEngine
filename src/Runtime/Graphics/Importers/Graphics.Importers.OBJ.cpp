module;
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics.Importers.TextParse.hpp"

module Graphics:Importers.OBJ.Impl;
import :Importers.OBJ;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Geometry;

#include "Graphics.Importers.PostProcess.hpp"

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

        GeometryCpuData outData;
        std::vector<glm::vec3> tempPos;
        std::vector<glm::vec3> tempNorm;
        std::vector<glm::vec2> tempUV;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVertices;

        outData.Topology = PrimitiveTopology::Triangles;
        std::string_view line;
        size_t lineCursor = 0;
        bool hasNormals = false;
        bool hasUVs = false;
        std::vector<std::string_view> tokens;
        tokens.reserve(16);

        while (Importers::TextParse::NextLine(text, lineCursor, line))
        {
            line = Importers::TextParse::Trim(line);
            if (line.empty() || line.front() == '#')
                continue;

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.empty())
                continue;

            const std::string_view type = tokens.front();

            if (type == "v")
            {
                if (tokens.size() < 4)
                    continue;

                const auto x = Importers::TextParse::ParseNumber<float>(tokens[1]);
                const auto y = Importers::TextParse::ParseNumber<float>(tokens[2]);
                const auto z = Importers::TextParse::ParseNumber<float>(tokens[3]);
                if (x && y && z)
                    tempPos.emplace_back(*x, *y, *z);
            }
            else if (type == "vn")
            {
                if (tokens.size() < 4)
                    continue;

                const auto x = Importers::TextParse::ParseNumber<float>(tokens[1]);
                const auto y = Importers::TextParse::ParseNumber<float>(tokens[2]);
                const auto z = Importers::TextParse::ParseNumber<float>(tokens[3]);
                if (x && y && z)
                {
                    tempNorm.emplace_back(*x, *y, *z);
                    hasNormals = true;
                }
            }
            else if (type == "vt")
            {
                if (tokens.size() < 3)
                    continue;

                const auto u = Importers::TextParse::ParseNumber<float>(tokens[1]);
                const auto v = Importers::TextParse::ParseNumber<float>(tokens[2]);
                if (u && v)
                {
                    tempUV.emplace_back(*u, *v);
                    hasUVs = true;
                }
            }
            else if (type == "f")
            {
                std::vector<uint32_t> faceIndices;
                faceIndices.reserve(tokens.size());

                for (size_t tokenIndex = 1; tokenIndex < tokens.size(); ++tokenIndex)
                {
                    const std::string_view vertexStr = tokens[tokenIndex];
                    VertexKey key;
                    const size_t s1 = vertexStr.find('/');
                    const size_t s2 = vertexStr.find('/', s1 == std::string_view::npos ? vertexStr.size() : s1 + 1);

                    std::from_chars(vertexStr.data(),
                                    vertexStr.data() + (s1 == std::string_view::npos ? vertexStr.size() : s1),
                                    key.p);
                    key.p--;

                    if (s1 != std::string_view::npos)
                    {
                        if (s2 != std::string_view::npos && s2 - s1 > 1)
                        {
                            std::from_chars(vertexStr.data() + s1 + 1, vertexStr.data() + s2, key.t);
                            key.t--;
                        }
                        if (s2 != std::string_view::npos && s2 + 1 < vertexStr.size())
                        {
                            std::from_chars(vertexStr.data() + s2 + 1, vertexStr.data() + vertexStr.size(), key.n);
                            key.n--;
                        }
                    }

                    if (key.p < 0 || static_cast<size_t>(key.p) >= tempPos.size())
                        continue;

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

        if (!Importers::ApplyGeometryImportPostProcess(
                outData,
                hasNormals,
                hasUVs,
                Geometry::MeshUtils::CalculateNormals,
                Geometry::MeshUtils::GenerateUVs))
        {
            return std::unexpected(AssetError::InvalidData);
        }

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
