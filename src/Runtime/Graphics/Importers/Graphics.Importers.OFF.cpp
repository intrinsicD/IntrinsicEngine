module;
#include <charconv>
#include <cstddef>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

module Graphics:Importers.OFF.Impl;
import :Importers.OFF;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Geometry;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".off" };

        enum class OFFVariant
        {
            Standard,  // OFF: positions only
            COFF,      // COFF: positions + colors
            NOFF,      // NOFF: positions + normals
            CNOFF      // CNOFF: positions + colors + normals
        };
    }

    std::span<const std::string_view> OFFLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> OFFLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        if (data.empty())
            return std::unexpected(AssetError::InvalidData);

        std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
        std::istringstream stream{std::string{text}};

        // Parse header: OFF, COFF, NOFF, or CNOFF
        std::string header;
        stream >> header;
        if (header.empty())
            return std::unexpected(AssetError::InvalidData);

        OFFVariant variant = OFFVariant::Standard;
        if (header == "OFF")
            variant = OFFVariant::Standard;
        else if (header == "COFF")
            variant = OFFVariant::COFF;
        else if (header == "NOFF")
            variant = OFFVariant::NOFF;
        else if (header == "CNOFF")
            variant = OFFVariant::CNOFF;
        else
            return std::unexpected(AssetError::InvalidData);

        // Parse counts: nVertices nFaces nEdges
        std::size_t nVertices = 0, nFaces = 0, nEdges = 0;
        stream >> nVertices >> nFaces >> nEdges;
        (void)nEdges; // Ignored per format spec

        if (nVertices == 0 || nFaces == 0)
            return std::unexpected(AssetError::InvalidData);

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Triangles;
        outData.Positions.resize(nVertices);
        outData.Normals.resize(nVertices, glm::vec3(0, 1, 0));
        outData.Aux.resize(nVertices, glm::vec4(0, 0, 0, 0));

        bool hasNormals = (variant == OFFVariant::NOFF || variant == OFFVariant::CNOFF);
        bool hasColors = (variant == OFFVariant::COFF || variant == OFFVariant::CNOFF);

        // Parse vertices
        for (std::size_t i = 0; i < nVertices; ++i)
        {
            float x = 0, y = 0, z = 0;
            stream >> x >> y >> z;
            outData.Positions[i] = glm::vec3(x, y, z);

            if (hasNormals)
            {
                float nx = 0, ny = 0, nz = 0;
                stream >> nx >> ny >> nz;
                outData.Normals[i] = glm::vec3(nx, ny, nz);
            }

            if (hasColors)
            {
                float r = 0, g = 0, b = 0, a = 1;
                stream >> r >> g >> b;

                // Peek for alpha (optional)
                // Check if next token is a number (alpha) or face data
                auto pos = stream.tellg();
                float maybeAlpha = 0;
                if (stream >> maybeAlpha)
                {
                    a = maybeAlpha;
                }
                else
                {
                    stream.clear();
                    stream.seekg(pos);
                }

                // Detect 0-255 range and normalize to 0-1
                if (r > 1.0f || g > 1.0f || b > 1.0f || a > 1.0f)
                {
                    r /= 255.0f;
                    g /= 255.0f;
                    b /= 255.0f;
                    if (a > 1.0f) a /= 255.0f;
                }

                // Store colors in Aux.zw (UV in xy, color in zw per codebase convention)
                outData.Aux[i] = glm::vec4(0, 0, r, g); // simplified: store R and G in zw
            }
        }

        // Parse faces
        outData.Indices.reserve(nFaces * 3);
        for (std::size_t i = 0; i < nFaces; ++i)
        {
            std::size_t faceVerts = 0;
            stream >> faceVerts;

            if (faceVerts < 3)
                continue; // Skip degenerate faces

            std::vector<uint32_t> faceIndices(faceVerts);
            for (std::size_t j = 0; j < faceVerts; ++j)
            {
                std::size_t idx = 0;
                stream >> idx;
                if (idx >= nVertices)
                    return std::unexpected(AssetError::InvalidData);
                faceIndices[j] = static_cast<uint32_t>(idx);
            }

            // Fan triangulation for polygons
            for (std::size_t j = 1; j + 1 < faceVerts; ++j)
            {
                outData.Indices.push_back(faceIndices[0]);
                outData.Indices.push_back(faceIndices[j]);
                outData.Indices.push_back(faceIndices[j + 1]);
            }
        }

        if (outData.Indices.empty())
            return std::unexpected(AssetError::InvalidData);

        // Compute normals if not provided
        if (!hasNormals)
        {
            Geometry::MeshUtils::CalculateNormals(outData.Positions, outData.Indices, outData.Normals);
        }

        // Generate UVs
        Geometry::MeshUtils::GenerateUVs(outData.Positions, outData.Aux);

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
