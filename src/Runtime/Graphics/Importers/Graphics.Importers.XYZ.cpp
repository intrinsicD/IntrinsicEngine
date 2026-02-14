module;
#include <cstddef>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

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
        std::istringstream stream{std::string{text}};

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Points;
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            outData.Positions.push_back(p);
            outData.Normals.emplace_back(0, 1, 0);

            if (!ss.eof())
            {
                float r, g, b;
                ss >> r >> g >> b;
                outData.Aux.emplace_back(r, g, b, 1.0f);
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
