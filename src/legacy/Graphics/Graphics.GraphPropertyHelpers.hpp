#pragma once

// =============================================================================
// GraphPropertyHelpers — shared graph property extraction utilities.
//
// Two flavours:
//
//   Graph-backed  (ExtractNodeColors / ExtractEdgeColors / ExtractNodeRadii)
//     Take a const Graph& for skip-deleted filtering.  Used when the calling
//     code still holds a Graph object (e.g. legacy GraphRef path, algorithms).
//
//   PropertySet-backed  (ExtractNodeColorsFromPropertySet / …)
//     Take a const PropertySet& directly.  Used when reading from authoritative
//     GeometrySources components, which are already compacted (no deleted gaps).
// =============================================================================

namespace Graphics::GraphPropertyHelpers
{
    // -------------------------------------------------------------------------
    // Graph-backed helpers (require a Graph for is-deleted check)
    // -------------------------------------------------------------------------

    // Extract per-element colors via ColorMapper with default property fallback.
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

    [[nodiscard]] inline std::vector<uint32_t> ExtractNodeColors(
        const Geometry::Graph::Graph& graph,
        Graphics::ColorSource& config)
    {
        return ExtractColors<Geometry::VertexHandle>(
            graph.VertexProperties(), config, graph, "v:color");
    }

    [[nodiscard]] inline std::vector<uint32_t> ExtractEdgeColors(
        const Geometry::Graph::Graph& graph,
        Graphics::ColorSource& config)
    {
        return ExtractColors<Geometry::EdgeHandle>(
            graph.EdgeProperties(), config, graph, "e:color");
    }

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

    // -------------------------------------------------------------------------
    // PropertySet-backed helpers (compacted GeometrySources — no deleted gaps)
    // -------------------------------------------------------------------------

    [[nodiscard]] inline std::vector<uint32_t> ExtractNodeColorsFromPropertySet(
        const Geometry::PropertySet& properties,
        Graphics::ColorSource& config)
    {
        if (config.PropertyName.empty() && properties.Exists("v:color"))
            config.PropertyName = "v:color";

        if (auto mapped = ColorMapper::MapProperty(properties, config))
            return std::move(mapped->Colors);

        return {};
    }

    [[nodiscard]] inline std::vector<uint32_t> ExtractEdgeColorsFromPropertySet(
        const Geometry::PropertySet& properties,
        Graphics::ColorSource& config)
    {
        if (config.PropertyName.empty() && properties.Exists("e:color"))
            config.PropertyName = "e:color";

        if (auto mapped = ColorMapper::MapProperty(properties, config))
            return std::move(mapped->Colors);

        return {};
    }

    [[nodiscard]] inline std::vector<float> ExtractNodeRadiiFromPropertySet(
        const Geometry::PropertySet& properties)
    {
        if (!properties.Exists("v:radius"))
            return {};

        auto radiusProp = properties.Get<float>("v:radius");
        if (!radiusProp)
            return {};

        return std::vector<float>(radiusProp.Vector().begin(), radiusProp.Vector().end());
    }
}
