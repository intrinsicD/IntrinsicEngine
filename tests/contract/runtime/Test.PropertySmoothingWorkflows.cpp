// Editor-session workflows of Smooth Property on a real mean-curvature field: one draft is edited
// step by step the way the panel does (edit, ReconcilePropertySmoothingConfig, preview, apply)
// through every method, fit variant, solver switch, vector input and face input.
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.Curvature;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace S = Geometry::Smoothing;
using D = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct Session
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity{};
        R::EditorProcessingContext Context;
        R::PropertySmoothingConfig Draft; // the panel's draft, carried through the whole session
        std::vector<std::pair<std::size_t, std::size_t>> Edges;
        std::size_t FaceCount{};

        Session()
        {
            // Noisy height field: mean curvature is rough, so every method has something to smooth.
            constexpr int n = 12;
            std::mt19937 random(17);
            std::normal_distribution<float> noise(0.0f, 0.04f);
            Geometry::HalfedgeMesh::Mesh mesh;
            std::vector<Geometry::VertexHandle> v;
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    v.push_back(mesh.AddVertex({float(x) * 0.25f, float(y) * 0.25f,
                        0.4f * std::sin(0.8f * float(x)) * std::cos(0.6f * float(y)) + noise(random)}));
            for (int y = 0; y + 1 < n; ++y)
                for (int x = 0; x + 1 < n; ++x)
                {
                    const auto at = [&](int i, int j) { return v[std::size_t(j * n + i)]; };
                    (void)mesh.AddTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1));
                    (void)mesh.AddTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1));
                }
            EXPECT_TRUE(Geometry::Curvature::ComputeMeanCurvature(mesh).has_value());
            for (std::size_t e = 0; e < mesh.EdgesSize(); ++e)
            {
                const Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(2 * e)};
                Edges.emplace_back(mesh.FromVertex(h).Index, mesh.ToVertex(h).Index);
            }
            FaceCount = mesh.FacesSize();
            Entity = Scene.Create();
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
            // A vec3 field (noisy normals-like directions) and a face scalar from the curvature.
            auto& vertices = Vertices();
            const auto h = std::as_const(vertices).Get<double>("v:mean_curvature").Vector();
            auto directions = vertices.GetOrAdd<glm::vec3>("v:direction", glm::vec3(0));
            for (std::size_t i = 0; i < vertices.Size(); ++i)
                directions[i] = glm::vec3(float(h[i]), noise(random), 1.0f + noise(random));
            auto tolerance = vertices.GetOrAdd<double>("tolerance", 0.0);
            for (std::size_t i = 0; i < vertices.Size(); ++i) tolerance[i] = 0.05;
            auto& faces = Scene.Raw().get<GS::Faces>(Entity).Properties;
            auto faceValues = faces.GetOrAdd<double>("f:roughness", 0.0);
            for (std::size_t f = 0; f < faces.Size(); ++f) faceValues[f] = h[f % h.size()] + (f % 2 ? 0.3 : -0.3);
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
        }
        Geometry::PropertySet& Vertices() { return Scene.Raw().get<GS::Vertices>(Entity).Properties; }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }

        // One panel edit: mutate, reconcile against the previous draft, then the button must be
        // enabled and the run must publish a finite field.
        R::EditorPropertySmoothingResult Edit(const std::string& step, const std::function<void(R::PropertySmoothingConfig&)>& edit)
        {
            SCOPED_TRACE(step);
            const auto before = Draft;
            edit(Draft);
            R::ReconcilePropertySmoothingConfig(Draft, before);
            const auto readiness = R::PreviewEditorPropertySmoothingCommand(Commands(), Id(), Draft);
            EXPECT_TRUE(readiness.Enabled) << step << ": " << readiness.DisabledReason;
            auto result = R::ApplyEditorPropertySmoothingCommand(Commands(), Id(), Draft);
            EXPECT_TRUE(result.Succeeded()) << step << ": " << result.Message;
            return result;
        }
        std::vector<double> Scalars(const R::GeometryPropertyRef& ref)
        {
            auto& props = ref.Domain == D::MeshFace ? Scene.Raw().get<GS::Faces>(Entity).Properties : Vertices();
            if (ref.ValueKind == K::Double) return std::as_const(props).Get<double>(ref.Name).Vector();
            std::vector<double> out;
            for (const float x : std::as_const(props).Get<float>(ref.Name).Vector()) out.push_back(x);
            return out;
        }
        double Roughness(const std::vector<double>& u) const
        {
            double sum = 0;
            for (const auto& [a, b] : Edges) sum += (u[a] - u[b]) * (u[a] - u[b]);
            return sum;
        }
        // The output is finite and rougher neither than the input nor (for strict smoothers) equal to it.
        void ExpectSmoother(const std::string& step, bool strict = true)
        {
            SCOPED_TRACE(step);
            const auto input = Scalars(Draft.Input), output = Scalars(Draft.Output);
            ASSERT_EQ(input.size(), output.size());
            for (const double x : output) ASSERT_TRUE(std::isfinite(x)) << step;
            const double before = Roughness(input), after = Roughness(output);
            if (strict) EXPECT_LT(after, 0.9 * before) << step;
            else EXPECT_LE(after, before * (1 + 1e-9)) << step;
        }
    };
}

TEST(PropertySmoothingWorkflows, EveryMethodOnMeanCurvatureInOneSession)
{
    Session s;
    ASSERT_TRUE(s.Vertices().Exists("v:mean_curvature"));
    // The session starts from the panel default and picks the curvature field.
    s.Edit("choose mean curvature", [](auto& c) { c.Input = {D::MeshVertex, "v:mean_curvature", K::Double}; });
    EXPECT_EQ(s.Draft.Output.Name, "smoothed") << "the default input was already selected";
    s.ExpectSmoother("averaging kNN");
    s.Edit("cotangent weights", [](auto& c) { c.Weight = S::PropertyWeight::Cotangent; });
    s.ExpectSmoother("averaging cotangent");
    s.Edit("spectral heat", [](auto& c) { c.Filter.Method = S::PropertyFilter::SpectralHeat; });
    s.ExpectSmoother("spectral heat");
    s.Edit("taubin", [](auto& c) { c.Filter.Method = S::PropertyFilter::Taubin; c.Filter.Iterations = 20; });
    s.ExpectSmoother("taubin");
    s.Edit("bilateral", [](auto& c) { c.Filter.Method = S::PropertyFilter::Bilateral; c.Filter.RangeSigma = 2.0; });
    s.ExpectSmoother("bilateral");
    s.Edit("implicit direct", [](auto& c) { c.Filter.Method = S::PropertyFilter::Implicit; c.Filter.Solver = S::PropertySolver::Direct; });
    s.ExpectSmoother("implicit direct");
    s.Edit("implicit cg", [](auto& c) { c.Filter.Solver = S::PropertySolver::ConjugateGradient; });
    s.ExpectSmoother("implicit cg");
    s.Edit("implicit lumped mass", [](auto& c) { c.Filter.Laplacian = S::PropertyLaplacian::LumpedMass; c.Filter.TimeStep = 0.05; });
    s.ExpectSmoother("implicit lumped");
    s.Edit("pin boundary", [](auto& c) { c.PreserveBoundary = true; });
    s.ExpectSmoother("implicit pinned");
    // Lumped mass is implicit/fit only: going back to averaging must not strand the draft.
    s.Edit("averaging after lumped implicit", [](auto& c) { c.Filter.Method = S::PropertyFilter::Averaging; });
    EXPECT_EQ(s.Draft.Filter.Laplacian, S::PropertyLaplacian::RandomWalk);
    s.ExpectSmoother("averaging again");
}

TEST(PropertySmoothingWorkflows, VariationalFitVariantsAndSolverSwitches)
{
    Session s;
    s.Edit("choose mean curvature", [](auto& c) {
        c.Input = {D::MeshVertex, "v:mean_curvature", K::Double};
        c.Weight = S::PropertyWeight::Cotangent;
    });
    const auto fit = [&](const std::string& step, const std::function<void(R::PropertySmoothingConfig&)>& edit, bool strict = true) {
        s.Edit(step, edit);
        s.ExpectSmoother(step, strict);
    };
    fit("quadratic fit", [](auto& c) { c.Filter.Method = S::PropertyFilter::VariationalFit; });
    fit("lumped mass fit", [](auto& c) { c.Filter.Laplacian = S::PropertyLaplacian::LumpedMass; });
    // Huber tails grow like 2 delta r: with delta 0.01 against curvature jumps of several units
    // the smoothing is deliberately weak (edge preserving).
    fit("huber smoothness", [](auto& c) { c.Filter.SmoothnessPenalty = S::FitPenalty::Huber; }, false);
    fit("total variation (reweighted)", [](auto& c) { c.Filter.SmoothnessPenalty = S::FitPenalty::L1; });
    fit("robust L1 data", [](auto& c) { c.Filter.DataPenalty = S::FitPenalty::L1; });
    fit("total variation ADMM delta 0", [](auto& c) {
        c.Filter.DataPenalty = S::FitPenalty::Quadratic;
        c.Filter.FitAlgorithm = S::FitSolver::Admm;
        c.Filter.PenaltyDelta = 0.0;
    });
    // The reported trap: after delta-0 TV the reweighted solver must stay selectable.
    fit("reweighted after delta 0", [](auto& c) { c.Filter.FitAlgorithm = S::FitSolver::Reweighted; });
    EXPECT_GT(s.Draft.Filter.PenaltyDelta, 0.0);
    fit("huber with delta 0 on ADMM", [](auto& c) {
        c.Filter.FitAlgorithm = S::FitSolver::Admm;
        c.Filter.PenaltyDelta = 0.0;
        c.Filter.SmoothnessPenalty = S::FitPenalty::Huber;
    }, false);
    fit("second-order TGV", [](auto& c) { c.Filter.SmoothnessPenalty = S::FitPenalty::L1; c.Filter.SmoothnessOrder = S::FitOrder::Second; });
    fit("reweighted after TGV", [](auto& c) { c.Filter.FitAlgorithm = S::FitSolver::Reweighted; });
    EXPECT_EQ(s.Draft.Filter.SmoothnessOrder, S::FitOrder::First);
    fit("noise-level fidelity", [](auto& c) { c.Filter.Fidelity = S::FitFidelity::NoiseLevel; c.Filter.NoiseLevel = 1.0; });
    fit("uniform bound", [](auto& c) { c.Filter.Fidelity = S::FitFidelity::FixedWeight; c.Filter.Bound = S::FitBound::Uniform; c.Filter.BoundRadius = 0.05; }, false);
    fit("per-row bound", [](auto& c) { c.Filter.Bound = S::FitBound::PerRow; c.BoundRadii = {D::MeshVertex, "tolerance", K::Double}; }, false);
    // Leaving the fit with every fit-only option still set: every other method runs.
    for (const auto method : {S::PropertyFilter::Averaging, S::PropertyFilter::SpectralHeat, S::PropertyFilter::Taubin,
                              S::PropertyFilter::Bilateral, S::PropertyFilter::Implicit})
        s.Edit("method " + std::to_string(int(method)) + " after fit", [&](auto& c) {
            c.Filter.FitAlgorithm = S::FitSolver::Admm;
            c.Filter.PenaltyDelta = 0.0;
            c.Filter.SmoothnessOrder = S::FitOrder::Second;
            c.Filter.Method = method;
        });
}

TEST(PropertySmoothingWorkflows, SwitchingToVectorAndFacePropertiesKeepsTheButtonUsable)
{
    Session s;
    s.Edit("mean curvature, mesh-only options", [](auto& c) {
        c.Input = {D::MeshVertex, "v:mean_curvature", K::Double};
        c.Output = {D::MeshVertex, "smoothed", K::Double};
        c.Weight = S::PropertyWeight::Cotangent;
        c.PreserveBoundary = true;
        c.Filter.Method = S::PropertyFilter::Implicit;
        c.Filter.Laplacian = S::PropertyLaplacian::LumpedMass;
    });
    ASSERT_TRUE(std::as_const(s.Vertices()).Get<double>("smoothed"));
    // Vector input: the double output "smoothed" exists, so the output must follow the input.
    s.Edit("vec3 input", [](auto& c) { c.Input = {D::MeshVertex, "v:direction", K::Vec3}; });
    EXPECT_EQ(s.Draft.Output.ValueKind, K::Vec3);
    EXPECT_TRUE(std::as_const(s.Vertices()).Get<glm::vec3>(s.Draft.Output.Name));
    s.Edit("vec3 averaging", [](auto& c) { c.Filter.Method = S::PropertyFilter::Averaging; });
    s.Edit("vec3 fit with euclidean bound", [](auto& c) {
        c.Filter.Method = S::PropertyFilter::VariationalFit;
        c.Filter.Bound = S::FitBound::Uniform;
        c.Filter.BoundRadius = 0.1;
        c.Filter.BoundNorm = S::FitBoundNorm::Euclidean;
    });
    EXPECT_EQ(s.Draft.Filter.FitAlgorithm, S::FitSolver::Admm);
    // Face input with mesh-only weights, pinning and lumped mass still selected.
    s.Edit("lumped fit on vertices", [](auto& c) { c.Filter.Laplacian = S::PropertyLaplacian::LumpedMass; c.PreserveBoundary = true; });
    s.Edit("face input", [](auto& c) { c.Input = {D::MeshFace, "f:roughness", K::Double}; });
    EXPECT_EQ(s.Draft.Weight, S::PropertyWeight::Uniform);
    EXPECT_FALSE(s.Draft.PreserveBoundary);
    EXPECT_EQ(s.Draft.Positions.Domain, D::MeshVertex) << "face centers derive from vertex positions";
    for (const auto method : {S::PropertyFilter::Averaging, S::PropertyFilter::SpectralHeat, S::PropertyFilter::Taubin,
                              S::PropertyFilter::Bilateral, S::PropertyFilter::Implicit, S::PropertyFilter::VariationalFit})
        s.Edit("face method " + std::to_string(int(method)), [&](auto& c) { c.Filter.Method = method; });
    // Explicitly choosing a mesh-only option on faces is reported precisely.
    auto draft = s.Draft;
    draft.Weight = S::PropertyWeight::Cotangent;
    const auto readiness = R::PreviewEditorPropertySmoothingCommand(s.Commands(), s.Id(), draft);
    EXPECT_FALSE(readiness.Enabled);
    EXPECT_NE(readiness.DisabledReason.find("Mesh edge weighting"), std::string::npos) << readiness.DisabledReason;
    // Back to vertex curvature: runs with the kNN weights left from the face step.
    s.Edit("back to curvature", [](auto& c) { c.Input = {D::MeshVertex, "v:mean_curvature", K::Double}; });
    s.ExpectSmoother("curvature after faces", false); // the 0.1 Euclidean bound is still selected
    // Overwrite stays an overwrite when the input changes.
    s.Edit("overwrite", [](auto& c) { c.Output = c.Input; });
    s.Edit("overwrite another input", [](auto& c) { c.Input = {D::MeshVertex, "tolerance", K::Double}; });
    EXPECT_EQ(s.Draft.Output, s.Draft.Input);
}
