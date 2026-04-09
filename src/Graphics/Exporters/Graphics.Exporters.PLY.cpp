module;
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

module Graphics.Exporters.PLY;
import Graphics.IORegistry;
import Graphics.Geometry;
import Asset.Errors;

#include "Graphics.ExportUtils.hpp"

namespace Graphics
{
    using ExportUtils::AppendString;
    using ExportUtils::AppendBytes;
    using ExportUtils::AppendValue;
    using ExportUtils::AppendFormatted;

    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".ply" };
    }

    std::span<const std::string_view> PLYExporter::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<std::vector<std::byte>, AssetError> PLYExporter::Export(
        const GeometryCpuData& data,
        const ExportOptions& options)
    {
        std::vector<std::byte> out;

        bool hasNormals = !data.Normals.empty() && data.Normals.size() == data.Positions.size();
        bool hasFaces = data.Topology == PrimitiveTopology::Triangles
                        && !data.Indices.empty()
                        && data.Indices.size() % 3 == 0;
        std::size_t faceCount = hasFaces ? data.Indices.size() / 3 : 0;

        // Build header
        std::string header;
        header += "ply\n";
        if (options.Binary)
            header += "format binary_little_endian 1.0\n";
        else
            header += "format ascii 1.0\n";
        header += "comment Exported by IntrinsicEngine\n";

        header += "element vertex " + std::to_string(data.Positions.size()) + "\n";
        header += "property float x\n";
        header += "property float y\n";
        header += "property float z\n";
        if (hasNormals)
        {
            header += "property float nx\n";
            header += "property float ny\n";
            header += "property float nz\n";
        }

        if (hasFaces)
        {
            header += "element face " + std::to_string(faceCount) + "\n";
            header += "property list uchar int vertex_indices\n";
        }

        header += "end_header\n";

        // Estimate output size
        std::size_t vertSize = hasNormals ? 24 : 12; // per vertex
        std::size_t faceSize = hasFaces ? (1 + 12) : 0; // per face
        out.reserve(header.size() + data.Positions.size() * vertSize + faceCount * faceSize);

        AppendString(out, header);

        if (options.Binary)
        {
            // Binary vertex data
            for (std::size_t i = 0; i < data.Positions.size(); ++i)
            {
                AppendValue(out, data.Positions[i].x);
                AppendValue(out, data.Positions[i].y);
                AppendValue(out, data.Positions[i].z);
                if (hasNormals)
                {
                    AppendValue(out, data.Normals[i].x);
                    AppendValue(out, data.Normals[i].y);
                    AppendValue(out, data.Normals[i].z);
                }
            }

            // Binary face data
            if (hasFaces)
            {
                for (std::size_t i = 0; i < data.Indices.size(); i += 3)
                {
                    AppendValue(out, static_cast<uint8_t>(3));
                    AppendValue(out, static_cast<int32_t>(data.Indices[i]));
                    AppendValue(out, static_cast<int32_t>(data.Indices[i + 1]));
                    AppendValue(out, static_cast<int32_t>(data.Indices[i + 2]));
                }
            }
        }
        else
        {
            // ASCII vertex data
            for (std::size_t i = 0; i < data.Positions.size(); ++i)
            {
                if (hasNormals)
                {
                    AppendFormatted(out, "%.6f %.6f %.6f %.6f %.6f %.6f\n",
                                    static_cast<double>(data.Positions[i].x),
                                    static_cast<double>(data.Positions[i].y),
                                    static_cast<double>(data.Positions[i].z),
                                    static_cast<double>(data.Normals[i].x),
                                    static_cast<double>(data.Normals[i].y),
                                    static_cast<double>(data.Normals[i].z));
                }
                else
                {
                    AppendFormatted(out, "%.6f %.6f %.6f\n",
                                    static_cast<double>(data.Positions[i].x),
                                    static_cast<double>(data.Positions[i].y),
                                    static_cast<double>(data.Positions[i].z));
                }
            }

            // ASCII face data
            if (hasFaces)
            {
                for (std::size_t i = 0; i < data.Indices.size(); i += 3)
                {
                    AppendFormatted(out, "3 %u %u %u\n",
                                    data.Indices[i], data.Indices[i + 1], data.Indices[i + 2]);
                }
            }
        }

        return out;
    }
}
