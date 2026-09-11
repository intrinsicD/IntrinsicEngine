// Shared path, text parsing and PLY scalar decoding for geometry IO implementations.
#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
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

    [[nodiscard]] inline std::optional<PlyScalar> ParsePlyScalarType(std::string_view token)
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

    [[nodiscard]] inline bool IsPlyFloatingScalar(PlyScalar scalar)
    {
        return scalar == PlyScalar::Float32 || scalar == PlyScalar::Float64;
    }

    inline void ByteSwap(std::byte* p, std::size_t n);

    [[nodiscard]] inline float ReadFloatingScalarAt(const std::byte* base,
                                             std::size_t offset,
                                             PlyScalar scalar,
                                             bool bigEndian)
    {
        std::array<std::byte, 8> tmp{};
        const std::size_t byteCount = PlyScalarBytes(scalar);
        std::memcpy(tmp.data(), base + offset, byteCount);
        if (bigEndian)
        {
            ByteSwap(tmp.data(), byteCount);
        }

        if (scalar == PlyScalar::Float64)
        {
            double value = 0.0;
            std::memcpy(&value, tmp.data(), 8);
            return static_cast<float>(value);
        }

        float value = 0.0f;
        std::memcpy(&value, tmp.data(), 4);
        return value;
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

    inline void ByteSwap(std::byte* p, std::size_t n)
    {
        for (std::size_t i = 0; i < n / 2; ++i)
        {
            const std::byte tmp = p[i];
            p[i] = p[n - 1 - i];
            p[n - 1 - i] = tmp;
        }
    }

    // The caller validates the complete scalar is available before decoding.
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

}
