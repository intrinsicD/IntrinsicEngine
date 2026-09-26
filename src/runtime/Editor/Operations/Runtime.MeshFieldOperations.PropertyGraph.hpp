// Private helpers shared by property smoothing and harmonic fields: canonical sample capture
// on every element domain, the weighted sample graph (kNN, mesh edges or nonnegative
// cotangent) with optional lumped masses and boundary rows, and typed property snapshots for
// guarded publication. Include after the PointFields and MeshSources processing headers.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

extern "C++"
{
namespace Extrinsic::Runtime::PropertyGraphDetail
{
    using Storage = std::variant<std::vector<float>, std::vector<double>, std::vector<glm::vec2>,
                                 std::vector<glm::vec3>, std::vector<glm::vec4>, std::vector<std::int32_t>>;
    struct Snapshot { bool Exists{}; Storage Values{}; };

    template<class T> constexpr std::size_t Channels()
    { if constexpr (std::is_arithmetic_v<T>) return 1; else return T::length(); }
    template<class T> double Channel(const T& v, std::size_t c)
    { if constexpr (std::is_arithmetic_v<T>) return static_cast<double>(v); else return v[c]; }
    template<class T> void SetChannel(T& v, std::size_t c, double x)
    { if constexpr (std::is_arithmetic_v<T>) v = static_cast<T>(x); else v[c] = static_cast<float>(x); }

    // Float by default; Double, Vec2..4 and Int32 map to their storage.
    [[nodiscard]] Storage EmptyStorage(Geometry::PropertyValueKind kind);
    // Bool refs are checked for masks; snapshots cover the Storage kinds only.
    [[nodiscard]] bool CountMatches(const Geometry::PropertySet& props, const GeometryPropertyRef& ref);
    [[nodiscard]] Snapshot Capture(const Geometry::PropertySet& props, const GeometryPropertyRef& ref);
    [[nodiscard]] bool Same(const Snapshot& a, const Snapshot& b);

    // Live rows of `inputDomain` and their anchor points: the bound positions on the same
    // domain, or face centers / edge and halfedge midpoints derived from vertex/node positions.
    // Topology and deletion revisions are added to `capture.Inputs` as publication guards.
    [[nodiscard]] bool CaptureSamples(const GeometryEntityAvailability& availability, GeometryElementDomain inputDomain,
                                      const GeometryPropertyRef& positions, GeometryProcessingDetail::PointInputCapture& capture,
                                      std::string& diagnostic);

    struct GraphRequest
    {
        GeometryPropertyRef Positions{};
        Geometry::Smoothing::PropertyWeight Weight{Geometry::Smoothing::PropertyWeight::Uniform};
        std::uint32_t Neighbors{12};
        double SpatialSigma{1.0};
        bool LumpedMass{false};     // DEC lumped vertex areas, one per live row
        bool BoundaryRows{false};   // compact rows of mesh boundary vertices
        std::string_view Operation{"Property graph"};
    };
    struct Graph
    {
        std::vector<Geometry::Smoothing::PropertyEdge> Edges{};
        std::vector<double> Mass{};
        std::vector<std::size_t> BoundaryRows{};
    };
    // Mesh weights, lumped masses and boundary rows need mesh-vertex rows and vertex positions.
    [[nodiscard]] bool UsesMeshTopology(const GraphRequest& request);
    // Builds the graph over compact sample rows. Mesh-derived requests add mesh topology revisions
    // to `samples.Inputs`.
    [[nodiscard]] bool BuildGraph(const GeometryEntityAvailability& availability, const GraphRequest& request,
                                  GeometryProcessingDetail::PointInputCapture& samples, Graph& graph, std::string& diagnostic);
}
}
