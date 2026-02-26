#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphics::Importers::TextParse
{
    inline std::string_view Trim(std::string_view line) noexcept
    {
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string_view::npos)
            return {};

        line.remove_prefix(first);
        const size_t last = line.find_last_not_of(" \t\r");
        if (last != std::string_view::npos)
            line = line.substr(0, last + 1);
        return line;
    }

    inline bool NextLine(std::string_view text, size_t& cursor, std::string_view& line) noexcept
    {
        if (cursor >= text.size())
            return false;

        const size_t begin = cursor;
        const size_t end = text.find('\n', begin);
        if (end == std::string_view::npos)
        {
            line = text.substr(begin);
            cursor = text.size();
        }
        else
        {
            line = text.substr(begin, end - begin);
            cursor = end + 1;
        }

        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        return true;
    }

    inline void SplitWhitespace(std::string_view line, std::vector<std::string_view>& tokens)
    {
        tokens.clear();
        size_t cursor = 0;
        while (cursor < line.size())
        {
            const size_t first = line.find_first_not_of(" \t", cursor);
            if (first == std::string_view::npos)
                break;

            const size_t end = line.find_first_of(" \t", first);
            if (end == std::string_view::npos)
            {
                tokens.emplace_back(line.substr(first));
                break;
            }

            tokens.emplace_back(line.substr(first, end - first));
            cursor = end + 1;
        }
    }

    template <typename T>
    inline std::optional<T> ParseNumber(std::string_view token) noexcept
    {
        T value{};
        const auto* begin = token.data();
        const auto* end = token.data() + token.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc{} || ptr != end)
            return std::nullopt;
        return value;
    }
}
