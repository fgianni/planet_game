#include "sim/core/math/banded_lu.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace planetsim {

BandedMatrix::BandedMatrix(std::size_t size, std::size_t lower, std::size_t upper)
    : size_(size), lower_(lower), upper_(upper), values_(size * (2U * lower + upper + 1U), 0.0) {}

bool BandedMatrix::in_band(std::size_t row, std::size_t column) const noexcept {
    return row < size_ && column < size_ && column + lower_ >= row && column <= row + upper_;
}

double& BandedMatrix::at(std::size_t row, std::size_t column) {
    if (!in_band(row, column)) {
        throw std::out_of_range("banded matrix element outside the band");
    }
    return raw(row, column);
}

double BandedMatrix::at(std::size_t row, std::size_t column) const {
    if (!in_band(row, column)) {
        throw std::out_of_range("banded matrix element outside the band");
    }
    return raw(row, column);
}

void BandedMatrix::set_zero() noexcept { std::fill(values_.begin(), values_.end(), 0.0); }

void BandedMatrix::multiply(std::span<const double> x, std::span<double> y) const {
    if (x.size() != size_ || y.size() != size_) {
        throw std::invalid_argument("banded multiply size mismatch");
    }
    for (std::size_t i = 0; i < size_; ++i) {
        const std::size_t first = i > lower_ ? i - lower_ : 0U;
        const std::size_t last = std::min(size_ - 1U, i + upper_);
        double sum = 0.0;
        for (std::size_t j = first; j <= last; ++j) {
            sum += raw(i, j) * x[j];
        }
        y[i] = sum;
    }
}

BandedLU::BandedLU(BandedMatrix matrix)
    : factors_(std::move(matrix)), multipliers_(factors_.size_ * factors_.lower_, 0.0),
      pivots_(factors_.size_) {
    auto& a = factors_;
    const std::size_t n = a.size_;
    const std::size_t kl = a.lower_;
    const std::size_t reach = a.upper_ + kl;   // columns right of the pivot after fill
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t last_row = std::min(n - 1U, k + kl);
        const std::size_t last_column = std::min(n - 1U, k + reach);
        std::size_t pivot = k;
        for (std::size_t r = k + 1U; r <= last_row; ++r) {
            if (std::abs(a.raw(r, k)) > std::abs(a.raw(pivot, k))) {
                pivot = r;
            }
        }
        const double diagonal = a.raw(pivot, k);
        if (!(diagonal != 0.0) || !std::isfinite(diagonal)) {
            throw std::runtime_error("banded LU: singular matrix");
        }
        pivots_[k] = pivot;
        if (pivot != k) {
            for (std::size_t c = k; c <= last_column; ++c) {
                std::swap(a.raw(k, c), a.raw(pivot, c));
            }
        }
        for (std::size_t r = k + 1U; r <= last_row; ++r) {
            const double m = a.raw(r, k) / diagonal;
            multipliers_[k * kl + (r - k - 1U)] = m;
            a.raw(r, k) = 0.0;
            if (m != 0.0) {
                for (std::size_t c = k + 1U; c <= last_column; ++c) {
                    a.raw(r, c) -= m * a.raw(k, c);
                }
            }
        }
    }
}

void BandedLU::solve(std::span<double> rhs) const {
    const auto& a = factors_;
    const std::size_t n = a.size_;
    if (rhs.size() != n) {
        throw std::invalid_argument("banded LU solve size mismatch");
    }
    const std::size_t kl = a.lower_;
    const std::size_t reach = a.upper_ + kl;
    for (std::size_t k = 0; k < n; ++k) {
        if (pivots_[k] != k) {
            std::swap(rhs[k], rhs[pivots_[k]]);
        }
        const std::size_t last_row = std::min(n - 1U, k + kl);
        for (std::size_t r = k + 1U; r <= last_row; ++r) {
            rhs[r] -= multipliers_[k * kl + (r - k - 1U)] * rhs[k];
        }
    }
    for (std::size_t k = n; k-- > 0U;) {
        const std::size_t last_column = std::min(n - 1U, k + reach);
        double sum = rhs[k];
        for (std::size_t c = k + 1U; c <= last_column; ++c) {
            sum -= a.raw(k, c) * rhs[c];
        }
        rhs[k] = sum / a.raw(k, k);
    }
}

}  // namespace planetsim
