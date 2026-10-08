#include "sim/core/math/incomplete_lu.hpp"

#include "sim/core/scheduler/worker_pool.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace planetsim {

namespace {

// Runs f(b) for every block b, worker w taking blocks w, w + W, ...
template <typename Function>
void for_each_block(std::size_t blocks, std::size_t worker_count, const Function& f) {
    const std::size_t workers = std::max<std::size_t>(1U, std::min(worker_count, blocks));
    if (workers == 1U) {
        for (std::size_t b = 0; b < blocks; ++b) {
            f(b);
        }
        return;
    }
    run_on_worker_pool(workers, [&](std::size_t worker) {
        for (std::size_t b = worker; b < blocks; b += workers) {
            f(b);
        }
    });
}

}  // namespace

IncompleteLU::IncompleteLU(std::span<const std::size_t> offset,
                           std::span<const std::size_t> neighbour,
                           std::span<const double> diagonal, std::span<const double> off,
                           double diagonal_scale)
    : IncompleteLU(offset, neighbour, diagonal, off,
                   std::vector<std::size_t>{0U, diagonal.size()}, diagonal_scale, 1U) {}

IncompleteLU::IncompleteLU(std::span<const std::size_t> offset,
                           std::span<const std::size_t> neighbour,
                           std::span<const double> diagonal, std::span<const double> off,
                           std::span<const std::size_t> block_start, double diagonal_scale,
                           std::size_t worker_count)
    : block_start_(block_start.begin(), block_start.end()),
      worker_count_(std::max<std::size_t>(1U, worker_count)) {
    if (offset.empty() || diagonal.size() + 1U != offset.size() ||
        off.size() != neighbour.size() || offset.back() != neighbour.size() ||
        block_start_.size() < 2U || block_start_.front() != 0U ||
        block_start_.back() != diagonal.size()) {
        throw std::invalid_argument("incomplete LU: the pattern does not match");
    }
    const std::size_t nodes = diagonal.size();
    start_.assign(nodes + 1U, 0U);
    for (std::size_t i = 0; i < nodes; ++i) {
        start_[i + 1U] = start_[i] + 1U + (offset[i + 1U] - offset[i]);
    }
    column_.resize(start_[nodes]);
    value_.resize(start_[nodes]);
    diagonal_.resize(nodes);
    std::vector<std::size_t> block_of(nodes);
    for (std::size_t b = 0; b + 1U < block_start_.size(); ++b) {
        for (std::size_t i = block_start_[b]; i < block_start_[b + 1U]; ++i) {
            block_of[i] = b;
        }
    }
    std::vector<std::pair<std::size_t, double>> row;
    for (std::size_t i = 0; i < nodes; ++i) {
        row.clear();
        row.emplace_back(i, diagonal[i] * diagonal_scale);
        for (std::size_t k = offset[i]; k < offset[i + 1U]; ++k) {
            // A block keeps only its own couplings.
            if (block_of[neighbour[k]] == block_of[i]) {
                row.emplace_back(neighbour[k], off[k]);
            }
        }
        std::sort(row.begin(), row.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        start_[i + 1U] = start_[i] + row.size();
        for (std::size_t q = 0; q < row.size(); ++q) {
            column_[start_[i] + q] = row[q].first;
            value_[start_[i] + q] = row[q].second;
            if (row[q].first == i) {
                diagonal_[i] = start_[i] + q;
            }
        }
    }
    column_.resize(start_[nodes]);
    value_.resize(start_[nodes]);
    for_each_block(block_start_.size() - 1U, worker_count_, [&](std::size_t b) {
        factor_rows(block_start_[b], block_start_[b + 1U]);
    });
}

// IKJ elimination restricted to the pattern, rows [first, last), whose
// columns all lie in the same range.
void IncompleteLU::factor_rows(std::size_t first, std::size_t last) {
    const std::size_t absent = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> position(last - first, absent);
    for (std::size_t i = first; i < last; ++i) {
        for (std::size_t q = start_[i]; q < start_[i + 1U]; ++q) {
            position[column_[q] - first] = q;
        }
        for (std::size_t q = start_[i]; q < diagonal_[i]; ++q) {
            const std::size_t j = column_[q];
            const double pivot = value_[diagonal_[j]];
            if (pivot == 0.0) {
                throw std::runtime_error("incomplete LU: zero pivot");
            }
            value_[q] /= pivot;
            for (std::size_t t = diagonal_[j] + 1U; t < start_[j + 1U]; ++t) {
                const std::size_t at = position[column_[t] - first];
                if (at != absent) {
                    value_[at] -= value_[q] * value_[t];
                }
            }
        }
        for (std::size_t q = start_[i]; q < start_[i + 1U]; ++q) {
            position[column_[q] - first] = absent;
        }
    }
}

void IncompleteLU::solve_rows(std::vector<double>& x, std::size_t first,
                              std::size_t last) const {
    for (std::size_t i = first; i < last; ++i) {
        double sum = x[i];
        for (std::size_t q = start_[i]; q < diagonal_[i]; ++q) {
            sum -= value_[q] * x[column_[q]];
        }
        x[i] = sum;
    }
    for (std::size_t i = last; i-- > first;) {
        double sum = x[i];
        for (std::size_t q = diagonal_[i] + 1U; q < start_[i + 1U]; ++q) {
            sum -= value_[q] * x[column_[q]];
        }
        x[i] = sum / value_[diagonal_[i]];
    }
}

void IncompleteLU::solve(std::vector<double>& x) const {
    for_each_block(block_start_.size() - 1U, worker_count_, [&](std::size_t b) {
        solve_rows(x, block_start_[b], block_start_[b + 1U]);
    });
}

}  // namespace planetsim
