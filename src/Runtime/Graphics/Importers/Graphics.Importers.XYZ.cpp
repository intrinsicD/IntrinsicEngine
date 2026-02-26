module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics.Importers.TextParse.hpp"

module Graphics:Importers.XYZ.Impl;
import :Importers.XYZ;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Geometry;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".xyz", ".pcd" };
    }

    std::span<const std::string_view> XYZLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> XYZLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Points;
        std::string_view line;
        size_t lineCursor = 0;
        std::vector<std::string_view> tokens;
        tokens.reserve(8);

        while (Importers::TextParse::NextLine(text, lineCursor, line))
        {
            line = Importers::TextParse::Trim(line);
            if (line.empty() || line.front() == '#')
                continue;

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.size() < 3)
                continue;

            const auto px = Importers::TextParse::ParseNumber<float>(tokens[0]);
            const auto py = Importers::TextParse::ParseNumber<float>(tokens[1]);
            const auto pz = Importers::TextParse::ParseNumber<float>(tokens[2]);
            if (!px || !py || !pz)
                continue;

            glm::vec3 p{*px, *py, *pz};
            outData.Positions.push_back(p);
            outData.Normals.emplace_back(0, 1, 0);

            if (tokens.size() >= 6)
            {
                const auto r = Importers::TextParse::ParseNumber<float>(tokens[3]);
                const auto g = Importers::TextParse::ParseNumber<float>(tokens[4]);
                const auto b = Importers::TextParse::ParseNumber<float>(tokens[5]);
                if (r && g && b)
                    outData.Aux.emplace_back(*r, *g, *b, 1.0f);
                else
                    outData.Aux.emplace_back(1.0f);
            }
            else
            {
                outData.Aux.emplace_back(1.0f);
            }
        }

        if (outData.Positions.empty())
            return std::unexpected(AssetError::InvalidData);

        Geometry::MeshUtils::GenerateUVs(outData.Positions, outData.Aux);

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
