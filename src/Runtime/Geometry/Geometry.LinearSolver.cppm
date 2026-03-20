module;

#include <array>
#include <cstdlib>

export module Geometry.LinearSolver;

export namespace Geometry::Solver
{
    template <std::size_t N>
    [[nodiscard]] bool SolveLinearSystem(
        std::array<std::array<double, N>, N> a,
        std::array<double, N> b,
        std::array<double, N>& x,
        double pivotEpsilon)
    {
        for (std::size_t col = 0; col < N; ++col)
        {
            std::size_t pivotRow = col;
            double pivotAbs = std::abs(a[col][col]);
            for (std::size_t row = col + 1; row < N; ++row)
            {
                const double candidate = std::abs(a[row][col]);
                if (candidate > pivotAbs)
                {
                    pivotAbs = candidate;
                    pivotRow = row;
                }
            }

            if (!(pivotAbs > pivotEpsilon))
            {
                return false;
            }

            if (pivotRow != col)
            {
                std::swap(a[col], a[pivotRow]);
                std::swap(b[col], b[pivotRow]);
            }

            const double invPivot = 1.0 / a[col][col];
            for (std::size_t c = col; c < N; ++c)
            {
                a[col][c] *= invPivot;
            }
            b[col] *= invPivot;

            for (std::size_t row = 0; row < N; ++row)
            {
                if (row == col)
                {
                    continue;
                }

                const double factor = a[row][col];
                if (factor == 0.0)
                {
                    continue;
                }

                for (std::size_t c = col; c < N; ++c)
                {
                    a[row][c] -= factor * a[col][c];
                }
                b[row] -= factor * b[col];
            }
        }

        x = b;
        return true;
    }
}
