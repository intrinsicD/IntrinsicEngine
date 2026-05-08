module;

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <sstream>

#include <glm/glm.hpp>

module Geometry.PointCloud.IO;

import Geometry.PointCloud;
import Geometry.Properties;
import Core.Error;

namespace Geometry::PointCloudIO
{
    namespace
    {
        struct PathInfo
        {
            std::string SourcePath;
            std::string BasePath;
        };

        [[nodiscard]] PathInfo MakePathInfo(std::string_view path)
        {
            PathInfo info{std::string(path), {}};
            const auto slash = path.find_last_of("/\\");
            if (slash != std::string_view::npos)
            {
                info.BasePath.assign(path.substr(0, slash + 1));
            }
            return info;
        }

        [[nodiscard]] Core::Expected<std::string> ReadTextFile(std::string_view path)
        {
            std::ifstream file(std::string(path), std::ios::binary);
            if (!file)
            {
                return Core::Err<std::string>(Core::ErrorCode::FileNotFound);
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            if (!file.good() && !file.eof())
            {
                return Core::Err<std::string>(Core::ErrorCode::FileReadError);
            }
            return buffer.str();
        }

        [[nodiscard]] std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        [[nodiscard]] bool NextLine(std::string_view text, std::size_t& cursor, std::string_view& line)
        {
            if (cursor >= text.size())
            {
                return false;
            }
            const std::size_t start = cursor;
            const std::size_t end = text.find('\n', cursor);
            if (end == std::string_view::npos)
            {
                cursor = text.size();
                line = text.substr(start);
            }
            else
            {
                cursor = end + 1;
                line = text.substr(start, end - start);
            }
            line = Trim(line);
            return true;
        }

        [[nodiscard]] std::vector<std::string_view> SplitWhitespace(std::string_view line)
        {
            std::vector<std::string_view> tokens;
            std::size_t cursor = 0;
            while (cursor < line.size())
            {
                while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t' || line[cursor] == '\r'))
                {
                    ++cursor;
                }
                const std::size_t start = cursor;
                while (cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t' && line[cursor] != '\r')
                {
                    ++cursor;
                }
                if (start < cursor)
                {
                    tokens.emplace_back(line.substr(start, cursor - start));
                }
            }
            return tokens;
        }

        template <class T>
        [[nodiscard]] std::optional<T> ParseNumber(std::string_view token)
        {
            T value{};
            const char* first = token.data();
            const char* last = token.data() + token.size();
            const auto [ptr, ec] = std::from_chars(first, last, value);
            if (ec != std::errc{} || ptr != last)
            {
                return std::nullopt;
            }
            return value;
        }

        [[nodiscard]] float NormalizeColorChannel(float value)
        {
            return value > 1.0f ? std::clamp(value / 255.0f, 0.0f, 1.0f) : std::clamp(value, 0.0f, 1.0f);
        }

        [[nodiscard]] std::optional<glm::vec4> ParseRgb(std::span<const std::string_view> tokens, std::size_t offset)
        {
            if (tokens.size() < offset + 3)
            {
                return std::nullopt;
            }
            const auto r = ParseNumber<float>(tokens[offset]);
            const auto g = ParseNumber<float>(tokens[offset + 1]);
            const auto b = ParseNumber<float>(tokens[offset + 2]);
            if (!r || !g || !b)
            {
                return std::nullopt;
            }
            return glm::vec4(NormalizeColorChannel(*r), NormalizeColorChannel(*g), NormalizeColorChannel(*b), 1.0f);
        }

        [[nodiscard]] Core::Expected<PointCloudIOResult> InvalidPointCloudFormat()
        {
            return Core::Err<PointCloudIOResult>(Core::ErrorCode::InvalidFormat);
        }

        void ApplyPathInfo(PointCloudIOResult& result, std::string_view path)
        {
            const auto pathInfo = MakePathInfo(path);
            result.SourcePath = pathInfo.SourcePath;
            result.BasePath = pathInfo.BasePath;
        }

        enum class PlyFormat
        {
            Ascii,
            BinaryLittleEndian,
            BinaryBigEndian,
        };

        enum class PlyScalar
        {
            Int8,
            UInt8,
            Int16,
            UInt16,
            Int32,
            UInt32,
            Float32,
            Float64,
        };

        [[nodiscard]] constexpr std::size_t PlyScalarBytes(PlyScalar s)
        {
            switch (s)
            {
            case PlyScalar::Int8:
            case PlyScalar::UInt8:
                return 1;
            case PlyScalar::Int16:
            case PlyScalar::UInt16:
                return 2;
            case PlyScalar::Int32:
            case PlyScalar::UInt32:
            case PlyScalar::Float32:
                return 4;
            case PlyScalar::Float64:
                return 8;
            }
            return 0;
        }

        [[nodiscard]] std::optional<PlyScalar> ParsePlyScalarType(std::string_view token)
        {
            if (token == "char" || token == "int8") return PlyScalar::Int8;
            if (token == "uchar" || token == "uint8") return PlyScalar::UInt8;
            if (token == "short" || token == "int16") return PlyScalar::Int16;
            if (token == "ushort" || token == "uint16") return PlyScalar::UInt16;
            if (token == "int" || token == "int32") return PlyScalar::Int32;
            if (token == "uint" || token == "uint32") return PlyScalar::UInt32;
            if (token == "float" || token == "float32") return PlyScalar::Float32;
            if (token == "double" || token == "float64") return PlyScalar::Float64;
            return std::nullopt;
        }

        struct PlyProperty
        {
            std::string Name;
            bool IsList = false;
            PlyScalar ScalarType = PlyScalar::Float32;
            PlyScalar ListCountType = PlyScalar::UInt8;
        };

        struct PlyElement
        {
            std::string Name;
            std::size_t Count = 0;
            std::vector<PlyProperty> Properties;
        };

        void ByteSwap(std::byte* p, std::size_t n)
        {
            for (std::size_t i = 0; i < n / 2; ++i)
            {
                const std::byte tmp = p[i];
                p[i] = p[n - 1 - i];
                p[n - 1 - i] = tmp;
            }
        }

        template <typename T>
        [[nodiscard]] T ReadScalarAs(const std::byte*& cursor, PlyScalar type, bool bigEndian)
        {
            std::array<std::byte, 8> buf{};
            const std::size_t n = PlyScalarBytes(type);
            std::memcpy(buf.data(), cursor, n);
            if (bigEndian)
            {
                ByteSwap(buf.data(), n);
            }
            cursor += n;

            switch (type)
            {
            case PlyScalar::Int8:
            {
                std::int8_t v = 0;
                std::memcpy(&v, buf.data(), 1);
                return static_cast<T>(v);
            }
            case PlyScalar::UInt8:
            {
                std::uint8_t v = 0;
                std::memcpy(&v, buf.data(), 1);
                return static_cast<T>(v);
            }
            case PlyScalar::Int16:
            {
                std::int16_t v = 0;
                std::memcpy(&v, buf.data(), 2);
                return static_cast<T>(v);
            }
            case PlyScalar::UInt16:
            {
                std::uint16_t v = 0;
                std::memcpy(&v, buf.data(), 2);
                return static_cast<T>(v);
            }
            case PlyScalar::Int32:
            {
                std::int32_t v = 0;
                std::memcpy(&v, buf.data(), 4);
                return static_cast<T>(v);
            }
            case PlyScalar::UInt32:
            {
                std::uint32_t v = 0;
                std::memcpy(&v, buf.data(), 4);
                return static_cast<T>(v);
            }
            case PlyScalar::Float32:
            {
                float v = 0.0f;
                std::memcpy(&v, buf.data(), 4);
                return static_cast<T>(v);
            }
            case PlyScalar::Float64:
            {
                double v = 0.0;
                std::memcpy(&v, buf.data(), 8);
                return static_cast<T>(v);
            }
            }
            return T{};
        }

        [[nodiscard]] Core::Expected<PointCloudIOResult> ParseAsciiPLYPointCloud(std::string_view text,
                                                                                std::size_t cursor,
                                                                                const std::vector<PlyElement>& elements,
                                                                                std::string_view absolute_path)
        {
            const PlyElement* vertexElement = nullptr;
            for (const auto& el : elements)
            {
                if (el.Name == "vertex" && vertexElement == nullptr)
                {
                    vertexElement = &el;
                }
            }
            if (vertexElement == nullptr || vertexElement->Count == 0)
            {
                return InvalidPointCloudFormat();
            }

            std::vector<std::string> vertexProperties;
            vertexProperties.reserve(vertexElement->Properties.size());
            for (const auto& p : vertexElement->Properties)
            {
                vertexProperties.push_back(p.Name);
            }

            auto propertyIndex = [&](std::string_view name) -> std::optional<std::size_t>
            {
                for (std::size_t i = 0; i < vertexProperties.size(); ++i)
                {
                    if (vertexProperties[i] == name)
                    {
                        return i;
                    }
                }
                return std::nullopt;
            };

            const auto xIndex = propertyIndex("x");
            const auto yIndex = propertyIndex("y");
            const auto zIndex = propertyIndex("z");
            if (!xIndex || !yIndex || !zIndex)
            {
                return InvalidPointCloudFormat();
            }
            const auto nxIndex = propertyIndex("nx");
            const auto nyIndex = propertyIndex("ny");
            const auto nzIndex = propertyIndex("nz");
            const auto rIndex = propertyIndex("red");
            const auto gIndex = propertyIndex("green");
            const auto bIndex = propertyIndex("blue");
            const bool hasNormals = nxIndex && nyIndex && nzIndex;
            const bool hasColors = rIndex && gIndex && bIndex;

            PointCloudIOResult result;
            ApplyPathInfo(result, absolute_path);
            result.Cloud.Reserve(vertexElement->Count);
            if (hasNormals)
            {
                result.Cloud.EnableNormals();
            }
            if (hasColors)
            {
                result.Cloud.EnableColors(glm::vec4(1.0f));
            }

            std::string_view line;
            for (std::size_t i = 0; i < vertexElement->Count; ++i)
            {
                if (!NextLine(text, cursor, line))
                {
                    return InvalidPointCloudFormat();
                }
                const auto tokens = SplitWhitespace(line);
                if (tokens.size() < vertexProperties.size())
                {
                    return InvalidPointCloudFormat();
                }
                const auto x = ParseNumber<float>(tokens[*xIndex]);
                const auto y = ParseNumber<float>(tokens[*yIndex]);
                const auto z = ParseNumber<float>(tokens[*zIndex]);
                if (!x || !y || !z)
                {
                    return InvalidPointCloudFormat();
                }
                const auto point = result.Cloud.AddPoint(glm::vec3(*x, *y, *z));

                if (hasNormals)
                {
                    const auto nx = ParseNumber<float>(tokens[*nxIndex]);
                    const auto ny = ParseNumber<float>(tokens[*nyIndex]);
                    const auto nz = ParseNumber<float>(tokens[*nzIndex]);
                    if (!nx || !ny || !nz)
                    {
                        return InvalidPointCloudFormat();
                    }
                    result.Cloud.Normal(point) = glm::vec3(*nx, *ny, *nz);
                }
                if (hasColors)
                {
                    const auto r = ParseNumber<float>(tokens[*rIndex]);
                    const auto g = ParseNumber<float>(tokens[*gIndex]);
                    const auto b = ParseNumber<float>(tokens[*bIndex]);
                    if (!r || !g || !b)
                    {
                        return InvalidPointCloudFormat();
                    }
                    result.Cloud.Color(point) = glm::vec4(NormalizeColorChannel(*r), NormalizeColorChannel(*g), NormalizeColorChannel(*b), 1.0f);
                }
            }

            return result;
        }

        [[nodiscard]] Core::Expected<PointCloudIOResult> ParseBinaryPLYPointCloud(std::span<const std::byte> body,
                                                                                 const std::vector<PlyElement>& elements,
                                                                                 bool bigEndian,
                                                                                 std::string_view absolute_path)
        {
            const PlyElement* vertexElement = nullptr;
            for (const auto& el : elements)
            {
                if (el.Name == "vertex" && vertexElement == nullptr)
                {
                    vertexElement = &el;
                }
            }
            if (vertexElement == nullptr || vertexElement->Count == 0)
            {
                return InvalidPointCloudFormat();
            }

            int xIndex = -1;
            int yIndex = -1;
            int zIndex = -1;
            int nxIndex = -1;
            int nyIndex = -1;
            int nzIndex = -1;
            int rIndex = -1;
            int gIndex = -1;
            int bIndex = -1;
            std::size_t vertexStride = 0;
            for (std::size_t i = 0; i < vertexElement->Properties.size(); ++i)
            {
                const auto& p = vertexElement->Properties[i];
                if (p.IsList)
                {
                    return InvalidPointCloudFormat();
                }
                if (p.Name == "x" && p.ScalarType == PlyScalar::Float32)
                {
                    xIndex = static_cast<int>(i);
                }
                else if (p.Name == "y" && p.ScalarType == PlyScalar::Float32)
                {
                    yIndex = static_cast<int>(i);
                }
                else if (p.Name == "z" && p.ScalarType == PlyScalar::Float32)
                {
                    zIndex = static_cast<int>(i);
                }
                else if (p.Name == "nx" && p.ScalarType == PlyScalar::Float32)
                {
                    nxIndex = static_cast<int>(i);
                }
                else if (p.Name == "ny" && p.ScalarType == PlyScalar::Float32)
                {
                    nyIndex = static_cast<int>(i);
                }
                else if (p.Name == "nz" && p.ScalarType == PlyScalar::Float32)
                {
                    nzIndex = static_cast<int>(i);
                }
                else if (p.Name == "red" && p.ScalarType == PlyScalar::UInt8)
                {
                    rIndex = static_cast<int>(i);
                }
                else if (p.Name == "green" && p.ScalarType == PlyScalar::UInt8)
                {
                    gIndex = static_cast<int>(i);
                }
                else if (p.Name == "blue" && p.ScalarType == PlyScalar::UInt8)
                {
                    bIndex = static_cast<int>(i);
                }
                vertexStride += PlyScalarBytes(p.ScalarType);
            }
            if (xIndex < 0 || yIndex < 0 || zIndex < 0)
            {
                return InvalidPointCloudFormat();
            }
            const bool hasNormals = nxIndex >= 0 && nyIndex >= 0 && nzIndex >= 0;
            const bool hasColors = rIndex >= 0 && gIndex >= 0 && bIndex >= 0;

            std::vector<std::size_t> vertexOffsets(vertexElement->Properties.size(), 0);
            {
                std::size_t off = 0;
                for (std::size_t i = 0; i < vertexElement->Properties.size(); ++i)
                {
                    vertexOffsets[i] = off;
                    off += PlyScalarBytes(vertexElement->Properties[i].ScalarType);
                }
            }

            const std::byte* cursor = body.data();
            const std::byte* const end = body.data() + body.size();

            PointCloudIOResult result;
            ApplyPathInfo(result, absolute_path);
            result.Cloud.Reserve(vertexElement->Count);
            if (hasNormals)
            {
                result.Cloud.EnableNormals();
            }
            if (hasColors)
            {
                result.Cloud.EnableColors(glm::vec4(1.0f));
            }

            auto readFloat = [&](const std::byte* base, std::size_t offset) -> float {
                std::array<std::byte, 4> tmp{};
                std::memcpy(tmp.data(), base + offset, 4);
                if (bigEndian)
                {
                    ByteSwap(tmp.data(), 4);
                }
                float v = 0.0f;
                std::memcpy(&v, tmp.data(), 4);
                return v;
            };

            auto readUInt8 = [&](const std::byte* base, std::size_t offset) -> std::uint8_t {
                std::uint8_t v = 0;
                std::memcpy(&v, base + offset, 1);
                return v;
            };

            for (const PlyElement& element : elements)
            {
                if (&element == vertexElement)
                {
                    const std::size_t total = element.Count * vertexStride;
                    if (static_cast<std::size_t>(end - cursor) < total)
                    {
                        return InvalidPointCloudFormat();
                    }
                    for (std::size_t row = 0; row < element.Count; ++row)
                    {
                        const std::byte* base = cursor + row * vertexStride;
                        const auto point = result.Cloud.AddPoint(glm::vec3(
                            readFloat(base, vertexOffsets[xIndex]),
                            readFloat(base, vertexOffsets[yIndex]),
                            readFloat(base, vertexOffsets[zIndex])));
                        if (hasNormals)
                        {
                            result.Cloud.Normal(point) = glm::vec3(
                                readFloat(base, vertexOffsets[nxIndex]),
                                readFloat(base, vertexOffsets[nyIndex]),
                                readFloat(base, vertexOffsets[nzIndex]));
                        }
                        if (hasColors)
                        {
                            const float r = static_cast<float>(readUInt8(base, vertexOffsets[rIndex]));
                            const float g = static_cast<float>(readUInt8(base, vertexOffsets[gIndex]));
                            const float b = static_cast<float>(readUInt8(base, vertexOffsets[bIndex]));
                            result.Cloud.Color(point) = glm::vec4(
                                NormalizeColorChannel(r),
                                NormalizeColorChannel(g),
                                NormalizeColorChannel(b),
                                1.0f);
                        }
                    }
                    cursor += total;
                }
                else
                {
                    std::size_t scalarStride = 0;
                    for (const auto& p : element.Properties)
                    {
                        if (p.IsList)
                        {
                            return InvalidPointCloudFormat();
                        }
                        scalarStride += PlyScalarBytes(p.ScalarType);
                    }
                    const std::size_t total = element.Count * scalarStride;
                    if (static_cast<std::size_t>(end - cursor) < total)
                    {
                        return InvalidPointCloudFormat();
                    }
                    cursor += total;
                }
            }

            return result;
        }
    }

    Core::Expected<PointCloudIOResult> LoadXYZ(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<PointCloudIOResult>(text.error());
        }

        PointCloudIOResult result;
        ApplyPathInfo(result, absolute_path);

        std::size_t cursor = 0;
        std::string_view line;
        bool firstPayloadLine = true;
        std::size_t expectedCount = 0;
        while (NextLine(*text, cursor, line))
        {
            const auto comment = line.find('#');
            if (comment != std::string_view::npos)
            {
                line = Trim(line.substr(0, comment));
            }
            if (line.empty())
            {
                continue;
            }

            const auto tokens = SplitWhitespace(line);
            if (tokens.empty())
            {
                continue;
            }
            if (firstPayloadLine && tokens.size() == 1)
            {
                if (const auto count = ParseNumber<std::size_t>(tokens[0]))
                {
                    expectedCount = *count;
                    result.Cloud.Reserve(expectedCount);
                    firstPayloadLine = false;
                    continue;
                }
            }
            firstPayloadLine = false;

            if (tokens.size() < 3)
            {
                return InvalidPointCloudFormat();
            }
            const auto x = ParseNumber<float>(tokens[0]);
            const auto y = ParseNumber<float>(tokens[1]);
            const auto z = ParseNumber<float>(tokens[2]);
            if (!x || !y || !z)
            {
                return InvalidPointCloudFormat();
            }

            std::optional<glm::vec4> color;
            if (tokens.size() >= 6)
            {
                color = ParseRgb(tokens, 3);
            }
            else if (tokens.size() == 4)
            {
                if (const auto intensity = ParseNumber<float>(tokens[3]))
                {
                    const float c = NormalizeColorChannel(*intensity);
                    color = glm::vec4(c, c, c, 1.0f);
                }
            }

            if (color && !result.Cloud.HasColors())
            {
                result.Cloud.EnableColors(glm::vec4(1.0f));
            }
            const auto point = result.Cloud.AddPoint(glm::vec3(*x, *y, *z));
            if (color)
            {
                result.Cloud.Color(point) = *color;
            }

            if (expectedCount > 0 && result.Cloud.VerticesSize() >= expectedCount)
            {
                break;
            }
        }

        if (result.Cloud.IsEmpty())
        {
            return InvalidPointCloudFormat();
        }
        return result;
    }

    Core::Expected<PointCloudIOResult> LoadPCD(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<PointCloudIOResult>(text.error());
        }

        std::vector<std::string> fields;
        std::size_t pointCount = 0;
        bool dataAscii = false;

        std::size_t cursor = 0;
        std::string_view line;
        while (NextLine(*text, cursor, line))
        {
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.empty())
            {
                continue;
            }
            if (tokens[0] == "FIELDS")
            {
                fields.clear();
                for (std::size_t i = 1; i < tokens.size(); ++i)
                {
                    fields.emplace_back(tokens[i]);
                }
            }
            else if (tokens[0] == "POINTS" && tokens.size() >= 2)
            {
                if (const auto parsed = ParseNumber<std::size_t>(tokens[1]))
                {
                    pointCount = *parsed;
                }
            }
            else if (tokens[0] == "DATA" && tokens.size() >= 2)
            {
                dataAscii = tokens[1] == "ascii";
                break;
            }
        }

        if (!dataAscii || fields.empty())
        {
            return InvalidPointCloudFormat();
        }

        auto fieldIndex = [&](std::string_view name) -> std::optional<std::size_t>
        {
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                if (fields[i] == name)
                {
                    return i;
                }
            }
            return std::nullopt;
        };

        const auto xIndex = fieldIndex("x");
        const auto yIndex = fieldIndex("y");
        const auto zIndex = fieldIndex("z");
        if (!xIndex || !yIndex || !zIndex)
        {
            return InvalidPointCloudFormat();
        }
        const auto nxIndex = fieldIndex("normal_x");
        const auto nyIndex = fieldIndex("normal_y");
        const auto nzIndex = fieldIndex("normal_z");
        const auto rIndex = fieldIndex("r");
        const auto gIndex = fieldIndex("g");
        const auto bIndex = fieldIndex("b");
        const bool hasNormals = nxIndex && nyIndex && nzIndex;
        const bool hasColors = rIndex && gIndex && bIndex;

        PointCloudIOResult result;
        ApplyPathInfo(result, absolute_path);
        if (pointCount > 0)
        {
            result.Cloud.Reserve(pointCount);
        }
        if (hasNormals)
        {
            result.Cloud.EnableNormals();
        }
        if (hasColors)
        {
            result.Cloud.EnableColors(glm::vec4(1.0f));
        }

        while (NextLine(*text, cursor, line))
        {
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.size() < fields.size())
            {
                return InvalidPointCloudFormat();
            }
            const auto x = ParseNumber<float>(tokens[*xIndex]);
            const auto y = ParseNumber<float>(tokens[*yIndex]);
            const auto z = ParseNumber<float>(tokens[*zIndex]);
            if (!x || !y || !z)
            {
                return InvalidPointCloudFormat();
            }
            const auto point = result.Cloud.AddPoint(glm::vec3(*x, *y, *z));

            if (hasNormals)
            {
                const auto nx = ParseNumber<float>(tokens[*nxIndex]);
                const auto ny = ParseNumber<float>(tokens[*nyIndex]);
                const auto nz = ParseNumber<float>(tokens[*nzIndex]);
                if (!nx || !ny || !nz)
                {
                    return InvalidPointCloudFormat();
                }
                result.Cloud.Normal(point) = glm::vec3(*nx, *ny, *nz);
            }
            if (hasColors)
            {
                const auto r = ParseNumber<float>(tokens[*rIndex]);
                const auto g = ParseNumber<float>(tokens[*gIndex]);
                const auto b = ParseNumber<float>(tokens[*bIndex]);
                if (!r || !g || !b)
                {
                    return InvalidPointCloudFormat();
                }
                result.Cloud.Color(point) = glm::vec4(NormalizeColorChannel(*r), NormalizeColorChannel(*g), NormalizeColorChannel(*b), 1.0f);
            }
        }

        if (result.Cloud.IsEmpty() || (pointCount > 0 && result.Cloud.VerticesSize() != pointCount))
        {
            return InvalidPointCloudFormat();
        }
        return result;
    }

    Core::Expected<PointCloudIOResult> LoadPLY(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<PointCloudIOResult>(text.error());
        }

        std::size_t cursor = 0;
        std::string_view line;
        if (!NextLine(*text, cursor, line) || line != "ply")
        {
            return InvalidPointCloudFormat();
        }

        PlyFormat format = PlyFormat::Ascii;
        bool formatSeen = false;
        bool headerEndSeen = false;
        std::vector<PlyElement> elements;
        while (NextLine(*text, cursor, line))
        {
            if (line == "end_header")
            {
                headerEndSeen = true;
                break;
            }
            if (line.empty())
            {
                continue;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.empty())
            {
                continue;
            }
            if (tokens[0] == "comment" || tokens[0] == "obj_info")
            {
                continue;
            }
            if (tokens[0] == "format")
            {
                if (tokens.size() < 2)
                {
                    return InvalidPointCloudFormat();
                }
                if (tokens[1] == "ascii")
                {
                    format = PlyFormat::Ascii;
                }
                else if (tokens[1] == "binary_little_endian")
                {
                    format = PlyFormat::BinaryLittleEndian;
                }
                else if (tokens[1] == "binary_big_endian")
                {
                    format = PlyFormat::BinaryBigEndian;
                }
                else
                {
                    return InvalidPointCloudFormat();
                }
                formatSeen = true;
            }
            else if (tokens[0] == "element")
            {
                if (tokens.size() < 3)
                {
                    return InvalidPointCloudFormat();
                }
                const auto count = ParseNumber<std::size_t>(tokens[2]);
                if (!count)
                {
                    return InvalidPointCloudFormat();
                }
                PlyElement element;
                element.Name = std::string(tokens[1]);
                element.Count = *count;
                elements.push_back(std::move(element));
            }
            else if (tokens[0] == "property")
            {
                if (elements.empty())
                {
                    return InvalidPointCloudFormat();
                }
                PlyProperty prop;
                if (tokens.size() >= 5 && tokens[1] == "list")
                {
                    const auto countType = ParsePlyScalarType(tokens[2]);
                    const auto elemType = ParsePlyScalarType(tokens[3]);
                    if (!countType || !elemType)
                    {
                        return InvalidPointCloudFormat();
                    }
                    prop.IsList = true;
                    prop.ListCountType = *countType;
                    prop.ScalarType = *elemType;
                    prop.Name = std::string(tokens[4]);
                }
                else if (tokens.size() >= 3)
                {
                    const auto scalarType = ParsePlyScalarType(tokens[1]);
                    if (!scalarType)
                    {
                        return InvalidPointCloudFormat();
                    }
                    prop.IsList = false;
                    prop.ScalarType = *scalarType;
                    prop.Name = std::string(tokens[2]);
                }
                else
                {
                    return InvalidPointCloudFormat();
                }
                elements.back().Properties.push_back(std::move(prop));
            }
        }

        if (!formatSeen || !headerEndSeen)
        {
            return InvalidPointCloudFormat();
        }

        if (format == PlyFormat::Ascii)
        {
            return ParseAsciiPLYPointCloud(*text, cursor, elements, absolute_path);
        }

        const std::span<const std::byte> body(
            reinterpret_cast<const std::byte*>(text->data() + cursor),
            text->size() - cursor);
        return ParseBinaryPLYPointCloud(body, elements, format == PlyFormat::BinaryBigEndian, absolute_path);
    }
}


