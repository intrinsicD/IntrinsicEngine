// Produces machine-readable local curvature samples from the public geometry
// path so external reference implementations can be compared without linking
// them into the engine or admitting their datasets to CI.

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

import Geometry.Curvature;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;

namespace
{
    template <typename T>
    bool WriteScalar(std::ofstream& output, const T& value)
    {
        output.write(
            reinterpret_cast<const char*>(&value),
            static_cast<std::streamsize>(sizeof(T)));
        return output.good();
    }

    bool WriteVector3(std::ofstream& output, const glm::vec3 value)
    {
        return WriteScalar(output, value.x)
            && WriteScalar(output, value.y)
            && WriteScalar(output, value.z);
    }

    double Median(std::vector<double> samples)
    {
        std::sort(samples.begin(), samples.end());
        const std::size_t middle = samples.size() / 2u;
        if ((samples.size() & 1u) != 0u)
            return samples[middle];
        return 0.5 * (samples[middle - 1u] + samples[middle]);
    }

    glm::vec3 GeometricVertexNormal(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const Geometry::VertexHandle vertex)
    {
        glm::dvec3 normal{0.0};
        for (const Geometry::FaceHandle face : mesh.FacesAroundVertex(vertex))
            normal += Geometry::MeshUtils::FaceAreaVector(mesh, face);
        const double length = glm::length(normal);
        if (!std::isfinite(length) || length <= 0.0)
            return glm::vec3{0.0f};
        return glm::vec3(normal / length);
    }

    struct BuiltMesh
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::uint64_t RejectedTriangleCount{0u};
    };

    BuiltMesh BuildTriangleMesh(const Geometry::MeshIO::MeshIOResult& source)
    {
        BuiltMesh result{};
        const auto positions =
            source.Vertices.Get<glm::vec3>("v:point");
        const auto faces = source.Faces.Get<std::vector<std::uint32_t>>(
            "f:vertices");
        if (!positions.IsValid() || !faces.IsValid())
            return result;

        std::vector<Geometry::VertexHandle> vertices{};
        vertices.reserve(positions.Size());
        for (const glm::vec3 position : positions.Span())
            vertices.push_back(result.Mesh.AddVertex(position));

        for (const std::vector<std::uint32_t>& face : faces.Vector())
        {
            if (face.size() < 3u)
            {
                ++result.RejectedTriangleCount;
                continue;
            }
            for (std::size_t corner = 1u; corner + 1u < face.size(); ++corner)
            {
                if (face[0] >= vertices.size()
                    || face[corner] >= vertices.size()
                    || face[corner + 1u] >= vertices.size()
                    || !result.Mesh.AddTriangle(
                        vertices[face[0]],
                        vertices[face[corner]],
                        vertices[face[corner + 1u]]))
                {
                    ++result.RejectedTriangleCount;
                }
            }
        }
        return result;
    }

    bool WriteResult(
        const std::string& path,
        const BuiltMesh& built,
        const Geometry::Curvature::CurvatureTensorResult& curvature,
        const double loadMilliseconds,
        const double computeMilliseconds)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;

        constexpr std::array<char, 8> magic{
            'I', 'C', 'U', 'R', 'V', '0', '0', '2'};
        output.write(magic.data(), static_cast<std::streamsize>(magic.size()));

        std::uint64_t boundaryCount = 0u;
        for (const Geometry::VertexHandle vertex : built.Mesh.LiveVertices())
        {
            if (built.Mesh.IsBoundary(vertex))
                ++boundaryCount;
        }
        const std::array<std::uint64_t, 8> counts{
            static_cast<std::uint64_t>(built.Mesh.VertexCount()),
            static_cast<std::uint64_t>(built.Mesh.EdgeCount()),
            static_cast<std::uint64_t>(built.Mesh.FaceCount()),
            boundaryCount,
            built.RejectedTriangleCount,
            static_cast<std::uint64_t>(
                curvature.Diagnostics.SupportedVertexCount),
            static_cast<std::uint64_t>(
                curvature.Diagnostics.NonZeroPrincipalVertexCount),
            1u,
        };
        for (const std::uint64_t count : counts)
        {
            if (!WriteScalar(output, count))
                return false;
        }
        if (!WriteScalar(output, loadMilliseconds)
            || !WriteScalar(output, computeMilliseconds))
        {
            return false;
        }

        for (const Geometry::VertexHandle vertex : built.Mesh.LiveVertices())
        {
            const glm::vec3 position = built.Mesh.Position(vertex);
            if (!WriteVector3(output, position)
                || !WriteScalar(
                    output,
                    curvature.MinPrincipalCurvatureProperty[vertex])
                || !WriteScalar(
                    output,
                    curvature.MaxPrincipalCurvatureProperty[vertex])
                || !WriteVector3(
                    output, curvature.PrincipalDir1Property[vertex])
                || !WriteVector3(
                    output, curvature.PrincipalDir2Property[vertex])
                || !WriteVector3(
                    output, GeometricVertexNormal(built.Mesh, vertex)))
            {
                return false;
            }
        }
        return output.good();
    }
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "usage: IntrinsicCurvatureCorpusProbe INPUT.obj OUTPUT.bin [repetitions]\n";
        return 2;
    }
    std::size_t repetitions = 1u;
    if (argc == 4)
    {
        const std::string_view value{argv[3]};
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), repetitions);
        if (error != std::errc{} || end != value.data() + value.size())
            return 2;
    }
    if (repetitions == 0u)
        return 2;

    const auto loadBegin = std::chrono::steady_clock::now();
    const auto source = Geometry::MeshIO::LoadOBJ(argv[1]);
    if (!source)
    {
        std::cerr << "OBJ import failed\n";
        return 3;
    }
    BuiltMesh built = BuildTriangleMesh(*source);
    const auto loadEnd = std::chrono::steady_clock::now();
    if (built.Mesh.FaceCount() == 0u)
    {
        std::cerr << "no accepted triangles\n";
        return 4;
    }

    (void)Geometry::Curvature::ComputeCurvatureTensor(built.Mesh);
    std::vector<double> samples{};
    samples.reserve(repetitions);
    std::optional<Geometry::Curvature::CurvatureTensorResult> curvature{};
    for (std::size_t iteration = 0u; iteration < repetitions; ++iteration)
    {
        const auto begin = std::chrono::steady_clock::now();
        curvature = Geometry::Curvature::ComputeCurvatureTensor(built.Mesh);
        const auto end = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(
            end - begin).count());
    }
    if (!curvature)
    {
        std::cerr << "curvature computation failed\n";
        return 5;
    }

    const double loadMilliseconds =
        std::chrono::duration<double, std::milli>(loadEnd - loadBegin).count();
    if (!WriteResult(
        argv[2], built, *curvature, loadMilliseconds, Median(samples)))
    {
        std::cerr << "result write failed\n";
        return 6;
    }
    return 0;
}
