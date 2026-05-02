#pragma once

#include <charconv>
#include <cstddef>
#include <expected>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Geometry::IOText
{
    enum class TextFileError
    {
        FileNotFound,
        FileReadError,
    };

    struct PathInfo
    {
        std::string SourcePath;
        std::string BasePath;
    };

    [[nodiscard]] inline PathInfo MakePathInfo(std::string_view path)
    {
        PathInfo info{std::string(path), {}};
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string_view::npos)
        {
            info.BasePath.assign(path.substr(0, slash + 1));
        }
        return info;
    }

    [[nodiscard]] inline std::expected<std::string, TextFileError> ReadTextFile(std::string_view path)
    {
        std::ifstream file(std::string(path), std::ios::binary);
        if (!file)
        {
            return std::unexpected(TextFileError::FileNotFound);
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (!file.good() && !file.eof())
        {
            return std::unexpected(TextFileError::FileReadError);
        }
        return buffer.str();
    }

    [[nodiscard]] inline std::string_view Trim(std::string_view text)
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

    [[nodiscard]] inline bool NextLine(std::string_view text, std::size_t& cursor, std::string_view& line)
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

    [[nodiscard]] inline std::vector<std::string_view> SplitWhitespace(std::string_view line)
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
}

