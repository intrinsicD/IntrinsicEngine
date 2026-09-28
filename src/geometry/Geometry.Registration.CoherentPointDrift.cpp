module;

#include <algorithm>
#include <bit>
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

        // Digamma for x > 0: recurrence up to x >= 6, then the asymptotic series (error < 1e-12).
        double Digamma(double x) noexcept
        {
            double result = 0.0;
            while (x < 6.0)
            {
                result -= 1.0 / x;
                x += 1.0;
            }
            const double f = 1.0 / (x * x);
            return result + std::log(x) - 0.5 / x -
                   f * (1.0 / 12.0 - f * (1.0 / 120.0 - f * (1.0 / 252.0 - f * (1.0 / 240.0 - f / 132.0))));
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
        case Variant::Bayesian: return "bayesian";
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
        // Nonrigid: T(Y) = Y + G W. With LowRank, G ~= Basis diag(Eigenvalues) Basis^T and only
        // Basis^T W is kept.
        Eigen::MatrixXd Kernel{}, Coefficients{};
        Eigen::MatrixXd Basis{}, BasisCoefficients{};
        Eigen::VectorXd Eigenvalues{};
        double KernelError{0.0};

        // Bayesian: T(y) = s R (y + v) + t with the posterior mean displacement v (M x 3), per-point
        // posterior variances, log mixing weights and the E-step source log-weights built from them.
        Eigen::MatrixXd Displacement{};
        Eigen::VectorXd PosteriorVariance{}, LogAlpha{};
        std::vector<double> SourceLogWeight{};
        double WeightShift{0.0};
        double Volume{1.0}; // target bounding-box volume (normalized) for the uniform outlier density
        // Subsampled Bayesian runs register Source = the samples; FullSource keeps every point.
        Points FullSource{};
        std::vector<std::uint32_t> Samples{};
        // Kernel-form coefficients of the displacement: v = G KernelWeights (full kernel, M x 3) or
        // v = Basis KernelWeights (low rank, k x 3); interpolation evaluates the same expansions at
        // new points (low rank through the Nystroem extension).
        Eigen::MatrixXd KernelWeights{};
        Eigen::MatrixXd LandmarkPoints{}, Extension{};

        // Optimized E-step (Params::EStep != Reference).
        EStep::Evaluator Accelerated{};
        EStep::Sums AcceleratedSums{};
        EStepPolicy LastPolicy{EStepPolicy::Reference};
        std::uint32_t UsedPolicies{0u}; // bit per EStepPolicy that ran an iteration
        // Current() caches the (expensive) subsampled interpolation per solver state.
        std::uint64_t Revision{0u};
        mutable std::uint64_t CachedRevision{~std::uint64_t{0}};
        mutable std::vector<glm::dvec3> CachedFullSource{};
        double LastErrorBound{0.0}, MaxErrorBound{0.0};
        std::uint64_t LastKernelEvaluations{0u}, TotalKernelEvaluations{0u};

        // E-step sufficient statistics: P1 = P 1, Pt1 = P^T 1, PX = P X.
        std::vector<double> P1{}, Pt1{}, Scratch{};
        Points PX{};

        // Responsibilities P(m | x_n) of the Gaussian-mixture-plus-uniform model, streamed row
        // by row (target point by target point) with a log-sum-exp shift so small sigma^2 cannot
        // underflow every term. Returns false when every target point went to the outlier term.
        [[nodiscard]] double LogOutlierConstant() const
        {
            const std::size_t n = Target.Size(), m = Moved.Size();
            const double w = Config.OutlierWeight;
            // BCPD: omega / V against (1 - omega) (2 pi sigma^2)^{-D/2}; the mixing weights alpha_m
            // are in the source log-weights.
            if (Config.Method == Variant::Bayesian)
                return w > 0.0 ? 0.5 * kDimension * std::log(std::numbers::pi * 2.0 * Sigma2) + std::log(w / (1.0 - w)) -
                                     std::log(Volume)
                               : -std::numeric_limits<double>::infinity();
            // c = (2 pi sigma^2)^{D/2} * w/(1-w) * M/N (paper eq. 7); log for overflow safety.
            return w > 0.0
                ? 0.5 * kDimension * std::log(std::numbers::pi * 2.0 * Sigma2) + std::log(w / (1.0 - w)) +
                      std::log(double(m) / double(n))
                : -std::numeric_limits<double>::infinity();
        }

        // p(x) = (1-w)/M (2 pi sigma^2)^{-D/2} (sum_m exp(a_m) + c); NLL = -sum_n log p(x_n).
        [[nodiscard]] double NegativeLogLikelihood(const double logDenominatorSum) const
        {
            const double n = double(Target.Size()), m = double(Moved.Size());
            // Bayesian mixing weights are inside the denominators already.
            const double mixing = Config.Method == Variant::Bayesian ? 1.0 : 1.0 / m;
            return -(logDenominatorSum + n * (std::log((1.0 - Config.OutlierWeight) * mixing) -
                                              0.5 * kDimension * std::log(std::numbers::pi * 2.0 * Sigma2)));
        }

        bool AcceleratedExpectationStep(double& negativeLogLikelihood, double& matched)
        {
            EStep::Sums& sums = AcceleratedSums;
            const EStep::Settings settings{.Policy = Config.EStep, .Tolerance = Config.EStepTolerance,
                                           .Threads = Config.Threads};
            if (!Accelerated.Evaluate({Moved.X, Moved.Y, Moved.Z}, Sigma2, LogOutlierConstant(), settings, sums,
                                      SourceLogWeight))
                return false;
            P1.swap(sums.P1); Pt1.swap(sums.Pt1);
            PX.X.swap(sums.PXx); PX.Y.swap(sums.PXy); PX.Z.swap(sums.PXz);
            negativeLogLikelihood = NegativeLogLikelihood(sums.LogDenominatorSum);
            matched = sums.Matched;
            LastPolicy = sums.Used;
            LastErrorBound = sums.ErrorBound;
            LastKernelEvaluations = sums.KernelEvaluations;
            MaxErrorBound = std::max(MaxErrorBound, LastErrorBound);
            TotalKernelEvaluations += LastKernelEvaluations;
            return std::isfinite(negativeLogLikelihood);
        }

        bool ExpectationStep(double& negativeLogLikelihood, double& matched)
        {
            if (Config.EStep != EStepPolicy::Reference) return AcceleratedExpectationStep(negativeLogLikelihood, matched);
            const std::size_t n = Target.Size(), m = Moved.Size();
            const double twoSigma2 = 2.0 * Sigma2;
            const double logC = LogOutlierConstant();
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
                // Shifted by the largest exponent a_max = -minDistance / (2 sigma^2) (Bayesian runs:
                // the largest weighted exponent), so every term stays <= 1.
                double aMax = -minDistance / twoSigma2;
                double sum = 0.0;
                if (SourceLogWeight.empty())
                    for (std::size_t i = 0; i < m; ++i)
                    {
                        e[i] = std::exp((minDistance - e[i]) / twoSigma2);
                        sum += e[i];
                    }
                else
                {
                    // Weighted: shift by the row's true largest weighted exponent, so a down-weighted
                    // nearest source cannot underflow the whole row.
                    double top = -std::numeric_limits<double>::infinity();
                    for (std::size_t i = 0; i < m; ++i)
                    {
                        e[i] = (minDistance - e[i]) / twoSigma2 + SourceLogWeight[i] - WeightShift;
                        top = std::max(top, e[i]);
                    }
                    for (std::size_t i = 0; i < m; ++i)
                    {
                        e[i] = std::exp(e[i] - top);
                        sum += e[i];
                    }
                    aMax += WeightShift + top;
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
            negativeLogLikelihood = NegativeLogLikelihood(logSum);
            matched = total;
            LastPolicy = EStepPolicy::Reference;
            LastKernelEvaluations = std::uint64_t(n) * std::uint64_t(m);
            TotalKernelEvaluations += LastKernelEvaluations;
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

        // E-step source log-weights of BCPD: log alpha_m - s^2 D sigma_m^2 / (2 sigma^2).
        void UpdateBayesianWeights()
        {
            const std::size_t m = Source.Size();
            SourceLogWeight.resize(m);
            const double factor = RigidScale * RigidScale * kDimension / (2.0 * Sigma2);
            WeightShift = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < m; ++i)
            {
                SourceLogWeight[i] = LogAlpha[Eigen::Index(i)] - factor * PosteriorVariance[Eigen::Index(i)];
                WeightShift = std::max(WeightShift, SourceLogWeight[i]);
            }
        }

        // One variational-Bayes update of BCPD (Hirose 2021, Algorithm 1) after the E-step:
        // deformation posterior (mean and per-point variances), mixing weights, similarity,
        // then sigma^2. u = y + v is the deformed source, T(y) = s R u + t.
        Status BayesianStep(double matched)
        {
            const Eigen::Index m = Eigen::Index(Source.Size());
            const double scale = RigidScale, ratio = scale * scale / Sigma2;
            // b = (s^2/sigma^2) nu o (T^{-1}(x^) - y), using nu_m T^{-1}(x^_m) = R^T (PX_m - nu_m t) / s.
            Eigen::MatrixXd b(m, 3);
            Eigen::VectorXd precision(m);
            for (Eigen::Index i = 0; i < m; ++i)
            {
                const std::size_t k = std::size_t(i);
                const Eigen::Vector3d px{PX.X[k], PX.Y[k], PX.Z[k]};
                const Eigen::Vector3d r = Rotation.transpose() * (px - P1[k] * Translation) / scale - P1[k] * Source.At(k);
                b.row(i) = ratio * r.transpose();
                precision(i) = ratio * P1[k];
            }
            const double lambda = Config.Lambda;
            const bool variances = Config.PosteriorVarianceTerms;
            if (Config.LowRank > 0u)
            {
                // Posterior over v = Q a with prior a ~ N(0, L / lambda): Sigma = Q (lambda L^{-1} + Q^T D Q)^{-1} Q^T.
                Eigen::MatrixXd inner = Basis.transpose() * precision.asDiagonal() * Basis;
                inner.diagonal() += lambda * Eigenvalues.cwiseInverse();
                const Eigen::LLT<Eigen::MatrixXd> llt(inner);
                if (llt.info() != Eigen::Success) return Status::SingularSystem;
                const Eigen::MatrixXd covariance = llt.solve(Eigen::MatrixXd::Identity(inner.rows(), inner.cols()));
                KernelWeights = covariance * (Basis.transpose() * b);
                Displacement = Basis * KernelWeights;
                if (variances)
                {
                    const Eigen::MatrixXd projected = Basis * covariance;
                    PosteriorVariance = (projected.cwiseProduct(Basis)).rowwise().sum();
                }
            }
            else
            {
                // Sigma = (lambda G^{-1} + D)^{-1} = G/lambda - G D^{1/2} (lambda I + D^{1/2} G D^{1/2})^{-1} D^{1/2} G / lambda.
                const Eigen::VectorXd root = precision.cwiseSqrt();
                Eigen::MatrixXd system = root.asDiagonal() * Kernel * root.asDiagonal();
                system.diagonal().array() += lambda;
                const Eigen::LLT<Eigen::MatrixXd> llt(system);
                if (llt.info() != Eigen::Success) return Status::SingularSystem;
                const Eigen::MatrixXd rootKernel = root.asDiagonal() * Kernel; // D^{1/2} G
                if (variances)
                {
                    const Eigen::MatrixXd whitened = llt.matrixL().solve(rootKernel);
                    PosteriorVariance = (Kernel.diagonal() - whitened.colwise().squaredNorm().transpose()) / lambda;
                }
                // Sigma b = G z with z = (b - D^{1/2} (lambda I + K)^{-1} D^{1/2} G b) / lambda.
                const Eigen::MatrixXd kernelB = Kernel * b;
                KernelWeights = (b - root.asDiagonal() * llt.solve(root.asDiagonal() * kernelB)) / lambda;
                Displacement = Kernel * KernelWeights;
            }
            PosteriorVariance = PosteriorVariance.cwiseMax(0.0);
            if (!Displacement.allFinite() || !PosteriorVariance.allFinite()) return Status::NumericalFailure;

            // Mixing weights: E[log alpha_m] under the Dirichlet posterior (equal for kappa = inf).
            // kappa M overflowing counts as infinity (equal weights).
            if (std::isinf(Config.Kappa) || !std::isfinite(Config.Kappa * double(m) + matched))
                LogAlpha.setConstant(-std::log(double(m)));
            else
            {
                const double normalizer = Digamma(Config.Kappa * double(m) + matched);
                for (Eigen::Index i = 0; i < m; ++i) LogAlpha(i) = Digamma(Config.Kappa + P1[std::size_t(i)]) - normalizer;
            }

            // Similarity from the deformed source u = y + v, weighted by nu; E[u] carries sigma_m^2 I.
            Eigen::Vector3d xBar = Eigen::Vector3d::Zero(), uBar = Eigen::Vector3d::Zero();
            double varianceBar = 0.0;
            for (Eigen::Index i = 0; i < m; ++i)
            {
                const std::size_t k = std::size_t(i);
                xBar += Eigen::Vector3d{PX.X[k], PX.Y[k], PX.Z[k]};
                uBar += P1[k] * (Source.At(k) + Displacement.row(i).transpose());
                varianceBar += P1[k] * PosteriorVariance(i);
            }
            xBar /= matched; uBar /= matched; varianceBar /= matched;
            Eigen::Matrix3d cross = Eigen::Matrix3d::Zero(), spread = Eigen::Matrix3d::Zero();
            for (Eigen::Index i = 0; i < m; ++i)
            {
                const std::size_t k = std::size_t(i);
                const Eigen::Vector3d u = Source.At(k) + Displacement.row(i).transpose() - uBar;
                cross += (Eigen::Vector3d{PX.X[k], PX.Y[k], PX.Z[k]} - P1[k] * xBar) * u.transpose();
                spread += P1[k] * u * u.transpose();
            }
            cross /= matched;
            spread = spread / matched + varianceBar * Eigen::Matrix3d::Identity();
            const Eigen::JacobiSVD<Eigen::Matrix3d> svd(cross, Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::Matrix3d correction = Eigen::Matrix3d::Identity();
            if (!Config.AllowReflection)
                correction(2, 2) = (svd.matrixU() * svd.matrixV().transpose()).determinant() < 0.0 ? -1.0 : 1.0;
            Rotation = svd.matrixU() * correction * svd.matrixV().transpose();
            RigidScale = Config.EstimateScale && spread.trace() > 0.0 ? (Rotation.transpose() * cross).trace() / spread.trace()
                                                                      : 1.0;
            Linear = RigidScale * Rotation;
            Translation = xBar - Linear * uBar;

            double xPx = 0.0, xTerm = 0.0, tTerm = 0.0;
            for (std::size_t j = 0; j < Target.Size(); ++j) xPx += Pt1[j] * Target.At(j).squaredNorm();
            for (Eigen::Index i = 0; i < m; ++i)
            {
                const std::size_t k = std::size_t(i);
                const Eigen::Vector3d moved = Linear * (Source.At(k) + Displacement.row(i).transpose()) + Translation;
                Moved.Set(k, moved);
                xTerm += PX.X[k] * moved.x() + PX.Y[k] * moved.y() + PX.Z[k] * moved.z();
                tTerm += P1[k] * moved.squaredNorm();
            }
            // sigma^2 = sum_mn P_mn |x_n - T(u_m)|^2 / (Np D) + s^2 sigma_bar^2
            Sigma2 = (xPx - 2.0 * xTerm + tTerm) / (matched * kDimension) + RigidScale * RigidScale * varianceBar;
            return Status::Success;
        }

        // Woodbury form of (d(P1) Q L Q^T + a I) W = F with a = lambda sigma^2:
        // W = (F - d(P1) Q (a L^{-1} + Q^T d(P1) Q)^{-1} Q^T F) / a.
        Status LowRankNonrigidStep(double matched)
        {
            const Eigen::Index m = Eigen::Index(Source.Size());
            const Eigen::Map<const Eigen::VectorXd> p1(P1.data(), m);
            Eigen::MatrixXd f(m, 3);
            for (Eigen::Index i = 0; i < m; ++i)
            {
                const std::size_t k = std::size_t(i);
                f(i, 0) = PX.X[k] - P1[k] * Source.X[k];
                f(i, 1) = PX.Y[k] - P1[k] * Source.Y[k];
                f(i, 2) = PX.Z[k] - P1[k] * Source.Z[k];
            }
            const double a = Config.Lambda * Sigma2;
            const Eigen::MatrixXd weighted = p1.asDiagonal() * Basis;
            Eigen::MatrixXd inner = Basis.transpose() * weighted;
            inner.diagonal() += a * Eigenvalues.cwiseInverse();
            const Eigen::LDLT<Eigen::MatrixXd> ldlt(inner);
            if (ldlt.info() != Eigen::Success) return Status::SingularSystem;
            const Eigen::MatrixXd z = ldlt.solve(Basis.transpose() * f);
            // Q^T W = ((a L^{-1} + Q^T d(P1) Q) z - Q^T d(P1) Q z) / a = L^{-1} z, so the displacement
            // G W = Q L Q^T W is Q z; forming W itself would amplify rounding by 1/a near the floor.
            BasisCoefficients = Eigenvalues.cwiseInverse().asDiagonal() * z;
            if (!BasisCoefficients.allFinite()) return Status::SingularSystem;
            return FinishNonrigid(Basis * z, matched);
        }

        Status FinishNonrigid(const Eigen::MatrixXd& displacement, double matched)
        {
            const Eigen::Index m = Eigen::Index(Source.Size());
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

        Status NonrigidStep(double matched)
        {
            if (Config.LowRank > 0u) return LowRankNonrigidStep(matched);
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
            return FinishNonrigid(Kernel * Coefficients, matched);
        }

        void ApplyLinear()
        {
            for (std::size_t i = 0; i < Source.Size(); ++i) Moved.Set(i, Linear * Source.At(i) + Translation);
        }

        // lambda/2 tr(W^T G W), the motion-coherence term of the nonrigid objective.
        [[nodiscard]] double Coherence() const
        {
            if (Config.Method != Variant::Nonrigid) return 0.0;
            if (Config.LowRank > 0u)
                return BasisCoefficients.size() == 0 ? 0.0
                    : 0.5 * Config.Lambda * (BasisCoefficients.transpose() * Eigenvalues.asDiagonal() * BasisCoefficients).trace();
            if (Coefficients.size() == 0) return 0.0;
            return 0.5 * Config.Lambda * (Coefficients.transpose() * Kernel * Coefficients).trace();
        }

        // Rigid/affine map, or the Bayesian similarity, in world units:
        // x_w = Scale (L y + t) + mu_x with y = (y_w - mu_y) / Scale, i.e. L y_w + (mu_x + Scale t - L mu_y).
        [[nodiscard]] glm::dmat4 WorldTransform() const
        {
            return AffineMatrix(Linear, MeanX + Scale * Translation - Linear * MeanY);
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
            ((params.Method == Variant::Nonrigid || params.Method == Variant::Bayesian) &&
             (!(params.Beta > 0.0) || !std::isfinite(params.Beta) || !(params.Lambda > 0.0) || !std::isfinite(params.Lambda))) ||
            (params.Method == Variant::Bayesian &&
             (!(params.Gamma > 0.0) || !std::isfinite(params.Gamma) || !(params.Kappa > 0.0) ||
              (params.SubsampleSource > 0u && params.SubsampleSource < 4u) ||
              (params.SubsampleTarget > 0u && params.SubsampleTarget < 4u))) ||
            params.Method > Variant::Bayesian || params.EStep > EStepPolicy::FastGauss ||
            (params.EStep != EStepPolicy::Reference &&
             !(params.EStepTolerance > 0.0 && params.EStepTolerance < 1.0)))
            return fail(Status::InvalidParameters);
        const bool deforming = params.Method == Variant::Nonrigid || params.Method == Variant::Bayesian;
        const bool subsampled = params.Method == Variant::Bayesian && params.SubsampleSource > 0u &&
                                params.SubsampleSource < source.size();
        // The kernel covers the registered points: every source point, or the Bayesian samples.
        const std::size_t registered = subsampled ? params.SubsampleSource : source.size();
        if (deforming && (registered > (params.LowRank > 0u ? kMaxLowRankSourcePoints : kMaxNonrigidSourcePoints) ||
                          source.size() > kMaxLowRankSourcePoints))
            return fail(Status::TooLarge);

        std::size_t n = target.size();
        std::size_t m = source.size();
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
        // Farthest-point samples from point 0 (ties: lowest index).
        const auto farthest = [](const Points& points, const std::size_t count)
        {
            std::vector<std::uint32_t> chosen;
            std::vector<double> distance(points.Size(), std::numeric_limits<double>::infinity());
            std::size_t next = 0;
            while (chosen.size() < count)
            {
                chosen.push_back(std::uint32_t(next));
                const Eigen::Vector3d center = points.At(next);
                for (std::size_t i = 0; i < points.Size(); ++i)
                    distance[i] = std::min(distance[i], (points.At(i) - center).squaredNorm());
                next = std::size_t(std::max_element(distance.begin(), distance.end()) - distance.begin());
                if (!(distance[next] > 0.0)) break; // the rest duplicates the samples
            }
            return chosen;
        };
        if (params.Method == Variant::Bayesian && params.SubsampleTarget > 0u && params.SubsampleTarget < n)
        {
            const Points full = s.Target;
            const auto chosen = farthest(full, params.SubsampleTarget);
            n = chosen.size();
            s.Target.Resize(n);
            s.Pt1.assign(n, 0.0);
            for (std::size_t k = 0; k < n; ++k) s.Target.Set(k, full.At(chosen[k]));
        }
        if (subsampled)
        {
            // Source becomes the samples; FullSource keeps every point for the interpolation.
            s.FullSource = s.Source;
            s.Samples = farthest(s.FullSource, registered);
            m = s.Samples.size();
            s.Source.Resize(m); s.Moved.Resize(m); s.PX.Resize(m);
            s.P1.assign(m, 0.0); s.Scratch.assign(m, 0.0);
            for (std::size_t k = 0; k < m; ++k)
            {
                s.Source.Set(k, s.FullSource.At(s.Samples[k]));
                s.Moved.Set(k, s.Source.At(k));
            }
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
            if (params.Method == Variant::Bayesian) s.Sigma2 *= params.Gamma;
        }
        if (!(s.Sigma2 > params.Sigma2Floor)) s.Sigma2 = std::max(s.Sigma2, params.Sigma2Floor);

        if (params.EStep != EStepPolicy::Reference) s.Accelerated.SetTarget({s.Target.X, s.Target.Y, s.Target.Z});
        if (params.Method == Variant::Bayesian)
        {
            // Start: v = 0, zero posterior variances, equal mixing weights, identity similarity; the
            // uniform outlier density is over the target's bounding box.
            s.Displacement = Eigen::MatrixXd::Zero(Eigen::Index(m), 3);
            s.PosteriorVariance = Eigen::VectorXd::Zero(Eigen::Index(m)); // stays zero without variance terms
            s.LogAlpha = Eigen::VectorXd::Constant(Eigen::Index(m), -std::log(double(m)));
            Eigen::Vector3d lo = Eigen::Vector3d::Constant(std::numeric_limits<double>::infinity()), hi = -lo;
            for (std::size_t j = 0; j < n; ++j) { lo = lo.cwiseMin(s.Target.At(j)); hi = hi.cwiseMax(s.Target.At(j)); }
            s.Volume = std::max((hi - lo).cwiseMax(1e-6).prod(), 1e-12);
        }
        if (deforming && params.LowRank > 0u)
        {
            EStep::LowRankKernel kernel;
            if (!EStep::BuildLowRankGaussianKernel({s.Source.X, s.Source.Y, s.Source.Z}, params.Beta, params.LowRank,
                                                   params.Threads, kernel))
                return fail(Status::SingularSystem);
            const Eigen::Index rank = Eigen::Index(kernel.Rank);
            s.Basis = Eigen::Map<const Eigen::MatrixXd>(kernel.Basis.data(), Eigen::Index(m), rank);
            s.Eigenvalues = Eigen::Map<const Eigen::VectorXd>(kernel.Eigenvalues.data(), rank);
            s.KernelError = kernel.EstimatedRelativeError;
            s.BasisCoefficients = Eigen::MatrixXd::Zero(rank, 3);
            if (subsampled)
            {
                s.LandmarkPoints.resize(Eigen::Index(kernel.LandmarkIndices.size()), 3);
                for (std::size_t l = 0; l < kernel.LandmarkIndices.size(); ++l)
                    s.LandmarkPoints.row(Eigen::Index(l)) = s.Source.At(kernel.LandmarkIndices[l]).transpose();
                s.Extension = Eigen::Map<const Eigen::MatrixXd>(kernel.Extension.data(),
                                                                Eigen::Index(kernel.LandmarkIndices.size()), rank);
            }
        }
        else if (deforming)
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
        if (s.Config.Method == Variant::Bayesian) s.UpdateBayesianWeights();
        ++s.Revision;
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
        case Variant::Bayesian: status = s.BayesianStep(matched); break;
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
        s.UsedPolicies |= 1u << unsigned(s.LastPolicy);
        if (observer)
        {
            // Built directly: Current() would copy histories and, when subsampled, interpolate.
            observer(IterationTrace{.Iteration = s.Iterations - 1u, .Sigma2 = s.Sigma2 * s.Scale * s.Scale,
                                    .NegativeLogLikelihood = nll, .Objective = objective,
                                    .MatchedWeight = matched,
                                    .Transform = s.Config.Method == Variant::Nonrigid ? glm::dmat4(1.0) : s.WorldTransform(),
                                    .EStep = s.LastPolicy, .EStepErrorBound = s.LastErrorBound,
                                    .KernelEvaluations = s.LastKernelEvaluations});
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
        result.RequestedBackend = BackendId(s.Config.EStep);
        result.Backend = result.RequestedBackend;
        if (std::popcount(s.UsedPolicies) == 1)
            result.Backend = BackendId(EStepPolicy(std::countr_zero(s.UsedPolicies)));
        else if (s.UsedPolicies != 0u)
            result.Backend = s.Config.EStep == EStepPolicy::Auto ? BackendId(EStepPolicy::Auto) : std::string_view{"cpu_mixed"};
        result.EStepErrorBound = s.MaxErrorBound;
        result.KernelEvaluations = s.TotalKernelEvaluations;
        result.KernelRank = std::uint32_t(s.Eigenvalues.size());
        result.KernelApproximationError = s.KernelError;
        if (s.Config.Method != Variant::Nonrigid)
        {
            // x_w = Scale (L y + t) + mu_x with y = (y_w - mu_y) / Scale:
            // x_w = L y_w + (mu_x + Scale t - L mu_y).
            const Eigen::Vector3d translation = s.MeanX + s.Scale * s.Translation - s.Linear * s.MeanY;
            result.Linear = ToGlm(s.Linear);
            result.Transform = s.WorldTransform();
            const bool similarity = s.Config.Method == Variant::Rigid || s.Config.Method == Variant::Bayesian;
            result.Rotation = ToGlm(similarity ? s.Rotation : Eigen::Matrix3d::Identity());
            result.Scale = similarity ? s.RigidScale : 1.0;
            result.Translation = {translation.x(), translation.y(), translation.z()};
        }
        if (!s.Samples.empty())
        {
            // Subsampled Bayesian run: evaluate the samples' kernel expansion (the posterior mean
            // deformation) at every source point, in parallel, once per solver state.
            if (s.CachedRevision != s.Revision)
            {
                const double inverse = -1.0 / (2.0 * s.Config.Beta * s.Config.Beta);
                const bool lowRank = s.Config.LowRank > 0u;
                const Eigen::Index centers = lowRank ? s.LandmarkPoints.rows() : Eigen::Index(s.Samples.size());
                const Eigen::MatrixXd coefficients = s.KernelWeights.size() == 0
                    ? Eigen::MatrixXd::Zero(centers, 3)
                    : (lowRank ? Eigen::MatrixXd(s.Extension * s.KernelWeights) : s.KernelWeights);
                Eigen::MatrixXd centerPoints(centers, 3);
                for (Eigen::Index k = 0; k < centers; ++k)
                    centerPoints.row(k) = lowRank ? Eigen::RowVector3d(s.LandmarkPoints.row(k))
                                                  : Eigen::RowVector3d(s.Source.At(std::size_t(k)).transpose());
                s.CachedFullSource.resize(s.FullSource.Size());
                EStep::ParallelRange(s.FullSource.Size(), 256, s.Config.Threads, [&](const std::size_t begin, const std::size_t end)
                {
                    for (std::size_t i = begin; i < end; ++i)
                    {
                        const Eigen::Vector3d y = s.FullSource.At(i);
                        Eigen::Vector3d v = Eigen::Vector3d::Zero();
                        for (Eigen::Index k = 0; k < centers; ++k)
                            v += std::exp(inverse * (y - centerPoints.row(k).transpose()).squaredNorm()) *
                                 coefficients.row(k).transpose();
                        const Eigen::Vector3d world = s.Scale * (s.Linear * (y + v) + s.Translation) + s.MeanX;
                        s.CachedFullSource[i] = {world.x(), world.y(), world.z()};
                    }
                });
                s.CachedRevision = s.Revision;
            }
            result.TransformedSource = s.CachedFullSource;
            return result;
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
