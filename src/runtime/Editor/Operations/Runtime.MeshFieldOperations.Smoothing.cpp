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
import Extrinsic.Runtime.GeometryPresentation;
import Geometry.DEC;
import Geometry.Properties;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
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
    namespace
    {
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

        struct SmoothingGpuWork
        {
            SmoothingPublication Publication{};
            std::vector<double> Values{};
            S::PropertyFilterPlan Plan{};
            std::vector<std::uint32_t> Edges{}, Fixed{};
            std::vector<double> Weights{};
            Graphics::PropertyFilterGpuParams Params{};
            // Implicit smoothing: the assembled system as chained CG solves (iterations x channels).
            bool Implicit{};
            std::vector<std::uint32_t> Offsets{}, Columns{};
            S::PropertyImplicitSystem System{};
            std::vector<double> ChannelMajor{}, SeedRhs{};
            std::shared_ptr<Graphics::SparseConjugateGradientWorkspace> Solver{};
            bool FinalQueued{}; // the solutions' single readback is under way
            std::shared_ptr<SpatialGpuResult> Gpu{};
            EditorPropertySmoothingResult Result{};
            bool Abandoned{};
        };

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
    }

    EditorPropertySmoothingResult ApplyEditorPropertySmoothingCommand(const EditorProcessingCommands& commands, std::uint32_t id,
                                                                      const PropertySmoothingConfig& c,
                                                                      std::function<void(EditorPropertySmoothingResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorPropertySmoothingResult result;
        result.RequestedBackend = c.Backend;
        const auto fail = [&](std::string message) { result.Status = EditorCommandStatus::InvalidProcessingParameters; result.Message = std::move(message); return result; };
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        if (!entity) return fail(diagnostic);
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        GP::PointInputCapture samples;
        if (!PropertyGraphDetail::CaptureSamples(a, c.Input.Domain, c.Positions, samples, diagnostic)) return fail(diagnostic);
        const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
        const auto input = Capture(*props, c.Input);
        const auto channels = GeometryPropertyComponentCount(c.Input.ValueKind);
        std::vector<double> values;
        std::visit([&](const auto& rows) {
            for (const auto slot : samples.Slots)
                for (std::size_t ch = 0; ch < channels; ++ch) values.push_back(Channel(rows[slot], ch));
        }, input.Values);
        PropertyGraphDetail::Graph graph;
        if (!PropertyGraphDetail::BuildGraph(a, {.Positions = c.Positions, .Weight = c.Weight, .Neighbors = c.Neighbors,
                .SpatialSigma = c.SpatialSigma, .LumpedMass = c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass,
                .BoundaryRows = c.PreserveBoundary, .Operation = "Property smoothing"}, samples, graph, diagnostic))
            return fail(diagnostic);
        result.LiveCount = samples.LiveCount;
        result.EdgeCount = graph.Edges.size();
        // An aliased output is guarded by its expected values; unrelated input revisions stay fixed.
        SmoothingPublication publication{.Entity = *entity, .Config = c, .Slots = samples.Slots,
            .SlotCount = samples.SlotCount, .Channels = channels, .Before = Capture(*props, c.Output),
            .Watches = std::move(samples.Inputs)};
        publication.Watches.push_back(GP::ObserveGeometryProperty(a, c.Input.Domain, c.Input.Name));
        if (PerRowBounds(c)) publication.Watches.push_back(GP::ObserveGeometryProperty(a, c.BoundRadii.Domain, c.BoundRadii.Name));
        std::erase_if(publication.Watches, [&](const auto& watch) { return watch.Domain == c.Output.Domain && watch.Name == c.Output.Name; });

        if (c.Backend == PropertySmoothingBackend::Vulkan)
        {
            auto w = std::make_shared<SmoothingGpuWork>();
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
                // Channel-major seeds: the first step of channel k starts from the input values.
                w->ChannelMajor.resize(values.size());
                w->SeedRhs.resize(values.size());
                for (std::size_t k = 0; k < channels; ++k)
                    for (std::size_t i = 0; i < rows; ++i)
                    {
                        const double x = values[i * channels + k];
                        w->ChannelMajor[k * rows + i] = x;
                        w->SeedRhs[k * rows + i] = w->System.RhsDiagonal[i] * x + w->System.RhsConstant[k * rows + i];
                    }
                // Chained solves share one right-hand-side diagonal across channels.
                std::vector<double> diagonal;
                for (std::size_t k = 0; k < channels; ++k)
                    diagonal.insert(diagonal.end(), w->System.RhsDiagonal.begin(), w->System.RhsDiagonal.end());
                w->System.RhsDiagonal = std::move(diagonal);
                w->Values = std::move(values);
                w->Publication = std::move(publication);
                w->Result = result;
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
            w->Values = std::move(values);
            w->Publication = std::move(publication);
            w->Result = result;
            }
            const EditorJobIdentity identity{.EntityId = id, .Scope = ToEditorJobScope(c.Output.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField, .OutputName = c.Output.Name};
            if (auto active = GP::MeshSupport::FindActiveEditorJob(context, identity); active && IsActiveEditorJobState(active->State))
            {
                result.Status = EditorCommandStatus::Pending;
                result.Message = "A smoothing job for this output is already active.";
                return result;
            }
            auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
            auto delivered = std::make_shared<bool>(false);
            auto pending = result;
            pending.Status = EditorCommandStatus::Pending;
            pending.Message = "Vulkan property smoothing queued.";
            const auto current = [context, w] {
                return !w->Abandoned && GP::GeometryPropertiesCurrent(context, w->Publication.Entity, w->Publication.Watches) &&
                       GP::EditorProcessingContextWorldCurrent(context);
            };
            JobDesc gpu{
                .DebugName = "Vulkan property smoothing", .Scope = context.World, .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [context, w, current] {
                    if (!current()) return true;
                    if (w->Implicit)
                    {
                        // One bounded chunk per immediate submission (GRAPHICS-150); each chunk reads
                        // back only the reports it is observed by, the solutions come once at the end
                        // (GRAPHICS-153).
                        const std::uint32_t solves = w->Publication.Config.Filter.Iterations * std::uint32_t(w->Plan.Channels);
                        const auto queue = [&] {
                            w->Gpu = context.SpatialIndices->QueueGpuCompute(
                                Graphics::SparseConjugateGradientWorkspace::ReportReadbackBytes(solves),
                                [solver = w->Solver, w](RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle {
                                    if (w->Abandoned) return {};
                                    return solver->RecordNext(commands);
                                }, SpatialGpuLatency::Immediate);
                        };
                        const auto queueFinal = [&] {
                            w->FinalQueued = true;
                            w->Gpu = context.SpatialIndices->QueueGpuCompute(
                                Graphics::SparseConjugateGradientWorkspace::ReadbackBytes(std::uint32_t(w->Plan.Count), solves),
                                [solver = w->Solver, w](RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle {
                                    if (w->Abandoned) return {};
                                    return solver->RecordFinal(commands);
                                }, SpatialGpuLatency::Immediate);
                        };
                        if (!w->Solver)
                        {
                            w->Solver = std::make_shared<Graphics::SparseConjugateGradientWorkspace>(*context.Device);
                            const auto& f = w->Publication.Config.Filter;
                            const auto channelCount = std::uint32_t(w->Plan.Channels);
                            if (!w->Solver->Begin({.Matrix = {.Rows = std::uint32_t(w->Plan.Count), .RowOffsets = w->Offsets,
                                    .Columns = w->Columns, .Values = w->System.Matrix.Values},
                                    .Solves = f.Iterations * channelCount, .RightHandSides = w->SeedRhs, .InitialGuesses = w->ChannelMajor,
                                    .RhsDiagonal = w->System.RhsDiagonal, .RhsConstant = w->System.RhsConstant,
                                    .ChainStride = channelCount, .MaxIterations = f.MaxSolverIterations, .Tolerance = f.SolverTolerance}))
                                return true;
                            queue();
                            return false;
                        }
                        if (!w->Gpu || w->Gpu->State == SpatialQueryState::Failed) return true;
                        if (w->Gpu->State != SpatialQueryState::Ready) return false;
                        if (w->FinalQueued) return true;
                        if (!w->Solver->Finished())
                        {
                            w->Solver->Observe(w->Gpu->Data);
                            if (!w->Solver->Finished()) { queue(); return false; }
                        }
                        queueFinal();
                        return false;
                    }
                    if (!w->Gpu)
                    {
                        auto workspace = std::make_shared<Graphics::PropertyFilterWorkspace>(*context.Device);
                        w->Gpu = context.SpatialIndices->QueueGpuCompute(w->Values.size() * sizeof(double),
                            [workspace, w](RHI::ICommandContext& commands, const SpatialGpuIndexView&) -> RHI::BufferHandle {
                                if (w->Abandoned) return {};
                                return workspace->Record(commands, {.Values = w->Values, .Channels = std::uint32_t(w->Plan.Channels),
                                    .Edges = w->Edges, .Weights = w->Weights, .Degree = w->Plan.Degree, .Fixed = w->Fixed}, w->Params);
                            });
                    }
                    return w->Gpu->State == SpatialQueryState::Ready || w->Gpu->State == SpatialQueryState::Failed;
                },
                .ValidateBeforeApply = [current] { return current() ? JobApplyValidation::Current : JobApplyValidation::StaleGeneration; },
                .PublishCompletion = [context, w, sink, delivered](KernelEventBus&, const JobResultEnvelope&) {
                    auto result = w->Result;
                    S::PropertyFilterResult filtered;
                    if (!w->Gpu || w->Gpu->State != SpatialQueryState::Ready)
                        filtered.Diagnostic = w->Gpu && !w->Gpu->Diagnostic.empty() ? w->Gpu->Diagnostic
                                              : "Vulkan property smoothing did not return a result; previous output retained.";
                    else if (w->Implicit)
                    {
                        // Every chained solve must converge, as on the CPU; the last step of each
                        // channel is the result.
                        const std::size_t rows = w->Plan.Count, channelCount = w->Plan.Channels;
                        const std::size_t solves = std::size_t(w->Publication.Config.Filter.Iterations) * channelCount;
                        bool converged = w->Solver && w->Solver->Finished() &&
                            w->Gpu->Data.size() == Graphics::SparseConjugateGradientWorkspace::ReadbackBytes(std::uint32_t(rows), std::uint32_t(solves));
                        std::size_t applications = 0;
                        for (std::size_t k = 0; converged && k < solves; ++k)
                        {
                            Graphics::SparseCgReport report{};
                            std::memcpy(&report, w->Gpu->Data.data() + k * sizeof(report), sizeof(report));
                            converged = report.Status == Graphics::SparseCgStatus::Converged;
                            applications += report.Iterations + 1;
                        }
                        if (!converged) filtered.Diagnostic = "Implicit solver failed to converge on Vulkan; no property was changed.";
                        else
                        {
                            std::vector<double> gpuValues(w->Values.size());
                            const std::byte* solutions = w->Gpu->Data.data() + solves * sizeof(Graphics::SparseCgReport);
                            for (std::size_t k = 0; k < channelCount; ++k)
                            {
                                const std::size_t solve = solves - channelCount + k;
                                for (std::size_t i = 0; i < rows; ++i)
                                    std::memcpy(&gpuValues[i * channelCount + k], solutions + (solve * rows + i) * sizeof(double), sizeof(double));
                            }
                            filtered = S::CompletePropertyFilter(w->Plan, w->Values, std::move(gpuValues), "vulkan_compute");
                            filtered.OperatorApplications = applications;
                        }
                    }
                    else
                    {
                        std::vector<double> gpuValues(w->Values.size());
                        std::memcpy(gpuValues.data(), w->Gpu->Data.data(), gpuValues.size() * sizeof(double));
                        filtered = S::CompletePropertyFilter(w->Plan, w->Values, std::move(gpuValues), "vulkan_compute");
                        filtered.OperatorApplications = Graphics::PropertyFilterWorkspace::DispatchCount(w->Params);
                    }
                    result = Publish(context, w->Publication, filtered, std::move(result), "");
                    if (!w->Gpu || w->Gpu->State != SpatialQueryState::Ready || !filtered.Success)
                        result.Status = EditorCommandStatus::GeometryProcessingFailed;
                    *delivered = true;
                    if (sink) sink(result);
                    return result.Succeeded();
                },
                .FinalizeUnpublishedOnMainThread = [w, sink, delivered, pending]() mutable {
                    w->Abandoned = true;
                    if (*delivered) return;
                    *delivered = true;
                    pending.Status = EditorCommandStatus::StaleEntity;
                    pending.Message = "Vulkan property smoothing cancelled or stale; previous output retained.";
                    if (sink) sink(std::move(pending));
                }};
            if (!context.JobCommands.Submit(std::move(gpu), identity).IsValid())
            {
                w->Abandoned = true;
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Vulkan property smoothing submission rejected.";
                return result;
            }
            return pending;
        }

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
        return Publish(context, publication, filtered, std::move(result), fitSummary);
    }
}
