module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.GeometryProcessingOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

namespace Extrinsic::Runtime
{
namespace GS = ECS::Components::GeometrySources;

// Private to this discovery unit: these classify domains and algorithms for the
// menu surface and are deliberately not in the shared mesh helper namespace,
// which every family compiles against.
namespace GeometryProcessingDetail::MeshDiscovery
{
        constexpr EditorGeometryProcessingDomain kMeshTopologyDomains =
            EditorGeometryProcessingDomain::MeshVertices |
            EditorGeometryProcessingDomain::MeshEdges |
            EditorGeometryProcessingDomain::MeshHalfedges |
            EditorGeometryProcessingDomain::MeshFaces;

        [[nodiscard]] constexpr bool IsSurfaceTopologyAlgorithm(
            const EditorGeometryProcessingAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
            case EditorGeometryProcessingAlgorithm::Geodesics:
            case EditorGeometryProcessingAlgorithm::MeshDenoise:
            case EditorGeometryProcessingAlgorithm::Curvature:
            case EditorGeometryProcessingAlgorithm::CurvatureSegmentation:
            case EditorGeometryProcessingAlgorithm::Remeshing:
            case EditorGeometryProcessingAlgorithm::Simplification:
            case EditorGeometryProcessingAlgorithm::Smoothing:
            case EditorGeometryProcessingAlgorithm::Subdivision:
            case EditorGeometryProcessingAlgorithm::Repair:
                return true;
            case EditorGeometryProcessingAlgorithm::KMeans:
            case EditorGeometryProcessingAlgorithm::NormalEstimation:
            case EditorGeometryProcessingAlgorithm::ShortestPath:
            case EditorGeometryProcessingAlgorithm::ConvexHull:
            case EditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            case EditorGeometryProcessingAlgorithm::KnnGraphConstruction:
            case EditorGeometryProcessingAlgorithm::VectorHeat:
            case EditorGeometryProcessingAlgorithm::Parameterization:
            case EditorGeometryProcessingAlgorithm::BooleanCSG:
            case EditorGeometryProcessingAlgorithm::Registration:
            case EditorGeometryProcessingAlgorithm::BilateralFilter:
            case EditorGeometryProcessingAlgorithm::OutlierEstimation:
            case EditorGeometryProcessingAlgorithm::KernelDensity:
            case EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            case EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            case EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
                return false;
            }
            return false;
        }

        [[nodiscard]] EditorGeometryProcessingDomain
        DomainsForSourceView(const GS::ConstSourceView& view) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            EditorGeometryProcessingDomain domains =
                EditorGeometryProcessingDomain::None;

            if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshVertex))
                    domains |= EditorGeometryProcessingDomain::MeshVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshEdge))
                    domains |= EditorGeometryProcessingDomain::MeshEdges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshHalfedge))
                    domains |= EditorGeometryProcessingDomain::MeshHalfedges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshFace))
                    domains |= EditorGeometryProcessingDomain::MeshFaces;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::Graph)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphNode))
                    domains |= EditorGeometryProcessingDomain::GraphVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphEdge))
                    domains |= EditorGeometryProcessingDomain::GraphEdges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphHalfedge))
                    domains |= EditorGeometryProcessingDomain::GraphHalfedges;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::PointCloudPoint))
                    domains |= EditorGeometryProcessingDomain::PointCloudPoints;
            }

            return domains;
        }
} // namespace GeometryProcessingDetail::MeshDiscovery
using namespace GeometryProcessingDetail::MeshDiscovery;

    std::vector<EditorGeometryProcessingMenuItem>
GetEditorGeometryProcessingMenuItems(
        const EditorDomainWindowKind kind)
    {
        using Domain = EditorGeometryProcessingDomain;
        switch (kind)
        {
        case EditorDomainWindowKind::Mesh:
            return {
                {.Domain = Domain::MeshVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true,
                 .HasDenoiseMethod = true,
                 .HasCurvatureMethod = true,
                 .HasRemeshMethod = true,
                 .HasSubdivideMethod = true,
                 .HasSimplifyMethod = true},
                {.Domain = Domain::MeshEdges, .Label = "Edges"},
                {.Domain = Domain::MeshFaces, .Label = "Faces"},
            };
        case EditorDomainWindowKind::Graph:
            return {
                {.Domain = Domain::GraphVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
                {.Domain = Domain::GraphEdges, .Label = "Edges"},
                {.Domain = Domain::GraphHalfedges, .Label = "Halfedges"},
            };
        case EditorDomainWindowKind::PointCloud:
            return {
                {.Domain = Domain::PointCloudPoints,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
            };
        }
        return {};
    }

    EditorGeometryProcessingDomain GetEditorSupportedGeometryProcessingDomains(
        const EditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        using Domain = EditorGeometryProcessingDomain;
        switch (algorithm)
        {
        case EditorGeometryProcessingAlgorithm::Geodesics:
            return Domain::MeshVertices;
        case EditorGeometryProcessingAlgorithm::KMeans:
        case EditorGeometryProcessingAlgorithm::NormalEstimation:
        case EditorGeometryProcessingAlgorithm::Registration:
        case EditorGeometryProcessingAlgorithm::BilateralFilter:
        case EditorGeometryProcessingAlgorithm::OutlierEstimation:
        case EditorGeometryProcessingAlgorithm::KernelDensity:
        case EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return Domain::MeshVertices | Domain::MeshEdges | Domain::MeshHalfedges | Domain::MeshFaces |
                   Domain::GraphVertices | Domain::GraphEdges | Domain::GraphHalfedges | Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::MeshDenoise:
        case EditorGeometryProcessingAlgorithm::Curvature:
            return Domain::MeshVertices;
        case EditorGeometryProcessingAlgorithm::CurvatureSegmentation:
            return kMeshTopologyDomains;
        case EditorGeometryProcessingAlgorithm::Remeshing:
        case EditorGeometryProcessingAlgorithm::Simplification:
        case EditorGeometryProcessingAlgorithm::Smoothing:
        case EditorGeometryProcessingAlgorithm::Subdivision:
        case EditorGeometryProcessingAlgorithm::Repair:
            return kMeshTopologyDomains;
        case EditorGeometryProcessingAlgorithm::ShortestPath:
            return Domain::MeshVertices | Domain::GraphVertices;
        case EditorGeometryProcessingAlgorithm::ConvexHull:
            return Domain::MeshVertices | Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::SurfaceReconstruction:
        case EditorGeometryProcessingAlgorithm::KnnGraphConstruction:
            return Domain::MeshVertices | Domain::MeshEdges | Domain::MeshHalfedges | Domain::MeshFaces |
                   Domain::GraphVertices | Domain::GraphEdges | Domain::GraphHalfedges | Domain::PointCloudPoints;
        case EditorGeometryProcessingAlgorithm::VectorHeat:
            return Domain::MeshVertices;
        case EditorGeometryProcessingAlgorithm::Parameterization:
            return Domain::MeshVertices | Domain::MeshFaces;
        case EditorGeometryProcessingAlgorithm::BooleanCSG:
            return Domain::MeshVertices | Domain::MeshFaces;
        case EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
        case EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return Domain::PointCloudPoints;
        }
        return Domain::None;
    }

    bool SupportsEditorGeometryProcessingDomain(
        const EditorGeometryProcessingAlgorithm algorithm,
        const EditorGeometryProcessingDomain domain) noexcept
    {
        return HasAnyEditorGeometryProcessingDomain(
      GetEditorSupportedGeometryProcessingDomains(algorithm),
            domain);
    }

    EditorGeometryProcessingCapabilities
GetEditorGeometryProcessingCapabilities(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        EditorGeometryProcessingCapabilities capabilities{};
        const entt::registry& raw = registry.Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return capabilities;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        capabilities.Domains = DomainsForSourceView(view);
        capabilities.HasEditableSurfaceMesh =
            availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshVertex) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshHalfedge) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshFace);
        return capabilities;
    }

    std::vector<EditorGeometryProcessingEntry>
ResolveEditorGeometryProcessingEntries(
        const EditorGeometryProcessingCapabilities capabilities)
    {
        static constexpr std::array<EditorGeometryProcessingAlgorithm, 25> kAlgorithmOrder{
            EditorGeometryProcessingAlgorithm::KMeans,
            EditorGeometryProcessingAlgorithm::NormalEstimation,
            EditorGeometryProcessingAlgorithm::MeshDenoise,
            EditorGeometryProcessingAlgorithm::Curvature,
            EditorGeometryProcessingAlgorithm::CurvatureSegmentation,
            EditorGeometryProcessingAlgorithm::Registration,
            EditorGeometryProcessingAlgorithm::BilateralFilter,
            EditorGeometryProcessingAlgorithm::OutlierEstimation,
            EditorGeometryProcessingAlgorithm::KernelDensity,
            EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling,
            EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval,
            EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval,
            EditorGeometryProcessingAlgorithm::ShortestPath,
            EditorGeometryProcessingAlgorithm::VectorHeat,
            EditorGeometryProcessingAlgorithm::Parameterization,
            EditorGeometryProcessingAlgorithm::ConvexHull,
            EditorGeometryProcessingAlgorithm::SurfaceReconstruction,
            EditorGeometryProcessingAlgorithm::KnnGraphConstruction,
            EditorGeometryProcessingAlgorithm::BooleanCSG,
            EditorGeometryProcessingAlgorithm::Remeshing,
            EditorGeometryProcessingAlgorithm::Simplification,
            EditorGeometryProcessingAlgorithm::Smoothing,
            EditorGeometryProcessingAlgorithm::Subdivision,
            EditorGeometryProcessingAlgorithm::Repair,
            EditorGeometryProcessingAlgorithm::Geodesics,
        };

        std::vector<EditorGeometryProcessingEntry> entries{};
        entries.reserve(kAlgorithmOrder.size());
        for (const EditorGeometryProcessingAlgorithm algorithm :
             kAlgorithmOrder)
        {
            if (IsSurfaceTopologyAlgorithm(algorithm) &&
                !capabilities.HasEditableSurfaceMesh)
            {
                continue;
            }

            const EditorGeometryProcessingDomain domains =
                capabilities.Domains &
        GetEditorSupportedGeometryProcessingDomains(algorithm);
            if (domains == EditorGeometryProcessingDomain::None)
                continue;

            entries.push_back(EditorGeometryProcessingEntry{
                .Algorithm = algorithm,
                .Domains = domains,
            });
        }
        return entries;
    }

    std::vector<EditorGeometryProcessingEntry>
ResolveEditorGeometryProcessingEntries(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        return ResolveEditorGeometryProcessingEntries(
      GetEditorGeometryProcessingCapabilities(registry, entity));
    }

    const char*DebugNameForEditorGeometryProcessingDomain(
        const EditorGeometryProcessingDomain domain) noexcept
    {
        using Domain = EditorGeometryProcessingDomain;
        switch (domain)
        {
        case Domain::None:
            return "None";
        case Domain::MeshVertices:
            return "Mesh Vertices";
        case Domain::MeshEdges:
            return "Mesh Edges";
        case Domain::MeshHalfedges:
            return "Mesh Halfedges";
        case Domain::MeshFaces:
            return "Mesh Faces";
        case Domain::GraphVertices:
            return "Graph Nodes";
        case Domain::GraphEdges:
            return "Graph Edges";
        case Domain::GraphHalfedges:
            return "Graph Halfedges";
        case Domain::PointCloudPoints:
            return "Point Cloud Points";
        }
        return "Mixed";
    }

    const char*DebugNameForEditorGeometryProcessingAlgorithm(
        const EditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
        case EditorGeometryProcessingAlgorithm::Geodesics:
            return "Geodesics (Virtual Source Propagation)";
        case EditorGeometryProcessingAlgorithm::KMeans:
            return "K-Means";
        case EditorGeometryProcessingAlgorithm::MeshDenoise:
            return "Mesh Denoise";
        case EditorGeometryProcessingAlgorithm::Curvature:
            return "Curvature";
        case EditorGeometryProcessingAlgorithm::CurvatureSegmentation:
            return "Curvature Segmentation";
        case EditorGeometryProcessingAlgorithm::Remeshing:
            return "Remeshing";
        case EditorGeometryProcessingAlgorithm::Simplification:
            return "Simplification";
        case EditorGeometryProcessingAlgorithm::Smoothing:
            return "Smoothing";
        case EditorGeometryProcessingAlgorithm::Subdivision:
            return "Subdivision";
        case EditorGeometryProcessingAlgorithm::Repair:
            return "Repair";
        case EditorGeometryProcessingAlgorithm::NormalEstimation:
            return "Normals";
        case EditorGeometryProcessingAlgorithm::ShortestPath:
            return "Shortest Path";
        case EditorGeometryProcessingAlgorithm::ConvexHull:
            return "Convex Hull";
        case EditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return "Surface Reconstruction";
        case EditorGeometryProcessingAlgorithm::KnnGraphConstruction:
            return "kNN Graph Construction";
        case EditorGeometryProcessingAlgorithm::VectorHeat:
            return "Vector Heat Method";
        case EditorGeometryProcessingAlgorithm::Parameterization:
            return "Parameterization";
        case EditorGeometryProcessingAlgorithm::BooleanCSG:
            return "Boolean CSG";
        case EditorGeometryProcessingAlgorithm::Registration:
            return "ICP Registration";
        case EditorGeometryProcessingAlgorithm::BilateralFilter:
            return "Bilateral Filter";
        case EditorGeometryProcessingAlgorithm::OutlierEstimation:
            return "Outlier Estimation";
        case EditorGeometryProcessingAlgorithm::KernelDensity:
            return "Kernel Density";
        case EditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            return "Statistical Outlier Removal";
        case EditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return "Radius Outlier Removal";
        case EditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return "Progressive Poisson Sampling";
        }
        return "Unknown";
    }

    Geometry::ConstPropertySet ResolveEditorSelectedMeshVertexProperties(
        const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.Scene == nullptr || context.Selection == nullptr)
            return {};

        const entt::registry& raw = context.Scene->Raw();
        for (const std::uint32_t stableId : context.Selection->SelectedStableIds())
        {
            const ECS::EntityHandle entity = SelectionController::ToEntityHandle(stableId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                continue;

            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(raw, entity);
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, GeometryElementDomain::MeshVertex);
            return properties != nullptr ? Geometry::ConstPropertySet{*properties}
                                         : Geometry::ConstPropertySet{};
        }
        return {};
    }

    PrimitiveSelectionSnapshot ReadEditorPrimitiveSelection(
        const EditorProcessingCommands& commands, const std::uint32_t entityId,
        const GeometryElementDomain domain)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.Scene || !context.Selection)
            return {.Message = "Selection service is unavailable."};
        return context.Selection->ReadPrimitives(*context.Scene, entityId, domain);
    }

    PrimitiveSelectionSnapshot ApplyEditorPrimitiveSelection(
        const EditorProcessingCommands& commands, const std::uint32_t entityId,
        const GeometryElementDomain domain, const PrimitiveSelectionEdit edit,
        const std::span<const std::uint32_t> indices)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.Scene || !context.Selection)
            return {.Message = "Selection service is unavailable."};
        auto result =
            context.Selection->EditPrimitives(*context.Scene, entityId, domain, edit, indices);
        if (result.Usable() && context.InvalidateWorkspaceSnapshotCache)
            context.InvalidateWorkspaceSnapshotCache();
        return result;
    }

    SelectionInteractionConfig GetEditorSelectionInteractionConfig(
        const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (context.EngineConfigControlState)
            return GetSelectionInteractionConfig(context.EngineConfigControlState->ActiveConfig)
                .value_or(SelectionInteractionConfig{});
        return context.Selection ? context.Selection->GetConfig().Interaction
                                 : SelectionInteractionConfig{};
    }

    RuntimeEngineConfigApplyResult ApplyEditorSelectionInteractionConfig(
        const EditorProcessingCommands& commands, const SelectionInteractionConfig& config)
    {
        return ApplyEditorProcessingConfig(
            commands,
            ValidateSelectionConfigSection(SerializeSelectionInteractionConfig(config), {},
                                           kSelectionConfigSectionName),
            std::string{kSelectionConfigSectionName},
            [&config](Core::Config::EngineConfig& candidate)
            { SetSelectionInteractionConfig(candidate, config); });
    }
} // namespace Extrinsic::Runtime
