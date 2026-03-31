module;

#include <algorithm>
#include <cstddef>
#include <utility>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics.VectorFieldManager;

import Graphics.Components;
import Graphics.OverlayEntityFactory;
import Graphics.VisualizationConfig;
import Graphics.ColorMapper;
import Graphics.GpuColor;

import Geometry.Graph;
import Geometry.Properties;
import ECS;

using namespace Graphics;

namespace
{
    using Domain = Graphics::VectorFieldDomain;

    [[nodiscard]] const char* DomainSuffix(Domain domain) noexcept
    {
        switch (domain)
        {
        case Domain::Vertex: return "V";
        case Domain::Edge:   return "E";
        case Domain::Face:   return "F";
        }
        return "V";
    }

    /// Build a child Graph from base positions + explicit endpoints.
    /// Layout:
    ///   - vertices [0, N)   = base points
    ///   - vertices [N, 2N)  = baked endpoints
    ///   - edges    [0, N)   = base[i] → endpoint[i]
    [[nodiscard]] std::shared_ptr<Geometry::Graph::Graph> BuildVectorFieldGraph(
        std::span<const glm::vec3> positions,
        const Geometry::PropertySet& domainProps,
        const VectorFieldEntry& entry)
    {
        const auto vectorVecProp = domainProps.Get<glm::vec3>(entry.VectorPropertyName);
        if (!vectorVecProp.IsValid())
            return nullptr;

        const auto& vectors = vectorVecProp.Vector();
        std::span<const glm::vec3> bases = positions;
        std::vector<glm::vec3> baseScratch;
        if (!entry.BasePropertyName.empty())
        {
            auto baseVecProp = domainProps.Get<glm::vec3>(entry.BasePropertyName);
            if (!baseVecProp.IsValid())
                return nullptr;
            const auto& baseValues = baseVecProp.Vector();
            baseScratch.assign(baseValues.begin(), baseValues.end());
            bases = baseScratch;
        }

        const size_t N = std::min(bases.size(), vectors.size());
        if (N == 0)
            return nullptr;

        // Optional per-vector length scaling from a scalar property.
        const float* lengthScales = nullptr;
        if (!entry.LengthPropertyName.empty())
        {
            auto lenProp = domainProps.Get<float>(entry.LengthPropertyName);
            if (lenProp.IsValid() && lenProp.Vector().size() >= N)
            {
                lengthScales = lenProp.Vector().data();
            }
        }

        auto graph = std::make_shared<Geometry::Graph::Graph>();

        graph->Reserve(N * 2u, N);

        // Add base points and baked endpoints.
        std::vector<Geometry::VertexHandle> baseVerts(N);
        std::vector<Geometry::VertexHandle> targetVerts(N);

        for (size_t i = 0; i < N; ++i)
        {
            const glm::vec3 base = bases[i];
            const float scale = entry.Scale * (lengthScales ? lengthScales[i] : 1.0f);
            const glm::vec3 end = base + vectors[i] * scale;

            baseVerts[i] = graph->AddVertex(base);
            targetVerts[i] = graph->AddVertex(end);
        }

        // Add edges from base to duplicate.
        for (size_t i = 0; i < N; ++i)
        {
            (void)graph->AddEdge(baseVerts[i], targetVerts[i]);
        }

        return graph;
    }

    [[nodiscard]] bool HasOverlayContract(entt::registry& registry, entt::entity entity) noexcept
    {
        return registry.valid(entity)
            && registry.all_of<ECS::Components::Transform::Component,
                               ECS::Components::Transform::WorldMatrix,
                               ECS::Components::Hierarchy::Component,
                               ECS::Graph::Data>(entity);
    }

    void DestroyOverlayIfValid(entt::registry& registry, entt::entity entity) noexcept
    {
        if (entity != entt::null && registry.valid(entity))
            OverlayEntityFactory::DestroyOverlay(registry, entity);
    }
} // namespace

void VectorFieldManager::SyncVectorFields(
    entt::registry& registry,
    entt::entity sourceEntity,
    std::span<const glm::vec3> positions,
    Domain domain,
    const Geometry::PropertySet& domainProps,
    VisualizationConfig& config,
    const std::string& sourceName)
{
    for (auto& entry : config.VectorFields)
    {
        if (entry.Domain != domain)
            continue;

        if (entry.VectorPropertyName.empty())
        {
            // No property selected — destroy child if it exists.
            DestroyOverlayIfValid(registry, entry.ChildEntity);
            entry.ChildEntity = entt::null;
            continue;
        }

        // Build the graph.
        auto graph = BuildVectorFieldGraph(positions, domainProps, entry);
        if (!graph)
        {
            // Property not found or empty — destroy child.
            DestroyOverlayIfValid(registry, entry.ChildEntity);
            entry.ChildEntity = entt::null;
            continue;
        }

        const size_t edgeCount = graph->EdgeCount();

        const std::string childName = sourceName + " [VF " + std::string(DomainSuffix(domain)) + ": " + entry.VectorPropertyName + "]";

        // Create or update child entity.
        if (!HasOverlayContract(registry, entry.ChildEntity))
        {
            DestroyOverlayIfValid(registry, entry.ChildEntity);
            entry.ChildEntity = OverlayEntityFactory::CreateGraphOverlay(
                registry, sourceEntity, std::move(graph), childName);

            if (entry.ChildEntity == entt::null || !registry.valid(entry.ChildEntity))
                continue;
        }
        else
        {
            auto& graphData = registry.get<ECS::Graph::Data>(entry.ChildEntity);
            graphData.GraphRef = std::move(graph);
            graphData.GpuDirty = true;
            graphData.VectorFieldMode = true;
        }

        // Per-vector colors: map a source property to per-edge colors on the child graph.
        bool hasPerVectorColors = false;
        std::vector<uint32_t> perEdgeColors;
        if (!entry.ArrowColor.PropertyName.empty())
        {
            auto colorConfig = entry.ArrowColor;
            auto mapped = ColorMapper::MapProperty(domainProps, colorConfig);
            if (mapped && mapped->Colors.size() >= edgeCount)
            {
                // Each edge i corresponds to base vertex i.
                perEdgeColors.resize(edgeCount);
                for (size_t i = 0; i < edgeCount; ++i)
                    perEdgeColors[i] = mapped->Colors[i];
                hasPerVectorColors = true;
            }
        }

        // Set up Graph::Data on the child entity.
        auto& graphData = registry.get_or_emplace<ECS::Graph::Data>(entry.ChildEntity);
        graphData.DefaultEdgeColor = entry.Color;
        graphData.DefaultNodeColor = entry.Color;
        graphData.DefaultNodeRadius = 0.003f;
        graphData.EdgeWidth = entry.EdgeWidth;
        graphData.EdgesOverlay = entry.Overlay;
        graphData.VectorFieldMode = true;
        graphData.Visible = true;
        graphData.GpuDirty = true;

        // Apply per-edge colors if available.
        if (hasPerVectorColors)
        {
            graphData.CachedEdgeColors = std::move(perEdgeColors);
        }
        else
        {
            graphData.CachedEdgeColors.clear();
        }

        if (auto* line = registry.try_get<ECS::Line::Component>(entry.ChildEntity))
        {
            line->ShowPerEdgeColors = hasPerVectorColors;
        }
    }
}

void VectorFieldManager::DestroyAllVectorFields(
    entt::registry& registry,
    VisualizationConfig& config)
{
    for (auto& entry : config.VectorFields)
    {
        DestroyOverlayIfValid(registry, entry.ChildEntity);
        entry.ChildEntity = entt::null;
    }
}
