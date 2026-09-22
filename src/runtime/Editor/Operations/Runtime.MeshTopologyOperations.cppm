// Mesh topology editing: denoise, remesh, subdivide and simplify. All four
// rebuild the entity's halfedge mesh from a scratch mesh, which is why they
// share one family, one UV-preservation contract and one replacement commit.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.MeshTopologyOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Core.Error;
export import Geometry.Smoothing.Types;
import Extrinsic.Runtime.EditorWorkspaceAttachment;

export namespace Extrinsic::Runtime
{
    enum class EditorMeshDenoiseStage : std::uint8_t
    {
        FullBilateral,
    };

    enum class EditorMeshRemeshMode : std::uint8_t
    {
        Uniform,
        Adaptive,
    };

    enum class EditorMeshRemeshSizingLaw : std::uint8_t
    {
        MeanCurvature,
        ErrorBoundedTaubin,
    };

    enum class EditorMeshSubdivideOperator : std::uint8_t
    {
        Loop,
        CatmullClark,
        Sqrt3,
    };
    enum class EditorMeshSimplifyMetric : std::uint8_t
    {
        ClassicalQEM,
        FA_QEM,
    };

    [[nodiscard]] const char* DebugNameForEditorMeshRemeshMode(EditorMeshRemeshMode mode) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorMeshRemeshSizingLaw(EditorMeshRemeshSizingLaw sizingLaw) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorMeshSubdivideOperator(EditorMeshSubdivideOperator op) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorMeshSimplifyMetric(EditorMeshSimplifyMetric metric) noexcept;

    struct EditorMeshDenoiseCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshDenoiseStage Stage{EditorMeshDenoiseStage::FullBilateral};
        std::uint32_t NormalIterations{5u};
        std::uint32_t VertexIterations{10u};
        double SigmaSpatial{0.0};
        double SigmaRange{0.0};
        bool PreserveBoundary{true};
        double DegenerateNormalLengthEpsilon{1.0e-12};
    };

    struct EditorMeshDenoiseResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::Smoothing::DenoiseStatus DenoiseStatus{
            Geometry::Smoothing::DenoiseStatus::Success};
        EditorMeshDenoiseStage Stage{EditorMeshDenoiseStage::FullBilateral};
        std::uint32_t NormalIterations{0u};
        std::uint32_t VertexIterations{0u};
        double SigmaSpatial{0.0};
        double SigmaRange{0.0};
        bool PreserveBoundary{true};
        std::size_t VertexSlotCount{0u};
        std::size_t WrittenCount{0u};
        std::size_t SkippedDeletedVertexCount{0u};
        std::size_t MovedVertexCount{0u};
        std::size_t ProcessedFaceCount{0u};
        std::size_t DegenerateFaceCount{0u};
        std::size_t NonFiniteFaceCount{0u};
        std::size_t SkippedDeletedFaceCount{0u};
        std::size_t PinnedBoundaryVertexCount{0u};
        double SigmaSpatialUsed{0.0};
        double SigmaRangeUsed{0.0};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        // Applied means the mesh actually changed. A run that writes every slot
        // but moves no vertex reports NoChange, because WrittenCount is
        // slot-derived and stays non-zero whenever the kernel ran at all.
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }

        // Share of live vertices held fixed as boundary, in [0, 1]. Surfaced
        // alongside the absolute count because the count alone does not say
        // whether the run had anything left to smooth. Derived, not stored.
        [[nodiscard]] double PinnedBoundaryRatio() const noexcept
        {
            const std::size_t live = VertexSlotCount - SkippedDeletedVertexCount;
            if (live == 0u)
            {
                return 0.0;
            }
            return static_cast<double>(PinnedBoundaryVertexCount) /
                   static_cast<double>(live);
        }
    };

    struct EditorMeshRemeshCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshRemeshMode Mode{EditorMeshRemeshMode::Uniform};
        EditorMeshRemeshSizingLaw SizingLaw{EditorMeshRemeshSizingLaw::MeanCurvature};
        std::uint32_t Iterations{1u};
        double TargetEdgeLength{0.0};
        double Lambda{0.5};
        double CurvatureAdaptation{1.0};
        double ApproximationError{0.01};
        bool PreserveBoundary{true};
        bool ProjectToSurface{false};
        std::uint32_t ReferenceProjectionK{16u};
        double MaxReferenceProjectionDistance{0.0};
    };

    // Records what a topology-replacing operation did to the mesh's UV
    // parameterization. These operations rebuild the entity's halfedge mesh, so
    // a UV property the rebuilt mesh does not carry is removed outright rather
    // than left stale. That outcome is reported, never silent.
    enum class EditorMeshTexcoordOutcome : std::uint8_t
    {
        // The mesh carried no resolvable UVs, so there was nothing to keep.
        None = 0,
        // The output carries the mesh's UVs.
        Preserved = 1,
        // The operation could not carry the UVs onto the topology it produced,
        // so the parameterization was discarded. Re-parameterize afterwards.
        Discarded = 2,
    };

    [[nodiscard]] const char* DebugNameForEditorMeshTexcoordOutcome(
        EditorMeshTexcoordOutcome outcome) noexcept;

    struct EditorMeshRemeshResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshRemeshMode Mode{EditorMeshRemeshMode::Uniform};
        EditorMeshRemeshSizingLaw SizingLaw{EditorMeshRemeshSizingLaw::MeanCurvature};
        std::uint32_t IterationsRequested{0u};
        std::uint32_t IterationsPerformed{0u};
        double TargetEdgeLength{0.0};
        bool ProjectToSurface{false};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        std::size_t SplitCount{0u};
        std::size_t CollapseCount{0u};
        std::size_t FlipCount{0u};
        EditorMeshTexcoordOutcome TexcoordOutcome{
            EditorMeshTexcoordOutcome::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorMeshSubdivideCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshSubdivideOperator Operator{EditorMeshSubdivideOperator::Loop};
        std::uint32_t Iterations{1u};
        bool PreserveLoopFeatureEdges{false};
        std::uint32_t MaxOutputFaces{0u};
        // Active Loop crease flags are exact 0/1 scalars; refined flags retain this storage kind.
        GeometryPropertyRef FeatureEdges{GeometryElementDomain::MeshEdge, "e:feature", Geometry::PropertyValueKind::Bool};
    };

    struct EditorMeshSubdivideResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshSubdivideOperator Operator{EditorMeshSubdivideOperator::Loop};
        std::uint32_t IterationsRequested{0u};
        std::uint32_t IterationsPerformed{0u};
        bool PreserveLoopFeatureEdges{false};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        EditorMeshTexcoordOutcome TexcoordOutcome{
            EditorMeshTexcoordOutcome::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorMeshSimplifyCommand
    {
        std::uint32_t StableEntityId{0u};
        EditorMeshSimplifyMetric Metric{EditorMeshSimplifyMetric::FA_QEM};
        // Stop the decimation when FaceCount() <= TargetFaces (0 = disabled).
        std::size_t TargetFaces{0u};
        // Maximum allowed error per collapse; 0 = unlimited (rely on TargetFaces).
        double MaxError{0.0};
        bool PreserveBoundary{true};
        // FA_QEM feature-aware weights (ignored under ClassicalQEM).
        double FeatureAngleThresholdDegrees{45.0};
        double NormalWeight{1.0};
        double BoundaryWeight{1.0};
        double CurvatureWeight{1.0};
        bool PreserveSharpFeatures{true};
        bool PreserveUvSeams{true};
    };

    struct EditorMeshSimplifyResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshSimplifyMetric Metric{EditorMeshSimplifyMetric::FA_QEM};
        std::size_t TargetFaces{0u};
        double MaxError{0.0};
        std::size_t InputVertexCount{0u};
        std::size_t InputFaceCount{0u};
        std::size_t OutputVertexCount{0u};
        std::size_t OutputFaceCount{0u};
        std::size_t CollapseCount{0u};
        double MaxCollapseError{0.0};
        std::size_t CollapsesRejectedTopology{0u};
        std::size_t CollapsesRejectedQuality{0u};
        std::size_t SharpFeatureVerticesPinned{0u};
        std::size_t SeamVerticesPinned{0u};
        EditorMeshTexcoordOutcome TexcoordOutcome{
            EditorMeshTexcoordOutcome::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    // Names one stored `Last<Operation>Result` slot so a panel can dismiss the
    // outcome it is showing. Every result is already superseded by the next run
    // of its own operation; this is the explicit clear, and it is one enum
    // rather than one sink per operation because the session's only reaction is
    // to reset the matching optional.
    enum class EditorMeshTopologyResultSlot : std::uint8_t
    {
        MeshDenoise,
        MeshRemesh,
        MeshSubdivide,
        MeshSimplify,
    };

    // Incomplete borrowed containers keep sibling workspace features independent
    // of mesh-topology records; prepared frames copy their values.
    extern "C++"
    {
        struct EditorMeshTopologyResultSinks
        {
            std::function<void(EditorMeshTopologyResultSlot)> DismissResult{};
            std::function<void(EditorMeshDenoiseResult)> MeshDenoise{};
            std::function<void(EditorMeshRemeshResult)> MeshRemesh{};
            std::function<void(EditorMeshSubdivideResult)> MeshSubdivide{};
            std::function<void(EditorMeshSimplifyResult)> MeshSimplify{};
        };
        struct EditorMeshTopologyResultsSnapshot
        {
            std::optional<EditorMeshDenoiseResult> LastMeshDenoiseResult{};
            std::optional<EditorMeshRemeshResult> LastMeshRemeshResult{};
            std::optional<EditorMeshSubdivideResult> LastMeshSubdivideResult{};
            std::optional<EditorMeshSimplifyResult> LastMeshSimplifyResult{};
        };
    }

    struct EditorMeshTopologyPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorMeshTopologyResultSinks ResultSinks{};
        EditorMeshTopologyResultsSnapshot Results{};
    };
    [[nodiscard]] EditorMeshTopologyPreparedFrame
    PrepareEditorMeshTopologyFrame(const EditorWorkspaceAttachment&);

    // Admission checks use parameter/capability/source metadata only; execution
    // still validates full geometry and topology before publishing a result.
    [[nodiscard]] ActionReadiness PreviewEditorMeshDenoiseCommand(
        const EditorProcessingCommands&, const EditorMeshDenoiseCommand&);
    [[nodiscard]] ActionReadiness PreviewEditorMeshRemeshCommand(
        const EditorProcessingCommands&, const EditorMeshRemeshCommand&);
    [[nodiscard]] ActionReadiness PreviewEditorMeshSubdivideCommand(
        const EditorProcessingCommands&, const EditorMeshSubdivideCommand&);
    [[nodiscard]] ActionReadiness PreviewEditorMeshSimplifyCommand(
        const EditorProcessingCommands&, const EditorMeshSimplifyCommand&);

    // Immediate outcomes return directly. Only a newly queued job delivers a
    // terminal callback, while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorMeshDenoiseResult ApplyEditorMeshDenoiseCommand(
        const EditorProcessingCommands&, const EditorMeshDenoiseCommand&,
        std::function<void(EditorMeshDenoiseResult)> onComplete = {});
    [[nodiscard]] EditorMeshRemeshResult ApplyEditorMeshRemeshCommand(
        const EditorProcessingCommands&, const EditorMeshRemeshCommand&,
        std::function<void(EditorMeshRemeshResult)> onComplete = {});
    [[nodiscard]] EditorMeshSubdivideResult ApplyEditorMeshSubdivideCommand(
        const EditorProcessingCommands&, const EditorMeshSubdivideCommand&,
        std::function<void(EditorMeshSubdivideResult)> onComplete = {});
    [[nodiscard]] EditorMeshSimplifyResult ApplyEditorMeshSimplifyCommand(
        const EditorProcessingCommands&, const EditorMeshSimplifyCommand&,
        std::function<void(EditorMeshSimplifyResult)> onComplete = {});
}
