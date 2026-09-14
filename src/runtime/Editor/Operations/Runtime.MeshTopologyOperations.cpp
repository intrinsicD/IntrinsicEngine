// Stable display names for the topology family's selectable operators and for
// the UV outcome a topology replacement reports.
module Extrinsic.Runtime.MeshTopologyOperations;
namespace Extrinsic::Runtime
{
    const char*
DebugNameForEditorMeshRemeshMode(
        const EditorMeshRemeshMode mode) noexcept
    {
        switch (mode)
        {
        case EditorMeshRemeshMode::Uniform:
            return "Uniform";
        case EditorMeshRemeshMode::Adaptive:
            return "Adaptive";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshRemeshSizingLaw(
        const EditorMeshRemeshSizingLaw sizingLaw) noexcept
    {
        switch (sizingLaw)
        {
        case EditorMeshRemeshSizingLaw::MeanCurvature:
            return "Mean curvature";
        case EditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
            return "Error-bounded Taubin";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshSubdivideOperator(
        const EditorMeshSubdivideOperator op) noexcept
    {
        switch (op)
        {
        case EditorMeshSubdivideOperator::Loop:
            return "Loop";
        case EditorMeshSubdivideOperator::CatmullClark:
            return "Catmull-Clark";
        case EditorMeshSubdivideOperator::Sqrt3:
            return "Sqrt(3)";
        }
        return "Unknown";
    }

    const char*DebugNameForEditorMeshSimplifyMetric(
        const EditorMeshSimplifyMetric metric) noexcept
    {
        switch (metric)
        {
        case EditorMeshSimplifyMetric::ClassicalQEM:
            return "Classical QEM";
        case EditorMeshSimplifyMetric::FA_QEM:
            return "FA-QEM (feature-aware)";
        }
        return "Unknown";
    }

    const char* DebugNameForEditorMeshTexcoordOutcome(
        const EditorMeshTexcoordOutcome outcome) noexcept
    {
        switch (outcome)
        {
        case EditorMeshTexcoordOutcome::None:
            return "no UVs";
        case EditorMeshTexcoordOutcome::Preserved:
            return "UVs preserved";
        case EditorMeshTexcoordOutcome::Discarded:
            return "UVs discarded";
        }
        return "Unknown";
    }
}
