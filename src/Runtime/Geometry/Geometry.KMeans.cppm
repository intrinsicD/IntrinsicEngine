module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:KMeans;

export namespace Geometry::KMeans
{
    enum class Backend : uint8_t
    {
        CPU = 0,
        CUDA = 1,
    };

    enum class Initialization : uint8_t
    {
        Random = 0,
        Hierarchical = 1,
    };

    struct Params
    {
        uint32_t ClusterCount = 8;
        uint32_t MaxIterations = 32;
        float ConvergenceTolerance = 1.0e-4f;
        uint32_t Seed = 42;
        Initialization Init = Initialization::Hierarchical;
        Backend Compute = Backend::CPU;
    };

    struct Result
    {
        std::vector<uint32_t> Labels{};
        std::vector<float> SquaredDistances{};
        std::vector<glm::vec3> Centroids{};
        uint32_t Iterations = 0;
        bool Converged = false;
        float Inertia = 0.0f;
        uint32_t MaxDistanceIndex = 0;
        Backend ActualBackend = Backend::CPU;
    };

    // Lloyd-style k-means on unstructured 3D point sets.
    //
    // Objective:
    //   $$ E(\ell, c) = \sum_{i=1}^{n} \|x_i - c_{\ell_i}\|_2^2 $$
    // where $\ell_i \in \{0, \dots, k-1\}$ is the cluster label and $c_j$
    // is centroid $j$.
    //
    // Experimental Engine24 parity:
    //   - iterative assign/update Lloyd loop,
    //   - random or farthest-point-style hierarchical seeding,
    //   - returns labels, squared distances, centroids, inertia, and farthest sample.
    //
    // Robustness:
    //   - returns nullopt for empty input or zero requested clusters,
    //   - clamps k <= n,
    //   - re-seeds empty clusters from the farthest currently assigned point.
    //
    // Complexity:
    //   - Time: O(n * k * iters)
    //   - Space: O(n + k)
    [[nodiscard]] std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params = {});
}

