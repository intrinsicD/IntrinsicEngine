module;

#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <glm/glm.hpp>

export module Geometry.Linalg;

export namespace Geometry::Linalg
{
    enum class NumericStatus : std::uint8_t
    {
        Success = 0,
        InvalidInput,
        RankDeficient,
        IllConditioned,
        NonFinite,
        NoConvergence
    };

    struct NumericDiagnostics
    {
        NumericStatus Status{NumericStatus::InvalidInput};
        double ResidualNorm{0.0};
        double RelativeResidual{0.0};
        double ConditionEstimate{0.0};
        std::size_t Rank{0};
        std::size_t Iterations{0};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == NumericStatus::Success; }
    };

    struct DenseMatrix
    {
        std::size_t Rows{0};
        std::size_t Cols{0};
        std::vector<double> Values;

        DenseMatrix() = default;
        DenseMatrix(std::size_t rows, std::size_t cols);

        [[nodiscard]] bool IsShapeValid() const noexcept { return Values.size() == Rows * Cols; }
        [[nodiscard]] double operator()(std::size_t row, std::size_t col) const;
        [[nodiscard]] double& operator()(std::size_t row, std::size_t col);
    };

    struct SVDResult
    {
        DenseMatrix U{};
        DenseMatrix Vt{};
        std::vector<double> SingularValues;
        NumericDiagnostics Diagnostics{};
    };

    struct QRResult
    {
        DenseMatrix Q{};
        DenseMatrix R{};
        NumericDiagnostics Diagnostics{};
    };

    struct SymmetricEigenResult
    {
        DenseMatrix Eigenvectors{};
        std::vector<double> Eigenvalues;
        NumericDiagnostics Diagnostics{};
    };

    struct PolarDecompositionResult
    {
        DenseMatrix Orthogonal{};
        DenseMatrix Symmetric{};
        NumericDiagnostics Diagnostics{};
    };

    struct LeastSquaresResult
    {
        std::vector<double> X;
        NumericDiagnostics Diagnostics{};
    };

    struct RobustPCAOptions
    {
        // Zero selects 1 / sqrt(max(rows, cols)).
        double Lambda{0.0};
        // Zero selects 1.25 / ||M||_2 from the input SVD.
        double Mu{0.0};
        std::size_t MaxIterations{1000};
        double Tolerance{1.0e-7};
        double RankTolerance{1.0e-8};
    };

    struct RobustPCAResult
    {
        DenseMatrix LowRank{};
        DenseMatrix Sparse{};
        std::size_t Rank{0};
        std::size_t Iterations{0};
        double ResidualNorm{0.0};
        double RelativeResidual{0.0};
        NumericDiagnostics Diagnostics{};
    };

    struct CovarianceResult
    {
        glm::dvec3 Mean{0.0};
        glm::dmat3 Covariance{0.0};
        std::size_t Count{0};
        bool Valid{false};
    };

    struct CovarianceAccumulator
    {
        void Reset() noexcept;
        void Add(glm::dvec3 sample) noexcept;
        [[nodiscard]] CovarianceResult Result() const noexcept;

    private:
        std::size_t Count_{0};
        glm::dvec3 Mean_{0.0};
        glm::dmat3 M2_{0.0};
    };

    using EigenVector2d = Eigen::Matrix<double, 2, 1>;
    using EigenVector3d = Eigen::Matrix<double, 3, 1>;
    using EigenVector4d = Eigen::Matrix<double, 4, 1>;
    using EigenMatrix3d = Eigen::Matrix<double, 3, 3>;
    using EigenMatrix4d = Eigen::Matrix<double, 4, 4>;
    template <typename Scalar>
    using EigenRowMajorMatrixX = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using EigenRowMajorMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using EigenDynamicStride = Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>;
    using EigenDenseMap = Eigen::Map<EigenRowMajorMatrixXd, Eigen::Unaligned>;
    using ConstEigenDenseMap = Eigen::Map<const EigenRowMajorMatrixXd, Eigen::Unaligned>;
    template <typename Scalar>
    using StridedEigenMap = Eigen::Map<EigenRowMajorMatrixX<Scalar>, Eigen::Unaligned, EigenDynamicStride>;
    template <typename Scalar>
    using ConstStridedEigenMap = Eigen::Map<const EigenRowMajorMatrixX<Scalar>, Eigen::Unaligned, EigenDynamicStride>;

    namespace Detail
    {
        template <typename T>
        struct FixedVectorTraits
        {
            static constexpr bool Supported = false;
        };

        template <>
        struct FixedVectorTraits<glm::vec2>
        {
            using Scalar = float;
            static constexpr std::size_t Dimensions = 2;
            static constexpr bool Supported = true;
        };

        template <>
        struct FixedVectorTraits<glm::vec3>
        {
            using Scalar = float;
            static constexpr std::size_t Dimensions = 3;
            static constexpr bool Supported = true;
        };

        template <>
        struct FixedVectorTraits<glm::vec4>
        {
            using Scalar = float;
            static constexpr std::size_t Dimensions = 4;
            static constexpr bool Supported = true;
        };

        template <>
        struct FixedVectorTraits<glm::dvec2>
        {
            using Scalar = double;
            static constexpr std::size_t Dimensions = 2;
            static constexpr bool Supported = true;
        };

        template <>
        struct FixedVectorTraits<glm::dvec3>
        {
            using Scalar = double;
            static constexpr std::size_t Dimensions = 3;
            static constexpr bool Supported = true;
        };

        template <>
        struct FixedVectorTraits<glm::dvec4>
        {
            using Scalar = double;
            static constexpr std::size_t Dimensions = 4;
            static constexpr bool Supported = true;
        };

        template <typename T>
        concept FixedSizeVector = FixedVectorTraits<std::remove_cv_t<T>>::Supported;

        [[nodiscard]] constexpr bool IsValidStridedExtent(std::size_t valueCount,
                                                          std::size_t rows,
                                                          std::size_t cols,
                                                          std::ptrdiff_t strideElements) noexcept
        {
            if (cols == 0 || strideElements <= 0)
            {
                return false;
            }
            const auto stride = static_cast<std::size_t>(strideElements);
            if (stride < cols)
            {
                return false;
            }
            if (rows == 0)
            {
                return true;
            }
            if (rows == 1)
            {
                return valueCount >= cols;
            }
            const std::size_t max = std::numeric_limits<std::size_t>::max();
            if (stride > (max - cols) / (rows - 1))
            {
                return false;
            }
            return valueCount >= ((rows - 1) * stride + cols);
        }

        template <typename Scalar>
        [[nodiscard]] StridedEigenMap<Scalar> EmptyStridedMap() noexcept
        {
            return StridedEigenMap<Scalar>(static_cast<Scalar*>(nullptr), 0, 0, EigenDynamicStride(0, 1));
        }

        template <typename Scalar>
        [[nodiscard]] ConstStridedEigenMap<Scalar> EmptyConstStridedMap() noexcept
        {
            return ConstStridedEigenMap<Scalar>(static_cast<const Scalar*>(nullptr), 0, 0, EigenDynamicStride(0, 1));
        }
    }

    template <typename T>
    concept FixedSizeVector = Detail::FixedSizeVector<T>;

    [[nodiscard]] EigenVector2d ToEigen(glm::dvec2 value) noexcept;
    [[nodiscard]] EigenVector3d ToEigen(glm::dvec3 value) noexcept;
    [[nodiscard]] EigenVector4d ToEigen(glm::dvec4 value) noexcept;
    [[nodiscard]] EigenMatrix3d ToEigen(const glm::dmat3& value) noexcept;
    [[nodiscard]] EigenMatrix4d ToEigen(const glm::dmat4& value) noexcept;

    [[nodiscard]] glm::dvec2 ToGlm(const EigenVector2d& value) noexcept;
    [[nodiscard]] glm::dvec3 ToGlm(const EigenVector3d& value) noexcept;
    [[nodiscard]] glm::dvec4 ToGlm(const EigenVector4d& value) noexcept;
    [[nodiscard]] glm::dmat3 ToGlm(const EigenMatrix3d& value) noexcept;
    [[nodiscard]] glm::dmat4 ToGlm(const EigenMatrix4d& value) noexcept;

    // Maps are row-major, unaligned views over contiguous scalar buffers. The
    // caller owns the buffer and must keep it alive for the lifetime of the map.
    [[nodiscard]] EigenDenseMap MapRowMajorMatrix(std::span<double> values,
                                                  std::size_t rows,
                                                  std::size_t cols);
    [[nodiscard]] ConstEigenDenseMap MapRowMajorMatrix(std::span<const double> values,
                                                       std::size_t rows,
                                                       std::size_t cols);

    [[nodiscard]] StridedEigenMap<double> MapAsMatrix(std::span<double> values,
                                                      std::size_t rows,
                                                      std::size_t cols,
                                                      std::ptrdiff_t strideElements);
    [[nodiscard]] ConstStridedEigenMap<double> MapAsMatrix(std::span<const double> values,
                                                           std::size_t rows,
                                                           std::size_t cols,
                                                           std::ptrdiff_t strideElements);

    template <typename Scalar>
        requires (!std::is_const_v<Scalar>)
    [[nodiscard]] StridedEigenMap<Scalar> MapAsMatrix(std::span<Scalar> values,
                                                      std::size_t rows,
                                                      std::size_t cols,
                                                      std::ptrdiff_t strideElements)
    {
        if (!Detail::IsValidStridedExtent(values.size(), rows, cols, strideElements))
        {
            return Detail::EmptyStridedMap<Scalar>();
        }
        return StridedEigenMap<Scalar>(values.data(),
                                       static_cast<Eigen::Index>(rows),
                                       static_cast<Eigen::Index>(cols),
                                       EigenDynamicStride(static_cast<Eigen::Index>(strideElements), 1));
    }

    template <typename Scalar>
    [[nodiscard]] ConstStridedEigenMap<Scalar> MapAsMatrix(std::span<const Scalar> values,
                                                           std::size_t rows,
                                                           std::size_t cols,
                                                           std::ptrdiff_t strideElements)
    {
        if (!Detail::IsValidStridedExtent(values.size(), rows, cols, strideElements))
        {
            return Detail::EmptyConstStridedMap<Scalar>();
        }
        return ConstStridedEigenMap<Scalar>(values.data(),
                                            static_cast<Eigen::Index>(rows),
                                            static_cast<Eigen::Index>(cols),
                                            EigenDynamicStride(static_cast<Eigen::Index>(strideElements), 1));
    }

    template <FixedSizeVector VectorT>
    [[nodiscard]] auto MapVectorAsMatrix(std::span<VectorT> values)
        -> StridedEigenMap<typename Detail::FixedVectorTraits<std::remove_cv_t<VectorT>>::Scalar>
    {
        using Traits = Detail::FixedVectorTraits<std::remove_cv_t<VectorT>>;
        using Scalar = typename Traits::Scalar;
        static_assert(sizeof(VectorT) % sizeof(Scalar) == 0);
        constexpr std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(sizeof(VectorT) / sizeof(Scalar));
        constexpr std::size_t dims = Traits::Dimensions;
        const std::size_t scalarExtent =
            values.empty() ? 0u : ((values.size() - 1u) * static_cast<std::size_t>(stride) + dims);
        return MapAsMatrix(std::span<Scalar>{reinterpret_cast<Scalar*>(values.data()), scalarExtent},
                           values.size(),
                           dims,
                           stride);
    }

    template <FixedSizeVector VectorT>
    [[nodiscard]] auto MapVectorAsMatrix(std::span<const VectorT> values)
        -> ConstStridedEigenMap<typename Detail::FixedVectorTraits<std::remove_cv_t<VectorT>>::Scalar>
    {
        using Traits = Detail::FixedVectorTraits<std::remove_cv_t<VectorT>>;
        using Scalar = typename Traits::Scalar;
        static_assert(sizeof(VectorT) % sizeof(Scalar) == 0);
        constexpr std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(sizeof(VectorT) / sizeof(Scalar));
        constexpr std::size_t dims = Traits::Dimensions;
        const std::size_t scalarExtent =
            values.empty() ? 0u : ((values.size() - 1u) * static_cast<std::size_t>(stride) + dims);
        return MapAsMatrix(std::span<const Scalar>{reinterpret_cast<const Scalar*>(values.data()), scalarExtent},
                           values.size(),
                           dims,
                           stride);
    }

    // View a contiguous array of glm::dvec3 as an N x 3 row-major Eigen matrix
    // without copying (glm::dvec3 is three tightly-packed doubles). The caller
    // owns the storage and must keep it alive for the lifetime of the map. An
    // empty span yields a 0 x 3 map.
    [[nodiscard]] EigenDenseMap MapVec3Array(std::span<glm::dvec3> values);
    [[nodiscard]] ConstEigenDenseMap MapVec3Array(std::span<const glm::dvec3> values);

    [[nodiscard]] SVDResult ComputeSVD(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] QRResult ComputeQR(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] SymmetricEigenResult ComputeSymmetricEigen(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] PolarDecompositionResult ComputePolarDecomposition(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] LeastSquaresResult SolveLeastSquares(const DenseMatrix& matrix,
                                                       std::span<const double> rhs,
                                                       double tolerance = 1e-12);
    [[nodiscard]] RobustPCAResult RobustPCA(const DenseMatrix& matrix, const RobustPCAOptions& options = {});
}
