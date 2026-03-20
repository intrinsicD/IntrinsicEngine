module;
#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Graphics.Importers.ColorParsing.hpp"
#include "Graphics.Importers.TextParse.hpp"

module Graphics.Importers.PCD;

import Graphics.IORegistry;
import Graphics.Geometry;
import Graphics.AssetErrors;
import Geometry.MeshUtils;

#include "Graphics.Importers.PostProcess.hpp"

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".pcd" };

        struct PCDField
        {
            std::string Name;
            std::size_t Size{0};
            std::size_t Count{1};
            std::size_t ByteOffset{0};
            std::size_t ScalarOffset{0};
            char Type{'F'};
        };

        struct PCDHeader
        {
            std::vector<PCDField> Fields;
            std::size_t Points{0};
            std::size_t Width{0};
            std::size_t Height{1};
            std::size_t PointStride{0};
            std::size_t ScalarValueCount{0};
            std::string DataEncoding;
        };

        [[nodiscard]] bool IsCommentOrEmpty(std::string_view line)
        {
            return line.empty() || line.front() == '#';
        }

        [[nodiscard]] const PCDField* FindField(std::span<const PCDField> fields, std::string_view name)
        {
            for (const auto& field : fields)
            {
                if (field.Name == name)
                    return &field;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<std::uint32_t> ParsePackedColorToken(std::string_view token)
        {
            if (const auto packedFloat = Importers::TextParse::ParseNumber<float>(token))
                return std::bit_cast<std::uint32_t>(*packedFloat);
            if (const auto packedUInt = Importers::TextParse::ParseNumber<std::uint32_t>(token))
                return *packedUInt;
            if (const auto packedInt = Importers::TextParse::ParseNumber<std::int32_t>(token))
                return static_cast<std::uint32_t>(*packedInt);
            return std::nullopt;
        }

        template <typename T>
        [[nodiscard]] T ReadScalar(const std::byte* ptr)
        {
            T value{};
            std::memcpy(&value, ptr, sizeof(T));
            if constexpr (sizeof(T) > 1 && std::integral<T>)
            {
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
            }
            return value;
        }

        template <>
        [[nodiscard]] float ReadScalar<float>(const std::byte* ptr)
        {
            std::uint32_t bits = ReadScalar<std::uint32_t>(ptr);
            return std::bit_cast<float>(bits);
        }

        template <>
        [[nodiscard]] double ReadScalar<double>(const std::byte* ptr)
        {
            std::uint64_t bits = ReadScalar<std::uint64_t>(ptr);
            return std::bit_cast<double>(bits);
        }

        [[nodiscard]] std::optional<float> ParseAsciiFieldValue(
            std::span<const std::string_view> tokens,
            const PCDField& field)
        {
            if (field.Count == 0 || field.ScalarOffset >= tokens.size())
                return std::nullopt;

            const auto asFloat = Importers::TextParse::ParseNumber<float>(tokens[field.ScalarOffset]);
            if (asFloat)
                return *asFloat;

            if (const auto asInt = Importers::TextParse::ParseNumber<std::int64_t>(tokens[field.ScalarOffset]))
                return static_cast<float>(*asInt);
            if (const auto asUInt = Importers::TextParse::ParseNumber<std::uint64_t>(tokens[field.ScalarOffset]))
                return static_cast<float>(*asUInt);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<float> ReadBinaryFieldValue(
            std::span<const std::byte> pointBytes,
            const PCDField& field)
        {
            if (field.Count == 0 || field.ByteOffset + field.Size > pointBytes.size())
                return std::nullopt;

            const std::byte* ptr = pointBytes.data() + field.ByteOffset;
            switch (field.Type)
            {
            case 'F':
                if (field.Size == 4) return ReadScalar<float>(ptr);
                if (field.Size == 8) return static_cast<float>(ReadScalar<double>(ptr));
                break;
            case 'I':
                if (field.Size == 1) return static_cast<float>(ReadScalar<std::int8_t>(ptr));
                if (field.Size == 2) return static_cast<float>(ReadScalar<std::int16_t>(ptr));
                if (field.Size == 4) return static_cast<float>(ReadScalar<std::int32_t>(ptr));
                if (field.Size == 8) return static_cast<float>(ReadScalar<std::int64_t>(ptr));
                break;
            case 'U':
                if (field.Size == 1) return static_cast<float>(ReadScalar<std::uint8_t>(ptr));
                if (field.Size == 2) return static_cast<float>(ReadScalar<std::uint16_t>(ptr));
                if (field.Size == 4) return static_cast<float>(ReadScalar<std::uint32_t>(ptr));
                if (field.Size == 8) return static_cast<float>(ReadScalar<std::uint64_t>(ptr));
                break;
            default:
                break;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint32_t> ReadBinaryPackedColor(
            std::span<const std::byte> pointBytes,
            const PCDField& field)
        {
            if (field.Count == 0 || field.ByteOffset + field.Size > pointBytes.size())
                return std::nullopt;

            const std::byte* ptr = pointBytes.data() + field.ByteOffset;
            if (field.Type == 'F' && field.Size == 4)
                return std::bit_cast<std::uint32_t>(ReadScalar<float>(ptr));
            if (field.Type == 'U' && field.Size == 4)
                return ReadScalar<std::uint32_t>(ptr);
            if (field.Type == 'I' && field.Size == 4)
                return static_cast<std::uint32_t>(ReadScalar<std::int32_t>(ptr));
            return std::nullopt;
        }

        [[nodiscard]] std::optional<glm::vec4> ParseAsciiColor(
            std::span<const std::string_view> tokens,
            std::span<const PCDField> fields)
        {
            const auto* rField = FindField(fields, "r");
            const auto* gField = FindField(fields, "g");
            const auto* bField = FindField(fields, "b");
            if (rField && gField && bField)
            {
                const auto r = ParseAsciiFieldValue(tokens, *rField);
                const auto g = ParseAsciiFieldValue(tokens, *gField);
                const auto b = ParseAsciiFieldValue(tokens, *bField);
                if (r && g && b)
                {
                    return glm::vec4(
                        Detail::NormalizeColorChannelToUnitRange(*r),
                        Detail::NormalizeColorChannelToUnitRange(*g),
                        Detail::NormalizeColorChannelToUnitRange(*b),
                        1.0f);
                }
            }

            if (const auto* rgbField = FindField(fields, "rgb"))
            {
                if (rgbField->ScalarOffset < tokens.size())
                {
                    if (const auto packed = ParsePackedColorToken(tokens[rgbField->ScalarOffset]))
                        return Importers::UnpackPackedRgb(*packed);
                }
            }

            if (const auto* rgbaField = FindField(fields, "rgba"))
            {
                if (rgbaField->ScalarOffset < tokens.size())
                {
                    if (const auto packed = ParsePackedColorToken(tokens[rgbaField->ScalarOffset]))
                        return Importers::UnpackPackedRgb(*packed);
                }
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<glm::vec4> ParseBinaryColor(
            std::span<const std::byte> pointBytes,
            std::span<const PCDField> fields)
        {
            const auto* rField = FindField(fields, "r");
            const auto* gField = FindField(fields, "g");
            const auto* bField = FindField(fields, "b");
            if (rField && gField && bField)
            {
                const auto r = ReadBinaryFieldValue(pointBytes, *rField);
                const auto g = ReadBinaryFieldValue(pointBytes, *gField);
                const auto b = ReadBinaryFieldValue(pointBytes, *bField);
                if (r && g && b)
                {
                    return glm::vec4(
                        Detail::NormalizeColorChannelToUnitRange(*r),
                        Detail::NormalizeColorChannelToUnitRange(*g),
                        Detail::NormalizeColorChannelToUnitRange(*b),
                        1.0f);
                }
            }

            if (const auto* rgbField = FindField(fields, "rgb"))
            {
                if (const auto packed = ReadBinaryPackedColor(pointBytes, *rgbField))
                    return Importers::UnpackPackedRgb(*packed);
            }

            if (const auto* rgbaField = FindField(fields, "rgba"))
            {
                if (const auto packed = ReadBinaryPackedColor(pointBytes, *rgbaField))
                    return Importers::UnpackPackedRgb(*packed);
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<PCDHeader> ParseHeader(std::string_view text, std::size_t& cursor)
        {
            PCDHeader header;
            std::vector<std::string> fieldNames;
            std::vector<std::size_t> fieldSizes;
            std::vector<char> fieldTypes;
            std::vector<std::size_t> fieldCounts;
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

                const std::string key = Detail::ToLowerAscii(tokens.front());
                if (key == "fields")
                {
                    fieldNames.clear();
                    fieldNames.reserve(tokens.size() - 1);
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                        fieldNames.push_back(Detail::ToLowerAscii(tokens[i]));
                }
                else if (key == "size")
                {
                    fieldSizes.clear();
                    fieldSizes.reserve(tokens.size() - 1);
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        const auto size = Importers::TextParse::ParseNumber<std::size_t>(tokens[i]);
                        if (!size || *size == 0)
                            return std::nullopt;
                        fieldSizes.push_back(*size);
                    }
                }
                else if (key == "type")
                {
                    fieldTypes.clear();
                    fieldTypes.reserve(tokens.size() - 1);
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        if (tokens[i].empty())
                            return std::nullopt;
                        fieldTypes.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[i].front()))));
                    }
                }
                else if (key == "count")
                {
                    fieldCounts.clear();
                    fieldCounts.reserve(tokens.size() - 1);
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        const auto count = Importers::TextParse::ParseNumber<std::size_t>(tokens[i]);
                        if (!count || *count == 0)
                            return std::nullopt;
                        fieldCounts.push_back(*count);
                    }
                }
                else if (key == "points" && tokens.size() >= 2)
                {
                    const auto points = Importers::TextParse::ParseNumber<std::size_t>(tokens[1]);
                    if (!points)
                        return std::nullopt;
                    header.Points = *points;
                }
                else if (key == "width" && tokens.size() >= 2)
                {
                    const auto width = Importers::TextParse::ParseNumber<std::size_t>(tokens[1]);
                    if (!width)
                        return std::nullopt;
                    header.Width = *width;
                }
                else if (key == "height" && tokens.size() >= 2)
                {
                    const auto height = Importers::TextParse::ParseNumber<std::size_t>(tokens[1]);
                    if (!height)
                        return std::nullopt;
                    header.Height = *height;
                }
                else if (key == "data" && tokens.size() >= 2)
                {
                    header.DataEncoding = Detail::ToLowerAscii(tokens[1]);
                    break;
                }
            }

            if (fieldNames.empty() || fieldSizes.empty() || fieldTypes.empty() || header.DataEncoding.empty())
                return std::nullopt;
            if (fieldNames.size() != fieldSizes.size() || fieldNames.size() != fieldTypes.size())
                return std::nullopt;

            if (fieldCounts.empty())
                fieldCounts.assign(fieldNames.size(), 1);
            if (fieldCounts.size() != fieldNames.size())
                return std::nullopt;

            if (header.Points == 0 && header.Width > 0 && header.Height > 0)
                header.Points = header.Width * header.Height;

            header.Fields.reserve(fieldNames.size());
            std::size_t byteOffset = 0;
            std::size_t scalarOffset = 0;
            for (std::size_t i = 0; i < fieldNames.size(); ++i)
            {
                PCDField field;
                field.Name = std::move(fieldNames[i]);
                field.Size = fieldSizes[i];
                field.Count = fieldCounts[i];
                field.ByteOffset = byteOffset;
                field.ScalarOffset = scalarOffset;
                field.Type = fieldTypes[i];

                byteOffset += field.Size * field.Count;
                scalarOffset += field.Count;
                header.Fields.push_back(std::move(field));
            }

            header.PointStride = byteOffset;
            header.ScalarValueCount = scalarOffset;
            return header;
        }

        [[nodiscard]] bool FinalizePointCloud(GeometryCpuData& outData)
        {
            if (outData.Positions.empty())
                return false;

            Importers::GeometryImportPostProcessPolicy policy;
            policy.GenerateNormalsForTrianglesIfMissing = false;
            policy.GenerateUVsIfMissing = false;
            return Importers::ApplyGeometryImportPostProcess(
                outData,
                true,
                true,
                Geometry::MeshUtils::CalculateNormals,
                Geometry::MeshUtils::GenerateUVs,
                policy);
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

        const auto* xField = FindField(header->Fields, "x");
        const auto* yField = FindField(header->Fields, "y");
        const auto* zField = FindField(header->Fields, "z");
        if (!xField || !yField || !zField)
            return std::unexpected(AssetError::InvalidData);

        GeometryCpuData outData;
        outData.Topology = PrimitiveTopology::Points;
        if (header->Points > 0)
        {
            outData.Positions.reserve(header->Points);
            outData.Normals.reserve(header->Points);
            outData.Aux.reserve(header->Points);
        }

        if (header->DataEncoding == "ascii")
        {
            std::string_view line;
            std::vector<std::string_view> tokens;
            tokens.reserve(std::max<std::size_t>(header->ScalarValueCount, 8));

            while (Importers::TextParse::NextLine(text, cursor, line))
            {
                line = Importers::TextParse::Trim(line);
                if (IsCommentOrEmpty(line))
                    continue;

                Importers::TextParse::SplitWhitespace(line, tokens);
                if (tokens.size() < header->ScalarValueCount)
                    continue;

                const auto x = ParseAsciiFieldValue(tokens, *xField);
                const auto y = ParseAsciiFieldValue(tokens, *yField);
                const auto z = ParseAsciiFieldValue(tokens, *zField);
                if (!x || !y || !z)
                    continue;

                outData.Positions.emplace_back(*x, *y, *z);
                outData.Normals.emplace_back(0.0f, 1.0f, 0.0f);
                outData.Aux.emplace_back(ParseAsciiColor(tokens, header->Fields).value_or(glm::vec4(1.0f)));

                if (header->Points > 0 && outData.Positions.size() >= header->Points)
                    break;
            }
        }
        else if (header->DataEncoding == "binary")
        {
            if (cursor > data.size() || header->PointStride == 0)
                return std::unexpected(AssetError::InvalidData);

            const std::size_t remainingBytes = data.size() - cursor;
            std::size_t pointCount = header->Points;
            if (pointCount == 0)
            {
                if (remainingBytes % header->PointStride != 0)
                    return std::unexpected(AssetError::InvalidData);
                pointCount = remainingBytes / header->PointStride;
            }

            const std::size_t requiredBytes = pointCount * header->PointStride;
            if (remainingBytes < requiredBytes)
                return std::unexpected(AssetError::InvalidData);

            for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
            {
                const auto pointBytes = data.subspan(cursor + pointIndex * header->PointStride, header->PointStride);
                const auto x = ReadBinaryFieldValue(pointBytes, *xField);
                const auto y = ReadBinaryFieldValue(pointBytes, *yField);
                const auto z = ReadBinaryFieldValue(pointBytes, *zField);
                if (!x || !y || !z)
                    continue;

                outData.Positions.emplace_back(*x, *y, *z);
                outData.Normals.emplace_back(0.0f, 1.0f, 0.0f);
                outData.Aux.emplace_back(ParseBinaryColor(pointBytes, header->Fields).value_or(glm::vec4(1.0f)));
            }
        }
        else
        {
            return std::unexpected(AssetError::UnsupportedFormat);
        }

        if (!FinalizePointCloud(outData))
            return std::unexpected(AssetError::InvalidData);

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
