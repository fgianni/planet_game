#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace planetsim {

// ILU(0) of a sparse matrix given by its diagonal and its off-diagonals on
// a CSR pattern (row i's columns neighbour[offset[i] .. offset[i + 1])), in
// the rows' order: the incomplete factor of a non-symmetric operator that
// a multigrid on its symmetric part does not see. Sequential, so
// deterministic. Throws std::runtime_error on a zero pivot.
class IncompleteLU {
  public:
    // `diagonal_scale` multiplies the diagonal (a regularisation of a
    // null mode, for example).
    IncompleteLU(std::span<const std::size_t> offset, std::span<const std::size_t> neighbour,
                 std::span<const double> diagonal, std::span<const double> off,
                 double diagonal_scale = 1.0);

    // Block ILU(0): rows [block_start[b], block_start[b + 1]) are factored
    // on their own, couplings between blocks dropped, so the blocks factor
    // and solve in parallel. The result does not depend on the worker count.
    IncompleteLU(std::span<const std::size_t> offset, std::span<const std::size_t> neighbour,
                 std::span<const double> diagonal, std::span<const double> off,
                 std::span<const std::size_t> block_start, double diagonal_scale,
                 std::size_t worker_count);

    // x ← (LU)⁻¹ x.
    void solve(std::vector<double>& x) const;

  private:
    void factor_rows(std::size_t first, std::size_t last);
    void solve_rows(std::vector<double>& x, std::size_t first, std::size_t last) const;

    std::vector<std::size_t> block_start_;   // {0, rows} without blocks
    std::size_t worker_count_ = 1;
    std::vector<std::size_t> start_;
    std::vector<std::size_t> column_;
    std::vector<double> value_;
    std::vector<std::size_t> diagonal_;
};

}  // namespace planetsim
