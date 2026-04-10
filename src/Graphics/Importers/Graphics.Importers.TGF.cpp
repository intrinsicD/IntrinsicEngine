module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics.Importers.TextParse.hpp"

module Graphics.Importers.TGF;
import Graphics.IORegistry;
import Graphics.Geometry;
import Asset.Errors;

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

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Lines;
        auto& positions = outData.Positions();
        auto& normals   = outData.Normals();
        auto& attrs     = outData.Attrs();
        std::string_view line;
        size_t lineCursor = 0;
        bool parsingEdges = false;
        std::unordered_map<int, uint32_t> idMap;
        std::vector<std::string_view> tokens;
        tokens.reserve(8);

        while (Importers::TextParse::NextLine(text, lineCursor, line))
        {
            line = Importers::TextParse::Trim(line);
            if (line.empty())
                continue;

            if (line.front() == '#')
            {
                parsingEdges = true;
                continue;
            }

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.empty())
                continue;

            if (!parsingEdges)
            {
                const auto id = Importers::TextParse::ParseNumber<int>(tokens[0]);
                if (!id)
                    continue;

                glm::vec3 p{0.0f};
                if (tokens.size() >= 4)
                {
                    const auto x = Importers::TextParse::ParseNumber<float>(tokens[1]);
                    const auto y = Importers::TextParse::ParseNumber<float>(tokens[2]);
                    const auto z = Importers::TextParse::ParseNumber<float>(tokens[3]);
                    if (x && y && z)
                        p = glm::vec3(*x, *y, *z);
                }

                auto idx = static_cast<uint32_t>(positions.size());
                idMap[*id] = idx;
                positions.push_back(p);
                normals.emplace_back(0, 1, 0);
                attrs.emplace_back(1);
            }
            else
            {
                if (tokens.size() < 2)
                    continue;

                const auto from = Importers::TextParse::ParseNumber<int>(tokens[0]);
                const auto to = Importers::TextParse::ParseNumber<int>(tokens[1]);
                if (!from || !to)
                    continue;

                if (idMap.count(*from) && idMap.count(*to))
                {
                    outData.Indices.push_back(idMap[*from]);
                    outData.Indices.push_back(idMap[*to]);
                }
            }
        }

        if (positions.empty())
            return std::unexpected(AssetError::InvalidData);

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
