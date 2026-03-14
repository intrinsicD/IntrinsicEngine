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
    template <typename HandleT>
    [[nodiscard]] inline std::vector<uint32_t> ExtractColors(
        const Geometry::PropertySet& properties,
        VisualizationConfig::PropertyColorConfig& config,
        const Geometry::Graph& graph,
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
        const Geometry::Graph& graph,
        VisualizationConfig::PropertyColorConfig& config)
    {
        return ExtractColors<Geometry::VertexHandle>(
            graph.VertexProperties(), config, graph, "v:color");
    }

    // Extract per-edge colors (convenience wrapper).
    [[nodiscard]] inline std::vector<uint32_t> ExtractEdgeColors(
        const Geometry::Graph& graph,
        VisualizationConfig::PropertyColorConfig& config)
    {
        return ExtractColors<Geometry::EdgeHandle>(
            graph.EdgeProperties(), config, graph, "e:color");
    }

    // Extract per-node radii from "v:radius" property, skipping deleted vertices.
    [[nodiscard]] inline std::vector<float> ExtractNodeRadii(
        const Geometry::Graph& graph)
    {
        std::vector<float> radii;
        if (!graph.VertexProperties().Exists("v:radius"))
            return radii;

        auto radiusProp = Geometry::VertexProperty<float>(
            graph.VertexProperties().Get<float>("v:radius"));

        radii.reserve(graph.VertexCount());

        const std::size_t vSize = graph.VerticesSize();
        for (std::size_t i = 0; i < vSize; ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (graph.IsDeleted(v))
                continue;

            radii.push_back(radiusProp[v]);
        }

        return radii;
    }
}
