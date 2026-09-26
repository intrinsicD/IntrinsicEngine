// Same-cardinality fields published on existing geometry: mesh curvature,
// segmentation, geodesics, gradients, and smoothing on any property domain.
// These methods preserve topology and own guarded property publication.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.MeshFieldOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.MeshCurvatureConfig;
export import Extrinsic.Runtime.CurvatureSegmentationConfig;
export import Extrinsic.Runtime.GeodesicsConfig;
export import Extrinsic.Core.Error;
export import Geometry.Geodesic.Types;
export import Geometry.Smoothing.Types;
export import Geometry.HarmonicField.Types;
export import Geometry.Segmentation.Diagnostics;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] const char*
    DebugNameForEditorMeshCurvatureOutput(EditorMeshCurvatureOutput output) noexcept;

    using EditorMeshCurvatureCommand = MeshCurvatureConfig;

    struct EditorMeshCurvatureResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorMeshCurvatureOutput Output{EditorMeshCurvatureOutput::All};
        bool DirectionsRequested{true};
        bool DirectionsAvailable{true};
        bool DirectionsPublished{false};
        std::size_t VertexSlotCount{0u};
        // Estimator support is distinct from finite publication count: a
        // supported flat vertex may be zero, while zero supported vertices
        // means the operation produced no informative field.
        std::size_t SupportedVertexCount{0u};
        std::size_t NonZeroPrincipalVertexCount{0u};
        double MinimumPrincipalValue{0.0};
        double MaximumPrincipalValue{0.0};
        std::size_t DegenerateFaceCount{0u};
        std::size_t IllConditionedFaceCount{0u};
        std::size_t UnsupportedFaceCount{0u};
        double MinimumTriangleQuality{0.0};
        double TriangleQualityThreshold{0.0};
        // Counts all four scalar fields written (mean, Gaussian, and minimum
        // and maximum principal curvature), regardless of value changes.
        std::size_t ScalarPropertyCount{0u};
        std::size_t ScalarWrittenCount{0u};
        // Counts published curvature values that differ from stored values,
        // counted across every property the run publishes.
        std::size_t ChangedValueCount{0u};
        std::size_t DirectionPropertyCount{0u};
        std::size_t DirectionWrittenCount{0u};
        std::size_t NonFiniteScalarCount{0u};
        std::size_t NonFiniteDirectionCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorCurvatureSegmentationCommand
    {
        std::uint32_t StableEntityId{0u};
        CurvatureSegmentationConfig Config{};
    };

    struct EditorCurvatureSegmentationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        CurvatureSegmentationConfig Config{};
        CurvatureSegmentationMethod RequestedMethod{
            CurvatureSegmentationMethod::CurvatureGmm};
        CurvatureSegmentationMethod ActualMethod{
            CurvatureSegmentationMethod::CurvatureGmm};
        Geometry::Segmentation::SegmentationDiagnostics
            Diagnostics{};
        std::optional<
            Geometry::Segmentation::FeatureEvidenceDiagnostics>
            FeatureDiagnostics{};
        std::optional<
            Geometry::Segmentation::CurvaturePatchDiagnostics>
            PatchDiagnostics{};
        std::optional<
            Geometry::Segmentation::BoundaryPartitionDiagnostics>
            BoundaryDiagnostics{};
        std::size_t ChangedValueCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    struct EditorGeodesicsCommand
    {
        std::uint32_t StableEntityId{0u};
        GeodesicsConfig Config{};
    };

    struct EditorGeodesicsResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        Geometry::Geodesic::VirtualSourceResult Diagnostics{};
        std::string BackendId{"cpu_reference"};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return (Status == EditorCommandStatus::Applied ||
                    Status == EditorCommandStatus::NoChange) &&
                   Diagnostics.Succeeded();
        }
    };

    inline constexpr std::string_view kPropertySmoothingConfigSectionName = "sandbox.property_smoothing";
    // Vulkan runs the explicit filters (averaging, spectral heat, Taubin, bilateral) in double
    // precision and needs an operational device with shader double support; it never falls back.
    enum class PropertySmoothingBackend : std::uint8_t { Cpu, Vulkan };
    struct PropertySmoothingConfig
    {
        GeometryPropertyRef Input{GeometryElementDomain::MeshVertex, "v:mean_curvature", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Output{GeometryElementDomain::MeshVertex, "smoothed", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Positions{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        Geometry::Smoothing::PropertyWeight Weight{Geometry::Smoothing::PropertyWeight::Uniform};
        Geometry::Smoothing::PropertyFilterParams Filter{};
        std::uint32_t Neighbors{12};
        double SpatialSigma{1.0};
        bool PreserveBoundary{false};
        // Variational fit with FitBound::PerRow: nonnegative float/double radius per input row.
        // An empty name serializes as null; the binding is only resolved when per-row bounds are used.
        GeometryPropertyRef BoundRadii{GeometryElementDomain::MeshVertex, "", Geometry::PropertyValueKind::Double};
        PropertySmoothingBackend Backend{PropertySmoothingBackend::Cpu};
    };
    struct EditorPropertySmoothingResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::size_t LiveCount{}, EdgeCount{}, OperatorApplications{};
        // Variational fit only: data weight used, mass-weighted RMS residual and active bounds.
        double FitWeight{}, RmsResidual{};
        std::size_t ActiveBounds{};
        PropertySmoothingBackend RequestedBackend{PropertySmoothingBackend::Cpu};
        // Actual backend: cpu_reference, cpu_sparse_cholesky, cpu_admm_sparse_cholesky, vulkan_compute, ...
        std::string BackendId{"cpu_reference"};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        { return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange; }
    };
    [[nodiscard]] std::string SerializePropertySmoothingConfig(const PropertySmoothingConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakePropertySmoothingConfigSectionRegistration();
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPropertySmoothingConfig(
        const EditorProcessingCommands&, const PropertySmoothingConfig&);
    [[nodiscard]] std::optional<PropertySmoothingConfig> GetEditorPropertySmoothingConfig(const EditorProcessingCommands&);
    [[nodiscard]] ActionReadiness PreviewEditorPropertySmoothingCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const PropertySmoothingConfig&);
    // CPU runs publish synchronously. Vulkan returns Pending and delivers the published (or stale
    // or failed) result to onComplete once the framed readback completes.
    [[nodiscard]] EditorPropertySmoothingResult ApplyEditorPropertySmoothingCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const PropertySmoothingConfig&,
        std::function<void(EditorPropertySmoothingResult)> onComplete = {});

    // Harmonic/biharmonic/triharmonic interpolation and Poisson solves of a typed property from
    // constrained rows, or random-walker propagation of Int32 seed labels, over the same sample
    // graphs as property smoothing.
    inline constexpr std::string_view kHarmonicFieldConfigSectionName = "sandbox.harmonic_field";
    enum class HarmonicFieldMode : std::uint8_t { Field, Labels };
    struct HarmonicFieldConfig
    {
        HarmonicFieldMode Mode{HarmonicFieldMode::Field};
        // Field: floating scalar/vector values whose constrained rows are the targets.
        // Labels: Int32 labels; rows different from Unlabeled are seeds.
        GeometryPropertyRef Input{GeometryElementDomain::MeshVertex, "v:harmonic_constraints", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Output{GeometryElementDomain::MeshVertex, "v:harmonic_field", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Positions{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        // Field constraints on the input domain; an empty name disables the source.
        GeometryPropertyRef HardMask{GeometryElementDomain::MeshVertex, "", Geometry::PropertyValueKind::Bool};    // true: hard row
        GeometryPropertyRef SoftWeights{GeometryElementDomain::MeshVertex, "", Geometry::PropertyValueKind::Float}; // > 0: soft row
        bool PinBoundary{false}; // mesh boundary vertices become hard rows
        // Field mode, optional: source density f with the input's channel count; the solve uses
        // b = M f (row masses, or 1 without lumped mass), i.e. -Delta u = f for the harmonic order.
        GeometryPropertyRef Source{GeometryElementDomain::MeshVertex, "", Geometry::PropertyValueKind::Double};
        std::int32_t Unlabeled{0};
        GeometryPropertyRef Confidence{GeometryElementDomain::MeshVertex, "", Geometry::PropertyValueKind::Float}; // optional, labels
        // Label mode, optional: publishes one float weight field per seed label L as
        // WeightsPrefix + decimal(L) (skinning/blending weights); empty disables.
        std::string WeightsPrefix{};
        Geometry::Smoothing::PropertyWeight Weight{Geometry::Smoothing::PropertyWeight::Cotangent};
        std::uint32_t Neighbors{12};
        double SpatialSigma{1.0};
        Geometry::HarmonicField::Params Field{};
        bool LumpedMass{false}; // DEC lumped vertex areas for orders > 1 and the source (mesh vertices)
    };
    struct EditorHarmonicFieldResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::size_t LiveCount{}, EdgeCount{}, FreeRows{}, HardRows{}, SoftRows{}, Components{}, UnconstrainedComponents{};
        std::size_t GroundedComponents{}, WeightOutputs{};
        double MaxRelativeResidual{}, MaxCompatibilityDefect{};
        std::string BackendId{"cpu_reference_sparse_cholesky"};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        { return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange; }
    };
    [[nodiscard]] std::string SerializeHarmonicFieldConfig(const HarmonicFieldConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeHarmonicFieldConfigSectionRegistration();
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorHarmonicFieldConfig(
        const EditorProcessingCommands&, const HarmonicFieldConfig&);
    [[nodiscard]] std::optional<HarmonicFieldConfig> GetEditorHarmonicFieldConfig(const EditorProcessingCommands&);
    [[nodiscard]] ActionReadiness PreviewEditorHarmonicFieldCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const HarmonicFieldConfig&);
    [[nodiscard]] EditorHarmonicFieldResult ApplyEditorHarmonicFieldCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const HarmonicFieldConfig&);

    inline constexpr std::string_view kScalarGradientConfigSectionName = "sandbox.scalar_gradient";
    struct ScalarGradientConfig
    {
        GeometryPropertyRef Positions{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Scalar{GeometryElementDomain::MeshVertex, "v:mean_curvature", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Output{GeometryElementDomain::MeshFace, "f:scalar_gradient", Geometry::PropertyValueKind::Vec3};
    };
    struct EditorScalarGradientResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::size_t FaceCount{};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange;
        }
    };
    [[nodiscard]] std::string SerializeScalarGradientConfig(const ScalarGradientConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeScalarGradientConfigSectionRegistration();
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorScalarGradientConfig(
        const EditorProcessingCommands&, const ScalarGradientConfig&);
    [[nodiscard]] std::optional<ScalarGradientConfig> GetEditorScalarGradientConfig(const EditorProcessingCommands&);
    [[nodiscard]] ActionReadiness PreviewEditorScalarGradientCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const ScalarGradientConfig&);
    [[nodiscard]] EditorScalarGradientResult ApplyEditorScalarGradientCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const ScalarGradientConfig&);

    // Incomplete borrowed containers keep sibling workspace features independent
    // of mesh-field records; prepared frames copy their values. Curvature is the
    // only field method whose outcome the session retains, because it is the
    // only one that can finish after the frame that requested it.
    extern "C++"
    {
        struct EditorMeshFieldResultSinks
        {
            std::function<void()> DismissResult{};
            std::function<void(EditorMeshCurvatureResult)> MeshCurvature{};
        };
        struct EditorMeshFieldResultsSnapshot
        {
            std::optional<EditorMeshCurvatureResult> LastMeshCurvatureResult{};
        };
    }

    struct EditorMeshFieldPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorMeshFieldResultSinks ResultSinks{};
        EditorMeshFieldResultsSnapshot Results{};
    };
    [[nodiscard]] EditorMeshFieldPreparedFrame
    PrepareEditorMeshFieldFrame(const EditorWorkspaceAttachment&);

    // Admission checks configuration/metadata and cached bound-input verdicts.
    // Execution recaptures inputs and checks solver/publication constraints.
    [[nodiscard]] ActionReadiness PreviewEditorMeshCurvatureCommand(
        const EditorProcessingCommands&, const EditorMeshCurvatureCommand&);
    [[nodiscard]] ActionReadiness PreviewEditorCurvatureSegmentationCommand(
        const EditorProcessingCommands&, const EditorCurvatureSegmentationCommand&);

    // Immediate outcomes return directly. Only a newly queued job delivers a
    // terminal callback, while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorMeshCurvatureResult ApplyEditorMeshCurvatureCommand(
        const EditorProcessingCommands&, const EditorMeshCurvatureCommand&,
        std::function<void(EditorMeshCurvatureResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorMeshCurvatureConfig(
        const EditorProcessingCommands&, const MeshCurvatureConfig&,
        std::string sourceId = "sandbox.mesh_curvature");
    [[nodiscard]] std::optional<MeshCurvatureConfig> GetEditorMeshCurvatureConfig(
        const EditorProcessingCommands&) noexcept;

    // Segmentation and geodesics publish inline: both run on the calling thread
    // and own their whole history commit, so neither has a queued completion.
    [[nodiscard]] EditorCurvatureSegmentationResult ApplyEditorCurvatureSegmentationCommand(
        const EditorProcessingCommands&, const EditorCurvatureSegmentationCommand&);
    [[nodiscard]] EditorCurvatureSegmentationResult
    ApplyEditorConfiguredCurvatureSegmentationCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId);
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorCurvatureSegmentationConfig(
        const EditorProcessingCommands&, const CurvatureSegmentationConfig&,
        std::string sourceId = "sandbox.curvature_segmentation");
    [[nodiscard]] std::optional<CurvatureSegmentationConfig>
    GetEditorCurvatureSegmentationConfig(const EditorProcessingCommands&) noexcept;

    [[nodiscard]] EditorGeodesicsResult ApplyEditorGeodesicsCommand(
        const EditorProcessingCommands&, const EditorGeodesicsCommand&);
    [[nodiscard]] EditorGeodesicsResult ApplyEditorConfiguredGeodesicsCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId);
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorGeodesicsConfig(
        const EditorProcessingCommands&, const GeodesicsConfig&,
        std::string sourceId = "sandbox.geodesics");
    [[nodiscard]] std::optional<GeodesicsConfig> GetEditorGeodesicsConfig(
        const EditorProcessingCommands&) noexcept;
}
