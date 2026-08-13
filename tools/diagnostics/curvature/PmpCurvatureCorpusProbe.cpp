// Produces scalar samples from an explicitly supplied PMP checkout for local
// differential testing; this file is compiled out-of-tree and is not an engine
// dependency or a benchmark producer.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "pmp/algorithms/curvature.h"
#include "pmp/io/io.h"
#include "pmp/surface_mesh.h"

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

    double Median(std::vector<double> samples)
    {
        std::sort(samples.begin(), samples.end());
        const std::size_t middle = samples.size() / 2u;
        if ((samples.size() & 1u) != 0u)
            return samples[middle];
        return 0.5 * (samples[middle - 1u] + samples[middle]);
    }

    bool WriteResult(
        const std::string& path,
        const pmp::SurfaceMesh& mesh,
        const std::vector<double>& minimum,
        const std::vector<double>& maximum,
        const double loadMilliseconds,
        const double computeMilliseconds)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;

        constexpr std::array<char, 8> magic{
            'P', 'C', 'U', 'R', 'V', '0', '0', '2'};
        output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        std::uint64_t boundaryCount = 0u;
        std::uint64_t nonzeroCount = 0u;
        for (const pmp::Vertex vertex : mesh.vertices())
        {
            if (mesh.is_boundary(vertex))
                ++boundaryCount;
            if (minimum[vertex.idx()] != 0.0 || maximum[vertex.idx()] != 0.0)
                ++nonzeroCount;
        }
        const std::array<std::uint64_t, 8> counts{
            static_cast<std::uint64_t>(mesh.n_vertices()),
            static_cast<std::uint64_t>(mesh.n_edges()),
            static_cast<std::uint64_t>(mesh.n_faces()),
            boundaryCount,
            0u,
            static_cast<std::uint64_t>(mesh.n_vertices()),
            nonzeroCount,
            0u,
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

        constexpr float zero = 0.0F;
        for (const pmp::Vertex vertex : mesh.vertices())
        {
            const pmp::Point position = mesh.position(vertex);
            for (int component = 0; component < 3; ++component)
            {
                const float value = static_cast<float>(position[component]);
                if (!WriteScalar(output, value))
                    return false;
            }
            if (!WriteScalar(output, minimum[vertex.idx()])
                || !WriteScalar(output, maximum[vertex.idx()]))
            {
                return false;
            }
            for (int component = 0; component < 9; ++component)
            {
                if (!WriteScalar(output, zero))
                    return false;
            }
        }
        return output.good();
    }
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 6)
    {
        std::cerr << "usage: PmpCurvatureCorpusProbe INPUT.obj OUTPUT.bin "
                     "[repetitions] [smoothing_steps] [two_ring 0|1]\n";
        return 2;
    }
    std::size_t repetitions = 1u;
    // Defaults reproduce the historical one-ring/no-smoothing PMP experiment.
    // Current Intrinsic targets Framework24, which is not parameter-equivalent
    // to either PMP setting. Pass `3 1` for PMP's own default tensor path.
    std::size_t smoothingSteps = 0u;
    bool twoRing = false;
    if (argc >= 4)
    {
        try
        {
            repetitions = std::stoull(argv[3]);
        }
        catch (...)
        {
            return 2;
        }
    }
    if (argc >= 5)
    {
        try
        {
            smoothingSteps = std::stoull(argv[4]);
        }
        catch (...)
        {
            return 2;
        }
    }
    if (argc == 6)
    {
        try
        {
            twoRing = std::stoull(argv[5]) != 0u;
        }
        catch (...)
        {
            return 2;
        }
    }
    if (repetitions == 0u)
        return 2;

    pmp::SurfaceMesh mesh{};
    const auto loadBegin = std::chrono::steady_clock::now();
    try
    {
        pmp::read(mesh, argv[1]);
    }
    catch (const std::exception& error)
    {
        std::cerr << "OBJ import failed: " << error.what() << '\n';
        return 3;
    }
    const auto loadEnd = std::chrono::steady_clock::now();
    if (mesh.n_faces() == 0u)
        return 4;

    pmp::curvature(
        mesh, pmp::Curvature::max,
        static_cast<int>(smoothingSteps), true, twoRing);
    std::vector<double> samples{};
    samples.reserve(repetitions);
    for (std::size_t iteration = 0u; iteration < repetitions; ++iteration)
    {
        const auto begin = std::chrono::steady_clock::now();
        pmp::curvature(
            mesh, pmp::Curvature::max,
            static_cast<int>(smoothingSteps), true, twoRing);
        const auto end = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(
            end - begin).count());
    }

    pmp::curvature(
        mesh, pmp::Curvature::min,
        static_cast<int>(smoothingSteps), true, twoRing);
    auto property = mesh.get_vertex_property<pmp::Scalar>("v:curv");
    std::vector<double> minimum(mesh.n_vertices(), 0.0);
    for (const pmp::Vertex vertex : mesh.vertices())
        minimum[vertex.idx()] = property[vertex];
    pmp::curvature(
        mesh, pmp::Curvature::max,
        static_cast<int>(smoothingSteps), true, twoRing);
    std::vector<double> maximum(mesh.n_vertices(), 0.0);
    for (const pmp::Vertex vertex : mesh.vertices())
        maximum[vertex.idx()] = property[vertex];

    const double loadMilliseconds =
        std::chrono::duration<double, std::milli>(loadEnd - loadBegin).count();
    if (!WriteResult(
        argv[2], mesh, minimum, maximum, loadMilliseconds, Median(samples)))
    {
        return 6;
    }
    return 0;
}
