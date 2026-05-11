module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

#include "Geometry.IOText.hpp"

module Geometry.HalfedgeMesh.IO;

import Geometry.Properties;
import Core.Error;

namespace Geometry::MeshIO
{
    namespace
    {
        using Geometry::IOText::MakePathInfo;
        using Geometry::IOText::NextLine;
        using Geometry::IOText::ParseNumber;
        using Geometry::IOText::ReadTextFile;
        using Geometry::IOText::SplitWhitespace;
        using Geometry::IOText::TextFileError;
        using Geometry::IOText::Trim;

        [[nodiscard]] Core::ErrorCode ToCoreError(TextFileError error)
        {
            switch (error)
            {
            case TextFileError::FileNotFound:
                return Core::ErrorCode::FileNotFound;
            case TextFileError::FileReadError:
                return Core::ErrorCode::FileReadError;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] std::optional<std::uint32_t> ParseOBJVertexIndex(std::string_view token, std::size_t vertexCount)
        {
            const std::size_t slash = token.find('/');
            if (slash != std::string_view::npos)
            {
                token = token.substr(0, slash);
            }
            const auto index = ParseNumber<int>(token);
            if (!index || *index == 0)
            {
                return std::nullopt;
            }

            const int resolved = *index > 0 ? *index - 1 : static_cast<int>(vertexCount) + *index;
            if (resolved < 0 || static_cast<std::size_t>(resolved) >= vertexCount)
            {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(resolved);
        }

        void PopulateResult(MeshIOResult& result,
                            std::span<const glm::vec3> vertices,
                            std::span<const std::vector<std::uint32_t>> faces,
                            std::span<const glm::vec3> normals = {},
                            std::span<const glm::vec4> colors = {})
        {
            result.Vertices.Resize(vertices.size());
            auto positions = result.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3(0.0f));
            for (std::size_t i = 0; i < vertices.size(); ++i)
            {
                positions[i] = vertices[i];
            }

            if (!normals.empty() && normals.size() == vertices.size())
            {
                auto normalProperty = result.Vertices.GetOrAdd<glm::vec3>("v:normal", glm::vec3(0.0f, 1.0f, 0.0f));
                for (std::size_t i = 0; i < normals.size(); ++i)
                {
                    normalProperty[i] = normals[i];
                }
            }

            if (!colors.empty() && colors.size() == vertices.size())
            {
                auto colorProperty = result.Vertices.GetOrAdd<glm::vec4>("v:color", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                for (std::size_t i = 0; i < colors.size(); ++i)
                {
                    colorProperty[i] = colors[i];
                }
            }

            result.Faces.Resize(faces.size());
            auto faceVertices = result.Faces.GetOrAdd<std::vector<std::uint32_t>>("f:vertices", {});
            for (std::size_t i = 0; i < faces.size(); ++i)
            {
                faceVertices[i] = faces[i];
            }
        }

        [[nodiscard]] Core::Expected<MeshIOResult> InvalidMeshFormat()
        {
            return Core::Err<MeshIOResult>(Core::ErrorCode::InvalidFormat);
        }

        enum class OFFVariant
        {
            Standard,
            COFF,
            NOFF,
            CNOFF,
        };

        [[nodiscard]] std::optional<OFFVariant> ParseOFFMagic(std::string_view line)
        {
            if (line == "OFF") return OFFVariant::Standard;
            if (line == "COFF") return OFFVariant::COFF;
            if (line == "NOFF") return OFFVariant::NOFF;
            if (line == "CNOFF") return OFFVariant::CNOFF;
            return std::nullopt;
        }

        [[nodiscard]] float NormalizeOFFColorChannel(float value)
        {
            return value > 1.0f ? std::clamp(value / 255.0f, 0.0f, 1.0f) : std::clamp(value, 0.0f, 1.0f);
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

        [[nodiscard]] Core::Expected<MeshIOResult> ParseAsciiPLY(std::string_view text,
                                                                std::size_t cursor,
                                                                const std::vector<PlyElement>& elements,
                                                                std::string_view absolute_path)
        {
            std::size_t vertexCount = 0;
            std::size_t faceCount = 0;
            for (const auto& el : elements)
            {
                if (el.Name == "vertex") vertexCount = el.Count;
                else if (el.Name == "face") faceCount = el.Count;
            }
            if (vertexCount == 0 || faceCount == 0)
            {
                return InvalidMeshFormat();
            }

            std::vector<glm::vec3> vertices;
            vertices.reserve(vertexCount);
            std::string_view line;
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                if (!NextLine(text, cursor, line))
                {
                    return InvalidMeshFormat();
                }
                const auto tokens = SplitWhitespace(line);
                if (tokens.size() < 3)
                {
                    return InvalidMeshFormat();
                }
                const auto x = ParseNumber<float>(tokens[0]);
                const auto y = ParseNumber<float>(tokens[1]);
                const auto z = ParseNumber<float>(tokens[2]);
                if (!x || !y || !z)
                {
                    return InvalidMeshFormat();
                }
                vertices.emplace_back(*x, *y, *z);
            }

            std::vector<std::vector<std::uint32_t>> faces;
            faces.reserve(faceCount);
            for (std::size_t i = 0; i < faceCount; ++i)
            {
                if (!NextLine(text, cursor, line))
                {
                    return InvalidMeshFormat();
                }
                const auto tokens = SplitWhitespace(line);
                if (tokens.empty())
                {
                    return InvalidMeshFormat();
                }
                const auto count = ParseNumber<std::size_t>(tokens[0]);
                if (!count || *count < 3 || tokens.size() < *count + 1)
                {
                    return InvalidMeshFormat();
                }
                std::vector<std::uint32_t> face;
                face.reserve(*count);
                for (std::size_t j = 0; j < *count; ++j)
                {
                    const auto index = ParseNumber<std::size_t>(tokens[j + 1]);
                    if (!index || *index >= vertices.size())
                    {
                        return InvalidMeshFormat();
                    }
                    face.push_back(static_cast<std::uint32_t>(*index));
                }
                faces.push_back(std::move(face));
            }

            MeshIOResult result;
            const auto pathInfo = MakePathInfo(absolute_path);
            result.SourcePath = pathInfo.SourcePath;
            result.BasePath = pathInfo.BasePath;
            PopulateResult(result, vertices, faces);
            return result;
        }

        [[nodiscard]] Core::Expected<MeshIOResult> ParseBinaryPLY(std::span<const std::byte> body,
                                                                 const std::vector<PlyElement>& elements,
                                                                 bool bigEndian,
                                                                 std::string_view absolute_path)
        {
            const PlyElement* vertexElement = nullptr;
            const PlyElement* faceElement = nullptr;
            for (const auto& el : elements)
            {
                if (el.Name == "vertex" && vertexElement == nullptr)
                {
                    vertexElement = &el;
                }
                else if (el.Name == "face" && faceElement == nullptr)
                {
                    faceElement = &el;
                }
            }
            if (vertexElement == nullptr || vertexElement->Count == 0)
            {
                return InvalidMeshFormat();
            }
            if (faceElement == nullptr || faceElement->Count == 0)
            {
                return InvalidMeshFormat();
            }

            int xIndex = -1;
            int yIndex = -1;
            int zIndex = -1;
            std::size_t vertexStride = 0;
            for (std::size_t i = 0; i < vertexElement->Properties.size(); ++i)
            {
                const auto& p = vertexElement->Properties[i];
                if (p.IsList)
                {
                    return InvalidMeshFormat();
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
                vertexStride += PlyScalarBytes(p.ScalarType);
            }
            if (xIndex < 0 || yIndex < 0 || zIndex < 0)
            {
                return InvalidMeshFormat();
            }

            int faceListIndex = -1;
            for (std::size_t i = 0; i < faceElement->Properties.size(); ++i)
            {
                const auto& p = faceElement->Properties[i];
                if (p.IsList && (p.Name == "vertex_indices" || p.Name == "vertex_index"))
                {
                    faceListIndex = static_cast<int>(i);
                    break;
                }
            }
            if (faceListIndex < 0)
            {
                return InvalidMeshFormat();
            }

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

            std::vector<glm::vec3> vertices;
            vertices.reserve(vertexElement->Count);
            std::vector<std::vector<std::uint32_t>> faces;
            faces.reserve(faceElement->Count);

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

            for (const PlyElement& element : elements)
            {
                if (&element == vertexElement)
                {
                    const std::size_t total = element.Count * vertexStride;
                    if (static_cast<std::size_t>(end - cursor) < total)
                    {
                        return InvalidMeshFormat();
                    }
                    for (std::size_t row = 0; row < element.Count; ++row)
                    {
                        const std::byte* base = cursor + row * vertexStride;
                        vertices.emplace_back(readFloat(base, vertexOffsets[xIndex]),
                                              readFloat(base, vertexOffsets[yIndex]),
                                              readFloat(base, vertexOffsets[zIndex]));
                    }
                    cursor += total;
                }
                else if (&element == faceElement)
                {
                    for (std::size_t row = 0; row < element.Count; ++row)
                    {
                        std::vector<std::uint32_t> face;
                        for (std::size_t i = 0; i < element.Properties.size(); ++i)
                        {
                            const auto& prop = element.Properties[i];
                            if (!prop.IsList)
                            {
                                const std::size_t n = PlyScalarBytes(prop.ScalarType);
                                if (static_cast<std::size_t>(end - cursor) < n)
                                {
                                    return InvalidMeshFormat();
                                }
                                cursor += n;
                                continue;
                            }

                            const std::size_t countBytes = PlyScalarBytes(prop.ListCountType);
                            if (static_cast<std::size_t>(end - cursor) < countBytes)
                            {
                                return InvalidMeshFormat();
                            }
                            const auto count = ReadScalarAs<std::uint64_t>(cursor, prop.ListCountType, bigEndian);

                            const std::size_t elemBytes = PlyScalarBytes(prop.ScalarType);
                            const std::size_t totalBytes = static_cast<std::size_t>(count) * elemBytes;
                            if (static_cast<std::size_t>(end - cursor) < totalBytes)
                            {
                                return InvalidMeshFormat();
                            }

                            if (static_cast<int>(i) == faceListIndex)
                            {
                                if (count < 3)
                                {
                                    return InvalidMeshFormat();
                                }
                                face.reserve(static_cast<std::size_t>(count));
                                for (std::uint64_t j = 0; j < count; ++j)
                                {
                                    const auto idx = ReadScalarAs<std::uint64_t>(cursor, prop.ScalarType, bigEndian);
                                    if (idx >= vertexElement->Count)
                                    {
                                        return InvalidMeshFormat();
                                    }
                                    face.push_back(static_cast<std::uint32_t>(idx));
                                }
                            }
                            else
                            {
                                cursor += totalBytes;
                            }
                        }
                        if (face.empty())
                        {
                            return InvalidMeshFormat();
                        }
                        faces.push_back(std::move(face));
                    }
                }
                else
                {
                    std::size_t scalarStride = 0;
                    bool hasList = false;
                    for (const auto& p : element.Properties)
                    {
                        if (p.IsList)
                        {
                            hasList = true;
                            break;
                        }
                        scalarStride += PlyScalarBytes(p.ScalarType);
                    }
                    if (hasList)
                    {
                        return InvalidMeshFormat();
                    }
                    const std::size_t total = element.Count * scalarStride;
                    if (static_cast<std::size_t>(end - cursor) < total)
                    {
                        return InvalidMeshFormat();
                    }
                    cursor += total;
                }
            }

            MeshIOResult result;
            const auto pathInfo = MakePathInfo(absolute_path);
            result.SourcePath = pathInfo.SourcePath;
            result.BasePath = pathInfo.BasePath;
            PopulateResult(result, vertices, faces);
            return result;
        }

        [[nodiscard]] bool IsBinarySTL(std::span<const std::byte> data)
        {
            if (data.size() < 84)
            {
                return false;
            }

            std::uint32_t triCount = 0;
            std::memcpy(&triCount, data.data() + 80, sizeof(std::uint32_t));

            const std::size_t expectedSize =
                std::size_t{84} + static_cast<std::size_t>(triCount) * std::size_t{50};
            if (expectedSize == data.size())
            {
                return true;
            }

            const std::size_t windowSize = std::min<std::size_t>(data.size(), 1024);
            const std::string_view window(reinterpret_cast<const char*>(data.data()), windowSize);
            const std::size_t firstNonWs = window.find_first_not_of(" \t\r\n");
            if (firstNonWs != std::string_view::npos)
            {
                const std::string_view trimmed = window.substr(firstNonWs);
                const bool startsSolid = trimmed.substr(0, 5) == "solid";
                const bool hasFacet = window.find("facet") != std::string_view::npos;
                if (startsSolid && hasFacet)
                {
                    return false;
                }
            }

            return data.size() >= 84;
        }

        [[nodiscard]] Core::Expected<MeshIOResult> ParseBinarySTL(std::span<const std::byte> data,
                                                                  std::string_view absolute_path)
        {
            if (data.size() < 84)
            {
                return InvalidMeshFormat();
            }

            std::uint32_t triCount = 0;
            std::memcpy(&triCount, data.data() + 80, sizeof(std::uint32_t));
            if (triCount == 0)
            {
                return InvalidMeshFormat();
            }

            const std::size_t expectedSize =
                std::size_t{84} + static_cast<std::size_t>(triCount) * std::size_t{50};
            if (data.size() < expectedSize)
            {
                return InvalidMeshFormat();
            }

            std::vector<glm::vec3> vertices;
            vertices.reserve(static_cast<std::size_t>(triCount) * 3);
            std::vector<std::vector<std::uint32_t>> faces;
            faces.reserve(triCount);

            const std::byte* base = data.data() + 84;
            for (std::uint32_t t = 0; t < triCount; ++t)
            {
                const std::byte* record = base + static_cast<std::size_t>(t) * 50;
                glm::vec3 triangle[3];
                for (int v = 0; v < 3; ++v)
                {
                    float x = 0.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                    const std::byte* vertexPtr = record + 12 + v * 12;
                    std::memcpy(&x, vertexPtr + 0, sizeof(float));
                    std::memcpy(&y, vertexPtr + 4, sizeof(float));
                    std::memcpy(&z, vertexPtr + 8, sizeof(float));
                    triangle[v] = glm::vec3(x, y, z);
                }
                const auto baseIndex = static_cast<std::uint32_t>(vertices.size());
                vertices.push_back(triangle[0]);
                vertices.push_back(triangle[1]);
                vertices.push_back(triangle[2]);
                faces.push_back({baseIndex, baseIndex + 1u, baseIndex + 2u});
            }

            MeshIOResult result;
            const auto pathInfo = MakePathInfo(absolute_path);
            result.SourcePath = pathInfo.SourcePath;
            result.BasePath = pathInfo.BasePath;
            PopulateResult(result, vertices, faces);
            return result;
        }
    }

    Core::Expected<MeshIOResult> LoadOBJ(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<MeshIOResult>(ToCoreError(text.error()));
        }

        std::vector<glm::vec3> vertices;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texcoords;
        std::vector<std::vector<std::uint32_t>> faces;
        std::size_t cursor = 0;
        std::string_view line;
        while (NextLine(*text, cursor, line))
        {
            const std::size_t comment = line.find('#');
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
            if (tokens[0] == "v")
            {
                if (tokens.size() < 4)
                {
                    return InvalidMeshFormat();
                }
                const auto x = ParseNumber<float>(tokens[1]);
                const auto y = ParseNumber<float>(tokens[2]);
                const auto z = ParseNumber<float>(tokens[3]);
                if (!x || !y || !z)
                {
                    return InvalidMeshFormat();
                }
                vertices.emplace_back(*x, *y, *z);
            }
            else if (tokens[0] == "vn")
            {
                if (tokens.size() < 4)
                {
                    return InvalidMeshFormat();
                }
                const auto x = ParseNumber<float>(tokens[1]);
                const auto y = ParseNumber<float>(tokens[2]);
                const auto z = ParseNumber<float>(tokens[3]);
                if (!x || !y || !z)
                {
                    return InvalidMeshFormat();
                }
                normals.emplace_back(*x, *y, *z);
            }
            else if (tokens[0] == "vt")
            {
                if (tokens.size() < 3)
                {
                    return InvalidMeshFormat();
                }
                const auto u = ParseNumber<float>(tokens[1]);
                const auto v = ParseNumber<float>(tokens[2]);
                if (!u || !v)
                {
                    return InvalidMeshFormat();
                }
                texcoords.emplace_back(*u, *v);
            }
            else if (tokens[0] == "f")
            {
                if (tokens.size() < 4)
                {
                    return InvalidMeshFormat();
                }
                std::vector<std::uint32_t> face;
                face.reserve(tokens.size() - 1);
                for (std::size_t i = 1; i < tokens.size(); ++i)
                {
                    const auto index = ParseOBJVertexIndex(tokens[i], vertices.size());
                    if (!index)
                    {
                        return InvalidMeshFormat();
                    }
                    face.push_back(*index);
                }
                faces.push_back(std::move(face));
            }
        }

        if (vertices.empty() || faces.empty())
        {
            return InvalidMeshFormat();
        }

        MeshIOResult result;
        const auto pathInfo = MakePathInfo(absolute_path);
        result.SourcePath = pathInfo.SourcePath;
        result.BasePath = pathInfo.BasePath;
        const std::span<const glm::vec3> normalsSpan =
            normals.size() == vertices.size() ? std::span<const glm::vec3>(normals) : std::span<const glm::vec3>{};
        PopulateResult(result, vertices, faces, normalsSpan);
        if (texcoords.size() == vertices.size())
        {
            auto texcoordProperty =
                result.Vertices.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
            for (std::size_t i = 0; i < texcoords.size(); ++i)
            {
                texcoordProperty[i] = texcoords[i];
            }
        }
        return result;
    }

    Core::Expected<MeshIOResult> LoadOFF(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<MeshIOResult>(ToCoreError(text.error()));
        }

        std::size_t cursor = 0;
        std::string_view line;
        do
        {
            if (!NextLine(*text, cursor, line))
            {
                return InvalidMeshFormat();
            }
        } while (line.empty() || line.front() == '#');

        const auto variant = ParseOFFMagic(line);
        if (!variant)
        {
            return InvalidMeshFormat();
        }
        const bool hasNormals = (*variant == OFFVariant::NOFF || *variant == OFFVariant::CNOFF);
        const bool hasColors = (*variant == OFFVariant::COFF || *variant == OFFVariant::CNOFF);

        do
        {
            if (!NextLine(*text, cursor, line))
            {
                return InvalidMeshFormat();
            }
        } while (line.empty() || line.front() == '#');

        const auto counts = SplitWhitespace(line);
        if (counts.size() < 2)
        {
            return InvalidMeshFormat();
        }
        const auto vertexCount = ParseNumber<std::size_t>(counts[0]);
        const auto faceCount = ParseNumber<std::size_t>(counts[1]);
        if (!vertexCount || !faceCount || *vertexCount == 0 || *faceCount == 0)
        {
            return InvalidMeshFormat();
        }

        std::vector<glm::vec3> vertices;
        vertices.reserve(*vertexCount);
        std::vector<glm::vec3> normals;
        if (hasNormals)
        {
            normals.reserve(*vertexCount);
        }
        std::vector<glm::vec4> colors;
        if (hasColors)
        {
            colors.reserve(*vertexCount);
        }
        for (std::size_t i = 0; i < *vertexCount; ++i)
        {
            if (!NextLine(*text, cursor, line))
            {
                return InvalidMeshFormat();
            }
            if (line.empty() || line.front() == '#')
            {
                --i;
                continue;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.size() < 3)
            {
                return InvalidMeshFormat();
            }
            const auto x = ParseNumber<float>(tokens[0]);
            const auto y = ParseNumber<float>(tokens[1]);
            const auto z = ParseNumber<float>(tokens[2]);
            if (!x || !y || !z)
            {
                return InvalidMeshFormat();
            }
            vertices.emplace_back(*x, *y, *z);

            std::size_t tokenIdx = 3;

            if (hasNormals)
            {
                glm::vec3 normal(0.0f, 1.0f, 0.0f);
                if (tokenIdx + 2 < tokens.size())
                {
                    const auto nx = ParseNumber<float>(tokens[tokenIdx]);
                    const auto ny = ParseNumber<float>(tokens[tokenIdx + 1]);
                    const auto nz = ParseNumber<float>(tokens[tokenIdx + 2]);
                    if (nx && ny && nz)
                    {
                        normal = glm::vec3(*nx, *ny, *nz);
                    }
                }
                normals.push_back(normal);
                tokenIdx += 3;
            }

            if (hasColors)
            {
                glm::vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
                if (tokenIdx + 2 < tokens.size())
                {
                    const auto r = ParseNumber<float>(tokens[tokenIdx]);
                    const auto g = ParseNumber<float>(tokens[tokenIdx + 1]);
                    const auto b = ParseNumber<float>(tokens[tokenIdx + 2]);
                    if (r && g && b)
                    {
                        color = glm::vec4(NormalizeOFFColorChannel(*r),
                                          NormalizeOFFColorChannel(*g),
                                          NormalizeOFFColorChannel(*b),
                                          1.0f);
                    }
                    tokenIdx += 3;
                    if (tokenIdx < tokens.size())
                    {
                        (void)ParseNumber<float>(tokens[tokenIdx]);
                    }
                }
                colors.push_back(color);
            }
        }

        std::vector<std::vector<std::uint32_t>> faces;
        faces.reserve(*faceCount);
        for (std::size_t i = 0; i < *faceCount; ++i)
        {
            if (!NextLine(*text, cursor, line))
            {
                return InvalidMeshFormat();
            }
            if (line.empty() || line.front() == '#')
            {
                --i;
                continue;
            }
            const auto tokens = SplitWhitespace(line);
            if (tokens.empty())
            {
                return InvalidMeshFormat();
            }
            const auto count = ParseNumber<std::size_t>(tokens[0]);
            if (!count || *count < 3)
            {
                continue;
            }
            if (tokens.size() < *count + 1)
            {
                return InvalidMeshFormat();
            }
            std::vector<std::uint32_t> face;
            face.reserve(*count);
            for (std::size_t j = 0; j < *count; ++j)
            {
                const auto index = ParseNumber<std::size_t>(tokens[j + 1]);
                if (!index || *index >= vertices.size())
                {
                    return InvalidMeshFormat();
                }
                face.push_back(static_cast<std::uint32_t>(*index));
            }
            faces.push_back(std::move(face));
        }

        if (faces.empty())
        {
            return InvalidMeshFormat();
        }

        MeshIOResult result;
        const auto pathInfo = MakePathInfo(absolute_path);
        result.SourcePath = pathInfo.SourcePath;
        result.BasePath = pathInfo.BasePath;
        PopulateResult(result, vertices, faces, normals, colors);
        return result;
    }

    Core::Expected<MeshIOResult> LoadPLY(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<MeshIOResult>(ToCoreError(text.error()));
        }

        std::size_t cursor = 0;
        std::string_view line;
        if (!NextLine(*text, cursor, line) || line != "ply")
        {
            return InvalidMeshFormat();
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
                    return InvalidMeshFormat();
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
                    return InvalidMeshFormat();
                }
                formatSeen = true;
            }
            else if (tokens[0] == "element")
            {
                if (tokens.size() < 3)
                {
                    return InvalidMeshFormat();
                }
                const auto count = ParseNumber<std::size_t>(tokens[2]);
                if (!count)
                {
                    return InvalidMeshFormat();
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
                    return InvalidMeshFormat();
                }
                PlyProperty prop;
                if (tokens.size() >= 5 && tokens[1] == "list")
                {
                    const auto countType = ParsePlyScalarType(tokens[2]);
                    const auto elemType = ParsePlyScalarType(tokens[3]);
                    if (!countType || !elemType)
                    {
                        return InvalidMeshFormat();
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
                        return InvalidMeshFormat();
                    }
                    prop.IsList = false;
                    prop.ScalarType = *scalarType;
                    prop.Name = std::string(tokens[2]);
                }
                else
                {
                    return InvalidMeshFormat();
                }
                elements.back().Properties.push_back(std::move(prop));
            }
        }

        if (!formatSeen || !headerEndSeen)
        {
            return InvalidMeshFormat();
        }

        if (format == PlyFormat::Ascii)
        {
            return ParseAsciiPLY(*text, cursor, elements, absolute_path);
        }

        const std::span<const std::byte> body(
            reinterpret_cast<const std::byte*>(text->data() + cursor),
            text->size() - cursor);
        return ParseBinaryPLY(body, elements, format == PlyFormat::BinaryBigEndian, absolute_path);
    }

    Core::Expected<MeshIOResult> LoadSTL(std::string_view absolute_path)
    {
        auto text = ReadTextFile(absolute_path);
        if (!text)
        {
            return Core::Err<MeshIOResult>(ToCoreError(text.error()));
        }

        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(text->data()), text->size());
        if (IsBinarySTL(bytes))
        {
            return ParseBinarySTL(bytes, absolute_path);
        }

        std::vector<glm::vec3> vertices;
        std::vector<std::vector<std::uint32_t>> faces;
        std::vector<std::uint32_t> currentFace;
        currentFace.reserve(3);

        std::size_t cursor = 0;
        std::string_view line;
        while (NextLine(*text, cursor, line))
        {
            const auto tokens = SplitWhitespace(line);
            if (tokens.size() == 4 && tokens[0] == "vertex")
            {
                const auto x = ParseNumber<float>(tokens[1]);
                const auto y = ParseNumber<float>(tokens[2]);
                const auto z = ParseNumber<float>(tokens[3]);
                if (!x || !y || !z)
                {
                    return InvalidMeshFormat();
                }
                vertices.emplace_back(*x, *y, *z);
                currentFace.push_back(static_cast<std::uint32_t>(vertices.size() - 1));
                if (currentFace.size() == 3)
                {
                    faces.push_back(currentFace);
                    currentFace.clear();
                }
            }
        }

        if (vertices.empty() || faces.empty() || !currentFace.empty())
        {
            return InvalidMeshFormat();
        }

        MeshIOResult result;
        const auto pathInfo = MakePathInfo(absolute_path);
        result.SourcePath = pathInfo.SourcePath;
        result.BasePath = pathInfo.BasePath;
        PopulateResult(result, vertices, faces);
        return result;
    }

    MeshIOWriteStatus WriteOBJ(std::string_view absolute_path, const MeshIOResult& mesh)
    {
        if (absolute_path.empty())
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        const auto positionsView = mesh.Vertices.Get<glm::vec3>("v:point");
        if (!positionsView.IsValid() || positionsView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& positions = positionsView.Vector();

        const auto facesView = mesh.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!facesView.IsValid() || facesView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& faces = facesView.Vector();

        for (const auto& face : faces)
        {
            if (face.size() < 3)
            {
                return MeshIOWriteStatus::InvalidFace;
            }
            for (const auto index : face)
            {
                if (static_cast<std::size_t>(index) >= positions.size())
                {
                    return MeshIOWriteStatus::InvalidFace;
                }
            }
        }

        const auto normalsView = mesh.Vertices.Get<glm::vec3>("v:normal");
        const bool hasNormals = normalsView.IsValid() && normalsView.Vector().size() == positions.size();

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        char buffer[128];

        stream << "# Exported by IntrinsicEngine\n";

        for (const auto& p : positions)
        {
            const int written = std::snprintf(buffer, sizeof(buffer), "v %.6f %.6f %.6f\n",
                                              static_cast<double>(p.x),
                                              static_cast<double>(p.y),
                                              static_cast<double>(p.z));
            if (written <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }

        if (hasNormals)
        {
            for (const auto& n : normalsView.Vector())
            {
                const int written = std::snprintf(buffer, sizeof(buffer), "vn %.6f %.6f %.6f\n",
                                                  static_cast<double>(n.x),
                                                  static_cast<double>(n.y),
                                                  static_cast<double>(n.z));
                if (written <= 0)
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }
        }

        for (const auto& face : faces)
        {
            stream.put('f');
            for (const auto index : face)
            {
                const auto oneBased = static_cast<unsigned long long>(index) + 1ULL;
                int written = 0;
                if (hasNormals)
                {
                    written = std::snprintf(buffer, sizeof(buffer), " %llu//%llu", oneBased, oneBased);
                }
                else
                {
                    written = std::snprintf(buffer, sizeof(buffer), " %llu", oneBased);
                }
                if (written <= 0)
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }
            stream.put('\n');
        }

        stream.flush();
        if (!stream.good())
        {
            return MeshIOWriteStatus::FileWriteError;
        }
        return MeshIOWriteStatus::Success;
    }

    MeshIOWriteStatus WritePLY(std::string_view absolute_path, const MeshIOResult& mesh)
    {
        if (absolute_path.empty())
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        const auto positionsView = mesh.Vertices.Get<glm::vec3>("v:point");
        if (!positionsView.IsValid() || positionsView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& positions = positionsView.Vector();

        const auto facesView = mesh.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!facesView.IsValid() || facesView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& faces = facesView.Vector();

        for (const auto& face : faces)
        {
            if (face.size() < 3)
            {
                return MeshIOWriteStatus::InvalidFace;
            }
            for (const auto index : face)
            {
                if (static_cast<std::size_t>(index) >= positions.size())
                {
                    return MeshIOWriteStatus::InvalidFace;
                }
            }
        }

        const auto normalsView = mesh.Vertices.Get<glm::vec3>("v:normal");
        const bool hasNormals = normalsView.IsValid() && normalsView.Vector().size() == positions.size();

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        char buffer[192];

        stream << "ply\n";
        stream << "format ascii 1.0\n";
        stream << "comment Exported by IntrinsicEngine\n";
        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "element vertex %zu\n",
                                              positions.size());
            if (written <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
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
        {
            const int written = std::snprintf(buffer, sizeof(buffer),
                                              "element face %zu\n",
                                              faces.size());
            if (written <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }
        stream << "property list uchar int vertex_indices\n";
        stream << "end_header\n";

        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            const auto& p = positions[i];
            int written = 0;
            if (hasNormals)
            {
                const auto& n = normalsView.Vector()[i];
                written = std::snprintf(buffer, sizeof(buffer),
                                        "%.6f %.6f %.6f %.6f %.6f %.6f\n",
                                        static_cast<double>(p.x),
                                        static_cast<double>(p.y),
                                        static_cast<double>(p.z),
                                        static_cast<double>(n.x),
                                        static_cast<double>(n.y),
                                        static_cast<double>(n.z));
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
                return MeshIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);
        }

        for (const auto& face : faces)
        {
            const int countWritten = std::snprintf(buffer, sizeof(buffer), "%zu", face.size());
            if (countWritten <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, countWritten);
            for (const auto index : face)
            {
                const int written = std::snprintf(buffer, sizeof(buffer), " %llu",
                                                  static_cast<unsigned long long>(index));
                if (written <= 0)
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }
            stream.put('\n');
        }

        stream.flush();
        if (!stream.good())
        {
            return MeshIOWriteStatus::FileWriteError;
        }
        return MeshIOWriteStatus::Success;
    }

    MeshIOWriteStatus WritePLYBinary(std::string_view absolute_path, const MeshIOResult& mesh)
    {
        if (absolute_path.empty())
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        const auto positionsView = mesh.Vertices.Get<glm::vec3>("v:point");
        if (!positionsView.IsValid() || positionsView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& positions = positionsView.Vector();

        const auto facesView = mesh.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!facesView.IsValid() || facesView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& faces = facesView.Vector();

        for (const auto& face : faces)
        {
            if (face.size() < 3)
            {
                return MeshIOWriteStatus::InvalidFace;
            }
            if (face.size() > 255u)
            {
                return MeshIOWriteStatus::InvalidFace;
            }
            for (const auto index : face)
            {
                if (static_cast<std::size_t>(index) >= positions.size())
                {
                    return MeshIOWriteStatus::InvalidFace;
                }
            }
        }

        const auto normalsView = mesh.Vertices.Get<glm::vec3>("v:normal");
        const bool hasNormals = normalsView.IsValid() && normalsView.Vector().size() == positions.size();

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        char headerBuffer[192];

        stream << "ply\n";
        stream << "format binary_little_endian 1.0\n";
        stream << "comment Exported by IntrinsicEngine\n";
        {
            const int written = std::snprintf(headerBuffer, sizeof(headerBuffer),
                                              "element vertex %zu\n",
                                              positions.size());
            if (written <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
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
        {
            const int written = std::snprintf(headerBuffer, sizeof(headerBuffer),
                                              "element face %zu\n",
                                              faces.size());
            if (written <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            stream.write(headerBuffer, written);
        }
        stream << "property list uchar int vertex_indices\n";
        stream << "end_header\n";

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

        auto writeInt32LE = [&](std::int32_t value) -> bool {
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

        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            const auto& p = positions[i];
            if (!writeFloatLE(p.x) || !writeFloatLE(p.y) || !writeFloatLE(p.z))
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            if (hasNormals)
            {
                const auto& n = normalsView.Vector()[i];
                if (!writeFloatLE(n.x) || !writeFloatLE(n.y) || !writeFloatLE(n.z))
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
            }
        }

        for (const auto& face : faces)
        {
            if (!writeUInt8(static_cast<std::uint8_t>(face.size())))
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            for (const auto index : face)
            {
                if (!writeInt32LE(static_cast<std::int32_t>(index)))
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
            }
        }

        stream.flush();
        if (!stream.good())
        {
            return MeshIOWriteStatus::FileWriteError;
        }
        return MeshIOWriteStatus::Success;
    }

    MeshIOWriteStatus WriteSTL(std::string_view absolute_path, const MeshIOResult& mesh)
    {
        if (absolute_path.empty())
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        const auto positionsView = mesh.Vertices.Get<glm::vec3>("v:point");
        if (!positionsView.IsValid() || positionsView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& positions = positionsView.Vector();

        const auto facesView = mesh.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!facesView.IsValid() || facesView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& faces = facesView.Vector();

        for (const auto& face : faces)
        {
            if (face.size() != 3)
            {
                return MeshIOWriteStatus::InvalidFace;
            }
            for (const auto index : face)
            {
                if (static_cast<std::size_t>(index) >= positions.size())
                {
                    return MeshIOWriteStatus::InvalidFace;
                }
            }
        }

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        char buffer[256];

        stream << "solid IntrinsicEngine\n";

        for (const auto& face : faces)
        {
            const glm::vec3& v0 = positions[face[0]];
            const glm::vec3& v1 = positions[face[1]];
            const glm::vec3& v2 = positions[face[2]];

            glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
            if (!std::isfinite(normal.x))
            {
                normal = glm::vec3(0.0f, 0.0f, 0.0f);
            }

            int written = std::snprintf(buffer, sizeof(buffer),
                                        "  facet normal %.6e %.6e %.6e\n"
                                        "    outer loop\n",
                                        static_cast<double>(normal.x),
                                        static_cast<double>(normal.y),
                                        static_cast<double>(normal.z));
            if (written <= 0)
            {
                return MeshIOWriteStatus::FileWriteError;
            }
            stream.write(buffer, written);

            const glm::vec3 vertices[3] = {v0, v1, v2};
            for (const auto& v : vertices)
            {
                written = std::snprintf(buffer, sizeof(buffer),
                                        "      vertex %.6e %.6e %.6e\n",
                                        static_cast<double>(v.x),
                                        static_cast<double>(v.y),
                                        static_cast<double>(v.z));
                if (written <= 0)
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
                stream.write(buffer, written);
            }

            stream << "    endloop\n"
                      "  endfacet\n";
        }

        stream << "endsolid IntrinsicEngine\n";

        stream.flush();
        if (!stream.good())
        {
            return MeshIOWriteStatus::FileWriteError;
        }
        return MeshIOWriteStatus::Success;
    }

    MeshIOWriteStatus WriteSTLBinary(std::string_view absolute_path, const MeshIOResult& mesh)
    {
        if (absolute_path.empty())
        {
            return MeshIOWriteStatus::InvalidPath;
        }

        const auto positionsView = mesh.Vertices.Get<glm::vec3>("v:point");
        if (!positionsView.IsValid() || positionsView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& positions = positionsView.Vector();

        const auto facesView = mesh.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!facesView.IsValid() || facesView.Vector().empty())
        {
            return MeshIOWriteStatus::EmptyMesh;
        }
        const auto& faces = facesView.Vector();

        for (const auto& face : faces)
        {
            if (face.size() != 3)
            {
                return MeshIOWriteStatus::InvalidFace;
            }
            for (const auto index : face)
            {
                if (static_cast<std::size_t>(index) >= positions.size())
                {
                    return MeshIOWriteStatus::InvalidFace;
                }
            }
        }

        if (faces.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            return MeshIOWriteStatus::InvalidFace;
        }

        std::ofstream stream(std::string(absolute_path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return MeshIOWriteStatus::InvalidPath;
        }

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

        auto writeUInt32LE = [&](std::uint32_t value) -> bool {
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

        auto writeUInt16LE = [&](std::uint16_t value) -> bool {
            std::array<std::byte, 2> bytes{};
            std::memcpy(bytes.data(), &value, 2);
            if constexpr (hostBigEndian)
            {
                std::swap(bytes[0], bytes[1]);
            }
            stream.write(reinterpret_cast<const char*>(bytes.data()), 2);
            return stream.good();
        };

        std::array<char, 80> header{};
        constexpr std::string_view tag = "IntrinsicEngine binary STL";
        std::memcpy(header.data(), tag.data(), tag.size());
        stream.write(header.data(), static_cast<std::streamsize>(header.size()));
        if (!stream.good())
        {
            return MeshIOWriteStatus::FileWriteError;
        }

        if (!writeUInt32LE(static_cast<std::uint32_t>(faces.size())))
        {
            return MeshIOWriteStatus::FileWriteError;
        }

        for (const auto& face : faces)
        {
            const glm::vec3& v0 = positions[face[0]];
            const glm::vec3& v1 = positions[face[1]];
            const glm::vec3& v2 = positions[face[2]];

            glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
            {
                normal = glm::vec3(0.0f, 0.0f, 0.0f);
            }

            if (!writeFloatLE(normal.x) || !writeFloatLE(normal.y) || !writeFloatLE(normal.z))
            {
                return MeshIOWriteStatus::FileWriteError;
            }

            for (const glm::vec3& v : {v0, v1, v2})
            {
                if (!writeFloatLE(v.x) || !writeFloatLE(v.y) || !writeFloatLE(v.z))
                {
                    return MeshIOWriteStatus::FileWriteError;
                }
            }

            if (!writeUInt16LE(0u))
            {
                return MeshIOWriteStatus::FileWriteError;
            }
        }

        stream.flush();
        if (!stream.good())
        {
            return MeshIOWriteStatus::FileWriteError;
        }
        return MeshIOWriteStatus::Success;
    }
}


