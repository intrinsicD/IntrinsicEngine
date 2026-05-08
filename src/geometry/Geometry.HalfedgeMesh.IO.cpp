module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

#include "Geometry.IOText.hpp"

module Geometry.MeshIO;

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
                            std::span<const glm::vec3> normals = {})
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
        PopulateResult(result, vertices, faces);
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

        if (line != "OFF")
        {
            return InvalidMeshFormat();
        }

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

        bool ascii = false;
        std::size_t vertexCount = 0;
        std::size_t faceCount = 0;
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
            else if (tokens.size() >= 3 && tokens[0] == "element" && tokens[1] == "vertex")
            {
                if (const auto parsed = ParseNumber<std::size_t>(tokens[2]))
                {
                    vertexCount = *parsed;
                }
            }
            else if (tokens.size() >= 3 && tokens[0] == "element" && tokens[1] == "face")
            {
                if (const auto parsed = ParseNumber<std::size_t>(tokens[2]))
                {
                    faceCount = *parsed;
                }
            }
        }

        if (!ascii || vertexCount == 0 || faceCount == 0)
        {
            return InvalidMeshFormat();
        }

        std::vector<glm::vec3> vertices;
        vertices.reserve(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            if (!NextLine(*text, cursor, line))
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
            if (!NextLine(*text, cursor, line))
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
}


