#pragma once

// =============================================================================
// GraphPropertyHelpers — shared graph property extraction utilities.
//
// Consolidates duplicated property extraction logic between
// GraphGeometrySync (full upload) and PropertySetDirtySync (incremental sync).
// This header is intended for inclusion in graph-related system .cpp files only.
// =============================================================================

namespace Graphics::GraphPropertyHelpers
{
    // Extract per-element colors via ColorMapper with default property fallback.
    // Sets config.PropertyName to defaultPropName if empty and the property exists.
    // Returns the extracted color vector (empty if no property found).
    template <typename HandleT, class PropertySetT>
    [[nodiscard]] inline std::vector<uint32_t> ExtractColors(
        const PropertySetT& properties,
        Graphics::ColorSource& config,
        const Geometry::Graph::Graph& graph,
        std::string_view defaultPropName)
    {
        if (config.PropertyName.empty() && properties.Exists(defaultPropName))
            config.PropertyName = std::string(defaultPropName);

        auto skipDeleted = [&graph](size_t i) -> bool {
            return graph.IsDeleted(HandleT{static_cast<Geometry::PropertyIndex>(i)});
        };

        if (auto mapped = ColorMapper::MapProperty(properties, config, skipDeleted))
            return std::move(mapped->Colors);

        return {};
    }

    // Extract per-node colors (convenience wrapper).
    [[nodiscard]] inline std::vector<uint32_t> ExtractNodeColors(
        const Geometry::Graph::Graph& graph,
        Graphics::ColorSource& config)
    {
        return ExtractColors<Geometry::VertexHandle>(
            graph.VertexProperties(), config, graph, "v:color");
    }

    // Extract per-edge colors (convenience wrapper).
    [[nodiscard]] inline std::vector<uint32_t> ExtractEdgeColors(
        const Geometry::Graph::Graph& graph,
        Graphics::ColorSource& config)
    {
        return ExtractColors<Geometry::EdgeHandle>(
            graph.EdgeProperties(), config, graph, "e:color");
    }

    // Extract per-node radii from "v:radius" property, skipping deleted vertices.
    [[nodiscard]] inline std::vector<float> ExtractNodeRadii(
        const Geometry::Graph::Graph& graph)
    {
        std::vector<float> radii;
        const auto properties = graph.VertexProperties();
        if (!properties.Exists("v:radius"))
            return radii;

        const auto radiusProp = properties.Get<float>("v:radius");
        if (!radiusProp)
            return radii;

        radii.reserve(graph.VertexCount());

        const std::size_t vSize = graph.VerticesSize();
        for (std::size_t i = 0; i < vSize; ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (graph.IsDeleted(v))
                continue;

            radii.push_back(radiusProp[static_cast<std::size_t>(i)]);
        }

        return radii;
    }
}
