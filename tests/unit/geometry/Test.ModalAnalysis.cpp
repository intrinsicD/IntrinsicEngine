#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.Subdivision;
import Geometry.DEC;
import Geometry.Sparse;
import Geometry.ModalAnalysis;
#include "Test_MeshBuilders.h"

namespace MA = Geometry::ModalAnalysis;
namespace Sp = Geometry::Sparse;
namespace
{
    Geometry::HalfedgeMesh::Mesh Grid(int size, float bump)
    {
        std::mt19937 random(7);
        std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> v;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float px = float(x) + jitter(random), py = float(y) + jitter(random);
                v.push_back(mesh.AddVertex({px, py, bump * std::sin(0.7f * px) * std::cos(0.5f * py)}));
            }
        for (int y = 0; y + 1 < size; ++y)
            for (int x = 0; x + 1 < size; ++x)
            {
                const auto at = [&](int i, int j) { return v[std::size_t(j * size + i)]; };
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1));
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1));
            }
        return mesh;
    }

    Geometry::HalfedgeMesh::Mesh UnitSphere(std::size_t iterations)
    {
        auto ico = MakeIcosahedron();
        Geometry::HalfedgeMesh::Mesh refined;
        Geometry::Subdivision::SubdivisionParams params;
        params.Iterations = iterations;
        (void)Geometry::Subdivision::Subdivide(ico, refined, params);
        for (std::size_t i = 0; i < refined.VerticesSize(); ++i)
        {
            auto& p = refined.Position(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)});
            p = glm::normalize(p);
        }
        return refined;
    }

    std::vector<double> Multiply(const Sp::SparseMatrix& A, const std::vector<double>& x)
    {
        std::vector<double> y(A.Rows, 0.0);
        for (std::size_t r = 0; r < A.Rows; ++r)
            for (auto k = A.RowOffsets[r]; k < A.RowOffsets[r + 1]; ++k) y[r] += A.Values[k] * x[A.ColIndices[k]];
        return y;
    }
    double Dot(const std::vector<double>& a, const std::vector<double>& b)
    {
        double s = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
        return s;
    }
    glm::dvec3 P(const Geometry::HalfedgeMesh::Mesh& mesh, std::size_t v)
    { return glm::dvec3(mesh.Position(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(v)})); }

    // Independent discrete-shells energy E(x) relative to the mesh's rest positions (paper eqs. 14–16).
    double ShellEnergy(const Geometry::HalfedgeMesh::Mesh& mesh, const std::vector<glm::dvec3>& x, const MA::ThinShellWeights& w)
    {
        const auto area = [](const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) { return 0.5 * glm::length(glm::cross(b - a, c - a)); };
        double energy = 0.0;
        for (std::size_t e = 0; e < mesh.EdgesSize(); ++e)
        {
            const Geometry::HalfedgeHandle h0{static_cast<Geometry::PropertyIndex>(2 * e)};
            const auto h1 = mesh.OppositeHalfedge(h0);
            const auto a = mesh.FromVertex(h0).Index, b = mesh.ToVertex(h0).Index;
            const double rest = glm::length(P(mesh, b) - P(mesh, a)), now = glm::length(x[b] - x[a]);
            energy += 0.5 * w.Length / rest * (now - rest) * (now - rest);
            if (mesh.IsBoundary(h0) || mesh.IsBoundary(h1)) continue;
            const auto c = mesh.ToVertex(mesh.NextHalfedge(h0)).Index, d = mesh.ToVertex(mesh.NextHalfedge(h1)).Index;
            const double restAngle = MA::DihedralAngle(P(mesh, a), P(mesh, b), P(mesh, c), P(mesh, d));
            const double angle = MA::DihedralAngle(x[a], x[b], x[c], x[d]);
            const double hinge = area(P(mesh, a), P(mesh, b), P(mesh, c)) + area(P(mesh, b), P(mesh, a), P(mesh, d));
            energy += 0.5 * w.Flexural * 3.0 * rest * rest / hinge * (angle - restAngle) * (angle - restAngle);
        }
        for (std::size_t f = 0; f < mesh.FacesSize(); ++f)
        {
            std::vector<std::size_t> v;
            for (const auto vh : mesh.VerticesAroundFace(Geometry::FaceHandle{static_cast<Geometry::PropertyIndex>(f)})) v.push_back(vh.Index);
            const double rest = area(P(mesh, v[0]), P(mesh, v[1]), P(mesh, v[2])), now = area(x[v[0]], x[v[1]], x[v[2]]);
            energy += 0.5 * w.Area / rest * (now - rest) * (now - rest);
        }
        return energy;
    }
}

TEST(ModalAnalysis, ModifiedDirichletEqualsTheCotanLaplacianOnPlanarMeshes)
{
    const auto mesh = Grid(6, 0.0f);
    const auto result = MA::BuildModifiedDirichletMatrix(mesh);
    ASSERT_TRUE(result.Succeeded());
    const auto S = Geometry::DEC::BuildLaplacian(mesh);
    ASSERT_EQ(result.Matrix.Values.size(), S.Values.size());
    for (std::size_t k = 0; k < S.Values.size(); ++k) EXPECT_NEAR(result.Matrix.Values[k], S.Values[k], 1e-12);
    EXPECT_EQ(result.FallbackNormalCount, 0u);
}

TEST(ModalAnalysis, ModifiedDirichletIsTheDirichletEnergyOfTheNormalVariation)
{
    const auto mesh = Grid(7, 0.8f);
    const auto A = MA::BuildModifiedDirichletMatrix(mesh);
    ASSERT_TRUE(A.Succeeded());
    const auto S = Geometry::DEC::BuildLaplacian(mesh);
    // Independent area-weighted normals.
    std::vector<glm::dvec3> normals(mesh.VerticesSize(), glm::dvec3(0.0));
    for (std::size_t f = 0; f < mesh.FacesSize(); ++f)
    {
        std::vector<std::size_t> v;
        for (const auto vh : mesh.VerticesAroundFace(Geometry::FaceHandle{static_cast<Geometry::PropertyIndex>(f)})) v.push_back(vh.Index);
        const auto n = glm::cross(P(mesh, v[1]) - P(mesh, v[0]), P(mesh, v[2]) - P(mesh, v[0]));
        for (auto i : v) normals[i] += n;
    }
    for (auto& n : normals) n = glm::normalize(n);
    std::mt19937 random(3);
    std::normal_distribution<double> gauss;
    std::vector<double> u(mesh.VerticesSize());
    for (auto& x : u) x = gauss(random);
    // E_D^N(u) = Σ_k E_D(V_u^k) with V_u = u N (paper eq. 12).
    double expected = 0.0;
    for (int k = 0; k < 3; ++k)
    {
        std::vector<double> component(u.size());
        for (std::size_t i = 0; i < u.size(); ++i) component[i] = u[i] * normals[i][k];
        expected += Dot(component, Multiply(S, component));
    }
    EXPECT_NEAR(Dot(u, Multiply(A.Matrix, u)), expected, 1e-10 * std::abs(expected));
    // Symmetric, and constants are no longer in the kernel on a curved surface.
    EXPECT_TRUE(Sp::AnalyzeSparseMatrix(A.Matrix).IsSymmetric);
    const std::vector<double> ones(u.size(), 1.0);
    EXPECT_GT(Dot(ones, Multiply(A.Matrix, ones)), 1e-3);
}

TEST(ModalAnalysis, ModifiedDirichletOfTheConstantConvergesToTheTotalCurvatureOnTheSphere)
{
    // Paper eq. 11: E_D^N(1) = ½∫(κ1² + κ2²) = ½ · 2 · 4π = 4π on the unit sphere.
    double previousError = 1e9;
    for (std::size_t iterations : {2u, 3u, 4u})
    {
        const auto mesh = UnitSphere(iterations);
        const auto A = MA::BuildModifiedDirichletMatrix(mesh);
        ASSERT_TRUE(A.Succeeded());
        const std::vector<double> ones(mesh.VerticesSize(), 1.0);
        const double energy = 0.5 * Dot(ones, Multiply(A.Matrix, ones));
        const double error = std::abs(energy - 4.0 * std::numbers::pi);
        EXPECT_LT(error, previousError) << iterations;
        previousError = error;
    }
    EXPECT_LT(previousError, 0.02 * 4.0 * std::numbers::pi);
}

TEST(ModalAnalysis, ElementaryGradientsMatchFiniteDifferences)
{
    std::mt19937 random(11);
    std::uniform_real_distribution<double> coordinate(-1.0, 1.0);
    const auto point = [&] { return glm::dvec3(coordinate(random), coordinate(random), coordinate(random)); };
    for (int trial = 0; trial < 20; ++trial)
    {
        std::array<glm::dvec3, 4> x{glm::dvec3(0, 0, 0), glm::dvec3(1, 0, 0), glm::dvec3(0.4, 1, 0), glm::dvec3(0.6, -1, 0)};
        for (auto& p : x) p += 0.3 * point();
        const auto theta = MA::DihedralAngleGradient(x[0], x[1], x[2], x[3]);
        const auto area = MA::TriangleAreaGradient(x[0], x[1], x[2]);
        ASSERT_TRUE(theta && area);
        const double h = 1e-6;
        for (std::size_t v = 0; v < 4; ++v)
            for (int c = 0; c < 3; ++c)
            {
                auto plus = x, minus = x;
                plus[v][c] += h;
                minus[v][c] -= h;
                const double dTheta = (MA::DihedralAngle(plus[0], plus[1], plus[2], plus[3]) -
                                       MA::DihedralAngle(minus[0], minus[1], minus[2], minus[3])) / (2 * h);
                EXPECT_NEAR((*theta)[v][c], dTheta, 1e-6) << trial << ' ' << v << ' ' << c;
                if (v == 3) continue;
                const auto a = [](const auto& p) { return 0.5 * glm::length(glm::cross(p[1] - p[0], p[2] - p[0])); };
                EXPECT_NEAR((*area)[v][c], (a(plus) - a(minus)) / (2 * h), 1e-7);
            }
    }
    EXPECT_FALSE(MA::DihedralAngleGradient(glm::dvec3(0), glm::dvec3(1, 0, 0), glm::dvec3(2, 0, 0), glm::dvec3(0, 1, 0)));
}

TEST(ModalAnalysis, ThinShellHessianMatchesTheSecondDerivativeOfTheEnergyAtRest)
{
    const auto mesh = Grid(5, 0.6f);
    const MA::ThinShellWeights weights{.Flexural = 2.0, .Length = 0.7, .Area = 1.3};
    const auto H = MA::BuildThinShellHessian(mesh, weights);
    ASSERT_TRUE(H.Succeeded());
    const std::size_t n = mesh.VerticesSize();
    EXPECT_EQ(H.Hessian.Rows, 3 * n);
    EXPECT_EQ(H.HingeCount, mesh.EdgeCount() - 4 * 4);
    EXPECT_EQ(H.EdgeCount, mesh.EdgeCount());
    EXPECT_EQ(H.TriangleCount, mesh.FaceCount());
    EXPECT_TRUE(Sp::AnalyzeSparseMatrix(H.Hessian).IsSymmetric);
    std::vector<glm::dvec3> rest(n);
    for (std::size_t i = 0; i < n; ++i) rest[i] = P(mesh, i);
    std::mt19937 random(5);
    std::normal_distribution<double> gauss;
    for (int trial = 0; trial < 5; ++trial)
    {
        std::vector<double> direction(3 * n);
        for (auto& x : direction) x = gauss(random);
        // E(x̄) = 0 and ∂E(x̄) = 0, so vᵀHv = lim (E(x̄+εv) + E(x̄−εv)) / ε².
        const double eps = 1e-4;
        auto plus = rest, minus = rest;
        for (std::size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c)
            {
                plus[i][c] += eps * direction[3 * i + std::size_t(c)];
                minus[i][c] -= eps * direction[3 * i + std::size_t(c)];
            }
        const double finite = (ShellEnergy(mesh, plus, weights) + ShellEnergy(mesh, minus, weights)) / (eps * eps);
        const double exact = Dot(direction, Multiply(H.Hessian, direction));
        EXPECT_NEAR(exact, finite, 1e-5 * exact) << trial;
    }
}

TEST(ModalAnalysis, ThinShellVibrationModesStartWithTheSixRigidMotions)
{
    const auto mesh = UnitSphere(1);
    const auto H = MA::BuildThinShellHessian(mesh);
    ASSERT_TRUE(H.Succeeded());
    const std::size_t n = mesh.VerticesSize();
    // Translations and infinitesimal rotations are in the nullspace.
    for (int axis = 0; axis < 3; ++axis)
    {
        std::vector<double> translation(3 * n, 0.0), rotation(3 * n, 0.0);
        glm::dvec3 w(0.0);
        w[axis] = 1.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            translation[3 * i + std::size_t(axis)] = 1.0;
            const auto r = glm::cross(w, P(mesh, i));
            for (int c = 0; c < 3; ++c) rotation[3 * i + std::size_t(c)] = r[c];
        }
        for (const double x : Multiply(H.Hessian, translation)) EXPECT_NEAR(x, 0.0, 1e-10);
        for (const double x : Multiply(H.Hessian, rotation)) EXPECT_NEAR(x, 0.0, 1e-6);
    }
    const auto mass = MA::ExpandMass(Geometry::DEC::BuildHodgeStar0(mesh, Geometry::DEC::MassMode::Barycentric), 3);
    ASSERT_EQ(mass.Size, 3 * n);
    const auto modes = Sp::SolveSymmetricGeneralizedEigen(H.Hessian, mass, {.Count = 10});
    ASSERT_TRUE(modes.Succeeded()) << modes.Diagnostic;
    for (std::size_t j = 0; j < 6; ++j) EXPECT_NEAR(modes.Eigenvalues[j], 0.0, 1e-8) << j;
    EXPECT_GT(modes.Eigenvalues[6], 1e-2);
}

TEST(ModalAnalysis, SignaturesAreConsistentWithTheModeNormalization)
{
    const auto mesh = UnitSphere(2);
    const auto A = MA::BuildModifiedDirichletMatrix(mesh);
    const auto M = Geometry::DEC::BuildHodgeStar0(mesh, Geometry::DEC::MassMode::Barycentric);
    const auto modes = Sp::SolveSymmetricGeneralizedEigen(A.Matrix, M, {.Count = 12});
    ASSERT_TRUE(modes.Succeeded()) << modes.Diagnostic;
    const std::size_t n = mesh.VerticesSize();
    const MA::ModalSpectrum spectrum{modes.Eigenvalues, modes.Eigenvectors, n, 1, 0};
    const auto range = MA::DefaultScaleRange(spectrum);
    ASSERT_TRUE(range.Valid());
    EXPECT_NEAR(range.Max, 4.0 * std::numbers::ln10 / modes.Eigenvalues.front(), 1e-12);
    EXPECT_NEAR(MA::ScaleAt(range, 0.0), range.Min, 1e-12 * range.Min);
    EXPECT_NEAR(MA::ScaleAt(range, 1.0), range.Max, 1e-12 * range.Max);
    // Σ_v m_v S_t(v) = Σ_j e^{−λ_j t} because every mode is M-normalized.
    const double t = MA::ScaleAt(range, 0.5);
    const auto signature = MA::ComputeModalSignature(spectrum, t);
    ASSERT_TRUE(signature.Succeeded());
    double weighted = 0.0, expected = 0.0;
    for (std::size_t v = 0; v < n; ++v) weighted += M.Diagonal[v] * signature.Values[v];
    for (const double l : modes.Eigenvalues) expected += std::exp(-l * t);
    EXPECT_NEAR(weighted, expected, 1e-9 * expected);
    // Distances vanish at the source, are nonnegative and satisfy the triangle inequality.
    const auto d0 = MA::ComputeModalDistance(spectrum, 0, range, 24);
    const auto d5 = MA::ComputeModalDistance(spectrum, 5, range, 24);
    ASSERT_TRUE(d0.Succeeded() && d5.Succeeded());
    EXPECT_DOUBLE_EQ(d0.Values[0], 0.0);
    EXPECT_NEAR(d0.Values[5], d5.Values[0], 1e-12);
    for (std::size_t v = 0; v < n; ++v)
    {
        EXPECT_GE(d0.Values[v], 0.0);
        EXPECT_LE(d0.Values[v], d0.Values[5] + d5.Values[v] + 1e-12);
    }
    // Skipping modes and invalid inputs.
    EXPECT_EQ(MA::ComputeModalSignature({modes.Eigenvalues, modes.Eigenvectors, n, 1, 3}, t).UsedModes, 9u);
    EXPECT_FALSE(MA::ComputeModalSignature({modes.Eigenvalues, modes.Eigenvectors, n, 1, 12}, t).Succeeded());
    EXPECT_FALSE(MA::ComputeModalSignature({modes.Eigenvalues, modes.Eigenvectors, n + 1, 1, 0}, t).Succeeded());
    EXPECT_FALSE(MA::ComputeModalSignature(spectrum, 0.0).Succeeded());
    EXPECT_FALSE(MA::ComputeModalDistance(spectrum, n, range).Succeeded());
    EXPECT_FALSE(MA::ComputeModalDistance(spectrum, 0, {range.Min, range.Min}).Succeeded());
}

TEST(ModalAnalysis, RejectsInvalidMeshesAndWeights)
{
    Geometry::HalfedgeMesh::Mesh empty;
    EXPECT_EQ(MA::BuildModifiedDirichletMatrix(empty).Status, MA::ModalStatus::EmptyMesh);
    EXPECT_EQ(MA::BuildThinShellHessian(empty).Status, MA::ModalStatus::EmptyMesh);
    const auto quads = MakeQuadPair();
    EXPECT_EQ(MA::BuildThinShellHessian(quads).Status, MA::ModalStatus::NonTriangularFace);
    auto mesh = Grid(3, 0.2f);
    EXPECT_EQ(MA::BuildThinShellHessian(mesh, {.Flexural = 0, .Length = 0, .Area = 0}).Status, MA::ModalStatus::InvalidParameters);
    EXPECT_EQ(MA::BuildThinShellHessian(mesh, {.Flexural = -1}).Status, MA::ModalStatus::InvalidParameters);
    mesh.Position(Geometry::VertexHandle{0}).x = std::nanf("");
    EXPECT_EQ(MA::BuildModifiedDirichletMatrix(mesh).Status, MA::ModalStatus::NonFinitePositions);
}
