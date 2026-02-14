module;
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

module Graphics:Importers.TGF.Impl;
import :Importers.TGF;
import :IORegistry;
import :Geometry;
import :AssetErrors;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".tgf" };
    }

    std::span<const std::string_view> TGFLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> TGFLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
        std::istringstream stream{std::string{text}};

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Lines;
        std::string line;
        bool parsingEdges = false;
        std::unordered_map<int, uint32_t> idMap;

        while (std::getline(stream, line))
        {
            if (line.empty()) continue;
            if (line[0] == '#')
            {
                parsingEdges = true;
                continue;
            }

            std::stringstream ss(line);
            if (!parsingEdges)
            {
                int id;
                ss >> id;
                glm::vec3 p{0.0f};
                if (!ss.eof()) ss >> p.x >> p.y >> p.z;

                auto idx = static_cast<uint32_t>(outData.Positions.size());
                idMap[id] = idx;
                outData.Positions.push_back(p);
                outData.Normals.emplace_back(0, 1, 0);
                outData.Aux.emplace_back(1);
            }
            else
            {
                int from, to;
                ss >> from >> to;
                if (idMap.count(from) && idMap.count(to))
                {
                    outData.Indices.push_back(idMap[from]);
                    outData.Indices.push_back(idMap[to]);
                }
            }
        }

        if (outData.Positions.empty())
            return std::unexpected(AssetError::InvalidData);

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
