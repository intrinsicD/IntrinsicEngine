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

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".obj" };

        void AppendString(std::vector<std::byte>& out, const std::string& s)
        {
            const auto* ptr = reinterpret_cast<const std::byte*>(s.data());
            out.insert(out.end(), ptr, ptr + s.size());
        }
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
            char buf[128];
            int n = std::snprintf(buf, sizeof(buf), "v %.6f %.6f %.6f\n",
                                  static_cast<double>(p.x),
                                  static_cast<double>(p.y),
                                  static_cast<double>(p.z));
            if (n > 0)
                AppendString(out, std::string(buf, static_cast<std::size_t>(n)));
        }

        // Write normals
        bool hasNormals = !data.Normals.empty() && data.Normals.size() == data.Positions.size();
        if (hasNormals)
        {
            for (const auto& n : data.Normals)
            {
                char buf[128];
                int len = std::snprintf(buf, sizeof(buf), "vn %.6f %.6f %.6f\n",
                                        static_cast<double>(n.x),
                                        static_cast<double>(n.y),
                                        static_cast<double>(n.z));
                if (len > 0)
                    AppendString(out, std::string(buf, static_cast<std::size_t>(len)));
            }
        }

        // Write UVs from Aux (xy components)
        bool hasUVs = !data.Aux.empty() && data.Aux.size() == data.Positions.size();
        if (hasUVs)
        {
            for (const auto& a : data.Aux)
            {
                char buf[128];
                int len = std::snprintf(buf, sizeof(buf), "vt %.6f %.6f\n",
                                        static_cast<double>(a.x),
                                        static_cast<double>(a.y));
                if (len > 0)
                    AppendString(out, std::string(buf, static_cast<std::size_t>(len)));
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

                char buf[256];
                int len = 0;
                if (hasNormals && hasUVs)
                {
                    len = std::snprintf(buf, sizeof(buf), "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                                        a, a, a, b, b, b, c, c, c);
                }
                else if (hasNormals)
                {
                    len = std::snprintf(buf, sizeof(buf), "f %u//%u %u//%u %u//%u\n",
                                        a, a, b, b, c, c);
                }
                else
                {
                    len = std::snprintf(buf, sizeof(buf), "f %u %u %u\n", a, b, c);
                }

                if (len > 0)
                    AppendString(out, std::string(buf, static_cast<std::size_t>(len)));
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
                char buf[64];
                int len = std::snprintf(buf, sizeof(buf), "l %u %u\n", a, b);
                if (len > 0)
                    AppendString(out, std::string(buf, static_cast<std::size_t>(len)));
            }
        }

        return out;
    }
}
