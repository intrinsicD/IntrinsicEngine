// Emits deterministic Framework24 Taubin curvature values so the engine port
// can be checked without linking the reference library into IntrinsicEngine.

#include "bcg_mesh.h"
#include "bcg_mesh_curvature_taubin.h"
#include "bcg_mesh_io.h"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: Framework24CurvatureParityProbe INPUT.obj\n";
        return 2;
    }

    Bcg::Mesh mesh{};
    Bcg::MeshIoFlags flags{};
    Bcg::MeshIo io{std::string(argv[1]), flags};
    if (!io.read(mesh))
        return 3;

    const Bcg::Curvatures curvature = Bcg::CurvatureTaubin(
        mesh, 0, false, Bcg::Policy::Sequential);
    std::cout << std::setprecision(17);
    for (std::size_t vertex = 0u; vertex < mesh.vertices.size(); ++vertex)
    {
        const auto& position = mesh.positions[vertex];
        std::cout << vertex << ' ' << position[0] << ' ' << position[1]
                  << ' ' << position[2] << ' ' << curvature.min[vertex]
                  << ' ' << curvature.max[vertex];
        for (int axis = 0; axis < 3; ++axis)
            std::cout << ' ' << curvature.min_direction[vertex][axis];
        for (int axis = 0; axis < 3; ++axis)
            std::cout << ' ' << curvature.max_direction[vertex][axis];
        std::cout << '\n';
    }
    return 0;
}
