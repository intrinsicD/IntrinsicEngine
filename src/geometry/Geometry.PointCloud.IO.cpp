module;

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
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

        [[nodiscard]] std::optional<glm::vec4> ParseXYZPointColor(std::span<const std::string_view> tokens)
        {
            if (tokens.size() >= 7)
            {
                if (auto color = ParseRgb(tokens, tokens.size() - 3))
                {
                    return color;
                }
            }
            if (tokens.size() >= 6)
            {
                if (auto color = ParseRgb(tokens, 3))
                {
                    return color;
                }
            }
            if (tokens.size() == 4)
            {
                if (const auto intensity = ParseNumber<float>(tokens[3]))
                {
                    const float c = NormalizeColorChannel(*intensity);
                    return glm::vec4(c, c, c, 1.0f);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsXYZScanLineMarker(std::span<const std::string_view> tokens)
        {
            if (tokens.size() != 1)
            {
                return false;
            }
            const std::string_view token = tokens.front();
            if (token.size() <= 2 || !token.starts_with("LH"))
            {
                return false;
            }
            for (char c : token.substr(2))
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool XYZNeedsDelimiterNormalization(std::string_view line)
        {
            return line.find(';') != std::string_view::npos;
        }

        void XYZNormalizeDelimitedLine(std::string_view line, std::string& scratch)
        {
            scratch.assign(line.begin(), line.end());
            for (char& c : scratch)
            {
                if (c == ';')
                {
                    c = ' ';
                }
            }
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

        struct PcdField
        {
            std::string Name;
            std::size_t Size = 0;
            char Type = 'F';
            std::size_t Count = 1;
            std::size_t ByteOffset = 0;
            std::size_t ScalarOffset = 0;
        };

        struct PcdHeader
        {
            std::vector<PcdField> Fields;
            std::size_t Points = 0;
            std::size_t Width = 0;
            std::size_t Height = 1;
            std::size_t PointStride = 0;
            std::size_t ScalarValueCount = 0;
            std::string DataEncoding;
        };

        [[nodiscard]] const PcdField* FindPCDField(std::span<const PcdField> fields, std::string_view name)
        {
            for (const auto& f : fields)
            {
                if (f.Name == name)
                {
                    return &f;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<PcdHeader> ParsePCDHeader(std::string_view text, std::size_t& cursor)
        {
            PcdHeader header;
            std::vector<std::string> fieldNames;
            std::vector<std::size_t> fieldSizes;
            std::vector<char> fieldTypes;
            std::vector<std::size_t> fieldCounts;

            std::string_view line;
            while (NextLine(text, cursor, line))
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
                const std::string_view key = tokens[0];
                if (key == "FIELDS")
                {
                    fieldNames.clear();
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        fieldNames.emplace_back(tokens[i]);
                    }
                }
                else if (key == "SIZE")
                {
                    fieldSizes.clear();
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        const auto s = ParseNumber<std::size_t>(tokens[i]);
                        if (!s || *s == 0)
                        {
                            return std::nullopt;
                        }
                        fieldSizes.push_back(*s);
                    }
                }
                else if (key == "TYPE")
                {
                    fieldTypes.clear();
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        if (tokens[i].empty())
                        {
                            return std::nullopt;
                        }
                        const char c = tokens[i].front();
                        const char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
                        if (upper != 'F' && upper != 'I' && upper != 'U')
                        {
                            return std::nullopt;
                        }
                        fieldTypes.push_back(upper);
                    }
                }
                else if (key == "COUNT")
                {
                    fieldCounts.clear();
                    for (std::size_t i = 1; i < tokens.size(); ++i)
                    {
                        const auto c = ParseNumber<std::size_t>(tokens[i]);
                        if (!c || *c == 0)
                        {
                            return std::nullopt;
                        }
                        fieldCounts.push_back(*c);
                    }
                }
                else if (key == "POINTS" && tokens.size() >= 2)
                {
                    const auto p = ParseNumber<std::size_t>(tokens[1]);
                    if (!p)
                    {
                        return std::nullopt;
                    }
                    header.Points = *p;
                }
                else if (key == "WIDTH" && tokens.size() >= 2)
                {
                    const auto w = ParseNumber<std::size_t>(tokens[1]);
                    if (!w)
                    {
                        return std::nullopt;
                    }
                    header.Width = *w;
                }
                else if (key == "HEIGHT" && tokens.size() >= 2)
                {
                    const auto h = ParseNumber<std::size_t>(tokens[1]);
                    if (!h)
                    {
                        return std::nullopt;
                    }
                    header.Height = *h;
                }
                else if (key == "DATA" && tokens.size() >= 2)
                {
                    header.DataEncoding.assign(tokens[1]);
                    for (auto& ch : header.DataEncoding)
                    {
                        if (ch >= 'A' && ch <= 'Z')
                        {
                            ch = static_cast<char>(ch - 'A' + 'a');
                        }
                    }
                    break;
                }
            }

            if (fieldNames.empty() || fieldSizes.empty() || fieldTypes.empty() || header.DataEncoding.empty())
            {
                return std::nullopt;
            }
            if (fieldNames.size() != fieldSizes.size() || fieldNames.size() != fieldTypes.size())
            {
                return std::nullopt;
            }
            if (fieldCounts.empty())
            {
                fieldCounts.assign(fieldNames.size(), 1);
            }
            if (fieldCounts.size() != fieldNames.size())
            {
                return std::nullopt;
            }

            if (header.Points == 0 && header.Width > 0 && header.Height > 0)
            {
                header.Points = header.Width * header.Height;
            }

            header.Fields.reserve(fieldNames.size());
            std::size_t byteOffset = 0;
            std::size_t scalarOffset = 0;
            for (std::size_t i = 0; i < fieldNames.size(); ++i)
            {
                PcdField field;
                field.Name = std::move(fieldNames[i]);
                field.Size = fieldSizes[i];
                field.Type = fieldTypes[i];
                field.Count = fieldCounts[i];
                field.ByteOffset = byteOffset;
                field.ScalarOffset = scalarOffset;
                byteOffset += field.Size * field.Count;
                scalarOffset += field.Count;
                header.Fields.push_back(std::move(field));
            }
            header.PointStride = byteOffset;
            header.ScalarValueCount = scalarOffset;
            return header;
        }

        template <typename T>
        [[nodiscard]] T ReadPCDIntegerScalar(const std::byte* ptr)
        {
            T value{};
            std::memcpy(&value, ptr, sizeof(T));
            if constexpr (sizeof(T) > 1)
            {
                if constexpr (std::endian::native == std::endian::big)
                {
                    value = std::byteswap(value);
                }
            }
            return value;
        }

        [[nodiscard]] std::optional<float> ReadPCDBinaryScalar(std::span<const std::byte> pointBytes,
                                                               const PcdField& field)
        {
            if (field.Count == 0 || field.ByteOffset + field.Size > pointBytes.size())
            {
                return std::nullopt;
            }
            const std::byte* ptr = pointBytes.data() + field.ByteOffset;
            switch (field.Type)
            {
            case 'F':
                if (field.Size == 4)
                {
                    const auto bits = ReadPCDIntegerScalar<std::uint32_t>(ptr);
                    return std::bit_cast<float>(bits);
                }
                if (field.Size == 8)
                {
                    const auto bits = ReadPCDIntegerScalar<std::uint64_t>(ptr);
                    return static_cast<float>(std::bit_cast<double>(bits));
                }
                return std::nullopt;
            case 'I':
                if (field.Size == 1) return static_cast<float>(ReadPCDIntegerScalar<std::int8_t>(ptr));
                if (field.Size == 2) return static_cast<float>(ReadPCDIntegerScalar<std::int16_t>(ptr));
                if (field.Size == 4) return static_cast<float>(ReadPCDIntegerScalar<std::int32_t>(ptr));
                if (field.Size == 8) return static_cast<float>(ReadPCDIntegerScalar<std::int64_t>(ptr));
                return std::nullopt;
            case 'U':
                if (field.Size == 1) return static_cast<float>(ReadPCDIntegerScalar<std::uint8_t>(ptr));
                if (field.Size == 2) return static_cast<float>(ReadPCDIntegerScalar<std::uint16_t>(ptr));
                if (field.Size == 4) return static_cast<float>(ReadPCDIntegerScalar<std::uint32_t>(ptr));
                if (field.Size == 8) return static_cast<float>(ReadPCDIntegerScalar<std::uint64_t>(ptr));
                return std::nullopt;
            default:
                return std::nullopt;
            }
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
        std::string normalizedLine;
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

            if (XYZNeedsDelimiterNormalization(line))
            {
                XYZNormalizeDelimitedLine(line, normalizedLine);
                line = Trim(std::string_view(normalizedLine));
                if (line.empty())
                {
                    continue;
                }
            }

            const auto tokens = SplitWhitespace(line);
            if (tokens.empty())
            {
                continue;
            }
            if (IsXYZScanLineMarker(tokens))
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
                continue;
            }
            const auto x = ParseNumber<float>(tokens[0]);
            const auto y = ParseNumber<float>(tokens[1]);
            const auto z = ParseNumber<float>(tokens[2]);
            if (!x || !y || !z)
            {
                continue;
            }

            const std::optional<glm::vec4> color = ParseXYZPointColor(tokens);

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

        std::size_t cursor = 0;
        const auto headerOpt = ParsePCDHeader(*text, cursor);
        if (!headerOpt)
        {
            return InvalidPointCloudFormat();
        }
        const PcdHeader& header = *headerOpt;

        const auto* xField = FindPCDField(header.Fields, "x");
        const auto* yField = FindPCDField(header.Fields, "y");
        const auto* zField = FindPCDField(header.Fields, "z");
        if (xField == nullptr || yField == nullptr || zField == nullptr)
        {
            return InvalidPointCloudFormat();
        }
        const auto* nxField = FindPCDField(header.Fields, "normal_x");
        const auto* nyField = FindPCDField(header.Fields, "normal_y");
        const auto* nzField = FindPCDField(header.Fields, "normal_z");
        const auto* rField = FindPCDField(header.Fields, "r");
        const auto* gField = FindPCDField(header.Fields, "g");
        const auto* bField = FindPCDField(header.Fields, "b");
        const bool hasNormals = nxField && nyField && nzField;
        const bool hasColors = rField && gField && bField;

        PointCloudIOResult result;
        ApplyPathInfo(result, absolute_path);
        if (header.Points > 0)
        {
            result.Cloud.Reserve(header.Points);
        }
        if (hasNormals)
        {
            result.Cloud.EnableNormals();
        }
        if (hasColors)
        {
            result.Cloud.EnableColors(glm::vec4(1.0f));
        }

        if (header.DataEncoding == "ascii")
        {
            std::string_view line;
            while (NextLine(*text, cursor, line))
            {
                if (line.empty() || line.front() == '#')
                {
                    continue;
                }
                const auto tokens = SplitWhitespace(line);
                if (tokens.size() < header.ScalarValueCount)
                {
                    return InvalidPointCloudFormat();
                }
                const auto x = ParseNumber<float>(tokens[xField->ScalarOffset]);
                const auto y = ParseNumber<float>(tokens[yField->ScalarOffset]);
                const auto z = ParseNumber<float>(tokens[zField->ScalarOffset]);
                if (!x || !y || !z)
                {
                    return InvalidPointCloudFormat();
                }
                const auto point = result.Cloud.AddPoint(glm::vec3(*x, *y, *z));

                if (hasNormals)
                {
                    const auto nx = ParseNumber<float>(tokens[nxField->ScalarOffset]);
                    const auto ny = ParseNumber<float>(tokens[nyField->ScalarOffset]);
                    const auto nz = ParseNumber<float>(tokens[nzField->ScalarOffset]);
                    if (!nx || !ny || !nz)
                    {
                        return InvalidPointCloudFormat();
                    }
                    result.Cloud.Normal(point) = glm::vec3(*nx, *ny, *nz);
                }
                if (hasColors)
                {
                    const auto r = ParseNumber<float>(tokens[rField->ScalarOffset]);
                    const auto g = ParseNumber<float>(tokens[gField->ScalarOffset]);
                    const auto b = ParseNumber<float>(tokens[bField->ScalarOffset]);
                    if (!r || !g || !b)
                    {
                        return InvalidPointCloudFormat();
                    }
                    result.Cloud.Color(point) = glm::vec4(
                        NormalizeColorChannel(*r),
                        NormalizeColorChannel(*g),
                        NormalizeColorChannel(*b),
                        1.0f);
                }

                if (header.Points > 0 && result.Cloud.VerticesSize() >= header.Points)
                {
                    break;
                }
            }
        }
        else if (header.DataEncoding == "binary")
        {
            if (header.PointStride == 0)
            {
                return InvalidPointCloudFormat();
            }
            if (cursor > text->size())
            {
                return InvalidPointCloudFormat();
            }
            const std::span<const std::byte> body(
                reinterpret_cast<const std::byte*>(text->data() + cursor),
                text->size() - cursor);

            std::size_t pointCount = header.Points;
            if (pointCount == 0)
            {
                if (header.PointStride == 0 || body.size() % header.PointStride != 0)
                {
                    return InvalidPointCloudFormat();
                }
                pointCount = body.size() / header.PointStride;
            }
            if (pointCount == 0)
            {
                return InvalidPointCloudFormat();
            }

            const std::size_t requiredBytes = pointCount * header.PointStride;
            if (body.size() < requiredBytes)
            {
                return InvalidPointCloudFormat();
            }

            for (std::size_t row = 0; row < pointCount; ++row)
            {
                const std::span<const std::byte> pointBytes =
                    body.subspan(row * header.PointStride, header.PointStride);

                const auto x = ReadPCDBinaryScalar(pointBytes, *xField);
                const auto y = ReadPCDBinaryScalar(pointBytes, *yField);
                const auto z = ReadPCDBinaryScalar(pointBytes, *zField);
                if (!x || !y || !z)
                {
                    return InvalidPointCloudFormat();
                }
                const auto point = result.Cloud.AddPoint(glm::vec3(*x, *y, *z));

                if (hasNormals)
                {
                    const auto nx = ReadPCDBinaryScalar(pointBytes, *nxField);
                    const auto ny = ReadPCDBinaryScalar(pointBytes, *nyField);
                    const auto nz = ReadPCDBinaryScalar(pointBytes, *nzField);
                    if (!nx || !ny || !nz)
                    {
                        return InvalidPointCloudFormat();
                    }
                    result.Cloud.Normal(point) = glm::vec3(*nx, *ny, *nz);
                }
                if (hasColors)
                {
                    const auto r = ReadPCDBinaryScalar(pointBytes, *rField);
                    const auto g = ReadPCDBinaryScalar(pointBytes, *gField);
                    const auto b = ReadPCDBinaryScalar(pointBytes, *bField);
                    if (!r || !g || !b)
                    {
                        return InvalidPointCloudFormat();
                    }
                    result.Cloud.Color(point) = glm::vec4(
                        NormalizeColorChannel(*r),
                        NormalizeColorChannel(*g),
                        NormalizeColorChannel(*b),
                        1.0f);
                }
            }
        }
        else
        {
            return InvalidPointCloudFormat();
        }

        if (result.Cloud.IsEmpty() || (header.Points > 0 && result.Cloud.VerticesSize() != header.Points))
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

    PointCloudIOWriteStatus WritePLY(std::string_view absolute_path, const PointCloudIOResult& cloud)
    {
        if (absolute_path.empty())
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        const auto& source = cloud.Cloud;
        if (source.IsEmpty())
        {
            return PointCloudIOWriteStatus::EmptyCloud;
        }

        const auto positions = source.Positions();
        const std::size_t pointCount = positions.size();

        const bool hasNormals = source.HasNormals() && source.Normals().size() == pointCount;
        const bool hasColors = source.HasColors() && source.Colors().size() == pointCount;
        const bool hasRadii = source.HasRadii() && source.Radii().size() == pointCount;

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        char buffer[256];

        stream << "ply\n";
        stream << "format ascii 1.0\n";
        stream << "comment Exported by IntrinsicEngine\n";
        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "element vertex %zu\n",
                                              pointCount);
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }
        stream << "property float x\n";
        stream << "property float y\n";
        stream << "property float z\n";
        if (hasNormals)
        {
            stream << "property float nx\n";
            stream << "property float ny\n";
            stream << "property float nz\n";
        }
        if (hasColors)
        {
            stream << "property uchar red\n";
            stream << "property uchar green\n";
            stream << "property uchar blue\n";
        }
        if (hasRadii)
        {
            stream << "property float radius\n";
        }
        stream << "end_header\n";

        const auto normals = hasNormals ? source.Normals() : std::span<const glm::vec3>{};
        const auto colors = hasColors ? source.Colors() : std::span<const glm::vec4>{};
        const auto radii = hasRadii ? source.Radii() : std::span<const float>{};

        auto encodeColorChannel = [](float channel) -> unsigned int {
            const float clamped = channel < 0.0f ? 0.0f : (channel > 1.0f ? 1.0f : channel);
            const float scaled = clamped * 255.0f + 0.5f;
            const unsigned int rounded = static_cast<unsigned int>(scaled);
            return rounded > 255u ? 255u : rounded;
        };

        for (std::size_t i = 0; i < pointCount; ++i)
        {
            const auto& p = positions[i];
            int written = std::snprintf(buffer, sizeof(buffer),
                                        "%.6f %.6f %.6f",
                                        static_cast<double>(p.x),
                                        static_cast<double>(p.y),
                                        static_cast<double>(p.z));
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);

            if (hasNormals)
            {
                const auto& n = normals[i];
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %.6f %.6f %.6f",
                                        static_cast<double>(n.x),
                                        static_cast<double>(n.y),
                                        static_cast<double>(n.z));
                if (written <= 0)
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            if (hasColors)
            {
                const auto& c = colors[i];
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %u %u %u",
                                        encodeColorChannel(c.r),
                                        encodeColorChannel(c.g),
                                        encodeColorChannel(c.b));
                if (written <= 0)
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            if (hasRadii)
            {
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %.6f",
                                        static_cast<double>(radii[i]));
                if (written <= 0)
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            stream.put('\n');
        }

        stream.flush();
        if (!stream.good())
        {
            return PointCloudIOWriteStatus::FileWriteError;
        }
        return PointCloudIOWriteStatus::Success;
    }

    PointCloudIOWriteStatus WritePLYBinary(std::string_view absolute_path, const PointCloudIOResult& cloud)
    {
        if (absolute_path.empty())
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        const auto& source = cloud.Cloud;
        if (source.IsEmpty())
        {
            return PointCloudIOWriteStatus::EmptyCloud;
        }

        const auto positions = source.Positions();
        const std::size_t pointCount = positions.size();

        const bool hasNormals = source.HasNormals() && source.Normals().size() == pointCount;
        const bool hasColors = source.HasColors() && source.Colors().size() == pointCount;
        const bool hasRadii = source.HasRadii() && source.Radii().size() == pointCount;

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        char headerBuffer[256];

        stream << "ply\n";
        stream << "format binary_little_endian 1.0\n";
        stream << "comment Exported by IntrinsicEngine\n";
        {
            const int written = std::snprintf(headerBuffer, sizeof(headerBuffer),
                                              "element vertex %zu\n",
                                              pointCount);
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(headerBuffer, written);
        }
        stream << "property float x\n";
        stream << "property float y\n";
        stream << "property float z\n";
        if (hasNormals)
        {
            stream << "property float nx\n";
            stream << "property float ny\n";
            stream << "property float nz\n";
        }
        if (hasColors)
        {
            stream << "property uchar red\n";
            stream << "property uchar green\n";
            stream << "property uchar blue\n";
        }
        if (hasRadii)
        {
            stream << "property float radius\n";
        }
        stream << "end_header\n";

        const auto normals = hasNormals ? source.Normals() : std::span<const glm::vec3>{};
        const auto colors = hasColors ? source.Colors() : std::span<const glm::vec4>{};
        const auto radii = hasRadii ? source.Radii() : std::span<const float>{};

        auto encodeColorChannel = [](float channel) -> std::uint8_t {
            const float clamped = channel < 0.0f ? 0.0f : (channel > 1.0f ? 1.0f : channel);
            const float scaled = clamped * 255.0f + 0.5f;
            const unsigned int rounded = static_cast<unsigned int>(scaled);
            return static_cast<std::uint8_t>(rounded > 255u ? 255u : rounded);
        };

        constexpr bool hostBigEndian = std::endian::native == std::endian::big;

        auto writeFloatLE = [&](float value) -> bool {
            std::array<std::byte, 4> bytes{};
            std::memcpy(bytes.data(), &value, 4);
            if constexpr (hostBigEndian)
            {
                std::swap(bytes[0], bytes[3]);
                std::swap(bytes[1], bytes[2]);
            }
            stream.write(reinterpret_cast<const char*>(bytes.data()), 4);
            return stream.good();
        };

        auto writeUInt8 = [&](std::uint8_t value) -> bool {
            stream.write(reinterpret_cast<const char*>(&value), 1);
            return stream.good();
        };

        for (std::size_t i = 0; i < pointCount; ++i)
        {
            const auto& p = positions[i];
            if (!writeFloatLE(p.x) || !writeFloatLE(p.y) || !writeFloatLE(p.z))
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }

            if (hasNormals)
            {
                const auto& n = normals[i];
                if (!writeFloatLE(n.x) || !writeFloatLE(n.y) || !writeFloatLE(n.z))
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
            }

            if (hasColors)
            {
                const auto& c = colors[i];
                if (!writeUInt8(encodeColorChannel(c.r)) ||
                    !writeUInt8(encodeColorChannel(c.g)) ||
                    !writeUInt8(encodeColorChannel(c.b)))
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
            }

            if (hasRadii)
            {
                if (!writeFloatLE(radii[i]))
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
            }
        }

        stream.flush();
        if (!stream.good())
        {
            return PointCloudIOWriteStatus::FileWriteError;
        }
        return PointCloudIOWriteStatus::Success;
    }

    PointCloudIOWriteStatus WriteXYZ(std::string_view absolute_path, const PointCloudIOResult& cloud)
    {
        if (absolute_path.empty())
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        const auto& source = cloud.Cloud;
        if (source.IsEmpty())
        {
            return PointCloudIOWriteStatus::EmptyCloud;
        }

        const auto positions = source.Positions();
        const std::size_t pointCount = positions.size();

        const bool hasColors = source.HasColors() && source.Colors().size() == pointCount;

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        const auto colors = hasColors ? source.Colors() : std::span<const glm::vec4>{};

        char buffer[256];
        for (std::size_t i = 0; i < pointCount; ++i)
        {
            const auto& p = positions[i];
            int written = 0;
            if (hasColors)
            {
                const auto& c = colors[i];
                const float r = c.r < 0.0f ? 0.0f : (c.r > 1.0f ? 1.0f : c.r);
                const float g = c.g < 0.0f ? 0.0f : (c.g > 1.0f ? 1.0f : c.g);
                const float b = c.b < 0.0f ? 0.0f : (c.b > 1.0f ? 1.0f : c.b);
                written = std::snprintf(buffer, sizeof(buffer),
                                        "%.6f %.6f %.6f %.6f %.6f %.6f\n",
                                        static_cast<double>(p.x),
                                        static_cast<double>(p.y),
                                        static_cast<double>(p.z),
                                        static_cast<double>(r),
                                        static_cast<double>(g),
                                        static_cast<double>(b));
            }
            else
            {
                written = std::snprintf(buffer, sizeof(buffer),
                                        "%.6f %.6f %.6f\n",
                                        static_cast<double>(p.x),
                                        static_cast<double>(p.y),
                                        static_cast<double>(p.z));
            }
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }

        stream.flush();
        if (!stream.good())
        {
            return PointCloudIOWriteStatus::FileWriteError;
        }
        return PointCloudIOWriteStatus::Success;
    }

    PointCloudIOWriteStatus WritePCD(std::string_view absolute_path, const PointCloudIOResult& cloud)
    {
        if (absolute_path.empty())
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        const auto& source = cloud.Cloud;
        if (source.IsEmpty())
        {
            return PointCloudIOWriteStatus::EmptyCloud;
        }

        const auto positions = source.Positions();
        const std::size_t pointCount = positions.size();

        const bool hasNormals = source.HasNormals() && source.Normals().size() == pointCount;
        const bool hasColors = source.HasColors() && source.Colors().size() == pointCount;

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        char buffer[256];

        stream << "# .PCD v0.7\n";
        stream << "VERSION 0.7\n";

        stream << "FIELDS x y z";
        if (hasNormals)
        {
            stream << " normal_x normal_y normal_z";
        }
        if (hasColors)
        {
            stream << " r g b";
        }
        stream.put('\n');

        stream << "SIZE 4 4 4";
        if (hasNormals)
        {
            stream << " 4 4 4";
        }
        if (hasColors)
        {
            stream << " 4 4 4";
        }
        stream.put('\n');

        stream << "TYPE F F F";
        if (hasNormals)
        {
            stream << " F F F";
        }
        if (hasColors)
        {
            stream << " F F F";
        }
        stream.put('\n');

        stream << "COUNT 1 1 1";
        if (hasNormals)
        {
            stream << " 1 1 1";
        }
        if (hasColors)
        {
            stream << " 1 1 1";
        }
        stream.put('\n');

        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "WIDTH %zu\n",
                                              pointCount);
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }
        stream << "HEIGHT 1\n";
        stream << "VIEWPOINT 0 0 0 1 0 0 0\n";
        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "POINTS %zu\n",
                                              pointCount);
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }
        stream << "DATA ascii\n";

        const auto normals = hasNormals ? source.Normals() : std::span<const glm::vec3>{};
        const auto colors = hasColors ? source.Colors() : std::span<const glm::vec4>{};

        auto clampUnit = [](float channel) -> float {
            return channel < 0.0f ? 0.0f : (channel > 1.0f ? 1.0f : channel);
        };

        for (std::size_t i = 0; i < pointCount; ++i)
        {
            const auto& p = positions[i];
            int written = std::snprintf(buffer, sizeof(buffer),
                                        "%.6f %.6f %.6f",
                                        static_cast<double>(p.x),
                                        static_cast<double>(p.y),
                                        static_cast<double>(p.z));
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);

            if (hasNormals)
            {
                const auto& n = normals[i];
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %.6f %.6f %.6f",
                                        static_cast<double>(n.x),
                                        static_cast<double>(n.y),
                                        static_cast<double>(n.z));
                if (written <= 0)
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            if (hasColors)
            {
                const auto& c = colors[i];
                written = std::snprintf(buffer, sizeof(buffer),
                                        " %.6f %.6f %.6f",
                                        static_cast<double>(clampUnit(c.r)),
                                        static_cast<double>(clampUnit(c.g)),
                                        static_cast<double>(clampUnit(c.b)));
                if (written <= 0)
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            stream.put('\n');
        }

        stream.flush();
        if (!stream.good())
        {
            return PointCloudIOWriteStatus::FileWriteError;
        }
        return PointCloudIOWriteStatus::Success;
    }

    PointCloudIOWriteStatus WritePCDBinary(std::string_view absolute_path, const PointCloudIOResult& cloud)
    {
        if (absolute_path.empty())
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        const auto& source = cloud.Cloud;
        if (source.IsEmpty())
        {
            return PointCloudIOWriteStatus::EmptyCloud;
        }

        const auto positions = source.Positions();
        const std::size_t pointCount = positions.size();

        const bool hasNormals = source.HasNormals() && source.Normals().size() == pointCount;
        const bool hasColors = source.HasColors() && source.Colors().size() == pointCount;

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return PointCloudIOWriteStatus::InvalidPath;
        }

        char buffer[256];

        stream << "# .PCD v0.7\n";
        stream << "VERSION 0.7\n";

        stream << "FIELDS x y z";
        if (hasNormals)
        {
            stream << " normal_x normal_y normal_z";
        }
        if (hasColors)
        {
            stream << " r g b";
        }
        stream.put('\n');

        stream << "SIZE 4 4 4";
        if (hasNormals)
        {
            stream << " 4 4 4";
        }
        if (hasColors)
        {
            stream << " 4 4 4";
        }
        stream.put('\n');

        stream << "TYPE F F F";
        if (hasNormals)
        {
            stream << " F F F";
        }
        if (hasColors)
        {
            stream << " F F F";
        }
        stream.put('\n');

        stream << "COUNT 1 1 1";
        if (hasNormals)
        {
            stream << " 1 1 1";
        }
        if (hasColors)
        {
            stream << " 1 1 1";
        }
        stream.put('\n');

        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "WIDTH %zu\n",
                                              pointCount);
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }
        stream << "HEIGHT 1\n";
        stream << "VIEWPOINT 0 0 0 1 0 0 0\n";
        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "POINTS %zu\n",
                                              pointCount);
            if (written <= 0)
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }
        stream << "DATA binary\n";

        const auto normals = hasNormals ? source.Normals() : std::span<const glm::vec3>{};
        const auto colors = hasColors ? source.Colors() : std::span<const glm::vec4>{};

        auto clampUnit = [](float channel) -> float {
            return channel < 0.0f ? 0.0f : (channel > 1.0f ? 1.0f : channel);
        };

        constexpr bool hostBigEndian = std::endian::native == std::endian::big;

        auto writeFloatLE = [&](float value) -> bool {
            std::array<std::byte, 4> bytes{};
            std::memcpy(bytes.data(), &value, 4);
            if constexpr (hostBigEndian)
            {
                std::swap(bytes[0], bytes[3]);
                std::swap(bytes[1], bytes[2]);
            }
            stream.write(reinterpret_cast<const char*>(bytes.data()), 4);
            return stream.good();
        };

        for (std::size_t i = 0; i < pointCount; ++i)
        {
            const auto& p = positions[i];
            if (!writeFloatLE(p.x) || !writeFloatLE(p.y) || !writeFloatLE(p.z))
            {
                return PointCloudIOWriteStatus::FileWriteError;
            }

            if (hasNormals)
            {
                const auto& n = normals[i];
                if (!writeFloatLE(n.x) || !writeFloatLE(n.y) || !writeFloatLE(n.z))
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
            }

            if (hasColors)
            {
                const auto& c = colors[i];
                if (!writeFloatLE(clampUnit(c.r)) ||
                    !writeFloatLE(clampUnit(c.g)) ||
                    !writeFloatLE(clampUnit(c.b)))
                {
                    return PointCloudIOWriteStatus::FileWriteError;
                }
            }
        }

        stream.flush();
        if (!stream.good())
        {
            return PointCloudIOWriteStatus::FileWriteError;
        }
        return PointCloudIOWriteStatus::Success;
    }
}


