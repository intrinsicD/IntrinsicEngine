module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Components.GeometrySources;

import Geometry.Properties;

export namespace Extrinsic::ECS::Components::GeometrySources
{
    // -----------------------------------------------------------------------
    // Canonical property-name constants for GeometrySources PropertySets.
    //
    // All lifecycle systems and attribute-sync systems must read from these
    // well-known keys, regardless of the originating geometry type.
    //
    // Domain: Vertices (mesh vertex OR point-cloud point)
    //   "v:position"  – glm::vec3  mandatory canonical position
    //   "v:normal"    – glm::vec3  optional surface / point-cloud normal
    //
    // Domain: Nodes (graph node)
    //   "v:position"  – glm::vec3  same canonical key as above (node pos)
    //   "v:normal"    – glm::vec3  optional, defaults to world-up on graphs
    //
    // Domain: Edges (mesh or graph)
    //   "e:v0"        – uint32_t   index of first  endpoint vertex / node
    //   "e:v1"        – uint32_t   index of second endpoint vertex / node
    //
    // Domain: Halfedges (mesh only)
    //   "h:to_vertex" – uint32_t   index of the halfedge's target vertex
    //   "h:next"      – uint32_t   index of the next halfedge around its face
    //   "h:face"      – uint32_t   index of the adjacent face (UINT32_MAX = boundary)
    //
    // Domain: Faces (mesh only)
    //   "f:halfedge"  – uint32_t   index of the face's first halfedge
    // -----------------------------------------------------------------------
    namespace PropertyNames
    {
        constexpr std::string_view kPosition        = "v:position";
        constexpr std::string_view kNormal          = "v:normal";

        constexpr std::string_view kEdgeV0          = "e:v0";
        constexpr std::string_view kEdgeV1          = "e:v1";

        constexpr std::string_view kHalfedgeToVertex = "h:to_vertex";
        constexpr std::string_view kHalfedgeNext     = "h:next";
        constexpr std::string_view kHalfedgeFace     = "h:face";

        constexpr std::string_view kFaceHalfedge    = "f:halfedge";
    }

    // Owned per-domain PropertySet components. After a `PopulateFrom*` call
    // (see `Extrinsic.ECS.Components.GeometrySourcesPopulate`) the entity is
    // the CPU authority for its geometry data; the source mesh/graph/cloud
    // object can be discarded without invalidating the ECS view.
    struct Vertices
    {
        Geometry::PropertySet Properties{};
        std::size_t NumDeleted{0};
    };

    struct Edges
    {
        Geometry::PropertySet Properties{};
        std::size_t NumDeleted{0};
    };

    struct Halfedges
    {
        Geometry::PropertySet Properties{};
    };

    struct Faces
    {
        Geometry::PropertySet Properties{};
        std::size_t NumDeleted{0};
    };

    struct Nodes
    {
        Geometry::PropertySet Properties{};
        std::size_t NumDeleted{0};
    };

    // Topology markers — let callers declare "this entity is a mesh / graph"
    // even when not every per-domain `PropertySet` has been emplaced (e.g.,
    // `PopulateFromGraph` populates `Nodes`+`Edges` without halfedges).
    struct HasMeshTopology
    {
    };

    struct HasGraphTopology
    {
    };

    enum class Domain : std::uint8_t
    {
        None,
        Mesh,
        Graph,
        PointCloud,
        Unknown
    };

    [[nodiscard]] inline std::size_t AliveCount(std::size_t total, std::size_t deleted) noexcept
    {
        return total > deleted ? (total - deleted) : 0;
    }

    [[nodiscard]] inline std::size_t VertexCount(const Vertices& source) noexcept
    {
        return AliveCount(source.Properties.Size(), source.NumDeleted);
    }

    [[nodiscard]] inline std::size_t EdgeCount(const Edges& source) noexcept
    {
        return AliveCount(source.Properties.Size(), source.NumDeleted);
    }

    [[nodiscard]] inline std::size_t HalfedgeCount(const Halfedges& source) noexcept
    {
        return source.Properties.Size();
    }

    [[nodiscard]] inline std::size_t FaceCount(const Faces& source) noexcept
    {
        return AliveCount(source.Properties.Size(), source.NumDeleted);
    }

    [[nodiscard]] inline std::size_t NodeCount(const Nodes& source) noexcept
    {
        return AliveCount(source.Properties.Size(), source.NumDeleted);
    }

    struct ConstSourceView
    {
        Domain ActiveDomain{Domain::None};

        const Vertices* VertexSource{nullptr};
        const Edges* EdgeSource{nullptr};
        const Halfedges* HalfedgeSource{nullptr};
        const Faces* FaceSource{nullptr};
        const Nodes* NodeSource{nullptr};

        [[nodiscard]] bool Valid() const noexcept
        {
            return ActiveDomain != Domain::None && ActiveDomain != Domain::Unknown;
        }

        [[nodiscard]] std::size_t VerticesAlive() const noexcept
        {
            return VertexSource ? VertexCount(*VertexSource) : 0;
        }

        [[nodiscard]] std::size_t EdgesAlive() const noexcept
        {
            return EdgeSource ? EdgeCount(*EdgeSource) : 0;
        }

        [[nodiscard]] std::size_t HalfedgesTotal() const noexcept
        {
            return HalfedgeSource ? HalfedgeCount(*HalfedgeSource) : 0;
        }

        [[nodiscard]] std::size_t FacesAlive() const noexcept
        {
            return FaceSource ? FaceCount(*FaceSource) : 0;
        }

        [[nodiscard]] std::size_t NodesAlive() const noexcept
        {
            return NodeSource ? NodeCount(*NodeSource) : 0;
        }
    };

    struct MutableSourceView
    {
        Domain ActiveDomain{Domain::None};

        Vertices* VertexSource{nullptr};
        Edges* EdgeSource{nullptr};
        Halfedges* HalfedgeSource{nullptr};
        Faces* FaceSource{nullptr};
        Nodes* NodeSource{nullptr};

        [[nodiscard]] bool Valid() const noexcept
        {
            return ActiveDomain != Domain::None && ActiveDomain != Domain::Unknown;
        }
    };

    [[nodiscard]] Domain DetectDomain(bool hasVertices,
                                      bool hasEdges,
                                      bool hasHalfedges,
                                      bool hasFaces,
                                      bool hasNodes) noexcept;

    [[nodiscard]] ConstSourceView BuildConstView(const entt::registry& registry,
                                                 entt::entity entity);

    [[nodiscard]] MutableSourceView BuildMutableView(entt::registry& registry,
                                                     entt::entity entity);
}
