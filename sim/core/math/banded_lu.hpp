#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace planetsim {

// A square matrix with `lower` sub- and `upper` super-diagonals, factorised
// in place by Gaussian elimination with partial pivoting (the scheme of
// LAPACK's dgbtrf): pivoting fills up to lower + upper super-diagonals, so
// each row stores the columns [i − lower, i + upper + lower]. Sequential and
// deterministic; ties between pivot candidates go to the first row.
class BandedMatrix {
  public:
    BandedMatrix(std::size_t size, std::size_t lower, std::size_t upper);

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t lower() const noexcept { return lower_; }
    [[nodiscard]] std::size_t upper() const noexcept { return upper_; }

    // Whether (row, column) lies in the band, i − lower ≤ j ≤ i + upper.
    [[nodiscard]] bool in_band(std::size_t row, std::size_t column) const noexcept;
    // Element (row, column) of the band; throws std::out_of_range outside it.
    double& at(std::size_t row, std::size_t column);
    [[nodiscard]] double at(std::size_t row, std::size_t column) const;
    void set_zero() noexcept;

    // y = A x.
    void multiply(std::span<const double> x, std::span<double> y) const;

  private:
    friend class BandedLU;
    [[nodiscard]] std::size_t width() const noexcept { return 2U * lower_ + upper_ + 1U; }
    [[nodiscard]] double& raw(std::size_t row, std::size_t column) noexcept {
        return values_[row * width() + (column + lower_ - row)];
    }
    [[nodiscard]] double raw(std::size_t row, std::size_t column) const noexcept {
        return values_[row * width() + (column + lower_ - row)];
    }

    std::size_t size_;
    std::size_t lower_;
    std::size_t upper_;
    std::vector<double> values_;
};

class BandedLU {
  public:
    // Factorises a copy of `matrix`; throws std::runtime_error if a pivot
    // is zero or not finite.
    explicit BandedLU(BandedMatrix matrix);

    // Solves A x = b in place.
    void solve(std::span<double> rhs) const;

  private:
    BandedMatrix factors_;                 // U in the band storage
    std::vector<double> multipliers_;      // L, size × lower
    std::vector<std::size_t> pivots_;
};

}  // namespace planetsim
