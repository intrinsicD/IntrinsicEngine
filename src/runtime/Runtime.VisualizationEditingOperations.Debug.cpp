module;

#include <string>
#include <string_view>

module Extrinsic.Runtime.VisualizationEditingOperations;

namespace Extrinsic::Runtime
{
    namespace G = Graphics::Components;

const char*DebugNameForEditorVisualizationColorSource(
        const G::VisualizationConfig::ColorSource source) noexcept
    {
        switch (source)
        {
        case G::VisualizationConfig::ColorSource::Material:
            return "Material";
        case G::VisualizationConfig::ColorSource::UniformColor:
            return "UniformColor";
        case G::VisualizationConfig::ColorSource::ScalarField:
            return "ScalarField";
        case G::VisualizationConfig::ColorSource::PerVertexBuffer:
            return "PerVertexBuffer";
        case G::VisualizationConfig::ColorSource::PerEdgeBuffer:
            return "PerEdgeBuffer";
        case G::VisualizationConfig::ColorSource::PerFaceBuffer:
            return "PerFaceBuffer";
        }
        return "Unknown";
    }

const char*DebugNameForEditorVisualizationDomain(
        const G::VisualizationConfig::Domain domain) noexcept
    {
        switch (domain)
        {
        case G::VisualizationConfig::Domain::Vertex:
            return "Vertex";
        case G::VisualizationConfig::Domain::Edge:
            return "Edge";
        case G::VisualizationConfig::Domain::Face:
            return "Face";
        }
        return "Unknown";
    }

const char*DebugNameForEditorVisualizationRecipeKind(
        const VisualizationRecipeKind kind) noexcept
    {
        switch (kind)
        {
        case VisualizationRecipeKind::Empty: return "Empty";
        case VisualizationRecipeKind::Scalar: return "Scalar";
        case VisualizationRecipeKind::Color: return "Color";
        case VisualizationRecipeKind::Label: return "Label";
        case VisualizationRecipeKind::VectorField: return "VectorField";
        case VisualizationRecipeKind::Isoline: return "Isoline";
        case VisualizationRecipeKind::HtexPreview: return "HtexPreview";
        case VisualizationRecipeKind::FragmentBake: return "FragmentBake";
        }
        return "Unknown";
    }

const char*DebugNameForEditorVisualizationPropertyDomain(
        const EditorVisualizationPropertyDomain domain) noexcept
    {
        using Domain = EditorVisualizationPropertyDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return "MeshVertices";
        case Domain::MeshEdges:
            return "MeshEdges";
        case Domain::MeshFaces:
            return "MeshFaces";
        case Domain::GraphVertices:
            return "GraphVertices";
        case Domain::GraphEdges:
            return "GraphEdges";
        case Domain::PointCloudPoints:
            return "PointCloudPoints";
        }
        return "Unknown";
    }

const char*DebugNameForEditorVisualizationPropertyPreset(
        const EditorVisualizationPropertyPreset preset) noexcept
    {
        using Preset = EditorVisualizationPropertyPreset;
        switch (preset)
        {
        case Preset::Scalar:
            return "Scalar";
        case Preset::Isoline:
            return "Isoline";
        case Preset::ColorBuffer:
            return "ColorBuffer";
        }
        return "Unknown";
    }

const char*DebugNameForEditorVisualizationTarget(
        const EditorVisualizationTarget target) noexcept
    {
        using Target = EditorVisualizationTarget;
        switch (target)
        {
        case Target::Entity:
            return "Entity";
        case Target::Surface:
            return "Surface";
        case Target::Edges:
            return "Edges";
        case Target::Points:
            return "Points";
        }
        return "Unknown";
    }

const char*DebugNameForEditorPropertyCatalogDomain(
        const EditorPropertyCatalogDomain domain) noexcept
    {
        using Domain = EditorPropertyCatalogDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return "MeshVertices";
        case Domain::MeshEdges:
            return "MeshEdges";
        case Domain::MeshHalfedges:
            return "MeshHalfedges";
        case Domain::MeshFaces:
            return "MeshFaces";
        case Domain::GraphVertices:
            return "GraphVertices";
        case Domain::GraphEdges:
            return "GraphEdges";
        case Domain::PointCloudPoints:
            return "PointCloudPoints";
        }
        return "Unknown";
    }

const char*DebugNameForEditorBoundRenderStateRowKind(
        const EditorBoundRenderStateRowKind kind) noexcept
    {
        using Kind = EditorBoundRenderStateRowKind;
        switch (kind)
        {
        case Kind::RenderHint:
            return "RenderHint";
        case Kind::GeometryPresentationSlot:
            return "GeometryPresentationSlot";
        case Kind::DerivedJob:
            return "DerivedJob";
        case Kind::CompositionSummary:
            return "CompositionSummary";
        case Kind::DisabledCommand:
            return "DisabledCommand";
        }
        return "Unknown";
    }
}
