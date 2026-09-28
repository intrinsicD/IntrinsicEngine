module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <glm/glm.hpp>

module Geometry.Registration.CoherentPointDrift;

namespace Geometry::CoherentPointDrift
{
    namespace
    {
        constexpr double kDimension = 3.0;

        struct Points
        {
            std::vector<double> X, Y, Z;
            void Resize(std::size_t n) { X.assign(n, 0.0); Y.assign(n, 0.0); Z.assign(n, 0.0); }
            [[nodiscard]] std::size_t Size() const noexcept { return X.size(); }
            [[nodiscard]] Eigen::Vector3d At(std::size_t i) const { return {X[i], Y[i], Z[i]}; }
            void Set(std::size_t i, const Eigen::Vector3d& p) { X[i] = p.x(); Y[i] = p.y(); Z[i] = p.z(); }
        };

        bool Finite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        glm::dmat3 ToGlm(const Eigen::Matrix3d& m)
        {
            glm::dmat3 out{};
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r) out[c][r] = m(r, c);
            return out;
        }

        glm::dmat4 AffineMatrix(const Eigen::Matrix3d& linear, const Eigen::Vector3d& translation)
        {
            glm::dmat4 out{1.0};
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r) out[c][r] = linear(r, c);
            out[3] = glm::dvec4(translation.x(), translation.y(), translation.z(), 1.0);
            return out;
        }
    }

    std::string_view ToString(const Variant value) noexcept
    {
        switch (value)
        {
        case Variant::Rigid: return "rigid";
        case Variant::Affine: return "affine";
        case Variant::Nonrigid: return "nonrigid";
        }
        return "unknown";
    }

    std::string_view ToString(const Status value) noexcept
    {
        switch (value)
        {
        case Status::Success: return "success";
        case Status::EmptyInput: return "empty_input";
        case Status::NonFiniteInput: return "non_finite_input";
        case Status::InvalidParameters: return "invalid_parameters";
        case Status::TooLarge: return "too_large";
        case Status::SingularSystem: return "singular_system";
        case Status::NumericalFailure: return "numerical_failure";
        }
        return "unknown";
    }

    std::string_view ToString(const Termination value) noexcept
    {
        switch (value)
        {
        case Termination::None: return "none";
        case Termination::Converged: return "converged";
        case Termination::SigmaFloor: return "sigma_floor";
        case Termination::IterationCap: return "iteration_cap";
        }
        return "unknown";
    }

    struct Solver::State
    {
        Params Config{};
        Status Failure{Status::InvalidParameters};
        Termination Stop{Termination::None};
        bool Ended{true};

        // Normalized problem: x_w = Scale * x + MeanX, y_w = Scale * y + MeanY.
        Eigen::Vector3d MeanX{Eigen::Vector3d::Zero()}, MeanY{Eigen::Vector3d::Zero()};
        double Scale{1.0};
        Points Target{}, Source{}, Moved{};

        double Sigma2{0.0};
        std::uint32_t Iterations{0u};
        double LastNll{0.0}, LastObjective{0.0}, LastMatched{0.0};
        bool HasPreviousObjective{false};
        std::vector<double> ObjectiveHistory{}, Sigma2History{};

        // Rigid/affine estimate in normalized coordinates: T(y) = Linear * y + Translation.
        Eigen::Matrix3d Rotation{Eigen::Matrix3d::Identity()};
        double RigidScale{1.0};
        Eigen::Matrix3d Linear{Eigen::Matrix3d::Identity()};
        Eigen::Vector3d Translation{Eigen::Vector3d::Zero()};
        // Nonrigid: T(Y) = Y + G W.
        Eigen::MatrixXd Kernel{}, Coefficients{};

        // E-step sufficient statistics: P1 = P 1, Pt1 = P^T 1, PX = P X.
        std::vector<double> P1{}, Pt1{}, Scratch{};
        Points PX{};

        // Responsibilities P(m | x_n) of the Gaussian-mixture-plus-uniform model, streamed row
        // by row (target point by target point) with a log-sum-exp shift so small sigma^2 cannot
        // underflow every term. Returns false when every target point went to the outlier term.
        bool ExpectationStep(double& negativeLogLikelihood, double& matched)
        {
            const std::size_t n = Target.Size(), m = Moved.Size();
            const double w = Config.OutlierWeight;
            const double twoSigma2 = 2.0 * Sigma2;
            // c = (2 pi sigma^2)^{D/2} * w/(1-w) * M/N (paper eq. 7); log for overflow safety.
            const double logC = w > 0.0
                ? 0.5 * kDimension * std::log(std::numbers::pi * twoSigma2) + std::log(w / (1.0 - w)) +
                      std::log(double(m) / double(n))
                : -std::numeric_limits<double>::infinity();
            std::fill(P1.begin(), P1.end(), 0.0);
            std::fill(PX.X.begin(), PX.X.end(), 0.0);
            std::fill(PX.Y.begin(), PX.Y.end(), 0.0);
            std::fill(PX.Z.begin(), PX.Z.end(), 0.0);
            double logSum = 0.0, total = 0.0;
            const double* tx = Moved.X.data(); const double* ty = Moved.Y.data(); const double* tz = Moved.Z.data();
            double* e = Scratch.data();
            for (std::size_t j = 0; j < n; ++j)
            {
                const double x = Target.X[j], y = Target.Y[j], z = Target.Z[j];
                double minDistance = std::numeric_limits<double>::infinity();
                for (std::size_t i = 0; i < m; ++i)
                {
                    const double dx = x - tx[i], dy = y - ty[i], dz = z - tz[i];
                    e[i] = dx * dx + dy * dy + dz * dz;
                    minDistance = std::min(minDistance, e[i]);
                }
                // Shifted by the largest exponent a_max = -minDistance / (2 sigma^2).
                const double aMax = -minDistance / twoSigma2;
                double sum = 0.0;
                for (std::size_t i = 0; i < m; ++i)
                {
                    e[i] = std::exp((minDistance - e[i]) / twoSigma2);
                    sum += e[i];
                }
                // log(sum_m exp(a_m) + c) = a_max + log(sum + c * exp(-a_max)).
                const double logOutlier = logC - aMax;
                const double logDenominator = logOutlier > 700.0
                    ? logC + std::log1p(sum * std::exp(-logOutlier))
                    : aMax + std::log(sum + std::exp(logOutlier));
                logSum += logDenominator;
                const double inverse = std::exp(aMax - logDenominator); // 1 / (sum + c e^{-a_max}) scaled
                double row = 0.0;
                for (std::size_t i = 0; i < m; ++i)
                {
                    const double p = e[i] * inverse;
                    P1[i] += p;
                    PX.X[i] += p * x;
                    PX.Y[i] += p * y;
                    PX.Z[i] += p * z;
                    row += p;
                }
                Pt1[j] = row;
                total += row;
            }
            // p(x) = (1-w)/M (2 pi sigma^2)^{-D/2} (sum_m exp(a_m) + c); NLL = -sum_n log p(x_n).
            negativeLogLikelihood = -(logSum + double(n) * (std::log((1.0 - w) / double(m)) -
                                                            0.5 * kDimension * std::log(std::numbers::pi * twoSigma2)));
            matched = total;
            return std::isfinite(negativeLogLikelihood) && total > std::numeric_limits<double>::min() * double(n);
        }

        // Weighted centroids and the cross-covariance A = sum_mn P_mn (x_n - mu_x)(y_m - mu_y)^T.
        void Moments(double matched, Eigen::Vector3d& muX, Eigen::Vector3d& muY, Eigen::Matrix3d& cross,
                     Eigen::Matrix3d& sourceSpread, double& targetSpread) const
        {
            muX.setZero(); muY.setZero();
            for (std::size_t j = 0; j < Target.Size(); ++j) muX += Pt1[j] * Target.At(j);
            for (std::size_t i = 0; i < Source.Size(); ++i) muY += P1[i] * Source.At(i);
            muX /= matched; muY /= matched;
            cross.setZero(); sourceSpread.setZero(); targetSpread = 0.0;
            for (std::size_t i = 0; i < Source.Size(); ++i)
            {
                const Eigen::Vector3d centered = Source.At(i) - muY;
                const Eigen::Vector3d px{PX.X[i], PX.Y[i], PX.Z[i]};
                // sum_n P_mn (x_n - mu_x) = PX_m - P1_m mu_x
                cross += (px - P1[i] * muX) * centered.transpose();
                sourceSpread += P1[i] * centered * centered.transpose();
            }
            for (std::size_t j = 0; j < Target.Size(); ++j)
                targetSpread += Pt1[j] * (Target.At(j) - muX).squaredNorm();
        }

        Status RigidStep(double matched)
        {
            Eigen::Vector3d muX, muY; Eigen::Matrix3d cross, spread; double targetSpread = 0.0;
            Moments(matched, muX, muY, cross, spread, targetSpread);
            const Eigen::JacobiSVD<Eigen::Matrix3d> svd(cross, Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::Matrix3d correction = Eigen::Matrix3d::Identity();
            if (!Config.AllowReflection)
                correction(2, 2) = (svd.matrixU() * svd.matrixV().transpose()).determinant() < 0.0 ? -1.0 : 1.0;
            Rotation = svd.matrixU() * correction * svd.matrixV().transpose();
            const double traceAR = (svd.singularValues().asDiagonal() * correction).trace();
            const double sourceVariance = spread.trace(); // sum_m P1_m |y_m - mu_y|^2
            RigidScale = Config.EstimateScale ? (sourceVariance > 0.0 ? traceAR / sourceVariance : 1.0) : 1.0;
            Linear = RigidScale * Rotation;
            Translation = muX - Linear * muY;
            // sigma^2 = (sum Pt1 |x^|^2 - 2 s tr(A^T R) + s^2 sum P1 |y^|^2) / (Np D)
            Sigma2 = (targetSpread - 2.0 * RigidScale * traceAR + RigidScale * RigidScale * sourceVariance) /
                     (matched * kDimension);
            ApplyLinear();
            return Status::Success;
        }

        Status AffineStep(double matched)
        {
            Eigen::Vector3d muX, muY; Eigen::Matrix3d cross, spread; double targetSpread = 0.0;
            Moments(matched, muX, muY, cross, spread, targetSpread);
            const Eigen::FullPivLU<Eigen::Matrix3d> lu(spread);
            const double size = spread.trace() / kDimension;
            if (!lu.isInvertible() || !(std::abs(spread.determinant()) > 1e-12 * size * size * size))
                return Status::SingularSystem; // the weighted source spans less than three dimensions
            Linear = cross * lu.inverse();
            Translation = muX - Linear * muY;
            // sigma^2 = (tr(X^T d(Pt1) X^) - tr(A B^T)) / (Np D)
            Sigma2 = (targetSpread - (cross * Linear.transpose()).trace()) / (matched * kDimension);
            ApplyLinear();
            return Status::Success;
        }

        Status NonrigidStep(double matched)
        {
            const Eigen::Index m = Eigen::Index(Source.Size());
            // (d(P1) G + lambda sigma^2 I) W = PX - d(P1) Y
            Eigen::MatrixXd system = Kernel;
            Eigen::MatrixXd rhs(m, 3);
            for (Eigen::Index i = 0; i < m; ++i)
            {
                system.row(i) *= P1[std::size_t(i)];
                system(i, i) += Config.Lambda * Sigma2;
                const std::size_t k = std::size_t(i);
                rhs(i, 0) = PX.X[k] - P1[k] * Source.X[k];
                rhs(i, 1) = PX.Y[k] - P1[k] * Source.Y[k];
                rhs(i, 2) = PX.Z[k] - P1[k] * Source.Z[k];
            }
            const Eigen::PartialPivLU<Eigen::MatrixXd> lu(system);
            Coefficients = lu.solve(rhs);
            if (!Coefficients.allFinite()) return Status::SingularSystem;
            const Eigen::MatrixXd displacement = Kernel * Coefficients;
            double xPx = 0.0, xTerm = 0.0, tTerm = 0.0;
            for (std::size_t j = 0; j < Target.Size(); ++j) xPx += Pt1[j] * Target.At(j).squaredNorm();
            for (Eigen::Index i = 0; i < m; ++i)
            {
                const std::size_t k = std::size_t(i);
                const Eigen::Vector3d moved = Source.At(k) + displacement.row(i).transpose();
                Moved.Set(k, moved);
                xTerm += PX.X[k] * moved.x() + PX.Y[k] * moved.y() + PX.Z[k] * moved.z();
                tTerm += P1[k] * moved.squaredNorm();
            }
            // sigma^2 = (tr(X^T d(Pt1) X) - 2 tr((PX)^T T) + tr(T^T d(P1) T)) / (Np D)
            Sigma2 = (xPx - 2.0 * xTerm + tTerm) / (matched * kDimension);
            return Status::Success;
        }

        void ApplyLinear()
        {
            for (std::size_t i = 0; i < Source.Size(); ++i) Moved.Set(i, Linear * Source.At(i) + Translation);
        }

        // lambda/2 tr(W^T G W), the motion-coherence term of the nonrigid objective.
        [[nodiscard]] double Coherence() const
        {
            if (Config.Method != Variant::Nonrigid || Coefficients.size() == 0) return 0.0;
            return 0.5 * Config.Lambda * (Coefficients.transpose() * Kernel * Coefficients).trace();
        }

        void Fail(Status status)
        {
            Failure = status;
            Stop = Termination::None;
            Ended = true;
        }
    };

    Solver::Solver() : m_State(std::make_unique<State>()) {}
    Solver::~Solver() = default;
    Solver::Solver(Solver&&) noexcept = default;
    Solver& Solver::operator=(Solver&&) noexcept = default;

    Status Solver::Initialize(const std::span<const glm::vec3> target, const std::span<const glm::vec3> source,
                              const Params& params)
    {
        m_State = std::make_unique<State>();
        State& s = *m_State;
        s.Config = params;
        s.Ended = true;
        const auto fail = [&](Status status) { s.Failure = status; return status; };
        if (target.empty() || source.empty()) return fail(Status::EmptyInput);
        if (!std::ranges::all_of(target, Finite) || !std::ranges::all_of(source, Finite))
            return fail(Status::NonFiniteInput);
        if (!(params.OutlierWeight >= 0.0 && params.OutlierWeight < 1.0) || params.MaxIterations == 0u ||
            !(params.Tolerance >= 0.0) || !std::isfinite(params.Tolerance) || !(params.InitialSigma2 >= 0.0) ||
            !std::isfinite(params.InitialSigma2) || !(params.Sigma2Floor > 0.0) || !std::isfinite(params.Sigma2Floor) ||
            (params.Method == Variant::Nonrigid &&
             (!(params.Beta > 0.0) || !std::isfinite(params.Beta) || !(params.Lambda > 0.0) || !std::isfinite(params.Lambda))) ||
            params.Method > Variant::Nonrigid)
            return fail(Status::InvalidParameters);
        if (params.Method == Variant::Nonrigid && source.size() > kMaxNonrigidSourcePoints) return fail(Status::TooLarge);

        const std::size_t n = target.size(), m = source.size();
        s.Target.Resize(n); s.Source.Resize(m); s.Moved.Resize(m); s.PX.Resize(m);
        s.P1.assign(m, 0.0); s.Pt1.assign(n, 0.0); s.Scratch.assign(m, 0.0);
        for (std::size_t j = 0; j < n; ++j) s.MeanX += Eigen::Vector3d(target[j].x, target[j].y, target[j].z);
        for (std::size_t i = 0; i < m; ++i) s.MeanY += Eigen::Vector3d(source[i].x, source[i].y, source[i].z);
        s.MeanX /= double(n); s.MeanY /= double(m);
        double radiusX = 0.0, radiusY = 0.0;
        for (std::size_t j = 0; j < n; ++j)
            radiusX += (Eigen::Vector3d(target[j].x, target[j].y, target[j].z) - s.MeanX).squaredNorm();
        for (std::size_t i = 0; i < m; ++i)
            radiusY += (Eigen::Vector3d(source[i].x, source[i].y, source[i].z) - s.MeanY).squaredNorm();
        s.Scale = std::max(std::sqrt(radiusX / double(n)), std::sqrt(radiusY / double(m)));
        if (!params.NormalizeInputs || !(s.Scale > 0.0))
        {
            s.Scale = 1.0;
            s.MeanX.setZero(); s.MeanY.setZero();
        }
        for (std::size_t j = 0; j < n; ++j)
            s.Target.Set(j, (Eigen::Vector3d(target[j].x, target[j].y, target[j].z) - s.MeanX) / s.Scale);
        for (std::size_t i = 0; i < m; ++i)
        {
            s.Source.Set(i, (Eigen::Vector3d(source[i].x, source[i].y, source[i].z) - s.MeanY) / s.Scale);
            s.Moved.Set(i, s.Source.At(i));
        }

        // Mean squared pair distance, from centered sums in O(N + M):
        // sum_nm |x_n - y_m|^2 = M sum|x - x_bar|^2 + N sum|y - y_bar|^2 + N M |x_bar - y_bar|^2.
        if (params.InitialSigma2 > 0.0) s.Sigma2 = params.InitialSigma2;
        else
        {
            Eigen::Vector3d xBar = Eigen::Vector3d::Zero(), yBar = Eigen::Vector3d::Zero();
            for (std::size_t j = 0; j < n; ++j) xBar += s.Target.At(j);
            for (std::size_t i = 0; i < m; ++i) yBar += s.Source.At(i);
            xBar /= double(n); yBar /= double(m);
            double sx = 0.0, sy = 0.0;
            for (std::size_t j = 0; j < n; ++j) sx += (s.Target.At(j) - xBar).squaredNorm();
            for (std::size_t i = 0; i < m; ++i) sy += (s.Source.At(i) - yBar).squaredNorm();
            s.Sigma2 = (double(m) * sx + double(n) * sy + double(n) * double(m) * (xBar - yBar).squaredNorm()) /
                       (kDimension * double(n) * double(m));
        }
        if (!(s.Sigma2 > params.Sigma2Floor)) s.Sigma2 = std::max(s.Sigma2, params.Sigma2Floor);

        if (params.Method == Variant::Nonrigid)
        {
            const Eigen::Index count = Eigen::Index(m);
            s.Kernel.resize(count, count);
            const double inverse = -1.0 / (2.0 * params.Beta * params.Beta);
            for (Eigen::Index a = 0; a < count; ++a)
            {
                s.Kernel(a, a) = 1.0;
                for (Eigen::Index b = a + 1; b < count; ++b)
                {
                    const double value = std::exp(inverse * (s.Source.At(std::size_t(a)) - s.Source.At(std::size_t(b))).squaredNorm());
                    s.Kernel(a, b) = value;
                    s.Kernel(b, a) = value;
                }
            }
            s.Coefficients = Eigen::MatrixXd::Zero(count, 3);
        }
        s.Failure = Status::Success;
        s.Ended = false;
        return Status::Success;
    }

    bool Solver::Step(const IterationObserver& observer)
    {
        State& s = *m_State;
        if (s.Ended) return false;
        double nll = 0.0, matched = 0.0;
        if (!s.ExpectationStep(nll, matched))
        {
            s.Fail(Status::NumericalFailure);
            return false;
        }
        const double objective = nll + s.Coherence();
        s.LastNll = nll;
        s.LastMatched = matched;
        const bool converged = s.HasPreviousObjective &&
            std::abs(s.LastObjective - objective) <= s.Config.Tolerance * std::abs(objective);
        s.LastObjective = objective;
        s.HasPreviousObjective = true;
        if (converged)
        {
            s.Stop = Termination::Converged;
            s.Ended = true;
            return false;
        }

        Status status = Status::Success;
        switch (s.Config.Method)
        {
        case Variant::Rigid: status = s.RigidStep(matched); break;
        case Variant::Affine: status = s.AffineStep(matched); break;
        case Variant::Nonrigid: status = s.NonrigidStep(matched); break;
        }
        if (status != Status::Success)
        {
            s.Fail(status);
            return false;
        }
        if (std::isnan(s.Sigma2) || std::isinf(s.Sigma2) || !s.Translation.allFinite() || !s.Linear.allFinite())
        {
            s.Fail(Status::NumericalFailure);
            return false;
        }
        // Rounding can drive sigma^2 to or below zero once the sets coincide.
        const bool floor = !(s.Sigma2 > s.Config.Sigma2Floor);
        if (floor) s.Sigma2 = s.Config.Sigma2Floor;
        ++s.Iterations;
        s.ObjectiveHistory.push_back(objective);
        s.Sigma2History.push_back(s.Sigma2 * s.Scale * s.Scale);
        if (observer)
        {
            const Result current = Current();
            observer(IterationTrace{.Iteration = s.Iterations - 1u, .Sigma2 = current.Sigma2,
                                    .NegativeLogLikelihood = nll, .Objective = objective,
                                    .MatchedWeight = matched, .Transform = current.Transform});
        }
        if (floor) s.Stop = Termination::SigmaFloor;
        else if (s.Iterations >= s.Config.MaxIterations) s.Stop = Termination::IterationCap;
        s.Ended = s.Stop != Termination::None;
        return !s.Ended;
    }

    void Solver::Run(const IterationObserver& observer)
    {
        while (Step(observer)) {}
    }

    bool Solver::Finished() const noexcept { return m_State->Ended; }

    Result Solver::Current() const
    {
        const State& s = *m_State;
        Result result{};
        result.State = s.Failure;
        result.Method = s.Config.Method;
        if (s.Failure != Status::Success) return result;
        result.Stop = s.Stop;
        result.Iterations = s.Iterations;
        result.Sigma2 = s.Sigma2 * s.Scale * s.Scale;
        result.NegativeLogLikelihood = s.LastNll;
        result.MatchedWeight = s.LastMatched;
        result.ObjectiveHistory = s.ObjectiveHistory;
        result.Sigma2History = s.Sigma2History;
        if (s.Config.Method != Variant::Nonrigid)
        {
            // x_w = Scale (L y + t) + mu_x with y = (y_w - mu_y) / Scale:
            // x_w = L y_w + (mu_x + Scale t - L mu_y).
            const Eigen::Vector3d translation = s.MeanX + s.Scale * s.Translation - s.Linear * s.MeanY;
            result.Linear = ToGlm(s.Linear);
            result.Rotation = ToGlm(s.Config.Method == Variant::Rigid ? s.Rotation : Eigen::Matrix3d::Identity());
            result.Scale = s.Config.Method == Variant::Rigid ? s.RigidScale : 1.0;
            result.Translation = {translation.x(), translation.y(), translation.z()};
            result.Transform = AffineMatrix(s.Linear, translation);
        }
        result.TransformedSource.resize(s.Moved.Size());
        for (std::size_t i = 0; i < s.Moved.Size(); ++i)
        {
            const Eigen::Vector3d world = s.Scale * s.Moved.At(i) + s.MeanX;
            result.TransformedSource[i] = {world.x(), world.y(), world.z()};
        }
        return result;
    }

    Result Register(const std::span<const glm::vec3> target, const std::span<const glm::vec3> source,
                    const Params& params, const IterationObserver& observer)
    {
        Solver solver;
        if (const Status status = solver.Initialize(target, source, params); status != Status::Success)
        {
            Result failed{};
            failed.State = status;
            failed.Method = params.Method;
            return failed;
        }
        solver.Run(observer);
        return solver.Current();
    }
}
