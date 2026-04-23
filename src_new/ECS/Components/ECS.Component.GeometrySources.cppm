module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Components.GeometrySources;

import Geometry.Properties;

export namespace Extrinsic::ECS::Components::GeometrySources
{
    struct Vertices
    {
        Geometry::PropertySet* PropertiesPtr{};
        size_t NumDeleted{0};
    };

    struct Edges
    {
        Geometry::PropertySet* PropertiesPtr{};
        size_t NumDeleted{0};
    };

    struct Halfedges
    {
        Geometry::PropertySet* PropertiesPtr{};
    };

    struct Faces
    {
        Geometry::PropertySet* PropertiesPtr{};
        size_t NumDeleted{0};
    };

    struct Nodes
    {
        Geometry::PropertySet* PropertiesPtr{};
        size_t NumDeleted{0};
    };

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
        return source.PropertiesPtr
            ? AliveCount(source.PropertiesPtr->Size(), source.NumDeleted)
            : 0;
    }

    [[nodiscard]] inline std::size_t EdgeCount(const Edges& source) noexcept
    {
        return source.PropertiesPtr
            ? AliveCount(source.PropertiesPtr->Size(), source.NumDeleted)
            : 0;
    }

    [[nodiscard]] inline std::size_t HalfedgeCount(const Halfedges& source) noexcept
    {
        return source.PropertiesPtr ? source.PropertiesPtr->Size() : 0;
    }

    [[nodiscard]] inline std::size_t FaceCount(const Faces& source) noexcept
    {
        return source.PropertiesPtr
            ? AliveCount(source.PropertiesPtr->Size(), source.NumDeleted)
            : 0;
    }

    [[nodiscard]] inline std::size_t NodeCount(const Nodes& source) noexcept
    {
        return source.PropertiesPtr
            ? AliveCount(source.PropertiesPtr->Size(), source.NumDeleted)
            : 0;
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

    [[nodiscard]] inline Domain DetectDomain(bool hasVertices,
                                             bool hasEdges,
                                             bool hasHalfedges,
                                             bool hasFaces,
                                             bool hasNodes) noexcept
    {
        if (hasVertices && hasEdges && hasHalfedges && hasFaces && !hasNodes)
            return Domain::Mesh;

        if (!hasVertices && hasEdges && hasHalfedges && !hasFaces && hasNodes)
            return Domain::Graph;

        if (hasVertices && !hasEdges && !hasHalfedges && !hasFaces && !hasNodes)
            return Domain::PointCloud;

        if (!hasVertices && !hasEdges && !hasHalfedges && !hasFaces && !hasNodes)
            return Domain::None;

        return Domain::Unknown;
    }

    [[nodiscard]] inline ConstSourceView BuildConstView(const entt::registry& registry,
                                                        entt::entity entity)
    {
        ConstSourceView view{};
        view.VertexSource = registry.try_get<Vertices>(entity);
        view.EdgeSource = registry.try_get<Edges>(entity);
        view.HalfedgeSource = registry.try_get<Halfedges>(entity);
        view.FaceSource = registry.try_get<Faces>(entity);
        view.NodeSource = registry.try_get<Nodes>(entity);

        const bool hasMeshTopology = registry.all_of<HasMeshTopology>(entity);
        const bool hasGraphTopology = registry.all_of<HasGraphTopology>(entity);

        view.ActiveDomain = DetectDomain(
            view.VertexSource != nullptr,
            view.EdgeSource != nullptr || hasGraphTopology,
            view.HalfedgeSource != nullptr || hasGraphTopology,
            view.FaceSource != nullptr || hasMeshTopology,
            view.NodeSource != nullptr);

        return view;
    }

    [[nodiscard]] inline MutableSourceView BuildMutableView(entt::registry& registry,
                                                            entt::entity entity)
    {
        MutableSourceView view{};
        view.VertexSource = registry.try_get<Vertices>(entity);
        view.EdgeSource = registry.try_get<Edges>(entity);
        view.HalfedgeSource = registry.try_get<Halfedges>(entity);
        view.FaceSource = registry.try_get<Faces>(entity);
        view.NodeSource = registry.try_get<Nodes>(entity);

        const bool hasMeshTopology = registry.all_of<HasMeshTopology>(entity);
        const bool hasGraphTopology = registry.all_of<HasGraphTopology>(entity);

        view.ActiveDomain = DetectDomain(
            view.VertexSource != nullptr,
            view.EdgeSource != nullptr || hasGraphTopology,
            view.HalfedgeSource != nullptr || hasGraphTopology,
            view.FaceSource != nullptr || hasMeshTopology,
            view.NodeSource != nullptr);

        return view;
    }
}
