module;

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <sstream>

#include <glm/glm.hpp>

module Geometry.PointCloudIO;

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

        bool ascii = false;
        bool inVertexElement = false;
        std::size_t vertexCount = 0;
        std::vector<std::string> vertexProperties;
        while (NextLine(*text, cursor, line))
        {
            if (line == "end_header")
            {
                break;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.size() >= 3 && tokens[0] == "format" && tokens[1] == "ascii")
            {
                ascii = true;
            }
            else if (tokens.size() >= 3 && tokens[0] == "element")
            {
                inVertexElement = tokens[1] == "vertex";
                if (inVertexElement)
                {
                    if (const auto parsed = ParseNumber<std::size_t>(tokens[2]))
                    {
                        vertexCount = *parsed;
                    }
                }
            }
            else if (inVertexElement && tokens.size() >= 3 && tokens[0] == "property")
            {
                vertexProperties.emplace_back(tokens.back());
            }
        }

        if (!ascii || vertexCount == 0 || vertexProperties.empty())
        {
            return InvalidPointCloudFormat();
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
        result.Cloud.Reserve(vertexCount);
        if (hasNormals)
        {
            result.Cloud.EnableNormals();
        }
        if (hasColors)
        {
            result.Cloud.EnableColors(glm::vec4(1.0f));
        }

        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            if (!NextLine(*text, cursor, line))
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
}


