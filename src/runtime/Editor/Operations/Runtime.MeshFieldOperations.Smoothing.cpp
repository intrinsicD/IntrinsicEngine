// Typed property smoothing uses canonical domain capture and one guarded history transaction.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
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
import Extrinsic.Runtime.MeshSurfaceTopology;
import Geometry.HalfedgeMesh;
import Geometry.Smoothing;
import Geometry.HarmonicField;
import Extrinsic.Graphics.PropertyFilter;
import Extrinsic.Graphics.SparseConjugateGradient;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.GeometryPresentation;
import Geometry.DEC;
import Geometry.Properties;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.PropertyGraph.hpp"

namespace Extrinsic::Runtime
{
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
        constexpr std::array kFields{
            ConfigFieldSpec{.Name = "input", .Type = FT::PropertyRef, .Description = "Floating scalar or vector property to smooth.", .RefKinds = kFloating},
            ConfigFieldSpec{.Name = "output", .Type = FT::PropertyRef, .Description = "Property receiving the result, on the input domain with the input's channel count.", .RefKinds = kFloating},
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .Description = "Positions that define neighborhoods and distances.", .RefKinds = kVec3},
            ConfigFieldSpec{.Name = "weight", .Type = FT::Enum, .Description = "Neighbor weighting; the mesh weights need mesh-vertex input.", .EnumNames = kWeightNames},
            ConfigFieldSpec{.Name = "method", .Type = FT::Enum, .Description = "Smoothing filter.", .EnumNames = kMethodNames},
            ConfigFieldSpec{.Name = "laplacian", .Type = FT::Enum, .Description = "Laplacian normalization.", .EnumNames = kLaplacianNames},
            ConfigFieldSpec{.Name = "iterations", .Type = FT::UInt, .Description = "Filter passes of the explicit methods.", .Min = 1, .Max = 10000},
            ConfigFieldSpec{.Name = "neighbors", .Type = FT::UInt, .Description = "k of the k-nearest-neighbor graph.", .Min = 1, .Max = 1024},
            ConfigFieldSpec{.Name = "spatial_sigma", .Type = FT::Float, .Description = "Distance scale of Gaussian and bilateral weights (position units).", .Min = 0, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "lambda", .Type = FT::Float, .Description = "Step size of averaging, bilateral and Taubin passes.", .Min = 0, .Max = 1, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "mu", .Type = FT::Float, .Description = "Taubin's negative inflation step.", .Min = -1, .Max = 0, .ExclusiveMax = true},
            ConfigFieldSpec{.Name = "heat_time", .Type = FT::Float, .Description = "Diffusion time of the spectral heat filter.", .Min = 0, .Max = 1000, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "range_sigma", .Type = FT::Float, .Description = "Value scale of the bilateral filter (property units).", .Min = 0, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "time_step", .Type = FT::Float, .Description = "Backward-Euler time step of implicit smoothing.", .Min = 0, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "solver_tolerance", .Type = FT::Float, .Description = "Relative residual at which conjugate gradient stops.", .Min = 0, .Max = 1, .ExclusiveMin = true, .ExclusiveMax = true},
            ConfigFieldSpec{.Name = "max_solver_iterations", .Type = FT::UInt, .Description = "Conjugate-gradient iteration cap.", .Min = 1, .Max = 100000},
            ConfigFieldSpec{.Name = "solver", .Type = FT::Enum, .Description = "Linear solver of implicit smoothing.", .EnumNames = kSolverNames},
            ConfigFieldSpec{.Name = "preserve_boundary", .Type = FT::Bool, .Description = "Keep mesh-boundary values fixed (mesh-vertex input only)."},
            ConfigFieldSpec{.Name = "smoothness_penalty", .Type = FT::Enum, .Description = "Penalty on neighbor differences in the variational fit.", .EnumNames = kSmoothnessPenaltyNames},
            ConfigFieldSpec{.Name = "data_penalty", .Type = FT::Enum, .Description = "Penalty on deviation from the input in the variational fit.", .EnumNames = kDataPenaltyNames},
            ConfigFieldSpec{.Name = "fidelity", .Type = FT::Enum, .Description = "Fixed data weight, or the weight whose RMS residual matches the noise level.", .EnumNames = kFidelityNames},
            ConfigFieldSpec{.Name = "fit_weight", .Type = FT::Float, .Description = "Data weight of the variational fit.", .Min = 0, .Max = 1e12, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "noise_level", .Type = FT::Float, .Description = "Target RMS residual (property units).", .Min = 0, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "penalty_delta", .Type = FT::Float, .Description = "Huber/L1 smoothing threshold (property units); 0 only with ADMM and no Huber penalty.", .Min = 0},
            ConfigFieldSpec{.Name = "bound", .Type = FT::Enum, .Description = "Tolerance bound on the deviation from the input.", .EnumNames = kBoundNames},
            ConfigFieldSpec{.Name = "bound_radius", .Type = FT::Float, .Description = "Uniform bound radius (property units).", .Min = 0},
            ConfigFieldSpec{.Name = "bound_radii", .Type = FT::PropertyRef, .Description = "Per-row bound radii on the input domain.", .Nullable = true, .RefKinds = kFloatScalar},
            ConfigFieldSpec{.Name = "fit_solver", .Type = FT::Enum, .Description = "Variational-fit algorithm.", .EnumNames = kFitSolverNames},
            ConfigFieldSpec{.Name = "max_fit_iterations", .Type = FT::UInt, .Description = "Variational-fit iteration cap.", .Min = 1, .Max = 100000},
            ConfigFieldSpec{.Name = "bound_norm", .Type = FT::Enum, .Description = "Bound shape on vector properties.", .EnumNames = kBoundNormNames},
            ConfigFieldSpec{.Name = "smoothness_order", .Type = FT::Enum, .Description = "First-order differences or second-order non-local TGV.", .EnumNames = kOrderNames},
            ConfigFieldSpec{.Name = "second_order_weight", .Type = FT::Float, .Description = "Weight of the second-order TGV term.", .Min = 0, .ExclusiveMin = true},
            ConfigFieldSpec{.Name = "backend", .Type = FT::Enum, .Description = "CPU reference or Vulkan for the explicit filters and CG implicit smoothing.", .EnumNames = kBackendNames},
            ConfigFieldSpec{.Name = "fit_tolerance", .Type = FT::Float, .Description = "Relative change at which the variational fit stops.", .Min = 0, .Max = 1, .ExclusiveMin = true, .ExclusiveMax = true},
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
                (IsStructuralVertexProperty(c.Output.Name) && c.Output.Name != "v:position") ||
                (c.Output.Name == "v:position" && (c.Output.ValueKind != K::Vec3 || c.Output.Domain != c.Positions.Domain)))
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

        std::optional<ECS::EntityHandle> Target(const EditorProcessingContext& context, std::uint32_t id,
            const PropertySmoothingConfig& c, std::string& diagnostic)
        {
            const auto validation = Validate(SerializePropertySmoothingConfig(c), {}, kPropertySmoothingConfigSectionName);
            if (!validation.Usable()) { diagnostic = validation.Diagnostics.front().Message; return {}; }
            if (!context.Scene || (context.AttachmentActive && !context.AttachmentActive()) || !GP::EditorProcessingContextWorldCurrent(context)) { diagnostic = "Workspace is unavailable."; return {}; }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity) { diagnostic = "Choose an existing geometry entity."; return {}; }
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
            if (!props || (!ResolveGeometryProperty(a, c.Input, props->Size(), false).Resolved() || !CountMatches(*props, c.Input)))
            { diagnostic = "Choose an existing floating scalar or vector property."; return {}; }
            if (props->Exists(c.Output.Name) && (!ResolveGeometryProperty(a, c.Output, props->Size(), false).Resolved() || !CountMatches(*props, c.Output)))
            { diagnostic = "Output exists with a different storage type or cardinality."; return {}; }
            if (c.Positions.Domain != c.Input.Domain && c.Positions.Domain != GP::PrimaryPointDomain(a))
            { diagnostic = "Positions must belong to the input domain or the entity's vertices/nodes."; return {}; }
            const auto* positions = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!positions || !ResolveGeometryProperty(a, c.Positions, positions->Size(), false).Resolved())
            { diagnostic = "Choose an existing vec3 position property."; return {}; }
            if (PerRowBounds(c) && (!ResolveGeometryProperty(a, c.BoundRadii, props->Size(), false).Resolved() || !CountMatches(*props, c.BoundRadii)))
            { diagnostic = "Choose an existing float/double bound radius property."; return {}; }
            if (c.Backend == PropertySmoothingBackend::Vulkan &&
                (!context.Device || !context.Device->IsOperational() || !context.Device->SupportsShaderFloat64() ||
                 !context.SpatialIndices || !context.SpatialIndices->GpuQueriesAvailable() || !context.JobCommands.Available()))
            { diagnostic = "Vulkan property smoothing needs an operational device with shader double precision and framed GPU jobs."; return {}; }
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
                if (meshFamily && c.Positions.Domain != D::MeshVertex) c.Positions = {D::MeshVertex, "v:position", K::Vec3};
                else if (graphFamily && c.Positions.Domain != D::GraphNode) c.Positions = {D::GraphNode, "v:position", K::Vec3};
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
        return Json{{"input", ConfigDetail::EncodePointPropertyRef(c.Input)}, {"output", ConfigDetail::EncodePointPropertyRef(c.Output)},
            {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)}, {"weight", unsigned(c.Weight)},
            {"method", unsigned(c.Filter.Method)}, {"laplacian", unsigned(c.Filter.Laplacian)},
            {"iterations", c.Filter.Iterations}, {"neighbors", c.Neighbors}, {"spatial_sigma", c.SpatialSigma},
            {"lambda", c.Filter.Lambda}, {"mu", c.Filter.Mu}, {"heat_time", c.Filter.HeatTime}, {"range_sigma", c.Filter.RangeSigma},
            {"time_step", c.Filter.TimeStep}, {"solver_tolerance", c.Filter.SolverTolerance},
            {"max_solver_iterations", c.Filter.MaxSolverIterations}, {"solver", unsigned(c.Filter.Solver)}, {"preserve_boundary", c.PreserveBoundary},
            {"smoothness_penalty", unsigned(c.Filter.SmoothnessPenalty)}, {"data_penalty", unsigned(c.Filter.DataPenalty)},
            {"fidelity", unsigned(c.Filter.Fidelity)}, {"fit_weight", c.Filter.FitWeight}, {"noise_level", c.Filter.NoiseLevel},
            {"penalty_delta", c.Filter.PenaltyDelta}, {"bound", unsigned(c.Filter.Bound)}, {"bound_radius", c.Filter.BoundRadius},
            {"bound_radii", c.BoundRadii.Name.empty() ? Json(nullptr) : ConfigDetail::EncodePointPropertyRef(c.BoundRadii)},
            {"fit_solver", unsigned(c.Filter.FitAlgorithm)}, {"max_fit_iterations", c.Filter.MaxFitIterations},
            {"bound_norm", unsigned(c.Filter.BoundNorm)}, {"smoothness_order", unsigned(c.Filter.SmoothnessOrder)},
            {"second_order_weight", c.Filter.SecondOrderWeight}, {"backend", unsigned(c.Backend)}, {"fit_tolerance", c.Filter.FitTolerance}}.dump();
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
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        return {entity.has_value(), std::move(diagnostic)};
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
                std::visit([&](const auto& rows) {
                    using T = typename std::decay_t<decltype(rows)>::value_type;
                    if (target.Exists) properties->GetOrAdd<T>(output.Name).Vector() = rows;
                    else if (auto property = properties->Get<T>(output.Name)) properties->Remove(property);
                }, target.Values);
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
                if (output.Name == "v:position") ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(), entity);
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
            const auto entity = Target(context, id, c, diagnostic);
            if (!entity) return std::nullopt;
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
    // residency slots it reads and writes, and the transaction phase the panel drives.
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
        // Device workspaces leased from the spatial cache; the recorders reach them through this
        // run, so they stay leased while queued work may still record or execute.
        std::shared_ptr<Graphics::SparseConjugateGradientWorkspace> Solver{};
        std::shared_ptr<Graphics::PropertyFilterWorkspace> Filter{}; // the explicit kernels, or the CG path's store
        bool FinalQueued{}; // the last chunk (reports only) is under way
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
        bool StoreRecorded{}; // the queued submission writes the output back
        std::uint32_t Previews{};
        bool StopRequested{}, Stopped{};
        // Accept's test seam: the front's values (no readback).
        std::optional<std::vector<double>> TestFront{};
    };

    namespace PropertySmoothingDetail
    {
        using Work = std::shared_ptr<EditorPropertySmoothingTransaction>;
        constexpr std::uint32_t kRingDepth = 3u;   // fronts bound directly by the renderer (ADR 0030 decision 4)
        constexpr const char* kRefused = "Vulkan property smoothing could not queue its device work; previous output retained.";

        // The captured inputs and the output are unchanged (an edit or undo of either while
        // the run computes or waits makes it stale; the publication keeps its own value guard).
        bool InputsCurrent(const EditorPropertySmoothingTransaction& w)
        {
            const auto& ctx = w.Core.Context;
            return GP::GeometryPropertiesCurrent(ctx, w.Publication.Entity, w.Publication.Watches) &&
                   GP::GeometryPropertiesCurrent(ctx, w.Publication.Entity, std::span{&w.OutputWatch, 1});
        }
        bool Current(const Work& w) { return GP::GpuTransactionCurrent(w->Core); }
        auto& Output(const Work& w) { return w->Core.Rings[0]; }
        auto& PresentationRing(const Work& w) { return w->Core.Rings[1]; }
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
        // The slots a submission needs: the canonical input (uploaded once per revision) and a
        // write slot of the output ring(s). False while the residency defers (a refused upload
        // or an exhausted ring); the caller polls again next frame.
        bool AcquireSlots(const Work& w)
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
            // A ring another run created defers like an exhausted one.
            if (GP::AcquireGpuTransactionBack(w->Core, 0, entity, c.Output, w->Count, kRingDepth) != GP::GpuRingAcquisition::Ready)
                return false;
            return !w->Presentation ||
                   GP::AcquireGpuTransactionBack(w->Core, 1, entity, GpuPropertyPresentationRef(c.Output), w->Count, kRingDepth) ==
                       GP::GpuRingAcquisition::Ready;
        }
        // Ends the compute job as failed without queueing more work.
        bool Refuse(const Work& w, std::string diagnostic)
        {
            ReleaseWorkspaces(w);
            auto& gpu = w->Core.Gpu;
            gpu = std::make_shared<SpatialGpuResult>();
            gpu->State = SpatialQueryState::Failed;
            gpu->Diagnostic = std::move(diagnostic);
            return true;
        }
        bool Defer(const Work& w)
        {
            if (!GP::GpuTransactionDeferralsExhausted(w->Core)) return false;
            return Refuse(w, "The GPU property residency refused the run's input or output slot; previous output retained.");
        }
        // The submission that wrote Back has finished: its slot becomes the front the renderer shows.
        void PublishPreview(const Work& w)
        {
            const bool recorded = std::exchange(w->StoreRecorded, false);
            Output(w).Back.reset();
            PresentationRing(w).Back.reset();
            auto* residency = w->Core.Residency;
            if (!recorded || !residency) return;
            if (residency->Publish(Output(w).Key)) ++w->Previews;
            if (w->Presentation) (void)residency->Publish(PresentationRing(w).Key);
        }
        bool QueueExplicit(const Work& w)
        {
            const auto& ctx = w->Core.Context;
            auto& gpu = w->Core.Gpu;
            w->Filter = ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::PropertyFilterWorkspace>();
            if (!w->Filter) return false;
            w->StoreRecorded = true;
            // The result stays on the device (the ring); the readback is one word that only
            // reports completion.
            gpu = ctx.SpatialIndices->QueueGpuCompute(sizeof(double),
                [w, filter = w->Filter](RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle {
                    if (w->Core.Abandoned) return {};
                    NoteUses(w);
                    const auto io = ResidentIo(w);
                    return filter->Record(commands, {.Values = w->Values, .Channels = std::uint32_t(w->Plan.Channels),
                        .Edges = w->Edges, .Weights = w->Weights, .Degree = w->Plan.Degree, .Fixed = w->Fixed}, w->Params, &io);
                });
            return gpu != nullptr;
        }
        // One bounded chunk per immediate submission (GRAPHICS-150); each chunk reads back only
        // the reports it is observed by. A chunk also stores the latest complete time step into
        // the ring when a write slot is held (a dropped preview otherwise); the final pass, after
        // every solve finished, stores the result.
        bool QueueChunk(const Work& w, const bool final)
        {
            const auto& ctx = w->Core.Context;
            auto& gpu = w->Core.Gpu;
            const std::uint32_t solves = w->Publication.Config.Filter.Iterations * std::uint32_t(w->Plan.Channels);
            w->FinalQueued = final;
            gpu = ctx.SpatialIndices->QueueGpuCompute(
                Graphics::SparseConjugateGradientWorkspace::ReportReadbackBytes(solves),
                [w, final, solver = w->Solver, filter = w->Filter](RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle {
                    if (w->Core.Abandoned) return {};
                    const auto channels = std::uint32_t(w->Plan.Channels), rows = std::uint32_t(w->Plan.Count);
                    if (solver->Chunks() == 0u)
                    {
                        // The seeds (each channel's first solve starts from the input) come from
                        // the canonical slot; only the CPU-assembled coupling was uploaded.
                        NoteUses(w);
                        const auto io = ResidentIo(w);
                        if (!solver->RecordUpload(commands) ||
                            !filter->RecordLoad(commands, io, channels, solver->ResultBuffer(), solver->SolutionsAddress(), 1u, rows))
                            return {};
                    }
                    const auto buffer = final ? solver->RecordFinal(commands) : solver->RecordNext(commands);
                    if (!buffer.IsValid()) return {};
                    const auto iterations = solver->CompletedSolves() / channels;
                    if (Output(w).Back && iterations > 0u)
                    {
                        NoteUses(w);
                        const auto io = ResidentIo(w);
                        const std::uint64_t solutions = solver->SolutionsAddress() +
                            std::uint64_t(iterations - 1u) * channels * rows * sizeof(double);
                        w->StoreRecorded = filter->RecordStore(commands, io, channels, solver->ResultBuffer(), solutions, 1u, rows);
                    }
                    return buffer;
                }, SpatialGpuLatency::Immediate);
            return gpu != nullptr;
        }
        // Main-thread readiness poll of the compute job: queues the device work, publishes each
        // finished preview and reports when the run reached its end (or failed / was stopped).
        bool Poll(const Work& w)
        {
            const auto& ctx = w->Core.Context;
            auto& gpu = w->Core.Gpu;
            if (w->Implicit)
            {
                if (!w->Solver)
                {
                    if (!AcquireSlots(w)) return Defer(w);
                    w->Solver = ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::SparseConjugateGradientWorkspace>();
                    w->Filter = ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::PropertyFilterWorkspace>();
                    if (!w->Solver || !w->Filter) return Refuse(w, "Vulkan property smoothing device workspace unavailable; previous output retained.");
                    const auto& f = w->Publication.Config.Filter;
                    const auto channelCount = std::uint32_t(w->Plan.Channels);
                    if (!w->Solver->Begin({.Matrix = {.Rows = std::uint32_t(w->Plan.Count), .RowOffsets = w->Offsets,
                            .Columns = w->Columns, .Values = w->System.Matrix.Values},
                            .Solves = f.Iterations * channelCount,
                            .RhsDiagonal = w->System.RhsDiagonal, .RhsConstant = w->System.RhsConstant,
                            .ChainStride = channelCount, .SeedsOnDevice = true,
                            .MaxIterations = f.MaxSolverIterations, .Tolerance = f.SolverTolerance}))
                        return Refuse(w, "The Vulkan solver refused the implicit system; previous output retained.");
                    return QueueChunk(w, false) ? false : Refuse(w, kRefused);
                }
                if (!gpu || gpu->State == SpatialQueryState::Failed) return true;
                if (gpu->State != SpatialQueryState::Ready) return false;
                PublishPreview(w);
                if (w->FinalQueued) return true;
                w->Solver->Observe(gpu->Data);
                if (ctx.JobCommands.ReportProgress && w->Core.RunToken.IsValid())
                {
                    // Chained solves (iterations x channels) are the one place a
                    // count is known; the explicit kernels are a single dispatch.
                    const auto total = std::max<std::uint32_t>(
                        1u, w->Publication.Config.Filter.Iterations * std::uint32_t(w->Plan.Channels));
                    ctx.JobCommands.ReportProgress(
                        w->Core.RunToken,
                        {.Normalized = std::min(1.0f, float(w->Solver->CompletedSolves()) / float(total)),
                         .Determinate = true});
                }
                if (w->Solver->Finished())
                {
                    // The final store needs a write slot: wait for one rather than accept a stale front.
                    if (!AcquireSlots(w)) return Defer(w);
                    return QueueChunk(w, true) ? false : Refuse(w, kRefused);
                }
                if (w->StopRequested)
                {
                    w->Stopped = true;
                    // Nothing to accept: the run ends here, not in an Accept of a missing front.
                    if (w->Previews == 0u)
                        Finish(w, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::NoChange,
                               "Vulkan property smoothing stopped before a preview; previous output retained.");
                    return true;
                }
                (void)AcquireSlots(w); // no slot: this chunk computes without a preview
                return QueueChunk(w, false) ? false : Refuse(w, kRefused);
            }
            if (!gpu)
            {
                if (!AcquireSlots(w)) return Defer(w);
                return QueueExplicit(w) ? false : Refuse(w, kRefused);
            }
            if (gpu->State == SpatialQueryState::Ready) PublishPreview(w);
            return gpu->State == SpatialQueryState::Ready || gpu->State == SpatialQueryState::Failed;
        }
        // The compute job ended: the run either waits for Accept or failed.
        void CompleteRun(const Work& w)
        {
            w->Input.reset();
            const auto& gpu = w->Core.Gpu;
            if (!gpu || gpu->State != SpatialQueryState::Ready)
            {
                Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                       gpu && !gpu->Diagnostic.empty() ? gpu->Diagnostic
                       : "Vulkan property smoothing did not return a result; previous output retained.");
                return;
            }
            if (w->Implicit && !w->Stopped)
            {
                // Every chained solve must converge, as on the CPU.
                const std::size_t solves = std::size_t(w->Publication.Config.Filter.Iterations) * w->Plan.Channels;
                bool converged = w->Solver && w->Solver->Finished() &&
                    gpu->Data.size() == Graphics::SparseConjugateGradientWorkspace::ReportReadbackBytes(std::uint32_t(solves));
                std::size_t applications = 0;
                for (std::size_t k = 0; converged && k < solves; ++k)
                {
                    Graphics::SparseCgReport report{};
                    std::memcpy(&report, gpu->Data.data() + k * sizeof(report), sizeof(report));
                    converged = report.Status == Graphics::SparseCgStatus::Converged;
                    applications += report.Iterations + 1;
                }
                if (!converged)
                {
                    Finish(w, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed,
                           "Implicit solver failed to converge on Vulkan; no property was changed.");
                    return;
                }
                w->Result.OperatorApplications = applications;
            }
            else if (!w->Implicit) w->Result.OperatorApplications = Graphics::PropertyFilterWorkspace::DispatchCount(w->Params);
            if (w->Previews == 0u)
            {
                Finish(w, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                       w->Stopped ? "Vulkan property smoothing stopped before a preview; previous output retained."
                                  : "Vulkan property smoothing published no preview; previous output retained.");
                return;
            }
            ReleaseWorkspaces(w);
            GP::ReadyGpuTransaction(w->Core);
            w->Result.Status = EditorCommandStatus::Pending;
            w->Result.Message = w->Stopped ? "Stopped; the latest preview waits for Accept or Discard."
                                           : "The GPU result waits for Accept or Discard.";
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
            t.Hooks = {
                .Current = [raw] { return InputsCurrent(*raw); },
                .Poll = [self] { return Poll(self()); },
                .CompleteRun = [self] { CompleteRun(self()); },
                .CompleteAccept = [self] { CompleteAccept(self()); },
                .Release = [self] {
                    const auto w = self();
                    ReleaseWorkspaces(w);
                    w->Input.reset();
                    w->Base.reset();
                },
                .Deliver = [raw](const EditorCommandStatus status, std::string message) {
                    raw->Result.Status = status;
                    raw->Result.Message = std::move(message);
                    if (auto sink = std::move(raw->Sink)) sink(raw->Result);
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
            if (!GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w))) return w->Result;
            result.Status = EditorCommandStatus::Pending;
            result.Message = "Reading the GPU result back.";
            w->Result = result;
            return result;
        }
        // Builds the run from the capture and queues its compute job. Null with `result` filled
        // (a rejection) when the request cannot run.
        Work StartVulkan(const EditorProcessingContext& context, const std::uint32_t id, const PropertySmoothingConfig& c,
                         Prepared prepared, EditorPropertySmoothingResult& result,
                         std::function<void(EditorPropertySmoothingResult)> onComplete, const bool autoAccept)
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
            for (std::size_t i = 0; i < w->Plan.Count; ++i)
                w->RestoreMask.push_back(w->Plan.Fixed[i] || w->Plan.Isolated[i] ? 1u : 0u);
            w->Values = std::move(values);
            w->Publication = std::move(prepared.Publication);
            w->OutputWatch = std::move(prepared.OutputWatch);
            auto& t = w->Core;
            t.Identity = {.EntityId = id, .Scope = ToEditorJobScope(c.Output.Domain),
                          .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField, .OutputName = c.Output.Name};
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
            t.AutoAccept = autoAccept;
            w->Sink = GuardEditorProcessingResult(context, std::move(onComplete));
            w->Result = result;
            w->Result.Status = EditorCommandStatus::Pending;
            w->Result.Message = "Vulkan property smoothing queued.";
            if (!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w), "Vulkan property smoothing").IsValid())
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Message = GP::MeshSupport::QueuedJobRejectedMessage(t.JobLabel);
                return {};
            }
            result = w->Result;
            return w;
        }
    }

    EditorPropertySmoothingTransactionHandle StartEditorPropertySmoothing(const EditorProcessingCommands& commands, const std::uint32_t id,
                                                                          const PropertySmoothingConfig& c,
                                                                          EditorPropertySmoothingResult& failure)
    {
        namespace PS = PropertySmoothingDetail;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        failure = {};
        failure.RequestedBackend = c.Backend;
        failure.Status = EditorCommandStatus::InvalidProcessingParameters;
        if (c.Backend != PropertySmoothingBackend::Vulkan)
        {
            failure.Message = "The smoothing transaction runs the Vulkan backend; CPU runs publish at once.";
            return {};
        }
        std::string diagnostic;
        auto prepared = PS::Prepare(context, id, c, failure, diagnostic);
        if (!prepared) { failure.Message = diagnostic; return {}; }
        return PS::StartVulkan(context, id, c, std::move(*prepared), failure, {}, false);
    }

    void StopEditorPropertySmoothing(const EditorPropertySmoothingTransactionHandle& run)
    {
        if (run) run->StopRequested = true;
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
        if (run->Core.Phase == EditorGpuTransactionPhase::ReadyToAccept)
        {
            snapshot.Stale = !PropertySmoothingDetail::Current(run);
            const bool resident = run->TestFront.has_value() ||
                                  (run->Core.Residency && run->Core.Residency->HasRing(run->Core.Rings[0].Key));
            snapshot.CanAccept = !snapshot.Stale && resident;
            if (snapshot.Stale) snapshot.AcceptDisabledReason = "The inputs changed since the run; discard the result and run again.";
            else if (!resident) snapshot.AcceptDisabledReason = "The GPU result is no longer resident; discard it.";
        }
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
                      .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField, .OutputName = c.Output.Name};
        w->Count = std::uint32_t(prepared->Props->Size());
        PS::SetRings(w, prepared->Entity, c);
        t.Residency = residency;
        if (residency)
        {
            auto& output = t.Rings[0];
            if (const auto back = AcquireGpuPropertyOutput(*residency, context.World, prepared->Entity, c.Output, w->Count, PS::kRingDepth);
                back && residency->Publish(output.Key))
                ++w->Previews;
            output.Generation = residency->RingGeneration(output.Key);
            if (w->Presentation)
            {
                auto& presentation = t.Rings[1];
                if (AcquireGpuPropertyOutput(*residency, context.World, prepared->Entity, GpuPropertyPresentationRef(c.Output), w->Count, PS::kRingDepth))
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
        using PropertyGraphDetail::Channel;
        using PropertyGraphDetail::Capture;
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorPropertySmoothingResult result;
        result.RequestedBackend = c.Backend;
        const auto fail = [&](std::string message) { result.Status = EditorCommandStatus::InvalidProcessingParameters; result.Message = std::move(message); return result; };
        std::string diagnostic;
        auto prepared = PS::Prepare(context, id, c, result, diagnostic);
        if (!prepared) return fail(diagnostic);
        if (c.Backend == PropertySmoothingBackend::Vulkan)
        {
            // Batch and agent callers accept automatically when the device finishes.
            (void)PS::StartVulkan(context, id, c, std::move(*prepared), result, std::move(onComplete), true);
            return result;
        }
        auto& samples = prepared->Samples;
        auto& values = prepared->Values;
        auto& graph = prepared->Graph;
        const auto channels = prepared->Channels;
        const auto* props = prepared->Props;
        S::PropertyFilterResult filtered;
        std::string fitSummary;
        if (c.Filter.Method == S::PropertyFilter::VariationalFit)
        {
            std::vector<double> radii;
            if (PerRowBounds(c))
            {
                bool valid = true;
                std::visit([&](const auto& stored) {
                    for (const auto slot : samples.Slots)
                    {
                        const double r = Channel(stored[slot], 0);
                        valid &= std::isfinite(r) && r >= 0;
                        radii.push_back(r);
                    }
                }, Capture(*props, c.BoundRadii).Values);
                if (!valid) return fail("Bound radii must be finite and nonnegative.");
            }
            // Second order measures gradients against the same sample points the graph was built from.
            std::vector<double> positions;
            if (c.Filter.SmoothnessOrder == S::FitOrder::Second)
                for (const auto& point : samples.Points) positions.insert(positions.end(), {point.x, point.y, point.z});
            const auto fit = Geometry::HarmonicField::FitProperty(values, channels, graph.Edges, c.Filter,
                                                                  graph.BoundaryRows, graph.Mass, radii, positions);
            filtered = {.Success = fit.Success, .Diagnostic = fit.Diagnostic, .Values = fit.Values,
                        .OperatorApplications = fit.Stats.Solves};
            result.FitWeight = fit.Stats.FitWeight;
            result.RmsResidual = fit.Stats.RmsResidual;
            result.ActiveBounds = fit.Stats.ActiveBounds;
            const auto number = [](double x) { char text[32]; std::snprintf(text, sizeof text, "%.6g", x); return std::string{text}; };
            fitSummary = "; weight " + number(fit.Stats.FitWeight) + ", RMS residual " +
                         number(fit.Stats.RmsResidual) + ", " + std::to_string(fit.Stats.Iterations) +
                         " iteration(s), " + std::to_string(fit.Stats.Factorizations) + " factorization(s), " + std::to_string(fit.Stats.ActiveBounds) + " active bound(s)" +
                         (fit.Stats.NoiseTargetClamped ? "; noise level unreachable, weight clamped" : "");
        }
        else filtered = S::FilterProperty(values, channels, graph.Edges, c.Filter, graph.BoundaryRows, graph.Mass);
        return PS::Publish(context, prepared->Publication, filtered, std::move(result), fitSummary);
    }
}
