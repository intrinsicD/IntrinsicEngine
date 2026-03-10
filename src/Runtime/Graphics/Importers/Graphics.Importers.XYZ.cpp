module;
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
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

#include "Graphics.Importers.PostProcess.hpp"

namespace Graphics
{
    namespace
    {
        constexpr std::string_view s_Extensions[] = { ".xyz", ".pts", ".xyzrgb", ".txt" };

        [[nodiscard]] float NormalizeColorChannel(float value)
        {
            return value > 1.0f ? (value / 255.0f) : value;
        }

        [[nodiscard]] std::optional<glm::vec4> ParseColorTriplet(
            std::span<const std::string_view> tokens,
            std::size_t offset)
        {
            if (offset + 2 >= tokens.size())
                return std::nullopt;

            const auto r = Importers::TextParse::ParseNumber<float>(tokens[offset + 0]);
            const auto g = Importers::TextParse::ParseNumber<float>(tokens[offset + 1]);
            const auto b = Importers::TextParse::ParseNumber<float>(tokens[offset + 2]);
            if (!r || !g || !b)
                return std::nullopt;

            return glm::vec4(
                NormalizeColorChannel(*r),
                NormalizeColorChannel(*g),
                NormalizeColorChannel(*b),
                1.0f);
        }

        [[nodiscard]] std::optional<glm::vec4> ParsePointColor(std::span<const std::string_view> tokens)
        {
            if (tokens.size() >= 7)
            {
                if (auto color = ParseColorTriplet(tokens, tokens.size() - 3))
                    return color;
            }

            if (tokens.size() >= 6)
            {
                if (auto color = ParseColorTriplet(tokens, 3))
                    return color;
            }

            if (tokens.size() == 4)
            {
                if (const auto intensity = Importers::TextParse::ParseNumber<float>(tokens[3]))
                {
                    const float value = NormalizeColorChannel(*intensity);
                    return glm::vec4(value, value, value, 1.0f);
                }
            }

            return std::nullopt;
        }

        [[nodiscard]] bool IsScanLineMarker(std::span<const std::string_view> tokens)
        {
            if (tokens.size() != 1)
                return false;

            const std::string_view token = tokens.front();
            if (token.size() <= 2 || !token.starts_with("LH"))
                return false;

            for (char c : token.substr(2))
            {
                if (c < '0' || c > '9')
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool NeedsDelimiterNormalization(std::string_view line)
        {
            return line.find(';') != std::string_view::npos;
        }

        void NormalizeDelimitedLine(std::string_view line, std::string& scratch)
        {
            scratch.assign(line.begin(), line.end());
            for (char& c : scratch)
            {
                if (c == ';')
                    c = ' ';
            }
        }
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
        std::size_t expectedPointCount = 0;
        bool firstPayloadLineSeen = false;
        std::vector<std::string_view> tokens;
        tokens.reserve(8);
        std::string normalizedLine;

        while (Importers::TextParse::NextLine(text, lineCursor, line))
        {
            line = Importers::TextParse::Trim(line);
            if (line.empty() || line.front() == '#')
                continue;

            if (NeedsDelimiterNormalization(line))
            {
                NormalizeDelimitedLine(line, normalizedLine);
                line = Importers::TextParse::Trim(normalizedLine);
            }

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.empty())
                continue;
            if (IsScanLineMarker(tokens))
                continue;

            if (!firstPayloadLineSeen && outData.Positions.empty() && tokens.size() == 1)
            {
                if (const auto count = Importers::TextParse::ParseNumber<std::size_t>(tokens[0]); count && *count > 0)
                {
                    expectedPointCount = *count;
                    outData.Positions.reserve(expectedPointCount);
                    outData.Normals.reserve(expectedPointCount);
                    outData.Aux.reserve(expectedPointCount);
                    continue;
                }
            }

            firstPayloadLineSeen = true;
            if (tokens.size() < 3)
                continue;

            const auto px = Importers::TextParse::ParseNumber<float>(tokens[0]);
            const auto py = Importers::TextParse::ParseNumber<float>(tokens[1]);
            const auto pz = Importers::TextParse::ParseNumber<float>(tokens[2]);
            if (!px || !py || !pz)
                continue;

            outData.Positions.emplace_back(*px, *py, *pz);
            outData.Normals.emplace_back(0.0f, 1.0f, 0.0f);
            outData.Aux.emplace_back(ParsePointColor(tokens).value_or(glm::vec4(1.0f)));

            if (expectedPointCount > 0 && outData.Positions.size() >= expectedPointCount)
                break;
        }

        if (outData.Positions.empty())
            return std::unexpected(AssetError::InvalidData);

        Importers::GeometryImportPostProcessPolicy policy;
        policy.GenerateNormalsForTrianglesIfMissing = false;
        policy.GenerateUVsIfMissing = false;
        if (!Importers::ApplyGeometryImportPostProcess(
                outData,
                true,
                true,
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
