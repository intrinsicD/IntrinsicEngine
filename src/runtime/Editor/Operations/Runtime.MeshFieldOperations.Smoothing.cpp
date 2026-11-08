// Typed property smoothing uses canonical domain capture and one guarded history transaction.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <initializer_list>
#include <limits>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <mutex>
module Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Smoothing;
import Geometry.HarmonicField;
import Extrinsic.Graphics.PropertyFilter;
import Extrinsic.Graphics.SparseConjugateGradient;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Private.WorkspaceLease;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.GeometryPresentation;
import Geometry.HalfedgeMesh.DEC;
import Geometry.Properties;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.ActionReadinessConfig.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.PropertyGraph.hpp"

namespace Extrinsic::Runtime
{
    template std::shared_ptr<Graphics::PropertyFilterWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::PropertyFilterWorkspace>();
    template std::shared_ptr<Graphics::SparseConjugateGradientWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::SparseConjugateGradientWorkspace>();

    namespace
    {
        namespace S = Geometry::Smoothing;
        namespace GP = GeometryProcessingDetail;
        namespace GS = ECS::Components::GeometrySources;
        using D = GeometryElementDomain;
        using K = Geometry::PropertyValueKind;
        using Json = nlohmann::json;
        constexpr std::string_view kSchema = "intrinsic.runtime.sandbox.property_smoothing";
        using FT = ConfigFieldType;
        constexpr std::array<K, 5> kFloating{K::Float, K::Double, K::Vec2, K::Vec3, K::Vec4};
        constexpr std::array<K, 1> kVec3{K::Vec3};
        constexpr std::array<K, 2> kFloatScalar{K::Float, K::Double};
        constexpr std::array<std::string_view, 5> kWeightNames{"Uniform kNN", "Gaussian kNN", "Inverse-distance kNN",
                                                               "Nonnegative mesh cotangent", "Uniform mesh edges"};
        constexpr std::array<std::string_view, 6> kMethodNames{"Averaging", "Spectral heat", "Taubin", "Bilateral",
                                                               "Implicit (backward Euler)", "Variational fit (robust / TV / bounded)"};
        constexpr std::array<std::string_view, 3> kLaplacianNames{"Random walk", "Combinatorial", "Lumped mesh area (implicit, fit)"};
        constexpr std::array<std::string_view, 2> kSolverNames{"Sparse Cholesky (direct)", "Conjugate gradient"};
        constexpr std::array<std::string_view, 3> kSmoothnessPenaltyNames{"Quadratic (Dirichlet)", "Huber", "L1 (total variation)"};
        constexpr std::array<std::string_view, 3> kDataPenaltyNames{"Quadratic", "Huber (robust)", "L1 (robust)"};
        constexpr std::array<std::string_view, 2> kFidelityNames{"Fixed weight", "Match noise level (discrepancy)"};
        constexpr std::array<std::string_view, 3> kBoundNames{"None", "Uniform radius", "Per-row radius property"};
        constexpr std::array<std::string_view, 2> kFitSolverNames{"Reweighted least squares (reference)",
                                                                  "ADMM (one factorization, delta 0 allowed)"};
        constexpr std::array<std::string_view, 2> kBoundNormNames{"Per channel (box)", "Euclidean (ball, ADMM)"};
        constexpr std::array<std::string_view, 2> kOrderNames{"First (differences)", "Second (non-local TGV, ADMM)"};
        constexpr std::array<std::string_view, 2> kBackendNames{"CPU reference", "Vulkan (shader double precision)"};
        // ARCH-023: the shared execution rows after max_iterations (the run's cap is `iterations`).
        constexpr auto kExecutionFields = StepRunExecutionFields(10000u);
        constexpr std::array kFields{
            ConfigFieldSpec{.Name = "input",
                .Type = FT::PropertyRef,
                .Description = "Floating scalar or vector property to smooth.",
                .RefKinds = kFloating},
            ConfigFieldSpec{.Name = "output",
                .Type = FT::PropertyRef,
                .Description = "Property receiving the result, on the input domain with the input's channel count.",
                .RefKinds = kFloating},
            ConfigFieldSpec{.Name = "positions",
                .Type = FT::PropertyRef,
                .Description = "Positions that define neighborhoods and distances.",
                .RefKinds = kVec3},
            ConfigFieldSpec{.Name = "weight",
                .Type = FT::Enum,
                .Description = "Neighbor weighting; the mesh weights need mesh-vertex input.",
                .EnumNames = kWeightNames},
            ConfigFieldSpec{
                .Name = "method", .Type = FT::Enum, .Description = "Smoothing filter.", .EnumNames = kMethodNames},
            ConfigFieldSpec{.Name = "laplacian",
                .Type = FT::Enum,
                .Description = "Laplacian normalization.",
                .EnumNames = kLaplacianNames},
            ConfigFieldSpec{.Name = "iterations",
                .Type = FT::UInt,
                .Description = "Filter passes of the explicit methods.",
                .Min = 1,
                .Max = 10000},
            ConfigFieldSpec{.Name = "neighbors",
                .Type = FT::UInt,
                .Description = "k of the k-nearest-neighbor graph.",
                .Min = 1,
                .Max = 1024},
            ConfigFieldSpec{.Name = "spatial_sigma",
                .Type = FT::Float,
                .Description = "Distance scale of Gaussian and bilateral weights (position units).",
                .Min = 0,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "lambda",
                .Type = FT::Float,
                .Description = "Step size of averaging, bilateral and Taubin passes.",
                .Min = 0,
                .Max = 1,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "mu",
                .Type = FT::Float,
                .Description = "Taubin's negative inflation step.",
                .Min = -1,
                .Max = 0,
                .ExclusiveMax = true},
            ConfigFieldSpec{.Name = "heat_time",
                .Type = FT::Float,
                .Description = "Diffusion time of the spectral heat filter.",
                .Min = 0,
                .Max = 1000,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "range_sigma",
                .Type = FT::Float,
                .Description = "Value scale of the bilateral filter (property units).",
                .Min = 0,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "time_step",
                .Type = FT::Float,
                .Description = "Backward-Euler time step of implicit smoothing.",
                .Min = 0,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "solver_tolerance",
                .Type = FT::Float,
                .Description = "Relative residual at which conjugate gradient stops.",
                .Min = 0,
                .Max = 1,
                .ExclusiveMin = true,
                .ExclusiveMax = true},
            ConfigFieldSpec{.Name = "max_solver_iterations",
                .Type = FT::UInt,
                .Description = "Conjugate-gradient iteration cap.",
                .Min = 1,
                .Max = 100000},
            ConfigFieldSpec{.Name = "solver",
                .Type = FT::Enum,
                .Description = "Linear solver of implicit smoothing.",
                .EnumNames = kSolverNames},
            ConfigFieldSpec{.Name = "preserve_boundary",
                .Type = FT::Bool,
                .Description = "Keep mesh-boundary values fixed (mesh-vertex input only)."},
            ConfigFieldSpec{.Name = "smoothness_penalty",
                .Type = FT::Enum,
                .Description = "Penalty on neighbor differences in the variational fit.",
                .EnumNames = kSmoothnessPenaltyNames},
            ConfigFieldSpec{.Name = "data_penalty",
                .Type = FT::Enum,
                .Description = "Penalty on deviation from the input in the variational fit.",
                .EnumNames = kDataPenaltyNames},
            ConfigFieldSpec{.Name = "fidelity",
                .Type = FT::Enum,
                .Description = "Fixed data weight, or the weight whose RMS residual matches the noise level.",
                .EnumNames = kFidelityNames},
            ConfigFieldSpec{.Name = "fit_weight",
                .Type = FT::Float,
                .Description = "Data weight of the variational fit.",
                .Min = 0,
                .Max = 1e12,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "noise_level",
                .Type = FT::Float,
                .Description = "Target RMS residual (property units).",
                .Min = 0,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "penalty_delta",
                .Type = FT::Float,
                .Description = "Huber/L1 smoothing threshold (property units); 0 only with ADMM and no Huber penalty.",
                .Min = 0},
            ConfigFieldSpec{.Name = "bound",
                .Type = FT::Enum,
                .Description = "Tolerance bound on the deviation from the input.",
                .EnumNames = kBoundNames},
            ConfigFieldSpec{.Name = "bound_radius",
                .Type = FT::Float,
                .Description = "Uniform bound radius (property units).",
                .Min = 0},
            ConfigFieldSpec{.Name = "bound_radii",
                .Type = FT::PropertyRef,
                .Description = "Per-row bound radii on the input domain.",
                .Nullable = true,
                .RefKinds = kFloatScalar},
            ConfigFieldSpec{.Name = "fit_solver",
                .Type = FT::Enum,
                .Description = "Variational-fit algorithm.",
                .EnumNames = kFitSolverNames},
            ConfigFieldSpec{.Name = "max_fit_iterations",
                .Type = FT::UInt,
                .Description = "Variational-fit iteration cap.",
                .Min = 1,
                .Max = 100000},
            ConfigFieldSpec{.Name = "bound_norm",
                .Type = FT::Enum,
                .Description = "Bound shape on vector properties.",
                .EnumNames = kBoundNormNames},
            ConfigFieldSpec{.Name = "smoothness_order",
                .Type = FT::Enum,
                .Description = "First-order differences or second-order non-local TGV.",
                .EnumNames = kOrderNames},
            ConfigFieldSpec{.Name = "second_order_weight",
                .Type = FT::Float,
                .Description = "Weight of the second-order TGV term.",
                .Min = 0,
                .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "backend",
                .Type = FT::Enum,
                .Description = "CPU reference or Vulkan for the explicit filters and CG implicit smoothing.",
                .EnumNames = kBackendNames},
            ConfigFieldSpec{.Name = "fit_tolerance",
                .Type = FT::Float,
                .Description = "Relative change at which the variational fit stops.",
                .Min = 0,
                .Max = 1,
                .ExclusiveMin = true,
                .ExclusiveMax = true},
            kExecutionFields[1],
            kExecutionFields[2],
            kExecutionFields[3],
            kExecutionFields[4],
            kExecutionFields[5],
        };
        using PropertyGraphDetail::Snapshot;
        using PropertyGraphDetail::Channels;
        using PropertyGraphDetail::Channel;
        using PropertyGraphDetail::SetChannel;
        using PropertyGraphDetail::CountMatches;
        using PropertyGraphDetail::Capture;
        using PropertyGraphDetail::Same;
        bool ExplicitFilter(S::PropertyFilter method)
        {
            return method == S::PropertyFilter::Averaging || method == S::PropertyFilter::SpectralHeat ||
                   method == S::PropertyFilter::Taubin || method == S::PropertyFilter::Bilateral;
        }
        bool PerRowBounds(const PropertySmoothingConfig& c)
        { return c.Filter.Method == S::PropertyFilter::VariationalFit && c.Filter.Bound == S::FitBound::PerRow; }
        // Specific reasons for the method/solver combinations a user can reach by switching controls;
        // plain range errors fall through to the generic parameter message.
        std::optional<std::string> FilterConflict(const PropertySmoothingConfig& c)
        {
            const auto& f = c.Filter;
            if (f.Laplacian == S::PropertyLaplacian::LumpedMass && f.Method != S::PropertyFilter::Implicit &&
                f.Method != S::PropertyFilter::VariationalFit)
                return "The lumped mesh-area Laplacian applies to implicit smoothing and the variational fit only; choose random walk or combinatorial.";
            if (f.Method != S::PropertyFilter::VariationalFit) return std::nullopt;
            if (f.FitAlgorithm == S::FitSolver::Reweighted)
            {
                if (f.SmoothnessOrder == S::FitOrder::Second) return "Second-order (TGV) smoothness needs the ADMM fit solver.";
                if (f.BoundNorm == S::FitBoundNorm::Euclidean) return "Euclidean (ball) bounds need the ADMM fit solver.";
                if (!(f.PenaltyDelta > 0)) return "The reweighted fit solver needs a positive penalty delta; use ADMM for delta 0.";
            }
            if (!(f.PenaltyDelta > 0) && (f.SmoothnessPenalty == S::FitPenalty::Huber || f.DataPenalty == S::FitPenalty::Huber))
                return "Huber penalties need a positive penalty delta.";
            return std::nullopt;
        }
        PropertySmoothingConfig Decode(const Json& doc)
        {
            PropertySmoothingConfig c;
            ConfigDetail::DecodePointPropertyRef(doc.at("input"), c.Input);
            ConfigDetail::DecodePointPropertyRef(doc.at("output"), c.Output);
            ConfigDetail::DecodePointPropertyRef(doc.at("positions"), c.Positions);
            c.Weight = S::PropertyWeight(doc.at("weight").get<unsigned>());
            c.Filter.Method = S::PropertyFilter(doc.at("method").get<unsigned>());
            c.Filter.Laplacian = S::PropertyLaplacian(doc.at("laplacian").get<unsigned>());
            c.Filter.Iterations = doc.at("iterations").get<std::uint32_t>();
            c.Neighbors = doc.at("neighbors").get<std::uint32_t>();
            c.SpatialSigma = doc.at("spatial_sigma").get<double>();
            c.Filter.Lambda = doc.at("lambda").get<double>();
            c.Filter.Mu = doc.at("mu").get<double>();
            c.Filter.HeatTime = doc.at("heat_time").get<double>();
            c.Filter.RangeSigma = doc.at("range_sigma").get<double>();
            c.Filter.TimeStep = doc.at("time_step").get<double>();
            c.Filter.SolverTolerance = doc.at("solver_tolerance").get<double>();
            c.Filter.MaxSolverIterations = doc.at("max_solver_iterations").get<std::uint32_t>();
            c.Filter.Solver = S::PropertySolver(doc.at("solver").get<unsigned>());
            c.PreserveBoundary = doc.at("preserve_boundary").get<bool>();
            c.Filter.SmoothnessPenalty = S::FitPenalty(doc.at("smoothness_penalty").get<unsigned>());
            c.Filter.DataPenalty = S::FitPenalty(doc.at("data_penalty").get<unsigned>());
            c.Filter.Fidelity = S::FitFidelity(doc.at("fidelity").get<unsigned>());
            c.Filter.FitWeight = doc.at("fit_weight").get<double>();
            c.Filter.NoiseLevel = doc.at("noise_level").get<double>();
            c.Filter.PenaltyDelta = doc.at("penalty_delta").get<double>();
            c.Filter.Bound = S::FitBound(doc.at("bound").get<unsigned>());
            c.Filter.BoundRadius = doc.at("bound_radius").get<double>();
            if (doc.at("bound_radii").is_null()) c.BoundRadii.Name.clear();
            else ConfigDetail::DecodePointPropertyRef(doc.at("bound_radii"), c.BoundRadii);
            c.Filter.FitAlgorithm = S::FitSolver(doc.at("fit_solver").get<unsigned>());
            c.Filter.BoundNorm = S::FitBoundNorm(doc.at("bound_norm").get<unsigned>());
            c.Filter.SmoothnessOrder = S::FitOrder(doc.at("smoothness_order").get<unsigned>());
            c.Filter.SecondOrderWeight = doc.at("second_order_weight").get<double>();
            c.Backend = PropertySmoothingBackend(doc.at("backend").get<unsigned>());
            c.Filter.MaxFitIterations = doc.at("max_fit_iterations").get<std::uint32_t>();
            c.Filter.FitTolerance = doc.at("fit_tolerance").get<double>();
            // The merged document passed the field table: the tokens parse.
            c.Execution.PreviewEvery = doc.at("preview_every").get<std::uint32_t>();
            c.Execution.PreviewIntervalMs = doc.at("preview_interval_ms").get<std::uint32_t>();
            c.Execution.PreviewBuffers = doc.at("preview_buffers").get<std::uint32_t>();
            c.Execution.AutoAccept =
                ParseStepRunAutoAccept(doc.at("auto_accept").get<std::string>()).value_or(StepRunAutoAccept::Never);
            c.Execution.Start = ParseStepRunStart(doc.at("start").get<std::string>()).value_or(StepRunStart::Ready);
            return c;
        }
        Core::Config::EngineConfigSectionValidationResult Validate(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            const auto doc = Json::parse(payload, nullptr, false);
            auto merged = Json::parse(SerializePropertySmoothingConfig({}));
            if (auto error = ConfigDetail::ValidateDeclaredFields(doc, merged, kFields,
                    "Smoothing config must be an object.", "Unknown smoothing field: "))
                return ConfigDetail::RejectConfigSection(subject, *error);
            const auto c = Decode(merged);
            if (auto conflict = FilterConflict(c)) return ConfigDetail::RejectConfigSection(subject, *conflict);
            // Ranges come from kFields; the geometry check keeps its solver/penalty combinations.
            if (!S::ValidatePropertyFilterParams(c.Filter))
                return ConfigDetail::RejectConfigSection(subject, "Invalid smoothing parameter combination: penalty delta 0 needs ADMM without Huber penalties; Euclidean bounds and second order need ADMM; the lumped Laplacian applies to implicit smoothing and the fit.");
            if (c.Backend == PropertySmoothingBackend::Vulkan && !ExplicitFilter(c.Filter.Method) &&
                !(c.Filter.Method == S::PropertyFilter::Implicit && c.Filter.Solver == S::PropertySolver::ConjugateGradient))
                return ConfigDetail::RejectConfigSection(subject, "Vulkan runs averaging, spectral heat, Taubin, bilateral and conjugate-gradient implicit smoothing; direct implicit and variational smoothing are CPU-only.");
            if (PerRowBounds(c) && (c.BoundRadii.Name.empty() || c.BoundRadii.Domain != c.Input.Domain))
                return ConfigDetail::RejectConfigSection(subject, "Per-row bounds need a float/double radius property on the input domain.");
            if (c.Input.Domain != c.Output.Domain || GeometryPropertyComponentCount(c.Input.ValueKind) != GeometryPropertyComponentCount(c.Output.ValueKind) ||
                c.Positions.ValueKind != K::Vec3 || c.Input.Name.empty() || c.Output.Name.empty() || c.Positions.Name.empty() ||
                IsTopologyProperty(c.Output.Domain, c.Output.Name) ||
                (IsStructuralVertexProperty(c.Output.Name) && c.Output.Name != GS::PropertyNames::kPosition) ||
                (c.Output.Name == GS::PropertyNames::kPosition && (c.Output.ValueKind != K::Vec3 || c.Output.Domain != c.Positions.Domain)))
                return ConfigDetail::RejectConfigSection(subject, "Output must have matching channels on the input domain and cannot replace structural storage.");
            if (c.Input.Domain != D::MeshVertex || c.Positions.Domain != D::MeshVertex)
            {
                const auto meshOnly = [&](std::string_view option) {
                    return ConfigDetail::RejectConfigSection(subject, std::string{option} +
                        " needs mesh-vertex input and vertex positions; choose kNN weights, the random-walk or combinatorial Laplacian, or a mesh-vertex property.");
                };
                if (c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform) return meshOnly("Mesh edge weighting");
                if (c.PreserveBoundary) return meshOnly("Boundary pinning");
                if (c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass) return meshOnly("The lumped mesh-area Laplacian");
            }
            return {.State = Core::Config::EngineConfigState::Valid,
                    .CanonicalPayloadJson = SerializePropertySmoothingConfig(c),
                    .ParsedFieldCount = static_cast<std::uint32_t>(doc.size())};
        }
        Core::Config::EngineConfigSection Section(const PropertySmoothingConfig& c)
        { return {.Name = std::string{kPropertySmoothingConfigSectionName}, .SchemaId = std::string{kSchema},
                  .SchemaVersion = 1u, .PayloadJson = SerializePropertySmoothingConfig(c)}; }

        // Every independent blocking reason in check order; property checks need the workspace and
        // entity and skip a field whose config value is already invalid.
        std::optional<ECS::EntityHandle> Target(const EditorProcessingContext& context, std::uint32_t id,
            const PropertySmoothingConfig& c, std::vector<ActionReadinessReason>& reasons)
        {
            using C = ActionReadinessCode;
            const auto payload = SerializePropertySmoothingConfig(c);
            AppendConfigReadinessReasons(reasons, payload, kFields, Validate(payload, {}, kPropertySmoothingConfigSectionName));
            if (!context.Scene || (context.AttachmentActive && !context.AttachmentActive()) || !GP::EditorProcessingContextWorldCurrent(context))
            {
                reasons.push_back({C::WorkspaceUnavailable, {}, "Workspace is unavailable."});
                return {};
            }
            const auto open = [&](std::string_view field) { return !ReadinessNamesField(reasons, field); };
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity) reasons.push_back({C::MissingEntity, {}, "Choose an existing geometry entity."});
            else
            {
                const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
                const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
                if (open("input") && (!props || !ResolveGeometryProperty(a, c.Input, props->Size(), false).Resolved() || !CountMatches(*props, c.Input)))
                    reasons.push_back({C::MissingProperty, "input", "Choose an existing floating scalar or vector property."});
                if (props && open("output") && props->Exists(c.Output.Name) &&
                    (!ResolveGeometryProperty(a, c.Output, props->Size(), false).Resolved() || !CountMatches(*props, c.Output)))
                    reasons.push_back({C::IncompatibleProperty, "output", "Output exists with a different storage type or cardinality."});
                if (open("positions"))
                {
                    const auto* positions = ResolveGeometryPropertySet(a, c.Positions.Domain);
                    if (c.Positions.Domain != c.Input.Domain && c.Positions.Domain != GP::PrimaryPointDomain(a))
                        reasons.push_back({C::WrongDomain, "positions", "Positions must belong to the input domain or the entity's vertices/nodes."});
                    else if (!positions || !ResolveGeometryProperty(a, c.Positions, positions->Size(), false).Resolved())
                        reasons.push_back({C::MissingProperty, "positions", "Choose an existing vec3 position property."});
                }
                if (props && open("bound_radii") && PerRowBounds(c) &&
                    (!ResolveGeometryProperty(a, c.BoundRadii, props->Size(), false).Resolved() || !CountMatches(*props, c.BoundRadii)))
                    reasons.push_back({C::MissingProperty, "bound_radii", "Choose an existing float/double bound radius property."});
            }
            if (c.Backend == PropertySmoothingBackend::Vulkan &&
                (!context.Device || !context.Device->IsOperational() || !context.Device->SupportsShaderFloat64() ||
                 !context.SpatialIndices || !context.SpatialIndices->GpuQueriesAvailable() || !context.JobCommands.Available()))
                reasons.push_back({C::DeviceUnavailable, "backend", "Vulkan property smoothing needs an operational device with shader double precision and framed GPU jobs."});
            if (!reasons.empty()) return {};
            return entity;
        }
    }

    void ReconcilePropertySmoothingConfig(PropertySmoothingConfig& c, const PropertySmoothingConfig& before)
    {
        auto& f = c.Filter;
        const auto& b = before.Filter;
        constexpr double kDefaultDelta = S::PropertyFilterParams{}.PenaltyDelta;
        const bool meshFamily = c.Input.Domain >= D::MeshVertex && c.Input.Domain <= D::MeshFace;
        const bool graphFamily = c.Input.Domain >= D::GraphNode && c.Input.Domain <= D::GraphEdge;
        if (!(c.Input == before.Input))
        {
            if (before.Output.Name == before.Input.Name && before.Output.Domain == before.Input.Domain) c.Output = c.Input;
            else c.Output = {c.Input.Domain, c.Input.Name + "_smoothed", c.Input.ValueKind};
            c.BoundRadii.Domain = c.Input.Domain;
            // Positions stay when they sit on the input domain or its family's vertices/nodes.
            if (c.Positions.Domain != c.Input.Domain)
            {
                if (meshFamily && c.Positions.Domain != D::MeshVertex) c.Positions = {D::MeshVertex, std::string{GS::PropertyNames::kPosition}, K::Vec3};
                else if (graphFamily && c.Positions.Domain != D::GraphNode) c.Positions = {D::GraphNode, std::string{GS::PropertyNames::kPosition}, K::Vec3};
                else if (c.Input.Domain == D::PointCloudPoint) c.Positions = {D::PointCloudPoint, "p:position", K::Vec3};
            }
        }
        if ((!(c.Input == before.Input) || !(c.Positions == before.Positions)) &&
            (c.Input.Domain != D::MeshVertex || c.Positions.Domain != D::MeshVertex))
        {
            if (c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform) c.Weight = S::PropertyWeight::Uniform;
            c.PreserveBoundary = false;
            if (f.Laplacian == S::PropertyLaplacian::LumpedMass) f.Laplacian = S::PropertyLaplacian::RandomWalk;
        }
        if (f.Method != b.Method)
        {
            if (f.Laplacian == S::PropertyLaplacian::LumpedMass && f.Method != S::PropertyFilter::Implicit &&
                f.Method != S::PropertyFilter::VariationalFit)
                f.Laplacian = S::PropertyLaplacian::RandomWalk;
            if (c.Backend == PropertySmoothingBackend::Vulkan && (f.Method == S::PropertyFilter::VariationalFit ||
                (f.Method == S::PropertyFilter::Implicit && f.Solver == S::PropertySolver::Direct)))
                c.Backend = PropertySmoothingBackend::Cpu;
        }
        if (c.Backend != before.Backend && c.Backend == PropertySmoothingBackend::Vulkan) f.Solver = S::PropertySolver::ConjugateGradient;
        if (f.Solver != b.Solver && f.Solver == S::PropertySolver::Direct) c.Backend = PropertySmoothingBackend::Cpu;
        // Variational fit: the edited fit option wins over the solver.
        if (f.FitAlgorithm != b.FitAlgorithm && f.FitAlgorithm == S::FitSolver::Reweighted)
        {
            f.SmoothnessOrder = S::FitOrder::First;
            f.BoundNorm = S::FitBoundNorm::PerChannel;
            if (!(f.PenaltyDelta > 0)) f.PenaltyDelta = kDefaultDelta;
        }
        if ((f.SmoothnessOrder != b.SmoothnessOrder && f.SmoothnessOrder == S::FitOrder::Second) ||
            (f.BoundNorm != b.BoundNorm && f.BoundNorm == S::FitBoundNorm::Euclidean))
            f.FitAlgorithm = S::FitSolver::Admm;
        const bool penaltyEdited = f.SmoothnessPenalty != b.SmoothnessPenalty || f.DataPenalty != b.DataPenalty;
        const bool huber = f.SmoothnessPenalty == S::FitPenalty::Huber || f.DataPenalty == S::FitPenalty::Huber;
        if (penaltyEdited && !(f.PenaltyDelta > 0) && (huber || f.FitAlgorithm == S::FitSolver::Reweighted)) f.PenaltyDelta = kDefaultDelta;
        if (f.Bound != b.Bound && f.Bound == S::FitBound::PerRow) c.BoundRadii.Domain = c.Input.Domain;
    }

    std::string SerializePropertySmoothingConfig(const PropertySmoothingConfig& c)
    {
        return Json{{"input", ConfigDetail::EncodePointPropertyRef(c.Input)},
            {"output", ConfigDetail::EncodePointPropertyRef(c.Output)},
            {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"weight", unsigned(c.Weight)},
            {"method", unsigned(c.Filter.Method)},
            {"laplacian", unsigned(c.Filter.Laplacian)},
            {"iterations", c.Filter.Iterations},
            {"neighbors", c.Neighbors},
            {"spatial_sigma", c.SpatialSigma},
            {"lambda", c.Filter.Lambda},
            {"mu", c.Filter.Mu},
            {"heat_time", c.Filter.HeatTime},
            {"range_sigma", c.Filter.RangeSigma},
            {"time_step", c.Filter.TimeStep},
            {"solver_tolerance", c.Filter.SolverTolerance},
            {"max_solver_iterations", c.Filter.MaxSolverIterations},
            {"solver", unsigned(c.Filter.Solver)},
            {"preserve_boundary", c.PreserveBoundary},
            {"smoothness_penalty", unsigned(c.Filter.SmoothnessPenalty)},
            {"data_penalty", unsigned(c.Filter.DataPenalty)},
            {"fidelity", unsigned(c.Filter.Fidelity)},
            {"fit_weight", c.Filter.FitWeight},
            {"noise_level", c.Filter.NoiseLevel},
            {"penalty_delta", c.Filter.PenaltyDelta},
            {"bound", unsigned(c.Filter.Bound)},
            {"bound_radius", c.Filter.BoundRadius},
            {"bound_radii",
                c.BoundRadii.Name.empty() ? Json(nullptr) : ConfigDetail::EncodePointPropertyRef(c.BoundRadii)},
            {"fit_solver", unsigned(c.Filter.FitAlgorithm)},
            {"max_fit_iterations", c.Filter.MaxFitIterations},
            {"bound_norm", unsigned(c.Filter.BoundNorm)},
            {"smoothness_order", unsigned(c.Filter.SmoothnessOrder)},
            {"second_order_weight", c.Filter.SecondOrderWeight},
            {"backend", unsigned(c.Backend)},
            {"fit_tolerance", c.Filter.FitTolerance},
            {"preview_every", c.Execution.PreviewEvery},
            {"preview_interval_ms", c.Execution.PreviewIntervalMs},
            {"preview_buffers", c.Execution.PreviewBuffers},
            {"auto_accept", std::string{ToString(c.Execution.AutoAccept)}},
            {"start", std::string{ToString(c.Execution.Start)}}}
            .dump();
    }
    Core::Config::EngineConfigSectionRegistration MakePropertySmoothingConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = Validate,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kSchema, "Smooth Property",
                    "Smooths a scalar or vector property over mesh edges or a kNN graph (averaging, spectral heat, "
                    "Taubin, bilateral, implicit, variational fit).",
                    kFields, Json::parse(SerializePropertySmoothingConfig({})))};
    }
    std::span<const ConfigFieldSpec> PropertySmoothingConfigFieldSpecs() noexcept { return kFields; }
    RuntimeEngineConfigApplyResult ApplyEditorPropertySmoothingConfig(const EditorProcessingCommands& commands, const PropertySmoothingConfig& c)
    {
        return ApplyEditorProcessingConfig(commands, Validate(SerializePropertySmoothingConfig(c), {}, kPropertySmoothingConfigSectionName),
            std::string{kPropertySmoothingConfigSectionName}, [&](Core::Config::EngineConfig& config) {
                Core::Config::UpsertEngineConfigSection(config.AppSections, Section(c)); });
    }
    std::optional<PropertySmoothingConfig> GetEditorPropertySmoothingConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return {};
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(context.EngineConfigControlState->ActiveConfig,
            kPropertySmoothingConfigSectionName, kSchema, 1u, nullptr, Validate);
        return payload ? std::optional{Decode(Json::parse(*payload))} : std::nullopt;
    }
    ActionReadiness PreviewEditorPropertySmoothingCommand(const EditorProcessingCommands& commands, std::uint32_t id, const PropertySmoothingConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::vector<ActionReadinessReason> reasons;
        (void)Target(context, id, c, reasons);
        return MakeActionReadiness(std::move(reasons));
    }

    // Publication and the device parameters shared by the synchronous path and the transaction.
    namespace PropertySmoothingDetail
    {
        using PropertyGraphDetail::Snapshot;
        using PropertyGraphDetail::Channels;
        using PropertyGraphDetail::Channel;
        using PropertyGraphDetail::SetChannel;
        using PropertyGraphDetail::Capture;
        using PropertyGraphDetail::Same;
        // Everything publication needs after the filter ran, synchronously or after a GPU readback.
        struct SmoothingPublication
        {
            ECS::EntityHandle Entity{};
            PropertySmoothingConfig Config{};
            std::vector<std::uint32_t> Slots{};
            std::size_t SlotCount{}, Channels{};
            Snapshot Before{};
            decltype(GP::PointInputCapture::Inputs) Watches{};
        };

        EditorPropertySmoothingResult Publish(const EditorProcessingContext& context, const SmoothingPublication& p,
                                              const S::PropertyFilterResult& filtered, EditorPropertySmoothingResult result,
                                              const std::string& summary)
        {
            if (!filtered.Success)
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Message = filtered.Diagnostic;
                return result;
            }
            Snapshot after = p.Before;
            after.Exists = true;
            bool representable = true;
            std::visit([&](auto& rows) {
                using T = typename std::decay_t<decltype(rows)>::value_type;
                rows.resize(p.SlotCount, T{0});
                for (std::size_t i = 0; i < p.Slots.size(); ++i)
                    for (std::size_t ch = 0; ch < Channels<T>(); ++ch)
                    {
                        const double value = filtered.Values[i * p.Channels + ch];
                        if constexpr (!std::is_same_v<T, double>)
                            if (std::abs(value) > std::numeric_limits<float>::max()) { representable = false; continue; }
                        SetChannel(rows[p.Slots[i]], ch, value);
                    }
            }, after.Values);
            if (!representable)
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Message = "Smoothed values exceed output storage range.";
                return result;
            }
            result.OperatorApplications = filtered.OperatorApplications;
            // The diagnostic starts with the backend identity; a CG fallback note may follow.
            result.BackendId = filtered.Diagnostic.substr(0, filtered.Diagnostic.find(' '));
            if (Same(p.Before, after)) { result.Status = EditorCommandStatus::NoChange; result.Message = "Smoothed property is unchanged."; return result; }
            const auto mutate = [context, entity = p.Entity, output = p.Config.Output, watches = p.Watches]
                (const Snapshot& expected, const Snapshot& target) {
                if (!GP::GeometryPropertiesCurrent(context, entity, watches) || !GP::EditorProcessingContextWorldCurrent(context))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* properties = GP::MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
                if (!properties || !Same(Capture(*properties, output), expected)) return EditorCommandHistoryStatus::StaleEntity;
                const auto historyBefore = GP::ObserveGeometryProperties(context, entity, std::span(&output, 1));
                std::visit([&](const auto& rows) {
                    using T = typename std::decay_t<decltype(rows)>::value_type;
                    if (target.Exists) properties->GetOrAdd<T>(output.Name).Vector() = rows;
                    else if (auto property = properties->Get<T>(output.Name)) properties->Remove(property);
                }, target.Values);
                GP::RebasePointPropertyHistory(context, entity, historyBefore);
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
                if (output.Name == GS::PropertyNames::kPosition) ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(), entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto& before = p.Before;
            const auto status = context.CommandHistory ? context.CommandHistory->Execute({.Label = "Smooth property",
                .Redo = [mutate, before, after] { return mutate(before, after); },
                .Undo = [mutate, before, after] { return mutate(after, before); }}).Status : mutate(before, after);
            result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
            result.Message = result.Succeeded() ? "Property smoothed (" + filtered.Diagnostic + summary + ")." : "Property publication rejected by history guards.";
            return result;
        }

        Graphics::PropertyFilterGpuParams GpuParams(const S::PropertyFilterParams& f, const S::PropertyFilterPlan& plan)
        {
            // Mirrors FilterProperty: combinatorial explicit steps divide by the maximum degree,
            // and a zero rate skips every iteration.
            const double scale = f.Laplacian == S::PropertyLaplacian::Combinatorial ? plan.Rate : 1.0;
            return {.Method = f.Method == S::PropertyFilter::SpectralHeat ? Graphics::PropertyFilterGpuMethod::SpectralHeat
                      : f.Method == S::PropertyFilter::Taubin ? Graphics::PropertyFilterGpuMethod::Taubin
                      : f.Method == S::PropertyFilter::Bilateral ? Graphics::PropertyFilterGpuMethod::Bilateral
                      : Graphics::PropertyFilterGpuMethod::Averaging,
                    .RandomWalk = f.Laplacian == S::PropertyLaplacian::RandomWalk,
                    .Iterations = plan.Rate > 0 ? f.Iterations : 0u,
                    .Step = plan.Rate > 0 ? f.Lambda / scale : 0.0, .TaubinStep = plan.Rate > 0 ? f.Mu / scale : 0.0,
                    .RangeSigma = f.RangeSigma, .HeatStep = plan.Rate > 0 ? 1.0 / plan.Rate : 0.0,
                    .HeatSplits = std::uint32_t(plan.HeatSplits), .HeatCoefficients = plan.HeatCoefficients,
                    .HeatMass = plan.HeatMass > 0 ? plan.HeatMass : 1.0};
        }

        // Every input the run captured, ready for the CPU kernels or the device.
        struct Prepared
        {
            ECS::EntityHandle Entity{};
            GP::PointInputCapture Samples{};
            const Geometry::PropertySet* Props{};
            std::size_t Channels{};
            std::vector<double> Values{};
            PropertyGraphDetail::Graph Graph{};
            SmoothingPublication Publication{};
            // The output's own revision: not a publication guard (the aliased output is
            // guarded by its expected values), but an edit of it makes a waiting GPU result stale.
            GP::PointPropertyWatch OutputWatch{};
        };
        std::optional<Prepared> Prepare(const EditorProcessingContext& context, const std::uint32_t id,
                                        const PropertySmoothingConfig& c, EditorPropertySmoothingResult& result,
                                        std::string& diagnostic)
        {
            std::vector<ActionReadinessReason> reasons;
            const auto entity = Target(context, id, c, reasons);
            if (!entity) { diagnostic = reasons.front().Message; return std::nullopt; }
            Prepared prepared{.Entity = *entity};
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (!PropertyGraphDetail::CaptureSamples(a, c.Input.Domain, c.Positions, prepared.Samples, diagnostic)) return std::nullopt;
            prepared.Props = ResolveGeometryPropertySet(a, c.Input.Domain);
            const auto input = Capture(*prepared.Props, c.Input);
            prepared.Channels = GeometryPropertyComponentCount(c.Input.ValueKind);
            std::visit([&](const auto& rows) {
                for (const auto slot : prepared.Samples.Slots)
                    for (std::size_t ch = 0; ch < prepared.Channels; ++ch) prepared.Values.push_back(Channel(rows[slot], ch));
            }, input.Values);
            if (!PropertyGraphDetail::BuildGraph(a, {.Positions = c.Positions, .Weight = c.Weight, .Neighbors = c.Neighbors,
                    .SpatialSigma = c.SpatialSigma, .LumpedMass = c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass,
                    .BoundaryRows = c.PreserveBoundary, .Operation = "Property smoothing"}, prepared.Samples, prepared.Graph, diagnostic))
                return std::nullopt;
            result.LiveCount = prepared.Samples.LiveCount;
            result.EdgeCount = prepared.Graph.Edges.size();
            // An aliased output is guarded by its expected values; unrelated input revisions stay fixed.
            auto& publication = prepared.Publication;
            publication = {.Entity = *entity, .Config = c, .Slots = prepared.Samples.Slots, .SlotCount = prepared.Samples.SlotCount,
                           .Channels = prepared.Channels, .Before = Capture(*prepared.Props, c.Output), .Watches = prepared.Samples.Inputs};
            publication.Watches.push_back(GP::ObserveGeometryProperty(a, c.Input.Domain, c.Input.Name));
            if (PerRowBounds(c)) publication.Watches.push_back(GP::ObserveGeometryProperty(a, c.BoundRadii.Domain, c.BoundRadii.Name));
            std::erase_if(publication.Watches, [&](const auto& watch) { return watch.Domain == c.Output.Domain && watch.Name == c.Output.Name; });
            prepared.OutputWatch = GP::ObserveGeometryProperty(a, c.Output.Domain, c.Output.Name);
            return prepared;
        }
    }

    // The Vulkan run's job state (ADR 0030 decision 1): the capture, the device workspaces, the
    // residency slots it reads and writes, and its logical steps. ARCH-023: the transaction is
    // worker-stepped (Core.Pages): its Init (Prepare, main thread) gathers the input and uploads the run's
    // constants in one owner page; each step then runs on the step run's worker, recording and waiting for its own
    // bounded pages without a frame: one explicit pass, or one implicit time step over every channel (its chained
    // CG solves in bounded chunks; the chunks are not steps). A step reads back only its completion word or the
    // solver reports. Live preview copies the latest step into the write slot the owner offers (Mailbox); Accept's
    // store of the latest step is an owner page.
    struct EditorPropertySmoothingTransaction : std::enable_shared_from_this<EditorPropertySmoothingTransaction>
    {
        // The shared Run/Accept lifecycle: ring 0 is the output (read back on Accept), ring 1 the
        // float presentation ring beside a double scalar's typed ring.
        GP::GpuTransactionCore Core{};
        PropertySmoothingDetail::SmoothingPublication Publication{};
        std::vector<double> Values{};
        S::PropertyFilterPlan Plan{};
        std::vector<std::uint32_t> Edges{}, Fixed{};
        std::vector<double> Weights{};
        Graphics::PropertyFilterGpuParams Params{};
        // Implicit smoothing: the assembled system as chained CG solves (iterations x channels).
        bool Implicit{};
        std::vector<std::uint32_t> Offsets{}, Columns{};
        S::PropertyImplicitSystem System{};
        // Device workspaces leased from the spatial cache for the whole run (until Accept, Discard or
        // the run's end); each recorder captures its own lease, so queued work keeps them alive.
        std::shared_ptr<Graphics::SparseConjugateGradientWorkspace> Solver{};
        std::shared_ptr<Graphics::PropertyFilterWorkspace> Filter{}; // the explicit kernels, or the CG path's store
        EditorPropertySmoothingResult Result{};
        std::function<void(EditorPropertySmoothingResult)> Sink{};
        bool Presentation{};
        std::uint32_t Count{}; // property rows
        // Input: the canonical slot the kernels gather from; Base: the output property's
        // canonical slot when it exists (rows outside the samples keep its bytes).
        std::optional<Graphics::GpuPropertyView> Input{}, Base{};
        std::vector<std::uint32_t> RestoreMask{}; // fixed or isolated working rows keep the input value
        GP::PointPropertyWatch OutputWatch{};
        std::uint64_t CpuStageBytes{}; // implicit: the CPU-assembled coupling uploaded per run (ADR 0030, 8)
        std::uint32_t Previews{};
        // The chunk in flight; Done: steps the device completed (the step run's count); Recorded: the steps the
        // explicit chunk in flight adds; Limit: the implicit chunk's solve bound; Stored: the step the ring's front
        // holds, Storing: the step the chunk in flight stores.
        std::shared_ptr<SpatialGpuResult> Chunk{};
        std::atomic<std::uint32_t> Done{}; // written by a worker step, read by the owner once none runs (or here)
        std::uint32_t Recorded{}, Limit{};
        std::optional<std::uint32_t> Stored{}, Storing{};
        std::size_t Applications{};
        std::string Failure{};
        // Accept's test seam: the front's values (no readback).
        std::optional<std::vector<double>> TestFront{};
        // ARCH-023: what the worker steps with, captured by Prepare and touched only by a step or preview (one at a
        // time) while the run computes: its own workspace references (Release drops the owner's at once), the input
        // endpoints and the solver reports of the step in flight.
        struct WorkerView
        {
            std::shared_ptr<Graphics::SparseConjugateGradientWorkspace> Solver{};
            std::shared_ptr<Graphics::PropertyFilterWorkspace> Filter{};
            Graphics::PropertyFilterResidentIo Io{};
            std::vector<std::byte> Reports{};
        } Worker{};
        // The live preview's write slot, offered by the owner and borrowed by a preview copy; the buffers ever
        // offered (at most the rings' depth) are stamped before the run's slots go. InitPage: the Init's owner page.
        struct PreviewSlot
        {
            std::optional<Graphics::GpuPropertyView> Output{}, Presentation{};
        };
        StepPreviewMailbox<PreviewSlot> Mailbox{};
        std::vector<RHI::BufferHandle> Offered{};
        std::shared_ptr<SpatialGpuResult> InitPage{};
        std::uint32_t Reported{}; // the step count last reported as the Run job's progress
    };

    namespace PropertySmoothingDetail
    {
        using Work = std::shared_ptr<EditorPropertySmoothingTransaction>;
        using PreviewSlot = EditorPropertySmoothingTransaction::PreviewSlot;
        constexpr const char* kRefused = "Vulkan property smoothing could not queue its device work; previous output retained.";

        // The captured inputs and the output are unchanged (an edit or undo of either while
        // the run computes or waits makes it stale; the publication keeps its own value guard).
        bool InputsCurrent(
            const EditorProcessingContext& ctx, const SmoothingPublication& p, const GP::PointPropertyWatch& output)
        {
            return GP::GeometryPropertiesCurrent(ctx, p.Entity, p.Watches) &&
                GP::GeometryPropertiesCurrent(ctx, p.Entity, std::span{&output, 1});
        }
        bool Current(const Work& w) { return GP::GpuTransactionCurrent(w->Core); }
        auto& Output(const Work& w) { return w->Core.Rings[0]; }
        auto& PresentationRing(const Work& w) { return w->Core.Rings[1]; }
        std::uint32_t ChannelCount(const Work& w)
        {
            return std::uint32_t(w->Plan.Channels);
        }
        std::uint32_t TotalSolves(const Work& w)
        {
            return w->Publication.Config.Filter.Iterations * ChannelCount(w);
        }
        // Recorders own their workspace leases (the cache keeps them until the readback is
        // safe), so the run returns its own at once.
        void ReleaseWorkspaces(const Work& w)
        {
            w->Solver.reset();
            w->Filter.reset();
        }
        void Finish(const Work& w, const EditorGpuTransactionPhase phase, const EditorCommandStatus status, std::string message)
        {
            GP::FinishGpuTransaction(w->Core, phase, status, std::move(message));
        }
        Graphics::PropertyFilterResidentView View(const std::optional<Graphics::GpuPropertyView>& v)
        {
            if (!v) return {};
            return {.Buffer = v->Buffer, .Address = v->Address, .Double = v->Layout.Scalar == Graphics::GpuScalarType::Float64};
        }
        Graphics::PropertyFilterResidentIo ResidentIo(const Work& w)
        {
            const auto& back = Output(w).Back;
            const auto& presentation = PresentationRing(w).Back;
            return {.Input = View(w->Input), .Output = View(back), .OutputBytes = back ? back->Bytes : 0u,
                    .Base = View(w->Base), .Presentation = View(presentation),
                    .PresentationBytes = presentation ? presentation->Bytes : 0u,
                    .Slots = w->Publication.Slots, .RestoreMask = w->RestoreMask,
                    .Rows = std::uint32_t(w->Publication.Slots.size())};
        }
        // The frame whose commands touch the run's slots (their reuse waits for it).
        void NoteUses(const Work& w)
        {
            if (!w->Core.Residency || !w->Core.Context.Device) return;
            const auto frame = w->Core.Context.Device->GetGlobalFrameNumber();
            for (const auto* view : {&w->Input, &w->Base, &Output(w).Back, &PresentationRing(w).Back})
                if (*view) w->Core.Residency->NoteUse((*view)->Buffer, frame);
        }
        // The canonical input (uploaded once per revision) and, for an existing output, its canonical slot. False
        // while the residency defers (a refused upload); the caller polls again next frame.
        bool AcquireInputs(const Work& w)
        {
            auto& r = *w->Core.Residency;
            const auto& c = w->Publication.Config;
            const auto& ctx = w->Core.Context;
            const auto entity = w->Publication.Entity;
            if (!w->Input)
            {
                w->Input = ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, entity, c.Input);
                if (!w->Input || w->Input->Layout.Count != w->Count) { w->Input.reset(); return false; }
            }
            // An existing output keeps its bytes outside the samples: the store copies its
            // canonical slot first (the input's own slot when the output is the input).
            if (w->Publication.Before.Exists && !w->Base)
            {
                const bool aliased = c.Output.Domain == c.Input.Domain && c.Output.Name == c.Input.Name;
                w->Base = aliased ? w->Input : ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, entity, c.Output);
                if (!w->Base || w->Base->Layout.Count != w->Count) { w->Base.reset(); return false; }
            }
            return true;
        }
        // A write slot of the output ring(s); false while the ring is exhausted or another run created it.
        bool AcquireBack(const Work& w)
        {
            const auto& c = w->Publication.Config;
            const auto entity = w->Publication.Entity;
            if (GP::AcquireGpuTransactionBack(w->Core, 0, entity, c.Output, w->Count, c.Execution.PreviewBuffers) !=
                GP::GpuRingAcquisition::Ready)
                return false;
            return !w->Presentation ||
                GP::AcquireGpuTransactionBack(
                    w->Core, 1, entity, GpuPropertyPresentationRef(c.Output), w->Count, c.Execution.PreviewBuffers) ==
                GP::GpuRingAcquisition::Ready;
        }
        // The run fails (CompleteRun reports it) without queueing more work.
        bool Fail(const Work& w, std::string diagnostic)
        {
            w->Failure = std::move(diagnostic);
            return true;
        }
        bool Defer(const Work& w)
        {
            if (!GP::GpuTransactionDeferralsExhausted(w->Core)) return false;
            return Fail(
                w, "The GPU property residency refused the run's input or output slot; previous output retained.");
        }
        // The submission that wrote Back has finished: its slot becomes the front the renderer shows.
        void PublishPreview(const Work& w)
        {
            const auto storing = std::exchange(w->Storing, std::nullopt);
            Output(w).Back.reset();
            PresentationRing(w).Back.reset();
            auto* residency = w->Core.Residency;
            if (!storing || !residency)
                return;
            if (residency->Publish(Output(w).Key))
            {
                ++w->Previews;
                w->Stored = storing;
            }
            if (w->Presentation) (void)residency->Publish(PresentationRing(w).Key);
        }
        void Queued(const Work& w, std::shared_ptr<SpatialGpuResult> chunk)
        {
            w->Chunk = w->Core.Gpu = std::move(chunk);
            GP::GpuTransactionRecording(w->Core, true);
            w->Core.Deferrals = 0u;
        }
        // Accept's store of the current values into the ring's write slot (an owner page, after the Init began the
        // explicit run). The result stays on the device; the readback is one word that only reports completion.
        void QueueExplicitStore(const Work& w)
        {
            w->Recorded = 0u;
            w->Storing = w->Done.load();
            Queued(w,
                w->Core.Context.SpatialIndices->QueueGpuCompute(sizeof(double),
                    [w, filter = w->Filter](
                        RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle
                    {
                        if (w->Core.Abandoned)
                            return {};
                        NoteUses(w);
                        const auto io = ResidentIo(w);
                        const auto buffer = filter->RecordIterations(commands, 0u);
                        if (!buffer.IsValid() || !filter->RecordStoreCurrent(commands, io))
                            return {};
                        return buffer;
                    }));
        }
        // One bounded chunk (GRAPHICS-150) of the chained solves below `limit`; each chunk reads back only the
        // reports. It stores the latest complete time step into the ring when a write slot is held.
        void QueueImplicit(const Work& w, const std::uint32_t limit)
        {
            const auto& ctx = w->Core.Context;
            w->Limit = limit;
            w->Storing.reset();
            Queued(w,
                ctx.SpatialIndices->QueueGpuCompute(
                    Graphics::SparseConjugateGradientWorkspace::ReportReadbackBytes(TotalSolves(w)),
                    [w, limit, solver = w->Solver, filter = w->Filter](
                        RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle
                    {
                        if (w->Core.Abandoned)
                            return {};
                        const auto channels = ChannelCount(w), rows = std::uint32_t(w->Plan.Count);
                        if (solver->Chunks() == 0u && !solver->Finished())
                        {
                            // The seeds (each channel's first solve starts from the input) come from
                            // the canonical slot; only the CPU-assembled coupling was uploaded.
                            NoteUses(w);
                            const auto io = ResidentIo(w);
                            if (!solver->RecordUpload(commands) ||
                                !filter->RecordLoad(commands,
                                    io,
                                    channels,
                                    solver->ResultBuffer(),
                                    solver->SolutionsAddress(),
                                    1u,
                                    rows))
                                return {};
                        }
                        const auto buffer =
                            solver->Finished() ? solver->RecordFinal(commands) : solver->RecordNext(commands, limit);
                        if (!buffer.IsValid())
                            return {};
                        const auto steps = solver->CompletedSolves() / channels;
                        if (Output(w).Back && steps > 0u && steps > w->Stored.value_or(0u))
                        {
                            NoteUses(w);
                            const auto io = ResidentIo(w);
                            const std::uint64_t solutions = solver->SolutionsAddress() +
                                std::uint64_t(steps - 1u) * channels * rows * sizeof(double);
                            if (filter->RecordStore(
                                    commands, io, channels, solver->ResultBuffer(), solutions, 1u, rows))
                            {
                                w->Storing = steps;
                            }
                        }
                        return buffer;
                    }));
        }
        bool BeginSolver(const Work& w)
        {
            const auto& f = w->Publication.Config.Filter;
            return w->Solver->Begin({.Matrix = {.Rows = std::uint32_t(w->Plan.Count),
                                         .RowOffsets = w->Offsets,
                                         .Columns = w->Columns,
                                         .Values = w->System.Matrix.Values},
                .Solves = TotalSolves(w),
                .RhsDiagonal = w->System.RhsDiagonal,
                .RhsConstant = w->System.RhsConstant,
                .ChainStride = ChannelCount(w),
                .SeedsOnDevice = true,
                .MaxIterations = f.MaxSolverIterations,
                .Tolerance = f.SolverTolerance});
        }
        // Accept's store of the latest complete step into the ring (an owner page; it waits for a write slot).
        bool QueueStore(const Work& w)
        {
            if (!AcquireBack(w))
                return Defer(w);
            if (w->Implicit)
                QueueImplicit(w, w->Done.load() * ChannelCount(w));
            else
                QueueExplicitStore(w);
            return false;
        }
        // Accept's consumed store page (it completes no step; the solver observes its reports): its store becomes the
        // front. False: the run failed.
        bool Consume(const Work& w, const SpatialGpuResult& chunk)
        {
            const std::uint32_t done = w->Done.load();
            std::uint32_t completed = done + w->Recorded;
            double metric = 0.0;
            if (w->Implicit)
            {
                const auto channels = ChannelCount(w);
                const auto solves = TotalSolves(w);
                if (chunk.Data.size() != Graphics::SparseConjugateGradientWorkspace::ReportReadbackBytes(solves))
                    return !Fail(
                        w, "Vulkan property smoothing did not return its solver reports; previous output retained.");
                w->Solver->Observe(chunk.Data);
                completed = w->Solver->CompletedSolves() / channels;
                for (std::size_t k = std::size_t(done) * channels; k < std::size_t(completed) * channels; ++k)
                {
                    Graphics::SparseCgReport report{};
                    std::memcpy(&report, chunk.Data.data() + k * sizeof(report), sizeof(report));
                    // Every chained solve must converge, as on the CPU.
                    if (report.Status != Graphics::SparseCgStatus::Converged)
                        return !Fail(w, "Implicit solver failed to converge on Vulkan; no property was changed.");
                    w->Applications += report.Iterations + 1;
                    metric += report.Iterations;
                }
            }
            else
            {
                auto done = w->Params;
                done.Iterations = w->Params.Iterations ? completed : 0u;
                w->Applications = Graphics::PropertyFilterWorkspace::DispatchCount(done);
                metric = double(w->Applications);
            }
            (void)metric;
            w->Recorded = 0u;
            PublishPreview(w);
            if (completed != done) // a store page records no solve past the latest complete step
                return !Fail(w, "Vulkan property smoothing stored an unfinished step; previous output retained.");
            return true;
        }
        // The worker's residency slots (input, base and every write slot offered), stamped with the compute fence
        // before they go: a preview copy is not waited for.
        void Uses(const Work& w, std::vector<RHI::BufferHandle>& buffers)
        {
            for (const auto* view : {&w->Input, &w->Base})
                if (*view)
                    buffers.push_back((*view)->Buffer);
            buffers.insert(buffers.end(), w->Offered.begin(), w->Offered.end());
        }
        void StampWorkerUses(const Work& w)
        {
            std::vector<RHI::BufferHandle> buffers;
            Uses(w, buffers);
            GP::StampGpuTransactionUses(w->Core, buffers);
        }
        // Owner, no step running: the preview slot goes (stamped first: its copy may be in flight).
        void ClearMailbox(const Work& w)
        {
            if (auto slot = w->Mailbox.Clear())
            {
                std::vector<RHI::BufferHandle> buffers;
                for (const auto* view : {&slot->first.Output, &slot->first.Presentation})
                    if (*view)
                        buffers.push_back((*view)->Buffer);
                GP::StampGpuTransactionUses(w->Core, buffers);
            }
        }
        // Owner, any time: a free write slot for the next live preview copy, never waited for (a busy ring offers
        // none and that preview is dropped); nothing while live preview is off, the Init runs or the run accepts.
        void OfferPreview(const Work& w)
        {
            auto& t = w->Core;
            if (t.Execution.PreviewEvery == 0u || !t.Prepared || t.CommitWanted || !w->Mailbox.Empty())
                return;
            const auto run = t.Context.StepRuns->Observe(t.StepRun);
            if (!run || IsTerminal(run->Phase) || run->Phase == StepRunPhase::Accepting || !AcquireBack(w))
                return;
            PreviewSlot slot{.Output = std::exchange(Output(w).Back, std::nullopt),
                .Presentation = std::exchange(PresentationRing(w).Back, std::nullopt)};
            for (const auto* view : {&slot.Output, &slot.Presentation})
                if (*view && std::ranges::find(w->Offered, (*view)->Buffer) == w->Offered.end())
                    w->Offered.push_back((*view)->Buffer);
            (void)w->Mailbox.Offer(std::move(slot));
        }
        // The Init's owner page: the explicit run's Begin (uploads, the resident gather), or the implicit solver's
        // upload and its seeds gathered from the canonical slot. Recorded once the last worker copy completed (Begin
        // rewrites host-visible buffers a copy may still read).
        std::shared_ptr<SpatialGpuResult> QueueInit(const Work& w)
        {
            return w->Core.Context.SpatialIndices->QueueGpuCompute(0u,
                [w, filter = w->Filter, solver = w->Solver](
                    RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle
                {
                    if (w->Core.Abandoned)
                        return {};
                    NoteUses(w);
                    auto io = ResidentIo(w);
                    io.Output = io.Presentation = {};
                    io.OutputBytes = io.PresentationBytes = 0u;
                    if (!w->Implicit)
                        return filter->Begin(commands,
                                   {.Values = w->Values,
                                       .Channels = std::uint32_t(w->Plan.Channels),
                                       .Edges = w->Edges,
                                       .Weights = w->Weights,
                                       .Degree = w->Plan.Degree,
                                       .Fixed = w->Fixed},
                                   w->Params,
                                   &io)
                            ? w->Input->Buffer
                            : RHI::BufferHandle{};
                    return solver->RecordUpload(commands) &&
                            filter->RecordLoad(commands,
                                io,
                                ChannelCount(w),
                                solver->ResultBuffer(),
                                solver->SolutionsAddress(),
                                1u,
                                std::uint32_t(w->Plan.Count))
                        ? solver->ResultBuffer()
                        : RHI::BufferHandle{};
                });
        }
        // The Init (main thread, the Run job's readiness): inputs, workspaces and the Init page; the worker view once
        // it completed. Pending while the residency defers or a page is in flight.
        GP::GpuTransactionPrepare Prepare(const Work& w, std::string& why)
        {
            using P = GP::GpuTransactionPrepare;
            auto& t = w->Core;
            if (w->InitPage)
            {
                if (w->InitPage->State == SpatialQueryState::Failed)
                {
                    why = w->InitPage->Diagnostic.empty() ? kRefused : w->InitPage->Diagnostic;
                    return P::Failed;
                }
                if (w->InitPage->State != SpatialQueryState::Ready)
                    return P::Pending;
                w->InitPage.reset();
                w->Worker.Solver = w->Solver;
                w->Worker.Filter = w->Filter;
                w->Worker.Io = ResidentIo(w);
                w->Worker.Io.Output = w->Worker.Io.Presentation = {};
                w->Worker.Io.OutputBytes = w->Worker.Io.PresentationBytes = 0u;
                t.Pages->ClearAbort(RHI::ComputeInputOrder::LaneOrderOnly); // owner Init ordered the input uploads
                return P::Ready;
            }
            if (!t.Context.Device->GetTransferQueue().IsComplete(t.Pages->Last()))
                return P::Pending; // a restart's older copy still reads the workspace
            if (!AcquireInputs(w))
            {
                if (!GP::GpuTransactionDeferralsExhausted(t))
                    return P::Pending;
                why = "The GPU property residency refused the run's input or output slot; previous output retained.";
                return P::Failed;
            }
            t.Deferrals = 0u;
            if (!w->Filter)
                w->Filter = t.Context.SpatialIndices->LeaseGpuWorkspace<Graphics::PropertyFilterWorkspace>();
            if (w->Implicit && !w->Solver)
            {
                w->Solver = t.Context.SpatialIndices->LeaseGpuWorkspace<Graphics::SparseConjugateGradientWorkspace>();
                if (w->Solver && !BeginSolver(w))
                {
                    why = "The Vulkan solver refused the implicit system; previous output retained.";
                    return P::Failed;
                }
            }
            if (!w->Filter || (w->Implicit && !w->Solver))
            {
                why = w->Implicit ? "Vulkan property smoothing device workspace unavailable; previous output retained."
                                  : kRefused;
                return P::Failed;
            }
            w->InitPage = t.Gpu = QueueInit(w); // the run's device work is queued (DeviceWorkQueued)
            if (w->InitPage->State == SpatialQueryState::Failed)
            {
                why = w->InitPage->Diagnostic.empty() ? kRefused : w->InitPage->Diagnostic;
                return P::Failed;
            }
            return P::Pending;
        }
        // One page of a worker step: nullopt once it completed, else the step's outcome (a failure says why).
        std::optional<StepOutcome> RunPage(EditorPropertySmoothingTransaction& w,
            const std::function<RHI::BufferHandle(RHI::ICommandContext&)>& record,
            const std::span<std::byte> out)
        {
            switch (w.Core.Pages->Run(record, out))
            {
            case WorkerGpuPageStatus::Complete: return std::nullopt;
            case WorkerGpuPageStatus::Aborted:  return StepOutcome::Failed; // given up: the owner ended the run
            case WorkerGpuPageStatus::Refused:  w.Core.WorkerFailure = kRefused; return StepOutcome::Failed;
            case WorkerGpuPageStatus::Failed:
            case WorkerGpuPageStatus::TimedOut: break;
            }
            w.Core.WorkerFailure = "Vulkan property smoothing page did not complete: the device was lost.";
            return StepOutcome::Failed;
        }
        // The step run's Step, on a worker: one explicit pass, or one implicit time step over every channel (its
        // chunks until the step's last solve finished; each one only reads its reports back). Every chained solve
        // must converge, as on the CPU. Owner state is never touched.
        // The explicit passes of a batch (Run: the step run's allowance, in one submission as before; one for Step).
        StepOutcome ExplicitSteps(const Work& self, StepRunMetric& metric, const std::uint32_t count)
        {
            auto& w = *self;
            if (w.Core.Pages->Aborted())
                return StepOutcome::Failed;
            const std::uint32_t done = w.Done.load();
            auto& worker = w.Worker;
            double word{};
            // A zero rate iterates nothing, as on the CPU.
            const std::uint32_t iterations = w.Params.Iterations ? count : 0u;
            if (const auto failed = RunPage(
                    w,
                    [&worker, iterations](RHI::ICommandContext& commands)
                    {
                        return worker.Filter->RecordIterations(commands, iterations);
                    },
                    std::as_writable_bytes(std::span{&word, 1u})))
                return *failed;
            auto params = w.Params;
            params.Iterations = w.Params.Iterations ? done + count : 0u;
            w.Applications = Graphics::PropertyFilterWorkspace::DispatchCount(params);
            metric.Values[0] = double(w.Applications);
            w.Done.store(done + count);
            return StepOutcome::Continue;
        }
        StepOutcome WorkerStep(const Work& self, StepRunMetric& metric)
        {
            auto& w = *self;
            if (!w.Implicit)
                return ExplicitSteps(self, metric, 1u);
            if (w.Core.Pages->Aborted())
                return StepOutcome::Failed;
            const std::uint32_t done = w.Done.load();
            auto& worker = w.Worker;
            const auto channels = ChannelCount(self);
            const std::uint32_t limit = (done + 1u) * channels;
            worker.Reports.resize(Graphics::SparseConjugateGradientWorkspace::ReportReadbackBytes(TotalSolves(self)));
            do
            {
                if (const auto failed = RunPage(
                        w,
                        [&worker, limit](RHI::ICommandContext& commands)
                        {
                            return worker.Solver->RecordNext(commands, limit);
                        },
                        worker.Reports))
                    return *failed;
                worker.Solver->Observe(worker.Reports);
            } while (worker.Solver->CompletedSolves() < limit);
            double iterations = 0.0;
            for (std::size_t k = std::size_t(done) * channels; k < limit; ++k)
            {
                Graphics::SparseCgReport report{};
                std::memcpy(&report, worker.Reports.data() + k * sizeof(report), sizeof(report));
                if (report.Status != Graphics::SparseCgStatus::Converged)
                {
                    w.Core.WorkerFailure = "Implicit solver failed to converge on Vulkan; no property was changed.";
                    return StepOutcome::Failed;
                }
                w.Applications += report.Iterations + 1;
                iterations += report.Iterations;
            }
            metric.Values[0] = iterations;
            w.Done.store(done + 1u);
            return StepOutcome::Continue;
        }
        // The step run's Preview, after a step on the worker (or at a rest on the owner): the latest step into the
        // slot the owner offered, its own submission, not waited for; Show publishes it once its fence completed.
        // No slot offered: no copy (the run goes on).
        bool WorkerPreview(const Work& self, StepPreview& preview)
        {
            auto& w = *self;
            PreviewSlot slot{};
            if (!w.Mailbox.Borrow(
                    [&slot](const PreviewSlot& offered)
                    {
                        slot = offered; // the views alone: the leases stay in the mailbox
                        for (auto* view : {&slot.Output, &slot.Presentation})
                            if (*view)
                                (*view)->Lease.reset();
                    }))
                return false;
            auto io = w.Worker.Io;
            io.Output = View(slot.Output);
            io.OutputBytes = slot.Output ? slot.Output->Bytes : 0u;
            io.Presentation = View(slot.Presentation);
            io.PresentationBytes = slot.Presentation ? slot.Presentation->Bytes : 0u;
            const std::uint32_t done = w.Done.load();
            const auto channels = ChannelCount(self), rows = std::uint32_t(w.Plan.Count);
            const auto token = done == 0u || !slot.Output
                ? RHI::ReadbackToken{}
                : w.Core.Pages->SubmitCopy(
                      [&](RHI::ICommandContext& commands) -> RHI::BufferHandle
                      {
                          if (!w.Implicit)
                              return w.Worker.Filter->RecordStoreCurrent(commands, io) ? slot.Output->Buffer
                                                                                       : RHI::BufferHandle{};
                          const auto& solver = *w.Worker.Solver;
                          const std::uint64_t solutions =
                              solver.SolutionsAddress() + std::uint64_t(done - 1u) * channels * rows * sizeof(double);
                          return w.Worker.Filter->RecordStore(
                                     commands, io, channels, solver.ResultBuffer(), solutions, 1u, rows)
                              ? slot.Output->Buffer
                              : RHI::BufferHandle{};
                      });
            w.Mailbox.Recorded(token.Value);
            preview.Fence = token.Value;
            return token.IsValid();
        }
        // The step run's Show (owner): a completed copy becomes the front; an incomplete one waits; one no longer in
        // the mailbox (cleared for Accept or a restart) is never shown.
        StepRunShow ShowPreview(const Work& w, const StepPreview& preview)
        {
            auto& t = w->Core;
            if (preview.Fence == 0u)
                return StepRunShow::Never;
            if (!t.Context.Device->GetTransferQueue().IsComplete(RHI::ReadbackToken{preview.Fence}))
                return StepRunShow::Later;
            auto slot = w->Mailbox.Take(preview.Fence);
            if (!slot)
                return StepRunShow::Never;
            slot.reset(); // its leases go before the publication (the copy completed)
            if (!t.Residency || !t.Residency->Publish(Output(w).Key))
                return StepRunShow::Never;
            if (w->Presentation)
                (void)t.Residency->Publish(PresentationRing(w).Key);
            ++w->Previews;
            w->Stored = preview.Step;
            return StepRunShow::Shown;
        }
        // The owner's side of the Run job's readiness (after the Init): offers live preview slots while steps run;
        // once none runs, reports progress and, at the step run's Commit, stores the latest complete step into the
        // ring (an owner page). True once that store is the front (or the run failed).
        bool Poll(const Work& w)
        {
            auto& t = w->Core;
            if (!w->Failure.empty())
                return true;
            OfferPreview(w);
            if (GP::GpuTransactionWorkerBusy(t))
                return false;
            const std::uint32_t done = w->Done.load();
            if (const auto& ctx = t.Context;
                done != w->Reported && ctx.JobCommands.ReportProgress && t.RunToken.IsValid())
            {
                w->Reported = done;
                ctx.JobCommands.ReportProgress(t.RunToken,
                    {.Normalized = std::min(1.0f, float(done) / float(std::max(1u, t.Execution.IterationCap))),
                        .Determinate = true});
            }
            if (w->Chunk)
            {
                if (w->Chunk->State == SpatialQueryState::Failed)
                {
                    GP::GpuTransactionRecording(t, false);
                    return Fail(w, w->Chunk->Diagnostic.empty() ? kRefused : w->Chunk->Diagnostic);
                }
                if (w->Chunk->State != SpatialQueryState::Ready)
                    return false;
                const auto chunk = std::exchange(w->Chunk, {});
                const bool consumed = Consume(w, *chunk);
                GP::GpuTransactionRecording(t, false);
                if (!consumed)
                    return true;
            }
            if (!t.CommitWanted)
                return false;
            ClearMailbox(w); // the latest step's own store replaces an unpublished live preview
            return done == 0u || w->Stored == done || QueueStore(w);
        }
        // The Run job ended at the step run's Commit: Accept starts on the front (or the run failed).
        void CompleteRun(const Work& w)
        {
            if (!w->Failure.empty())
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed, w->Failure);
                return;
            }
            if (w->Previews == 0u)
            {
                Finish(w,
                    EditorGpuTransactionPhase::Discarded,
                    EditorCommandStatus::StaleEntity,
                    "Vulkan property smoothing published no result; previous output retained.");
                return;
            }
            ClearMailbox(w);
            StampWorkerUses(w); // a live preview copy is not waited for
            ReleaseWorkspaces(w);
            w->Input.reset();
            w->Base.reset();
            w->Result.OperatorApplications = w->Applications;
            w->Result.Steps = w->Done.load();
            GP::ReadyGpuTransaction(w->Core);
            w->Result.Status = EditorCommandStatus::Pending;
            w->Result.Message = "The GPU result waits for Accept or Discard.";
        }
        // run_control restart: the captured input again, a fresh output ring, nothing of the older steps.
        bool Restart(const Work& w, std::string& diagnostic)
        {
            if (!w->Failure.empty())
            {
                diagnostic = w->Failure;
                return false;
            }
            if (w->Solver && !BeginSolver(w))
            {
                diagnostic = "The Vulkan solver refused the implicit system.";
                return false;
            }
            w->Done.store(0u);
            w->Recorded = w->Limit = w->Reported = 0u;
            w->Stored.reset();
            w->Storing.reset();
            w->Previews = 0u;
            w->Applications = 0u;
            // The workspaces stay leased (their capacity too); the next Init begins the explicit run again and
            // regathers the captured input, or uploads the solver's operator and seeds again.
            w->InitPage.reset();
            ClearMailbox(w);
            GP::RestartGpuTransactionRings(w->Core);
            return true;
        }
        // Accept landed: the front's rows become the published property and the canonical slot.
        void CompleteAccept(const Work& w)
        {
            const auto& ctx = w->Core.Context;
            const auto& p = w->Publication;
            auto* residency = w->Core.Residency;
            const auto& readback = Output(w).Readback;
            // The publication the readback leased: BindRevision binds exactly that one.
            const std::uint64_t accepted = readback && readback->Lease ? readback->Lease->Publication : 0u;
            if (readback) readback->Lease.reset();
            if (readback && readback->Failed)
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                       "Vulkan property smoothing readback failed; previous output retained.");
                return;
            }
            const std::size_t channels = p.Channels;
            std::vector<double> gpuValues(p.Slots.size() * channels);
            if (w->TestFront) gpuValues = *w->TestFront;
            else
            {
                const bool wide = p.Config.Output.ValueKind == K::Double;
                const std::size_t elementBytes = wide ? sizeof(double) : sizeof(float);
                if (!readback || readback->Bytes.size() != std::size_t(w->Count) * channels * elementBytes)
                {
                    Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                           "Vulkan property smoothing readback has the wrong size; previous output retained.");
                    return;
                }
                for (std::size_t i = 0; i < p.Slots.size(); ++i)
                    for (std::size_t c = 0; c < channels; ++c)
                    {
                        const std::byte* at = readback->Bytes.data() + (std::size_t(p.Slots[i]) * channels + c) * elementBytes;
                        if (wide) std::memcpy(&gpuValues[i * channels + c], at, sizeof(double));
                        else { float value{}; std::memcpy(&value, at, sizeof(float)); gpuValues[i * channels + c] = value; }
                    }
            }
            auto filtered = S::CompletePropertyFilter(w->Plan, w->Values, std::move(gpuValues), "vulkan_compute");
            filtered.OperatorApplications = w->Result.OperatorApplications;
            // ADR 0030 decision 8: the implicit system's fixed-row coupling and mass are assembled
            // on the CPU and uploaded per run; report that traffic.
            const std::string summary = w->CpuStageBytes
                ? "; CPU-assembled coupling uploaded: " + std::to_string(w->CpuStageBytes) + " bytes" : "";
            auto result = Publish(ctx, p, filtered, w->Result, summary);
            if (!filtered.Success) result.Status = EditorCommandStatus::GeometryProcessingFailed;
            w->Result = result;
            if (!filtered.Success || !result.Succeeded())
            {
                Finish(w, EditorGpuTransactionPhase::Failed, result.Status, std::move(result.Message));
                return;
            }
            // The front becomes the canonical slot of the new CPU revision (ADR 0030 decision 6).
            if (residency)
            {
                const auto watch = GP::ObserveGeometryProperty(BuildGeometryAvailability(ctx.Scene->Raw(), p.Entity),
                                                               p.Config.Output.Domain, p.Config.Output.Name);
                if (!watch.Revision || !residency->BindRevision(Output(w).Key, *watch.Revision, accepted))
                    (void)residency->Discard(Output(w).Key, Output(w).Generation);
                if (w->Presentation) (void)residency->Discard(PresentationRing(w).Key, PresentationRing(w).Generation);
            }
            Finish(w, EditorGpuTransactionPhase::Applied, result.Status, std::move(result.Message));
        }
        // Wires the typed hooks of a transaction whose core keys are set.
        void Install(const Work& owner)
        {
            auto* raw = owner.get();
            const auto self = [raw] { return raw->shared_from_this(); };
            auto& t = raw->Core;
            t.Label = "Vulkan property smoothing";
            t.AcceptJobName = "Vulkan property smoothing accept";
            t.JobLabel = "Property smoothing";
            t.Hooks = {.Current =
                           [raw]
                {
                    return InputsCurrent(raw->Core.Context, raw->Publication, raw->OutputWatch);
                },
                .Poll =
                    [self]
                {
                    return Poll(self());
                },
                .CompleteRun =
                    [self]
                {
                    CompleteRun(self());
                },
                .CompleteAccept =
                    [self]
                {
                    CompleteAccept(self());
                },
                .Accepting =
                    [raw]
                {
                    raw->Result.Status = EditorCommandStatus::Pending;
                    raw->Result.Message = "Reading the GPU result back.";
                },
                .Restart =
                    [self](std::string& diagnostic)
                {
                    return Restart(self(), diagnostic);
                },
                .Prepare =
                    [self](std::string& diagnostic)
                {
                    return Prepare(self(), diagnostic);
                },
                .Step =
                    [self](StepRunMetric& metric)
                {
                    return WorkerStep(self(), metric);
                },
                .Preview =
                    [self](StepPreview& preview)
                {
                    return WorkerPreview(self(), preview);
                },
                .Show =
                    [self](const StepPreview& preview)
                {
                    return ShowPreview(self(), preview);
                },
                .Uses =
                    [self](std::vector<RHI::BufferHandle>& buffers)
                {
                    Uses(self(), buffers);
                },
                .Release =
                    [self]
                {
                    const auto w = self();
                    ClearMailbox(w); // the core sealed the pages and stamped the slots
                    ReleaseWorkspaces(w);
                    w->Input.reset();
                    w->Base.reset();
                    w->Worker = {}; // no step or preview runs any more (the core waits for that)
                },
                .Deliver =
                    [raw](const EditorCommandStatus status, std::string message)
                {
                    raw->Result.Status = status;
                    raw->Result.Message = std::move(message);
                    raw->Result.StepRun = raw->Core.StepRun;
                    raw->Result.Steps = raw->Done.load();
                    if (auto sink = std::move(raw->Sink))
                        sink(raw->Result);
                }};
        }
        // Sets the output (and presentation) ring keys of `entity`'s output.
        void SetRings(const Work& w, const entt::entity entity, const PropertySmoothingConfig& c)
        {
            auto& t = w->Core;
            w->Presentation = c.Output.ValueKind == K::Double;
            t.Rings[0] = {.Key = MakeGpuPropertyKey(t.Context.World, entity, c.Output), .ReadBack = true};
            t.Rings[1] = {.Key = MakeGpuPropertyKey(t.Context.World, entity, GpuPropertyPresentationRef(c.Output))};
            t.RingCount = w->Presentation ? 2u : 1u;
        }
        // Queues the front's readback and the job that publishes it.
        EditorPropertySmoothingResult BeginAccept(const Work& w, std::function<void(EditorPropertySmoothingResult)> onComplete)
        {
            auto result = w->Result;
            if (auto refused = GP::GpuTransactionAcceptRefusal(w->Core, bool(onComplete)))
            {
                result.Status = refused->Status;
                result.Message = std::move(refused->Message);
                return result;
            }
            if (onComplete) w->Sink = GuardEditorProcessingResult(w->Core.Context, std::move(onComplete));
            (void)GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w));
            return w->Result;
        }
        // The run's logical cap: the method's iterations (explicit passes, implicit time steps), or 1 for the
        // variational fit, solved whole.
        StepRunExecution RunExecution(const PropertySmoothingConfig& c, StepRunExecution execution)
        {
            execution.IterationCap = c.Filter.Method == S::PropertyFilter::VariationalFit ? 1u : c.Filter.Iterations;
            return execution;
        }
        // The batch command's execution: at once, accepted at its end.
        StepRunExecution BatchExecution(StepRunExecution execution)
        {
            execution.Start = StepRunStart::Run;
            execution.AutoAccept = StepRunAutoAccept::End;
            return execution;
        }
        // Builds the run from the capture and queues its Run job and step run. Null with `result` filled
        // (a rejection) when the request cannot run.
        Work StartVulkan(const EditorProcessingContext& context,
            const std::uint32_t id,
            const PropertySmoothingConfig& c,
            Prepared prepared,
            EditorPropertySmoothingResult& result,
            std::function<void(EditorPropertySmoothingResult)> onComplete,
            const StepRunExecution& execution)
        {
            const auto fail = [&](std::string message) {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Message = std::move(message);
                return Work{};
            };
            std::string diagnostic;
            auto w = std::make_shared<EditorPropertySmoothingTransaction>();
            w->Core.Context = context;
            Install(w);
            auto& values = prepared.Values;
            const auto channels = prepared.Channels;
            auto& graph = prepared.Graph;
            auto plan = S::PlanPropertyFilter(values, channels, graph.Edges, c.Filter, graph.BoundaryRows, graph.Mass, diagnostic);
            if (!plan) return fail(diagnostic);
            w->Plan = std::move(*plan);
            w->Implicit = c.Filter.Method == S::PropertyFilter::Implicit;
            if (w->Implicit)
            {
                auto system = S::AssemblePropertyImplicitSystem(w->Plan, values, graph.Edges, c.Filter, graph.Mass, diagnostic);
                if (!system) return fail(diagnostic);
                w->System = std::move(*system);
                const auto rows = w->Plan.Count;
                if (rows > (1u << 24) || w->System.Matrix.NonZeros() > (1u << 26) ||
                    Graphics::SparseConjugateGradientWorkspace::ReadbackBytes(std::uint32_t(rows), c.Filter.Iterations * std::uint32_t(channels)) >
                        (std::uint64_t{1} << 28))
                    return fail("Vulkan implicit smoothing supports at most 2^24 rows, 2^26 nonzeros and 256 MiB of solutions.");
                for (const auto offset : w->System.Matrix.RowOffsets) w->Offsets.push_back(std::uint32_t(offset));
                for (const auto column : w->System.Matrix.ColIndices) w->Columns.push_back(std::uint32_t(column));
                // The seeds (channel k's first step starts from the input) are gathered from the
                // canonical slot on the device; chained solves share one right-hand-side
                // diagonal across channels.
                std::vector<double> diagonal;
                for (std::size_t k = 0; k < channels; ++k)
                    diagonal.insert(diagonal.end(), w->System.RhsDiagonal.begin(), w->System.RhsDiagonal.end());
                w->System.RhsDiagonal = std::move(diagonal);
            }
            else
            {
                w->Params = GpuParams(c.Filter, w->Plan);
                if (Graphics::PropertyFilterWorkspace::DispatchCount(w->Params) > Graphics::PropertyFilterWorkspace::MaxDispatches)
                    return fail("Vulkan smoothing would exceed its dispatch budget; lower iterations or heat time, or use the CPU.");
                if (values.size() * sizeof(double) > (std::size_t{1} << 28) || w->Plan.Count > (1u << 24) || graph.Edges.size() > (1u << 26))
                    return fail("Vulkan smoothing supports at most 2^24 rows, 2^26 edges and 256 MiB of values.");
                for (const auto& edge : graph.Edges)
                {
                    w->Edges.insert(w->Edges.end(), {std::uint32_t(edge.A), std::uint32_t(edge.B)});
                    w->Weights.push_back(edge.Weight);
                }
                for (const bool fixed : w->Plan.Fixed) w->Fixed.push_back(fixed ? 1u : 0u);
            }
            if (w->Implicit)
                w->CpuStageBytes = (w->System.RhsDiagonal.size() + w->System.RhsConstant.size()) * sizeof(double);
            else // Run records the allowed passes in one submission
                w->Core.Hooks.StepBatch =
                    [raw = w.get()](StepRunMetric& metric, const std::uint32_t allowance, std::uint32_t& completed)
                {
                    completed = allowance;
                    return ExplicitSteps(raw->shared_from_this(), metric, allowance);
                };
            for (std::size_t i = 0; i < w->Plan.Count; ++i)
                w->RestoreMask.push_back(w->Plan.Fixed[i] || w->Plan.Isolated[i] ? 1u : 0u);
            w->Values = std::move(values);
            w->Publication = std::move(prepared.Publication);
            w->OutputWatch = std::move(prepared.OutputWatch);
            auto& t = w->Core;
            t.Identity = {.EntityId = id, .Scope = ToEditorJobScope(c.Output.Domain),
                          .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField, .OutputName = c.Output.Name,
                          .RequestedDomain = EditorJobDomain::GpuCompute};
            // The output ring, keyed like every other GPU user of the property (ADR 0030).
            t.Residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
            w->Count = std::uint32_t(prepared.Props->Size());
            SetRings(w, prepared.Entity, c);
            if (auto refused = GP::GpuTransactionStartRefusal(t))
            {
                result.Status = refused->Status;
                result.Message = std::move(refused->Message);
                return {};
            }
            if (!context.Device)
                return fail("Vulkan property smoothing requires a device.");
            t.Pages = std::make_shared<WorkerGpuPages>(*context.Device); // its steps run on the step run's worker
            t.Kind = GP::GpuTransactionKind::Iterative;
            t.Execution = RunExecution(c, execution);
            w->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            w->Result = result;
            w->Result.Status = EditorCommandStatus::Pending;
            w->Result.Message = t.Execution.Start == StepRunStart::Ready
                ? "Vulkan property smoothing is ready; run or step it."
                : "Vulkan property smoothing queued.";
            if (!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w), "Vulkan property smoothing").IsValid())
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Message = GP::MeshSupport::QueuedJobRejectedMessage(t.JobLabel);
                return {};
            }
            w->Result.StepRun = t.StepRun;
            result = w->Result;
            return w;
        }

        // The per-row bound radii of a variational fit, captured on the main thread with the other inputs; false
        // when one is negative or not finite.
        bool CaptureRadii(const Prepared& prepared, const PropertySmoothingConfig& c, std::vector<double>& radii)
        {
            radii.clear();
            if (!PerRowBounds(c))
                return true;
            bool valid = true;
            std::visit(
                [&](const auto& stored)
                {
                    for (const auto slot : prepared.Samples.Slots)
                    {
                        const double r = Channel(stored[slot], 0);
                        valid &= std::isfinite(r) && r >= 0;
                        radii.push_back(r);
                    }
                },
                Capture(*prepared.Props, c.BoundRadii).Values);
            return valid;
        }
        // The variational fit, solved whole (one logical step); its summary for the result message. Reads only the
        // captured inputs (it may run on a worker).
        S::PropertyFilterResult Fit(const Prepared& prepared,
            const PropertySmoothingConfig& c,
            std::span<const double> radii,
            EditorPropertySmoothingResult& result,
            std::string& summary)
        {
            const auto& samples = prepared.Samples;
            // Second order measures gradients against the same sample points the graph was built from.
            std::vector<double> positions;
            if (c.Filter.SmoothnessOrder == S::FitOrder::Second)
                for (const auto& point : samples.Points)
                    positions.insert(positions.end(), {point.x, point.y, point.z});
            const auto& graph = prepared.Graph;
            const auto fit = Geometry::HarmonicField::FitProperty(prepared.Values,
                prepared.Channels,
                graph.Edges,
                c.Filter,
                graph.BoundaryRows,
                graph.Mass,
                radii,
                positions);
            result.FitWeight = fit.Stats.FitWeight;
            result.RmsResidual = fit.Stats.RmsResidual;
            result.ActiveBounds = fit.Stats.ActiveBounds;
            const auto number = [](double x)
            {
                char text[32];
                std::snprintf(text, sizeof text, "%.6g", x);
                return std::string{text};
            };
            summary = "; weight " + number(fit.Stats.FitWeight) + ", RMS residual " + number(fit.Stats.RmsResidual) +
                ", " + std::to_string(fit.Stats.Iterations) + " iteration(s), " +
                std::to_string(fit.Stats.Factorizations) + " factorization(s), " +
                std::to_string(fit.Stats.ActiveBounds) + " active bound(s)" +
                (fit.Stats.NoiseTargetClamped ? "; noise level unreachable, weight clamped" : "");
            return {.Success = fit.Success,
                .Diagnostic = fit.Diagnostic,
                .Values = fit.Values,
                .OperatorApplications = fit.Stats.Solves};
        }

        // ARCH-023: the CPU counterpart as a worker step run of the session's step runs. A step is one explicit
        // pass or implicit time step (Geometry PropertyFilterRun), the fit one step solved whole; Commit publishes
        // the last complete step once through the undoable smoothing transaction. The CPU run has no live preview.
        struct CpuRun
        {
            EditorProcessingContext Context{};
            PropertySmoothingConfig Config{};
            Prepared Captured{};
            // Written by Step on the worker, read by Commit, Restart and a failed run's Discard once no step runs (the
            // run orders them); Steps is also read by a Discard during a step.
            std::optional<S::PropertyFilterRun> Filter{};
            S::PropertyFilterResult Fitted{};
            std::string FitSummary{}, Failure{};
            std::vector<double> Radii{}; // the fit's per-row bounds, captured at the start
            std::atomic<std::uint32_t> Steps{};
            EditorPropertySmoothingResult Result{}; // the fit's statistics are written by its step
            EditorPropertySmoothingResult Base{}; // main thread only
            std::uint64_t Id{};
            std::function<void(EditorPropertySmoothingResult)> Sink{};
            bool Delivered{};
            void Deliver(EditorPropertySmoothingResult result)
            {
                if (std::exchange(Delivered, true))
                    return;
                Result = std::move(result);
                if (auto sink = std::move(Sink))
                    sink(Result);
            }
        };
        bool BeginCpu(CpuRun& run, std::string& diagnostic)
        {
            run.Steps.store(0u);
            run.Fitted = {};
            run.Result = run.Base;
            run.Failure.clear();
            run.Filter.reset();
            if (run.Config.Filter.Method == S::PropertyFilter::VariationalFit)
                return true;
            const auto& graph = run.Captured.Graph;
            run.Filter.emplace();
            return run.Filter->Begin(run.Captured.Values,
                run.Captured.Channels,
                graph.Edges,
                run.Config.Filter,
                graph.BoundaryRows,
                graph.Mass,
                diagnostic);
        }
        EditorPropertySmoothingResult StartCpu(const EditorProcessingContext& context,
            const std::uint32_t id,
            const PropertySmoothingConfig& c,
            Prepared prepared,
            EditorPropertySmoothingResult result,
            std::function<void(EditorPropertySmoothingResult)> onComplete,
            StepRunExecution execution)
        {
            const auto refuse = [&](EditorCommandStatus status, std::string message)
            {
                result.Status = status;
                result.Message = std::move(message);
                return result;
            };
            const EditorJobIdentity identity{.EntityId = id,
                .Scope = ToEditorJobScope(c.Output.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                .OutputName = c.Output.Name,
                .RequestedDomain = EditorJobDomain::Cpu};
            if (auto busy = GP::MeshSupport::ActiveOutputJobRefusal(context, identity, "Property smoothing"))
                return refuse(
                    EditorCommandStatus::Pending, std::move(busy->Message)); // the active run keeps its callback
            auto run = std::make_shared<CpuRun>();
            run->Context = context;
            run->Config = c;
            run->Captured = std::move(prepared);
            run->Base = result;
            run->Base.BackendId = "cpu_reference";
            run->Result = run->Base;
            std::string diagnostic;
            if (!CaptureRadii(run->Captured, c, run->Radii))
                return refuse(
                    EditorCommandStatus::InvalidProcessingParameters, "Bound radii must be finite and nonnegative.");
            if (!BeginCpu(*run, diagnostic))
                return refuse(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            // No live preview on the CPU: the run says so rather than accepting the option silently.
            const bool previewAsked = execution.PreviewEvery != 0u;
            execution = RunExecution(c, execution);
            execution.PreviewEvery = 0u;
            const std::weak_ptr<CpuRun> weak = run;
            StepRunCallbacks callbacks{.Step =
                                           [run](StepRunMetric& metric)
                {
                    if (run->Config.Filter.Method == S::PropertyFilter::VariationalFit)
                    {
                        run->Fitted = Fit(run->Captured, run->Config, run->Radii, run->Result, run->FitSummary);
                        run->Steps.store(1u);
                        if (!run->Fitted.Success)
                        {
                            run->Failure = run->Fitted.Diagnostic;
                            return StepOutcome::Failed;
                        }
                        metric.Values[0] = double(run->Fitted.OperatorApplications);
                        return StepOutcome::Converged;
                    }
                    std::string why;
                    if (!run->Filter->Step(why))
                    {
                        run->Failure = std::move(why);
                        return StepOutcome::Failed;
                    }
                    run->Steps.store(run->Filter->Steps());
                    metric.Values[0] = double(run->Filter->Finish().OperatorApplications);
                    return StepOutcome::Continue;
                },
                .Valid =
                    [weak]
                {
                    const auto r = weak.lock();
                    return r && GP::EditorProcessingContextWorldCurrent(r->Context) &&
                        InputsCurrent(r->Context, r->Captured.Publication, r->Captured.OutputWatch);
                },
                .Commit =
                    [weak](std::string& diagnostic)
                {
                    const auto r = weak.lock();
                    if (!r)
                    {
                        diagnostic = "The smoothing run ended.";
                        return StepRunCommit::Refused;
                    }
                    const bool fit = r->Config.Filter.Method == S::PropertyFilter::VariationalFit;
                    auto published = Publish(r->Context,
                        r->Captured.Publication,
                        fit ? r->Fitted : r->Filter->Finish(),
                        r->Result,
                        r->FitSummary);
                    published.Steps = r->Steps.load();
                    const bool committed = published.Succeeded();
                    if (!committed)
                        diagnostic = published.Message;
                    r->Deliver(std::move(published));
                    return committed ? StepRunCommit::Committed : StepRunCommit::Refused;
                },
                .Discard =
                    [weak]
                {
                    const auto r = weak.lock();
                    if (!r || r->Delivered)
                        return;
                    // A failed run's step has completed; a discard may come while a step runs (its state is not read).
                    const auto observed = r->Context.StepRuns->Observe(r->Id);
                    const bool failed = observed && observed->Phase == StepRunPhase::Failed;
                    auto ended = r->Base;
                    ended.StepRun = r->Id;
                    ended.Steps = r->Steps.load();
                    ended.Status =
                        failed ? EditorCommandStatus::GeometryProcessingFailed : EditorCommandStatus::StaleEntity;
                    ended.Message = failed ? (r->Failure.empty() ? observed->Diagnostic : r->Failure)
                                           : "Property smoothing discarded or stale; previous output retained.";
                    r->Deliver(std::move(ended));
                },
                .Restart =
                    [weak](std::string& diagnostic)
                {
                    const auto r = weak.lock();
                    return r && BeginCpu(*r, diagnostic);
                }};
            const auto holder = std::make_shared<JobToken>();
            std::string refusal;
            run->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            const auto stepRun = context.StepRuns->Start({.Label = "Property smoothing",
                                                             .Owner = "Runtime.PropertySmoothing",
                                                             .Execution = execution,
                                                             .MetricNames = {"operator_applications", "", "", ""}},
                std::move(callbacks),
                EditorStepRunJobs(context.JobCommands, identity, holder),
                refusal);
            if (!stepRun)
            {
                run->Sink = {};
                return refuse(EditorCommandStatus::GeometryProcessingFailed,
                    GP::MeshSupport::QueuedJobRejectedMessage("Property smoothing") + " " + refusal);
            }
            run->Id = stepRun;
            run->Base.StepRun = run->Result.StepRun = stepRun;
            result = run->Base;
            result.Status = EditorCommandStatus::Pending;
            result.Message = execution.Start == StepRunStart::Ready ? "Property smoothing is ready; run or step it."
                                                                    : "Property smoothing queued.";
            if (previewAsked)
                result.Message += " The CPU backend has no live preview.";
            return result;
        }
        // The CPU run inline, without run controls (no step-run service or job lane): the same steps, published at
        // once.
        EditorPropertySmoothingResult RunCpuInline(const EditorProcessingContext& context,
            const PropertySmoothingConfig& c,
            const Prepared& prepared,
            EditorPropertySmoothingResult result)
        {
            if (c.Filter.Method == S::PropertyFilter::VariationalFit)
            {
                std::vector<double> radii;
                if (!CaptureRadii(prepared, c, radii))
                {
                    result.Status = EditorCommandStatus::InvalidProcessingParameters;
                    result.Message = "Bound radii must be finite and nonnegative.";
                    return result;
                }
                std::string summary;
                const auto fitted = Fit(prepared, c, radii, result, summary);
                result.Steps = 1u;
                return Publish(context, prepared.Publication, fitted, std::move(result), summary);
            }
            const auto filtered = S::FilterProperty(prepared.Values,
                prepared.Channels,
                prepared.Graph.Edges,
                c.Filter,
                prepared.Graph.BoundaryRows,
                prepared.Graph.Mass);
            result.Steps = filtered.Success ? c.Filter.Iterations : 0u;
            return Publish(context, prepared.Publication, filtered, std::move(result), {});
        }
        bool StepRunsAvailable(const EditorProcessingContext& context)
        {
            return context.StepRuns && context.JobCommands.Available();
        }
    }

    EditorPropertySmoothingTransactionHandle StartEditorPropertySmoothing(const EditorProcessingCommands& commands,
        const std::uint32_t id,
        const PropertySmoothingConfig& c,
        EditorPropertySmoothingResult& result,
        std::function<void(EditorPropertySmoothingResult)> onComplete)
    {
        namespace PS = PropertySmoothingDetail;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        result = {};
        result.RequestedBackend = c.Backend;
        result.Status = EditorCommandStatus::InvalidProcessingParameters;
        std::string diagnostic;
        auto prepared = PS::Prepare(context, id, c, result, diagnostic);
        if (!prepared)
        {
            result.Message = diagnostic;
            return {};
        }
        if (c.Backend == PropertySmoothingBackend::Vulkan)
            return PS::StartVulkan(context, id, c, std::move(*prepared), result, std::move(onComplete), c.Execution);
        if (!PS::StepRunsAvailable(context))
        {
            result.Message = "Interactive smoothing needs the session's step runs and job lane.";
            return {};
        }
        result = PS::StartCpu(context, id, c, std::move(*prepared), result, std::move(onComplete), c.Execution);
        return {};
    }

    EditorPropertySmoothingTransactionSnapshot SnapshotEditorPropertySmoothing(const EditorProcessingCommands& commands,
                                                                               const EditorPropertySmoothingTransactionHandle& run)
    {
        EditorPropertySmoothingTransactionSnapshot snapshot;
        // Always asked, so even "no run" carries the session scene epoch.
        if (!run) { snapshot.Progress = GetEditorOperationProgress(commands, JobToken{}); return snapshot; }
        snapshot.StableEntityId = run->Core.Identity.EntityId;
        snapshot.OutputName = run->Core.Identity.OutputName;
        // Key by the run's own jobs, not the output: the Accept readback job
        // shares the identity and would otherwise mask the solver's fraction.
        // Once Accept started, that job is the run's current (and final) one.
        snapshot.Progress = GetEditorOperationProgress(
            commands, run->Core.AcceptToken.IsValid() ? run->Core.AcceptToken : run->Core.RunToken);
        snapshot.Phase = run->Core.Phase;
        snapshot.Previews = run->Previews;
        snapshot.DeviceWorkQueued = run->Core.Gpu != nullptr;
        snapshot.Result = run->Result;
        snapshot.Result.StepRun = run->Core.StepRun;
        snapshot.Result.Steps = run->Done.load();
        if (run->Core.Phase == EditorGpuTransactionPhase::ReadyToAccept)
        {
            snapshot.Stale = !PropertySmoothingDetail::Current(run);
            const bool resident = run->TestFront.has_value() ||
                                  (run->Core.Residency && run->Core.Residency->HasRing(run->Core.Rings[0].Key));
            snapshot.CanAccept = !snapshot.Stale && resident;
            if (snapshot.Stale) snapshot.AcceptDisabledReason = "The inputs changed since the run; discard the result and run again.";
            else if (!resident) snapshot.AcceptDisabledReason = "The GPU result is no longer resident; discard it.";
        }
        // Running, accepting or finished: the lifecycle's own refusal says why Accept is not available now.
        else if (const auto refused = GeometryProcessingDetail::GpuTransactionAcceptRefusal(run->Core)) snapshot.AcceptDisabledReason = refused->Message;
        return snapshot;
    }

    EditorPropertySmoothingResult AcceptEditorPropertySmoothing(const EditorProcessingCommands&,
                                                                const EditorPropertySmoothingTransactionHandle& run,
                                                                std::function<void(EditorPropertySmoothingResult)> onComplete)
    {
        if (!run)
        {
            EditorPropertySmoothingResult result;
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Message = "No smoothing transaction.";
            return result;
        }
        return PropertySmoothingDetail::BeginAccept(run, std::move(onComplete));
    }

    void DiscardEditorPropertySmoothing(const EditorProcessingCommands&, const EditorPropertySmoothingTransactionHandle& run)
    {
        // A queued job finalizes as cancelled on its next drain; the rings go now (freed after
        // their completions), so observation returns to the canonical slot at once.
        if (run)
            GP::DiscardGpuTransaction(run->Core, EditorCommandStatus::StaleEntity,
                                      "Vulkan property smoothing discarded; previous output retained.");
    }

    EditorPropertySmoothingTransactionHandle MakeEditorPropertySmoothingTransactionForTest(
        const EditorProcessingCommands& commands, const std::uint32_t id, const PropertySmoothingConfig& c,
        std::vector<double> frontValues, Graphics::GpuPropertyResidency* residency)
    {
        namespace PS = PropertySmoothingDetail;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorPropertySmoothingResult result;
        result.RequestedBackend = c.Backend;
        std::string diagnostic;
        // Like Prepare without the backend admission: the seam runs on a null device.
        auto config = c;
        config.Backend = PropertySmoothingBackend::Cpu;
        auto prepared = PS::Prepare(context, id, config, result, diagnostic);
        if (!prepared) return {};
        auto w = std::make_shared<EditorPropertySmoothingTransaction>();
        w->Core.Context = context;
        PS::Install(w);
        auto plan = S::PlanPropertyFilter(prepared->Values, prepared->Channels, prepared->Graph.Edges, c.Filter,
                                          prepared->Graph.BoundaryRows, prepared->Graph.Mass, diagnostic);
        if (!plan || frontValues.size() != prepared->Values.size()) return {};
        w->Plan = std::move(*plan);
        w->Values = std::move(prepared->Values);
        w->Publication = std::move(prepared->Publication);
        w->OutputWatch = std::move(prepared->OutputWatch);
        w->Publication.Config = c;
        auto& t = w->Core;
        t.Identity = {.EntityId = id, .Scope = ToEditorJobScope(c.Output.Domain),
                      .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField, .OutputName = c.Output.Name,
                          .RequestedDomain = EditorJobDomain::GpuCompute};
        w->Count = std::uint32_t(prepared->Props->Size());
        PS::SetRings(w, prepared->Entity, c);
        t.Residency = residency;
        if (residency)
        {
            auto& output = t.Rings[0];
            if (const auto back = AcquireGpuPropertyOutput(
                    *residency, context.World, prepared->Entity, c.Output, w->Count, c.Execution.PreviewBuffers);
                back && residency->Publish(output.Key))
                ++w->Previews;
            output.Generation = residency->RingGeneration(output.Key);
            if (w->Presentation)
            {
                auto& presentation = t.Rings[1];
                if (AcquireGpuPropertyOutput(*residency,
                        context.World,
                        prepared->Entity,
                        GpuPropertyPresentationRef(c.Output),
                        w->Count,
                        c.Execution.PreviewBuffers))
                    (void)residency->Publish(presentation.Key);
                presentation.Generation = residency->RingGeneration(presentation.Key);
            }
        }
        else ++w->Previews;
        w->TestFront = std::move(frontValues);
        t.TestFront = true;
        w->Result = result;
        w->Result.Status = EditorCommandStatus::Pending;
        w->Result.Message = "The GPU result waits for Accept or Discard.";
        w->Result.BackendId = "vulkan_compute";
        GP::ReadyGpuTransaction(t);
        return w;
    }

    EditorPropertySmoothingResult ApplyEditorPropertySmoothingCommand(const EditorProcessingCommands& commands, std::uint32_t id,
                                                                      const PropertySmoothingConfig& c,
                                                                      std::function<void(EditorPropertySmoothingResult)> onComplete)
    {
        namespace PS = PropertySmoothingDetail;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorPropertySmoothingResult result;
        result.RequestedBackend = c.Backend;
        std::string diagnostic;
        auto prepared = PS::Prepare(context, id, c, result, diagnostic);
        if (!prepared)
        {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Message = std::move(diagnostic);
            return result;
        }
        // Batch and agent callers: at once, accepted at the run's end.
        const auto execution = PS::BatchExecution(c.Execution);
        if (c.Backend == PropertySmoothingBackend::Vulkan)
        {
            (void)PS::StartVulkan(context, id, c, std::move(*prepared), result, std::move(onComplete), execution);
            return result;
        }
        if (!PS::StepRunsAvailable(context))
            return PS::RunCpuInline(context, c, *prepared, std::move(result));
        return PS::StartCpu(context, id, c, std::move(*prepared), std::move(result), std::move(onComplete), execution);
    }
}
