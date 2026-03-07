module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:VectorFieldManager.Impl;

import :VectorFieldManager;
import :Components;
import :VisualizationConfig;
import :ColorMapper;
import :GpuColor;

import Geometry;
import ECS;

using namespace Graphics;

namespace
{
    /// Build a Graph from base positions + vec3 offsets.
    /// Nodes: 0..N-1 = base, N..2N-1 = target.
    /// Edges: base[i] → target[i].
    /// Supports per-vector length scaling via a scalar property.
    [[nodiscard]] std::shared_ptr<Geometry::Graph::Graph> BuildVectorFieldGraph(
        std::span<const glm::vec3> positions,
        const Geometry::PropertySet& vertexProps,
        const VectorFieldEntry& entry)
    {
        auto vecProp = vertexProps.Get<glm::vec3>(entry.PropertyName);
        if (!vecProp.IsValid())
            return nullptr;

        const auto& vectors = vecProp.Vector();
        const size_t N = std::min(positions.size(), vectors.size());
        if (N == 0)
            return nullptr;

        // Optional per-vector length scaling from a scalar property.
        const float* lengthScales = nullptr;
        std::vector<float> lengthBuf;
        if (!entry.LengthPropertyName.empty())
        {
            auto lenProp = vertexProps.Get<float>(entry.LengthPropertyName);
            if (lenProp.IsValid() && lenProp.Vector().size() >= N)
            {
                lengthScales = lenProp.Vector().data();
            }
        }

        auto graph = std::make_shared<Geometry::Graph::Graph>();

        // Add base points and target points.
        std::vector<Geometry::VertexHandle> baseVerts(N);
        std::vector<Geometry::VertexHandle> targetVerts(N);

        for (size_t i = 0; i < N; ++i)
        {
            const float scale = entry.Scale * (lengthScales ? lengthScales[i] : 1.0f);
            baseVerts[i] = graph->AddVertex(positions[i]);
            targetVerts[i] = graph->AddVertex(positions[i] + vectors[i] * scale);
        }

        // Add edges from base to target.
        for (size_t i = 0; i < N; ++i)
        {
            (void)graph->AddEdge(baseVerts[i], targetVerts[i]);
        }

        return graph;
    }
} // namespace

void VectorFieldManager::SyncVectorFields(
    entt::registry& registry,
    entt::entity sourceEntity,
    std::span<const glm::vec3> positions,
    const Geometry::PropertySet& vertexProps,
    VisualizationConfig& config,
    const std::string& sourceName)
{
    for (auto& entry : config.VectorFields)
    {
        if (entry.PropertyName.empty())
        {
            // No property selected — destroy child if it exists.
            if (entry.ChildEntity != entt::null && registry.valid(entry.ChildEntity))
            {
                registry.destroy(entry.ChildEntity);
            }
            entry.ChildEntity = entt::null;
            continue;
        }

        // Build the graph.
        auto graph = BuildVectorFieldGraph(positions, vertexProps, entry);
        if (!graph)
        {
            // Property not found or empty — destroy child.
            if (entry.ChildEntity != entt::null && registry.valid(entry.ChildEntity))
            {
                registry.destroy(entry.ChildEntity);
            }
            entry.ChildEntity = entt::null;
            continue;
        }

        // Create or update child entity.
        if (entry.ChildEntity == entt::null || !registry.valid(entry.ChildEntity))
        {
            // Create new child entity.
            entry.ChildEntity = registry.create();

            std::string childName = sourceName + " [VF: " + entry.PropertyName + "]";
            registry.emplace<ECS::Components::NameTag::Component>(
                entry.ChildEntity, ECS::Components::NameTag::Component{std::move(childName)});

            // Attach as child via hierarchy.
            ECS::Components::Hierarchy::Attach(registry, entry.ChildEntity, sourceEntity);
        }

        // Per-vector colors: map a source property to per-edge colors on the child graph.
        bool hasPerVectorColors = false;
        std::vector<uint32_t> perEdgeColors;
        if (!entry.ColorPropertyName.empty())
        {
            ColorSource colorConfig;
            colorConfig.PropertyName = entry.ColorPropertyName;
            colorConfig.AutoRange = true;

            auto mapped = ColorMapper::MapProperty(vertexProps, colorConfig);
            if (mapped && mapped->Colors.size() >= static_cast<size_t>(graph->EdgeCount()))
            {
                // Each edge i corresponds to base vertex i. Use base vertex color.
                const size_t edgeCount = graph->EdgeCount();
                perEdgeColors.resize(edgeCount);
                for (size_t i = 0; i < edgeCount; ++i)
                    perEdgeColors[i] = mapped->Colors[i];
                hasPerVectorColors = true;
            }
        }

        // Set up Graph::Data on the child entity.
        auto& graphData = registry.get_or_emplace<ECS::Graph::Data>(entry.ChildEntity);
        graphData.GraphRef = std::move(graph);
        graphData.DefaultEdgeColor = entry.Color;
        graphData.DefaultNodeColor = entry.Color;
        graphData.DefaultNodeRadius = 0.003f;
        graphData.EdgeWidth = entry.EdgeWidth;
        graphData.EdgesOverlay = entry.Overlay;
        graphData.Visible = true;
        graphData.GpuDirty = true;

        // Apply per-edge colors if available.
        if (hasPerVectorColors)
        {
            graphData.CachedEdgeColors = std::move(perEdgeColors);
            graphData.ShowPerEdgeColors = true;
        }
        else
        {
            graphData.CachedEdgeColors.clear();
            graphData.ShowPerEdgeColors = false;
        }
    }
}

void VectorFieldManager::DestroyAllVectorFields(
    entt::registry& registry,
    VisualizationConfig& config)
{
    for (auto& entry : config.VectorFields)
    {
        if (entry.ChildEntity != entt::null && registry.valid(entry.ChildEntity))
        {
            registry.destroy(entry.ChildEntity);
        }
        entry.ChildEntity = entt::null;
    }
}
