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
#include <glm/geometric.hpp>

module Graphics:Exporters.STL.Impl;
import :Exporters.STL;
import :IORegistry;
import :Geometry;
import :AssetErrors;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".stl" };

        void AppendString(std::vector<std::byte>& out, const std::string& s)
        {
            const auto* ptr = reinterpret_cast<const std::byte*>(s.data());
            out.insert(out.end(), ptr, ptr + s.size());
        }

        void AppendBytes(std::vector<std::byte>& out, const void* data, std::size_t size)
        {
            const auto* ptr = reinterpret_cast<const std::byte*>(data);
            out.insert(out.end(), ptr, ptr + size);
        }

        template <typename T>
        void AppendValue(std::vector<std::byte>& out, T value)
        {
            AppendBytes(out, &value, sizeof(T));
        }
    }

    std::span<const std::string_view> STLExporter::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<std::vector<std::byte>, AssetError> STLExporter::Export(
        const GeometryCpuData& data,
        const ExportOptions& options)
    {
        // STL only supports triangle meshes
        if (data.Topology != PrimitiveTopology::Triangles)
            return std::unexpected(AssetError::InvalidData);

        if (data.Indices.empty() || data.Indices.size() % 3 != 0)
            return std::unexpected(AssetError::InvalidData);

        if (data.Positions.empty())
            return std::unexpected(AssetError::InvalidData);

        uint32_t triCount = static_cast<uint32_t>(data.Indices.size() / 3);

        if (options.Binary)
        {
            // Binary STL: 80-byte header + 4-byte triangle count + 50 bytes per triangle
            std::size_t totalSize = 80 + 4 + static_cast<std::size_t>(triCount) * 50;
            std::vector<std::byte> out(totalSize, std::byte{0});

            // Header: 80 zero bytes (already initialized)
            // Triangle count
            std::memcpy(out.data() + 80, &triCount, sizeof(uint32_t));

            std::byte* ptr = out.data() + 84;

            for (uint32_t t = 0; t < triCount; ++t)
            {
                uint32_t i0 = data.Indices[t * 3];
                uint32_t i1 = data.Indices[t * 3 + 1];
                uint32_t i2 = data.Indices[t * 3 + 2];

                glm::vec3 v0 = data.Positions[i0];
                glm::vec3 v1 = data.Positions[i1];
                glm::vec3 v2 = data.Positions[i2];

                // Compute face normal
                glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                if (!std::isfinite(normal.x))
                    normal = glm::vec3(0.0f, 0.0f, 0.0f);

                // Normal (12 bytes)
                std::memcpy(ptr + 0, &normal.x, 4);
                std::memcpy(ptr + 4, &normal.y, 4);
                std::memcpy(ptr + 8, &normal.z, 4);

                // Vertex 0 (12 bytes)
                std::memcpy(ptr + 12, &v0.x, 4);
                std::memcpy(ptr + 16, &v0.y, 4);
                std::memcpy(ptr + 20, &v0.z, 4);

                // Vertex 1 (12 bytes)
                std::memcpy(ptr + 24, &v1.x, 4);
                std::memcpy(ptr + 28, &v1.y, 4);
                std::memcpy(ptr + 32, &v1.z, 4);

                // Vertex 2 (12 bytes)
                std::memcpy(ptr + 36, &v2.x, 4);
                std::memcpy(ptr + 40, &v2.y, 4);
                std::memcpy(ptr + 44, &v2.z, 4);

                // Attribute byte count (2 bytes) — already zero

                ptr += 50;
            }

            return out;
        }
        else
        {
            // ASCII STL
            std::vector<std::byte> out;
            out.reserve(triCount * 300); // rough estimate

            AppendString(out, "solid IntrinsicEngine\n");

            for (uint32_t t = 0; t < triCount; ++t)
            {
                uint32_t i0 = data.Indices[t * 3];
                uint32_t i1 = data.Indices[t * 3 + 1];
                uint32_t i2 = data.Indices[t * 3 + 2];

                glm::vec3 v0 = data.Positions[i0];
                glm::vec3 v1 = data.Positions[i1];
                glm::vec3 v2 = data.Positions[i2];

                glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                if (!std::isfinite(normal.x))
                    normal = glm::vec3(0.0f, 0.0f, 0.0f);

                char buf[512];
                int len = std::snprintf(buf, sizeof(buf),
                    "  facet normal %.6e %.6e %.6e\n"
                    "    outer loop\n"
                    "      vertex %.6e %.6e %.6e\n"
                    "      vertex %.6e %.6e %.6e\n"
                    "      vertex %.6e %.6e %.6e\n"
                    "    endloop\n"
                    "  endfacet\n",
                    static_cast<double>(normal.x), static_cast<double>(normal.y), static_cast<double>(normal.z),
                    static_cast<double>(v0.x), static_cast<double>(v0.y), static_cast<double>(v0.z),
                    static_cast<double>(v1.x), static_cast<double>(v1.y), static_cast<double>(v1.z),
                    static_cast<double>(v2.x), static_cast<double>(v2.y), static_cast<double>(v2.z));

                if (len > 0)
                    AppendString(out, std::string(buf, static_cast<std::size_t>(len)));
            }

            AppendString(out, "endsolid IntrinsicEngine\n");

            return out;
        }
    }
}
