// Same-cardinality fields published on existing geometry: mesh curvature,
// segmentation, geodesics, gradients, and smoothing on any property domain.
// These methods preserve topology and own guarded property publication.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module Extrinsic.Runtime.MeshFieldOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.MeshCurvatureConfig;
export import Extrinsic.Runtime.CurvatureSegmentationConfig;
export import Extrinsic.Runtime.GeodesicsConfig;
export import Extrinsic.Core.Error;
export import Extrinsic.Runtime.ConfigFieldSpec;
export import Geometry.Geodesic.Types;
export import Geometry.Smoothing.Types;
export import Geometry.HarmonicField.Types;
export import Geometry.Segmentation.Diagnostics;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Graphics.GpuPropertyResidency;

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
    // Keeps an edited draft runnable: compares it with the draft before the edit and resolves the
    // conflicts the edit created in favor of the edited choice. A new input retargets the output
    // (an overwrite stays an overwrite, otherwise `<input>_smoothed` with the input's storage),
    // radii and positions to the input's domain family and drops mesh-only weights, boundary
    // pinning and lumped mass off mesh vertices; a new method drops lumped mass and Vulkan where
    // they do not apply; fit solver, order, bound shape and penalty edits keep the ADMM-only and
    // positive-delta requirements. Conflicts an edit of a dependent option creates stay for the
    // validator to report.
    void ReconcilePropertySmoothingConfig(PropertySmoothingConfig& draft, const PropertySmoothingConfig& before);
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
    // Declared fields of the section: validation, generated schema and editor hints.
    [[nodiscard]] std::span<const ConfigFieldSpec> PropertySmoothingConfigFieldSpecs() noexcept;
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPropertySmoothingConfig(
        const EditorProcessingCommands&, const PropertySmoothingConfig&);
    [[nodiscard]] std::optional<PropertySmoothingConfig> GetEditorPropertySmoothingConfig(const EditorProcessingCommands&);
    [[nodiscard]] ActionReadiness PreviewEditorPropertySmoothingCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const PropertySmoothingConfig&);
    // CPU runs publish synchronously. Vulkan returns Pending, runs as a GPU property
    // transaction that accepts automatically when the device finishes (batch and agent
    // callers), and delivers the published (or stale or failed) result to onComplete.
    [[nodiscard]] EditorPropertySmoothingResult ApplyEditorPropertySmoothingCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const PropertySmoothingConfig&,
        std::function<void(EditorPropertySmoothingResult)> onComplete = {});

    // Interactive Vulkan smoothing as a GPU property transaction (ADR 0030 decisions 5-7).
    // The run reads its input from the property's canonical residency slot and writes the
    // output property's ring; the renderer shows the ring front while the appearance selects
    // that scalar. When the run finishes or is stopped it waits for the user: Accept reads the
    // front back once, publishes it through the undoable smoothing transaction and binds the
    // front as the canonical slot of the new CPU revision (the next run uploads nothing);
    // Discard publishes nothing and releases the ring. A result whose inputs changed while it
    // waits is stale: Accept is refused with the reason and only Discard remains.
    struct EditorPropertySmoothingTransactionSnapshot
    {
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        bool Stale{};                        // the inputs changed while the result waits
        bool CanAccept{};                    // ReadyToAccept, current and a front exists
        std::string AcceptDisabledReason{};  // why not, when a result waits but cannot be accepted
        std::uint32_t Previews{};            // fronts published so far
        bool DeviceWorkQueued{};             // a submission of this run has been queued to the device
        EditorPropertySmoothingResult Result{}; // Pending until Applied, Failed or Discarded
        // The run's own job (compute, then the Accept readback); the chained
        // implicit solves report a determinate fraction, everything else is
        // indeterminate Running.
        EditorOperationProgress Progress{};
    };
    // The run's job state; the handle keeps it alive across frames.
    struct EditorPropertySmoothingTransaction;
    using EditorPropertySmoothingTransactionHandle = std::shared_ptr<EditorPropertySmoothingTransaction>;
    // Validates and captures like the command, then queues the device run. Null with `failure`
    // filled when the request is rejected (including while a result for the same output awaits
    // Accept or Discard). Vulkan backend only.
    [[nodiscard]] EditorPropertySmoothingTransactionHandle StartEditorPropertySmoothing(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const PropertySmoothingConfig&,
        EditorPropertySmoothingResult& failure);
    // Stops a running chunked solve after its current chunk (its latest preview becomes the
    // result); an explicit filter, recorded as one submission, completes as usual.
    void StopEditorPropertySmoothing(const EditorPropertySmoothingTransactionHandle&);
    [[nodiscard]] EditorPropertySmoothingTransactionSnapshot SnapshotEditorPropertySmoothing(
        const EditorProcessingCommands&, const EditorPropertySmoothingTransactionHandle&);
    // Pending when the readback was queued (onComplete then receives the published result);
    // otherwise the refusal (stale, wrong phase, no front).
    [[nodiscard]] EditorPropertySmoothingResult AcceptEditorPropertySmoothing(
        const EditorProcessingCommands&, const EditorPropertySmoothingTransactionHandle&,
        std::function<void(EditorPropertySmoothingResult)> onComplete = {});
    // Cancels a running or accepting run, publishes nothing and releases the ring.
    void DiscardEditorPropertySmoothing(const EditorProcessingCommands&, const EditorPropertySmoothingTransactionHandle&);
    // Test seam (null or mock device): a transaction over the entity's captured inputs that
    // already waits for Accept with `frontValues` (rows x channels in sample order) as the
    // device result. With `residency`, the output property's ring holds a published front there
    // so Accept binds it. Accept publishes from `frontValues` without a device readback.
    [[nodiscard]] EditorPropertySmoothingTransactionHandle MakeEditorPropertySmoothingTransactionForTest(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const PropertySmoothingConfig&,
        std::vector<double> frontValues, Graphics::GpuPropertyResidency* residency = nullptr);

    // Smallest eigenpairs of a modal operator with unit or lumped-area mass, published as one
    // property per mode (GEOM-024 solver; UI-056 viewer; METHOD-051 operators): the sample-graph
    // Laplacian A = D - W on every point domain, or on mesh vertices the modified Dirichlet
    // energy E_D^N (float modes) and the rest-state discrete-shells Hessian (vec3 vibration
    // modes) of Hildebrandt et al. 2012. Optionally publishes the modal signature
    // S_t(v) = Σ e^{-λt}‖Φ(v)‖² (the heat kernel signature for the Laplacian) and the
    // multi-scale modal distance to a source row.
    enum class ModalOperator : std::uint8_t { GraphLaplacian = 0, ModifiedDirichlet = 1, ThinShell = 2 };
    inline constexpr std::string_view kLaplacianEigenbasisConfigSectionName = "sandbox.laplacian_eigenbasis";
    struct LaplacianEigenbasisConfig
    {
        ModalOperator Operator{ModalOperator::GraphLaplacian};
        GeometryElementDomain Domain{GeometryElementDomain::MeshVertex};
        GeometryPropertyRef Positions{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        Geometry::Smoothing::PropertyWeight Weight{Geometry::Smoothing::PropertyWeight::Cotangent};
        std::uint32_t Neighbors{12};
        double SpatialSigma{1.0};
        bool LumpedMass{true}; // DEC lumped vertex areas; mesh vertices only, else unit mass
        std::uint32_t Count{10};
        std::string OutputPrefix{"eigen_"};
        std::uint32_t MaxIterations{500};
        double Tolerance{1e-10};
        // Discrete-shells term weights (ThinShell only): flexural, edge length and triangle area.
        double ShellFlexural{1.0}, ShellLength{1.0}, ShellArea{1.0};
        // Leading modes left out of signatures and distances (6 rigid motions for ThinShell).
        std::uint32_t SkipModes{0};
        std::string SignatureOutput{};  // empty: no signature
        double SignatureScale{0.5};     // s in [0,1]: t = t_min^(1-s) t_max^s, Sun et al.'s range
        std::int64_t DistanceSource{-1}; // row slot on Domain; negative: no distance
        std::string DistanceOutput{"modal_distance"};
        std::uint32_t DistanceSamples{32};
    };
    struct EditorLaplacianEigenbasisResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        std::size_t LiveCount{}, EdgeCount{}, Iterations{};
        double ScaleMin{}, ScaleMax{}, SignatureTime{};
        std::vector<double> Eigenvalues{}, RelativeResiduals{};
        std::vector<std::string> Outputs{};
        std::string BackendId{"cpu_reference_subspace_iteration"};
        std::string Message{};
        [[nodiscard]] bool Succeeded() const noexcept
        { return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange; }
    };
    [[nodiscard]] std::string SerializeLaplacianEigenbasisConfig(const LaplacianEigenbasisConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeLaplacianEigenbasisConfigSectionRegistration();
    [[nodiscard]] std::span<const ConfigFieldSpec> LaplacianEigenbasisConfigFieldSpecs() noexcept;
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorLaplacianEigenbasisConfig(
        const EditorProcessingCommands&, const LaplacianEigenbasisConfig&);
    [[nodiscard]] std::optional<LaplacianEigenbasisConfig> GetEditorLaplacianEigenbasisConfig(const EditorProcessingCommands&);
    [[nodiscard]] ActionReadiness PreviewEditorLaplacianEigenbasisCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const LaplacianEigenbasisConfig&);
    [[nodiscard]] EditorLaplacianEigenbasisResult ApplyEditorLaplacianEigenbasisCommand(
        const EditorProcessingCommands&, std::uint32_t stableEntityId, const LaplacianEigenbasisConfig&);

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
    [[nodiscard]] std::span<const ConfigFieldSpec> HarmonicFieldConfigFieldSpecs() noexcept;
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
    [[nodiscard]] std::span<const ConfigFieldSpec> ScalarGradientConfigFieldSpecs() noexcept;
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

    // Admission: the run needs at least one source vertex or a source vertex property.
    [[nodiscard]] ActionReadiness PreviewEditorGeodesicsCommand(
        const EditorProcessingCommands&, const GeodesicsConfig&);
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
