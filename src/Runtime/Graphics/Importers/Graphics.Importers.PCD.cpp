module;
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Graphics.Importers.TextParse.hpp"

module Graphics:Importers.PCD.Impl;

import :Importers.PCD;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Geometry;

#include "Graphics.Importers.PostProcess.hpp"

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".pcd" };

        struct PCDHeader
        {
            std::vector<std::string_view> Fields;
            std::size_t Points{0};
            std::string_view DataEncoding{};
        };

        [[nodiscard]] bool IsCommentOrEmpty(std::string_view line)
        {
            return line.empty() || line.front() == '#';
        }

        [[nodiscard]] std::optional<glm::vec4> ParseColor(std::span<const std::string_view> tokens,
                                                          std::span<const std::string_view> fields)
        {
            auto fieldIndex = [&](std::string_view fieldName) -> std::optional<std::size_t> {
                for (std::size_t i = 0; i < fields.size(); ++i)
                {
                    if (fields[i] == fieldName)
                        return i;
                }
                return std::nullopt;
            };

            const auto rIdx = fieldIndex("r");
            const auto gIdx = fieldIndex("g");
            const auto bIdx = fieldIndex("b");
            if (rIdx && gIdx && bIdx && *rIdx < tokens.size() && *gIdx < tokens.size() && *bIdx < tokens.size())
            {
                const auto r = Importers::TextParse::ParseNumber<float>(tokens[*rIdx]);
                const auto g = Importers::TextParse::ParseNumber<float>(tokens[*gIdx]);
                const auto b = Importers::TextParse::ParseNumber<float>(tokens[*bIdx]);
                if (r && g && b)
                {
                    auto normalize = [](float c) {
                        return c > 1.0f ? (c / 255.0f) : c;
                    };
                    return glm::vec4(normalize(*r), normalize(*g), normalize(*b), 1.0f);
                }
            }

            const auto rgbIdx = fieldIndex("rgb");
            if (rgbIdx && *rgbIdx < tokens.size())
            {
                const auto packed = Importers::TextParse::ParseNumber<float>(tokens[*rgbIdx]);
                if (packed)
                {
                    const uint32_t bits = std::bit_cast<uint32_t>(*packed);
                    const float r = static_cast<float>((bits >> 16) & 0xFFu) / 255.0f;
                    const float g = static_cast<float>((bits >> 8) & 0xFFu) / 255.0f;
                    const float b = static_cast<float>(bits & 0xFFu) / 255.0f;
                    return glm::vec4(r, g, b, 1.0f);
                }
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<PCDHeader> ParseHeader(std::string_view text, std::size_t& cursor)
        {
            PCDHeader header;
            std::string_view line;
            std::vector<std::string_view> tokens;
            tokens.reserve(16);

            while (Importers::TextParse::NextLine(text, cursor, line))
            {
                line = Importers::TextParse::Trim(line);
                if (IsCommentOrEmpty(line))
                    continue;

                Importers::TextParse::SplitWhitespace(line, tokens);
                if (tokens.empty())
                    continue;

                const std::string_view key = tokens.front();
                if (key == "FIELDS")
                {
                    header.Fields.assign(tokens.begin() + 1, tokens.end());
                }
                else if (key == "POINTS" && tokens.size() >= 2)
                {
                    const auto points = Importers::TextParse::ParseNumber<std::size_t>(tokens[1]);
                    if (!points)
                        return std::nullopt;
                    header.Points = *points;
                }
                else if (key == "DATA" && tokens.size() >= 2)
                {
                    header.DataEncoding = tokens[1];
                    break;
                }
            }

            if (header.Fields.empty() || header.DataEncoding.empty())
                return std::nullopt;
            return header;
        }
    }

    std::span<const std::string_view> PCDLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> PCDLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());

        std::size_t cursor = 0;
        auto header = ParseHeader(text, cursor);
        if (!header)
            return std::unexpected(AssetError::InvalidData);

        if (header->DataEncoding != "ascii")
            return std::unexpected(AssetError::UnsupportedFormat);

        auto fieldIndex = [&](std::string_view fieldName) -> std::optional<std::size_t> {
            for (std::size_t i = 0; i < header->Fields.size(); ++i)
            {
                if (header->Fields[i] == fieldName)
                    return i;
            }
            return std::nullopt;
        };

        const auto xIdx = fieldIndex("x");
        const auto yIdx = fieldIndex("y");
        const auto zIdx = fieldIndex("z");
        if (!xIdx || !yIdx || !zIdx)
            return std::unexpected(AssetError::InvalidData);

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Points;

        std::string_view line;
        std::vector<std::string_view> tokens;
        tokens.reserve(std::max<std::size_t>(header->Fields.size(), 8));

        while (Importers::TextParse::NextLine(text, cursor, line))
        {
            line = Importers::TextParse::Trim(line);
            if (IsCommentOrEmpty(line))
                continue;

            Importers::TextParse::SplitWhitespace(line, tokens);
            if (tokens.size() < header->Fields.size())
                continue;

            const auto x = Importers::TextParse::ParseNumber<float>(tokens[*xIdx]);
            const auto y = Importers::TextParse::ParseNumber<float>(tokens[*yIdx]);
            const auto z = Importers::TextParse::ParseNumber<float>(tokens[*zIdx]);
            if (!x || !y || !z)
                continue;

            outData.Positions.emplace_back(*x, *y, *z);
            outData.Normals.emplace_back(0.0f, 1.0f, 0.0f);
            outData.Aux.emplace_back(ParseColor(tokens, header->Fields).value_or(glm::vec4(1.0f)));

            if (header->Points > 0 && outData.Positions.size() >= header->Points)
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
