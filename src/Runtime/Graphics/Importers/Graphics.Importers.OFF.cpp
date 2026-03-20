module;
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics.Importers.ColorParsing.hpp"
#include "Graphics.Importers.TextParse.hpp"
#include "Graphics.Importers.TriangulationUtils.hpp"

module Graphics.Importers.OFF;
import Graphics.IORegistry;
import Graphics.Geometry;
import Graphics.AssetErrors;
import Geometry;

#include "Graphics.Importers.PostProcess.hpp"

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
        size_t cursor = 0;
        std::string_view line;
        std::vector<std::string_view> tokens;
        tokens.reserve(16);

        // Parse header: OFF, COFF, NOFF, or CNOFF
        if (!Importers::TextParse::NextLine(text, cursor, line))
            return std::unexpected(AssetError::InvalidData);

        line = Importers::TextParse::Trim(line);
        if (line.empty())
            return std::unexpected(AssetError::InvalidData);

        OFFVariant variant = OFFVariant::Standard;
        if (line == "OFF")
            variant = OFFVariant::Standard;
        else if (line == "COFF")
            variant = OFFVariant::COFF;
        else if (line == "NOFF")
            variant = OFFVariant::NOFF;
        else if (line == "CNOFF")
            variant = OFFVariant::CNOFF;
        else
            return std::unexpected(AssetError::InvalidData);

        // Parse counts: nVertices nFaces nEdges
        if (!Importers::TextParse::NextLine(text, cursor, line))
            return std::unexpected(AssetError::InvalidData);

        Importers::TextParse::SplitWhitespace(line, tokens);
        if (tokens.size() < 2)
            return std::unexpected(AssetError::InvalidData);

        const auto nVertices = Importers::TextParse::ParseNumber<std::size_t>(tokens[0]);
        const auto nFaces = Importers::TextParse::ParseNumber<std::size_t>(tokens[1]);
        if (!nVertices || !nFaces || *nVertices == 0 || *nFaces == 0)
            return std::unexpected(AssetError::InvalidData);

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Triangles;
        outData.Positions.resize(*nVertices);
        outData.Normals.resize(*nVertices, glm::vec3(0, 1, 0));
        outData.Aux.resize(*nVertices, glm::vec4(0, 0, 0, 0));

        bool hasNormals = (variant == OFFVariant::NOFF || variant == OFFVariant::CNOFF);
        bool hasColors = (variant == OFFVariant::COFF || variant == OFFVariant::CNOFF);

        // Parse vertices
        for (std::size_t i = 0; i < *nVertices; ++i)
        {
            if (!Importers::TextParse::NextLine(text, cursor, line))
                return std::unexpected(AssetError::InvalidData);

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.size() < 3)
                return std::unexpected(AssetError::InvalidData);

            const auto x = Importers::TextParse::ParseNumber<float>(tokens[0]);
            const auto y = Importers::TextParse::ParseNumber<float>(tokens[1]);
            const auto z = Importers::TextParse::ParseNumber<float>(tokens[2]);
            if (!x || !y || !z)
                return std::unexpected(AssetError::InvalidData);

            outData.Positions[i] = glm::vec3(*x, *y, *z);

            std::size_t tokenIdx = 3;

            if (hasNormals && tokenIdx + 2 < tokens.size())
            {
                const auto nx = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx]);
                const auto ny = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx + 1]);
                const auto nz = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx + 2]);
                if (nx && ny && nz)
                    outData.Normals[i] = glm::vec3(*nx, *ny, *nz);
                tokenIdx += 3;
            }

            if (hasColors && tokenIdx + 2 < tokens.size())
            {
                const auto r = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx]);
                const auto g = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx + 1]);
                const auto b = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx + 2]);
                if (r && g && b)
                {
                    float rf = Detail::NormalizeColorChannelToUnitRange(*r);
                    float gf = Detail::NormalizeColorChannelToUnitRange(*g);
                    float bf = Detail::NormalizeColorChannelToUnitRange(*b);

                    // Store colors in Aux.zw (UV in xy, color in zw per codebase convention)
                    outData.Aux[i] = glm::vec4(0, 0, rf, gf); // simplified: store R and G in zw
                }
                tokenIdx += 3;

                // Peek for alpha (optional)
                if (tokenIdx < tokens.size())
                {
                    if (const auto a = Importers::TextParse::ParseNumber<float>(tokens[tokenIdx]))
                        (void)Detail::NormalizeColorChannelToUnitRange(*a); // alpha available but not stored in current format
                }
            }
        }

        // Parse faces
        outData.Indices.reserve(*nFaces * 3);
        for (std::size_t i = 0; i < *nFaces; ++i)
        {
            if (!Importers::TextParse::NextLine(text, cursor, line))
                return std::unexpected(AssetError::InvalidData);

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.empty())
                continue;

            const auto faceVerts = Importers::TextParse::ParseNumber<std::size_t>(tokens[0]);
            if (!faceVerts || *faceVerts < 3)
                continue; // Skip degenerate faces

            if (tokens.size() < 1 + *faceVerts)
                return std::unexpected(AssetError::InvalidData);

            std::vector<uint32_t> faceIndices(*faceVerts);
            for (std::size_t j = 0; j < *faceVerts; ++j)
            {
                const auto idx = Importers::TextParse::ParseNumber<std::size_t>(tokens[1 + j]);
                if (!idx || *idx >= *nVertices)
                    return std::unexpected(AssetError::InvalidData);
                faceIndices[j] = static_cast<uint32_t>(*idx);
            }

            // Fan triangulation for polygons
            Importers::TriangulateFan(faceIndices, outData.Indices);
        }

        Importers::GeometryImportPostProcessPolicy policy;
        policy.RequireIndicesForTriangles = true;
        if (!Importers::ApplyGeometryImportPostProcess(
                outData,
                hasNormals,
                false,
                Geometry::MeshUtils::CalculateNormals,
                Geometry::MeshUtils::GenerateUVs,
                policy))
        {
            return std::unexpected(AssetError::InvalidData);
        }

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
