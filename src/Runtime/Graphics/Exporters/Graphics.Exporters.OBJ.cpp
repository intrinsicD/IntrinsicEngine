module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

module Graphics:Exporters.OBJ.Impl;
import :Exporters.OBJ;
import :IORegistry;
import :Geometry;
import :AssetErrors;

#include "Graphics.ExportUtils.hpp"

namespace Graphics
{
    using ExportUtils::AppendString;
    using ExportUtils::AppendFormatted;

    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".obj" };
    }

    std::span<const std::string_view> OBJExporter::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<std::vector<std::byte>, AssetError> OBJExporter::Export(
        const GeometryCpuData& data,
        const ExportOptions& /*options*/)
    {
        // OBJ is always ASCII
        std::vector<std::byte> out;
        out.reserve(data.Positions.size() * 40); // rough estimate

        AppendString(out, "# Exported by IntrinsicEngine\n");

        // Write vertices
        for (const auto& p : data.Positions)
        {
            AppendFormatted(out, "v %.6f %.6f %.6f\n",
                            static_cast<double>(p.x),
                            static_cast<double>(p.y),
                            static_cast<double>(p.z));
        }

        // Write normals
        bool hasNormals = !data.Normals.empty() && data.Normals.size() == data.Positions.size();
        if (hasNormals)
        {
            for (const auto& n : data.Normals)
            {
                AppendFormatted(out, "vn %.6f %.6f %.6f\n",
                                static_cast<double>(n.x),
                                static_cast<double>(n.y),
                                static_cast<double>(n.z));
            }
        }

        // Write UVs from Aux (xy components)
        bool hasUVs = !data.Aux.empty() && data.Aux.size() == data.Positions.size();
        if (hasUVs)
        {
            for (const auto& a : data.Aux)
            {
                AppendFormatted(out, "vt %.6f %.6f\n",
                                static_cast<double>(a.x),
                                static_cast<double>(a.y));
            }
        }

        // Write faces (only for triangle topology with indices)
        if (data.Topology == PrimitiveTopology::Triangles && !data.Indices.empty())
        {
            if (data.Indices.size() % 3 != 0)
                return std::unexpected(AssetError::InvalidData);

            for (std::size_t i = 0; i < data.Indices.size(); i += 3)
            {
                // OBJ indices are 1-based
                uint32_t a = data.Indices[i] + 1;
                uint32_t b = data.Indices[i + 1] + 1;
                uint32_t c = data.Indices[i + 2] + 1;

                if (hasNormals && hasUVs)
                    AppendFormatted(out, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
                else if (hasNormals)
                    AppendFormatted(out, "f %u//%u %u//%u %u//%u\n", a, a, b, b, c, c);
                else
                    AppendFormatted(out, "f %u %u %u\n", a, b, c);
            }
        }
        else if (data.Topology == PrimitiveTopology::Lines && !data.Indices.empty())
        {
            if (data.Indices.size() % 2 != 0)
                return std::unexpected(AssetError::InvalidData);

            for (std::size_t i = 0; i < data.Indices.size(); i += 2)
            {
                uint32_t a = data.Indices[i] + 1;
                uint32_t b = data.Indices[i + 1] + 1;
                AppendFormatted(out, "l %u %u\n", a, b);
            }
        }

        return out;
    }
}
