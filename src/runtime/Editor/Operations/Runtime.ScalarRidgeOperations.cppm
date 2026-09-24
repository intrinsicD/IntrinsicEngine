// Ridge and valley curves of a mesh vertex scalar property (for example mean
// curvature), as Hessian height ridges or gradient-flow watershed separatrices.
// Curves publish as a new graph entity and, on request, as undoable boolean
// vertex/edge feature masks (plus watershed basin labels) on the source mesh.
module;
#include <cstddef>
#include <cstdint>
#include <string>
export module Extrinsic.Runtime.ScalarRidgeOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.GeometryProperty.Types;
export namespace Extrinsic::Runtime
{
    // See Geometry::ScalarfieldExtrema::Method.
    enum class EditorScalarExtremaMethod : std::uint8_t
    {
        HessianRidge,
        Watershed,
    };

    struct EditorScalarRidgeCommand
    {
        std::uint32_t StableEntityId{0u};
        // Any scalar vertex property; values are converted to double.
        GeometryPropertyRef Property{GeometryElementDomain::MeshVertex, "v:mean_curvature",
                                     Geometry::PropertyValueKind::Double};
        // Fit radius as a fraction of the bounding-box diagonal.
        double RadiusRatio{0.02};
        // Fractions of the field range; see Geometry::ScalarfieldExtrema.
        double MinimumSharpness{0.01};
        double MinimumStrength{0.01};
        // 0, 1, 2 select 0.5x, 1x, 2x the fit radius.
        std::uint8_t Scale{1u};
        bool Ridges{true};
        bool Valleys{true};
        // Keep only segments that reappear at another scale.
        bool RequirePersistence{false};
        // Watershed ignores radius, scale, sharpness and scale persistence; it
        // merges basins shallower than this fraction of the field range.
        EditorScalarExtremaMethod Method{EditorScalarExtremaMethod::HessianRidge};
        double MinimumPersistence{0.05};
        bool PublishGraph{true};
        // Snapped selected curves as boolean masks on the source mesh; with
        // Watershed also the ridge-side (descending) basin label per vertex.
        bool PublishMeshFeatures{false};
        GeometryPropertyRef VertexFeatures{GeometryElementDomain::MeshVertex, "v:feature",
                                           Geometry::PropertyValueKind::Bool};
        GeometryPropertyRef EdgeFeatures{GeometryElementDomain::MeshEdge, "e:feature",
                                         Geometry::PropertyValueKind::Bool};
        GeometryPropertyRef BasinLabels{GeometryElementDomain::MeshVertex, "v:watershed_basin",
                                        Geometry::PropertyValueKind::UInt32};
    };

    struct EditorScalarRidgeResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::string Message{};
        // Stable id of the created graph entity; zero until publication succeeds.
        std::uint32_t OutputEntityId{0u};
        std::size_t RidgeSegmentCount{0u};
        std::size_t ValleySegmentCount{0u};
        std::size_t OutputVertexCount{0u};
        // Mesh feature publication; basins are watershed-only.
        std::size_t FeatureVertexCount{0u};
        std::size_t FeatureEdgeCount{0u};
        std::size_t BasinCount{0u};
        double ComputeMilliseconds{0.0};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    // Runs synchronously on the CPU reference extractor. No curves is NoChange,
    // not an empty entity. Edge property "e:scalar_extremum" is +1 on ridge and
    // -1 on valley segments; "e:strength" is the relative field height. Mesh
    // features and the graph are separate undo steps (features first).
    [[nodiscard]] EditorScalarRidgeResult ApplyEditorScalarRidgeCommand(
        const EditorProcessingCommands& commands, const EditorScalarRidgeCommand& command);
}
