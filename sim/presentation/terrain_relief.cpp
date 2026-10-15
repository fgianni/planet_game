#include "sim/presentation/terrain_relief.hpp"

#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/presentation/detail_seed.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim::presentation {
namespace {

[[nodiscard]] double fade(double t) noexcept {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

[[nodiscard]] double smoothstep01(double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double lattice_value(std::uint64_t key, std::int64_t x, std::int64_t y,
                                   std::int64_t z) noexcept {
    std::uint64_t h = mix_random_key(key ^ static_cast<std::uint64_t>(x));
    h = mix_random_key(h ^ static_cast<std::uint64_t>(y));
    h = mix_random_key(h ^ static_cast<std::uint64_t>(z));
    constexpr double inverse_53_bits = 1.0 / 9'007'199'254'740'992.0;
    return static_cast<double>(h >> 11U) * inverse_53_bits;
}

[[nodiscard]] double value_noise(std::uint64_t key, double px, double py, double pz) noexcept {
    const double fx = std::floor(px);
    const double fy = std::floor(py);
    const double fz = std::floor(pz);
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iy = static_cast<std::int64_t>(fy);
    const auto iz = static_cast<std::int64_t>(fz);
    const double ux = fade(px - fx);
    const double uy = fade(py - fy);
    const double uz = fade(pz - fz);
    double result = 0.0;
    for (int corner = 0; corner < 8; ++corner) {
        const int dx = corner & 1;
        const int dy = (corner >> 1) & 1;
        const int dz = (corner >> 2) & 1;
        const double weight = (dx != 0 ? ux : 1.0 - ux) * (dy != 0 ? uy : 1.0 - uy) *
                              (dz != 0 ? uz : 1.0 - uz);
        result += weight * lattice_value(key, ix + dx, iy + dy, iz + dz);
    }
    return result;
}

}  // namespace

TerrainRelief::TerrainRelief(std::shared_ptr<const PlanetMesh> mesh, const TerrainSnapshot& terrain,
                             std::uint64_t world_seed)
    : mesh_(std::move(mesh)) {
    if (!mesh_) throw std::invalid_argument("terrain relief requires a mesh");
    const std::size_t cells = mesh_->cell_count();
    if (terrain.mean_elevation_m.size() != cells) {
        throw std::invalid_argument("terrain relief: the terrain does not match the mesh");
    }
    constexpr std::size_t q = hypsometry_quantile_count;
    quantiles_m_.resize(cells * q);
    if (terrain.hypsometry_m.size() == quantiles_m_.size()) {
        quantiles_m_ = terrain.hypsometry_m;
    } else {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            std::fill_n(quantiles_m_.begin() + static_cast<std::ptrdiff_t>(cell * q), q,
                        terrain.mean_elevation_m[cell]);
        }
    }
    mean_m_.resize(cells);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        HypsometryQuantiles curve{};
        std::copy_n(quantiles_m_.begin() + static_cast<std::ptrdiff_t>(cell * q), q, curve.begin());
        mean_m_[cell] = mean_elevation_m(curve);
    }
    // A corner's curve is the mean of its cells' curves (quantile averaging
    // keeps a curve monotone), as the cell mesh averages corners.
    std::vector<double> corner_sum(mesh_->corner_count() * q, 0.0);
    corner_mean_m_.assign(mesh_->corner_count(), 0.0);
    std::vector<int> corner_cells(mesh_->corner_count(), 0);
    for (const auto& cell : mesh_->cells()) {
        for (const CornerIndex corner : mesh_->cell_corners(cell.id)) {
            for (std::size_t k = 0; k < q; ++k)
                corner_sum[corner * q + k] += quantiles_m_[cell.id.to_index() * q + k];
            corner_mean_m_[corner] += mean_m_[cell.id.to_index()];
            ++corner_cells[corner];
        }
    }
    corner_quantiles_m_.resize(corner_sum.size());
    for (std::size_t corner = 0; corner < corner_mean_m_.size(); ++corner) {
        const double count = std::max(corner_cells[corner], 1);
        corner_mean_m_[corner] /= count;
        for (std::size_t k = 0; k < q; ++k)
            corner_quantiles_m_[corner * q + k] = static_cast<float>(corner_sum[corner * q + k] / count);
    }
    spacing_rad_ = std::sqrt(4.0 * std::numbers::pi / static_cast<double>(cells));
    frequency_ = relief_features_per_cell / spacing_rad_;
    offset_ = detail_noise_offset(world_seed, DetailKind::terrain);
    lattice_key_ = keyed_random_u64(world_seed, RandomStreamId::presentation_detail, 0,
                                    static_cast<std::uint32_t>(DetailKind::terrain), 3U);
    corner_cells_.assign(mesh_->corner_count(), {0U, 0U, 0U});
    std::vector<std::uint32_t> fill(mesh_->corner_count(), 0U);
    for (const auto& cell : mesh_->cells()) {
        for (const CornerIndex corner : mesh_->cell_corners(cell.id)) {
            if (fill[corner] < 3U) corner_cells_[corner][fill[corner]++] = cell.id.value();
        }
    }
    ranks_ = std::make_unique<RankTable[]>(cells);
}

double TerrainRelief::noise(const Vec3d& direction_unit) const {
    double px = direction_unit.x * frequency_ + offset_[0];
    double py = direction_unit.y * frequency_ + offset_[1];
    double pz = direction_unit.z * frequency_ + offset_[2];
    double amplitude = 0.5;
    double total = 0.0;
    double sum = 0.0;
    for (int octave = 0; octave < relief_octaves; ++octave) {
        sum += amplitude * value_noise(lattice_key_ + static_cast<std::uint64_t>(octave), px, py, pz);
        total += amplitude;
        px = px * 2.0 + 17.13;
        py = py * 2.0 + 31.71;
        pz = pz * 2.0 + 47.39;
        amplitude *= 0.5;
    }
    return sum / total;
}

CellId TerrainRelief::nearest_cell(const Vec3d& direction_unit, CellId hint) const {
    // A greedy walk on the cell graph: on a Voronoi (Delaunay-dual) mesh a
    // cell that is not the nearest always has a nearer neighbour.
    CellId current = hint.to_index() < mesh_->cell_count() ? hint : CellId{0};
    double best = dot(direction_unit, mesh_->cell(current).center_unit);
    for (;;) {
        CellId next = current;
        for (const CellEdgeGeometry& edge : mesh_->cell_edges(current)) {
            const double alignment = dot(direction_unit, mesh_->cell(edge.neighbor).center_unit);
            if (alignment > best) {
                best = alignment;
                next = edge.neighbor;
            }
        }
        if (next == current) return current;
        current = next;
    }
}

const std::array<double, relief_rank_knots>& TerrainRelief::rank_table(CellId cell) const {
    RankTable& table = ranks_[cell.to_index()];
    std::call_once(table.once, [this, cell, &table] { build_rank_table(cell, table.knots); });
    return table.knots;
}

// F_cell from relief_rank_samples points spread by area over the cell's fan
// of triangles (centre and consecutive corners), on an R2 low-discrepancy
// sequence: the same points for every run and every caller.
void TerrainRelief::build_rank_table(CellId cell, std::array<double, relief_rank_knots>& knots) const {
    const Vec3d center = mesh_->cell(cell).center_unit;
    const auto corners = mesh_->cell_corners(cell);
    std::vector<double> areas(corners.size());
    double total_area = 0.0;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const Vec3d a = mesh_->corners_unit()[corners[k]];
        const Vec3d b = mesh_->corners_unit()[corners[(k + 1U) % corners.size()]];
        areas[k] = 0.5 * length(cross(a - center, b - center));
        total_area += areas[k];
    }
    std::vector<double> values;
    values.reserve(relief_rank_samples + corners.size());
    constexpr double alpha_u = 0.7548776662466927;
    constexpr double alpha_v = 0.5698402909980532;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const Vec3d a = mesh_->corners_unit()[corners[k]];
        const Vec3d b = mesh_->corners_unit()[corners[(k + 1U) % corners.size()]];
        const auto count = static_cast<std::size_t>(
            std::llround(static_cast<double>(relief_rank_samples) * areas[k] / total_area));
        for (std::size_t i = 0; i < count; ++i) {
            double u = std::fmod(0.5 + alpha_u * static_cast<double>(i + 1U), 1.0);
            double v = std::fmod(0.5 + alpha_v * static_cast<double>(i + 1U), 1.0);
            if (u + v > 1.0) {
                u = 1.0 - u;
                v = 1.0 - v;
            }
            values.push_back(noise(normalized(center * (1.0 - u - v) + a * u + b * v)));
        }
    }
    std::sort(values.begin(), values.end());
    for (std::size_t knot = 0; knot < relief_rank_knots; ++knot) {
        const double position = static_cast<double>(knot) / static_cast<double>(relief_rank_knots - 1U) *
                                static_cast<double>(values.size() - 1U);
        const auto lower = static_cast<std::size_t>(std::floor(position));
        const std::size_t upper = std::min(lower + 1U, values.size() - 1U);
        const double t = position - static_cast<double>(lower);
        knots[knot] = values[lower] * (1.0 - t) + values[upper] * t;
    }
}

double TerrainRelief::cell_rank(CellId cell, double noise_value) const {
    const auto& knots = rank_table(cell);
    if (noise_value <= knots.front()) return 0.0;
    if (noise_value >= knots.back()) return 1.0;
    const auto upper = static_cast<std::size_t>(
        std::upper_bound(knots.begin(), knots.end(), noise_value) - knots.begin());
    const std::size_t lower = upper - 1U;
    const double span = knots[upper] - knots[lower];
    const double t = span > 0.0 ? (noise_value - knots[lower]) / span : 0.5;
    return (static_cast<double>(lower) + t) / static_cast<double>(relief_rank_knots - 1U);
}

double TerrainRelief::corner_rank(CornerIndex corner, double noise_value) const {
    double sum = 0.0;
    for (const std::uint32_t cell : corner_cells_[corner]) sum += cell_rank(CellId{cell}, noise_value);
    return sum / 3.0;
}

double TerrainRelief::curve_m(const float* quantiles, double rank) noexcept {
    const double position = std::clamp(rank, 0.0, 1.0) * static_cast<double>(hypsometry_quantile_count - 1U);
    const auto lower = std::min(static_cast<std::size_t>(position), hypsometry_quantile_count - 2U);
    const double t = position - static_cast<double>(lower);
    return static_cast<double>(quantiles[lower]) * (1.0 - t) + static_cast<double>(quantiles[lower + 1U]) * t;
}

double TerrainRelief::cell_elevation_m(CellId cell, double rank) const {
    return curve_m(quantiles_m_.data() + cell.to_index() * hypsometry_quantile_count, rank);
}

// The fan triangle (centre, corner, next corner) of `cell` the direction is
// in, and the weights of the three curves there: the cell's own weight is a
// smoothstep of its barycentric weight over relief_handover, the rest goes
// to the corners in proportion to theirs. Along a centre-corner line one
// corner drops out on both sides; on the cell's edge the cell drops out, and
// the neighbour, sharing those corners and weights, gives the same curve.
TerrainRelief::Blend TerrainRelief::blend(const Vec3d& direction_unit, CellId cell) const {
    const Vec3d center = mesh_->cell(cell).center_unit;
    const auto corners = mesh_->cell_corners(cell);
    Blend best;
    best.cell = cell;
    double best_margin = -1e300;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const CornerIndex first = corners[k];
        const CornerIndex second = corners[(k + 1U) % corners.size()];
        const Vec3d a = mesh_->corners_unit()[first];
        const Vec3d b = mesh_->corners_unit()[second];
        double wc = dot(direction_unit, cross(a, b));
        double wa = dot(direction_unit, cross(b, center));
        double wb = dot(direction_unit, cross(center, a));
        const double total = wc + wa + wb;
        wc /= total;
        wa /= total;
        wb /= total;
        const double margin = std::min({wc, wa, wb});
        if (margin <= best_margin) continue;
        best_margin = margin;
        const double own = smoothstep01(std::max(wc, 0.0) / relief_handover);
        const double corner_total = std::max(wa, 0.0) + std::max(wb, 0.0);
        best.first = first;
        best.second = second;
        best.own = own;
        best.first_weight = corner_total > 0.0 ? (1.0 - own) * std::max(wa, 0.0) / corner_total : 0.0;
        best.second_weight = corner_total > 0.0 ? (1.0 - own) * std::max(wb, 0.0) / corner_total : 0.0;
        if (corner_total <= 0.0) best.own = 1.0;
    }
    return best;
}

ReliefSample TerrainRelief::sample(const Vec3d& direction_unit, CellId hint) const {
    const CellId cell = nearest_cell(direction_unit, hint);
    const double n = noise(direction_unit);
    const Blend w = blend(direction_unit, cell);
    // Rank and curve hand over with the same weights.
    const double r = w.own * cell_rank(cell, n) +
                     (w.first_weight > 0.0 ? w.first_weight * corner_rank(w.first, n) : 0.0) +
                     (w.second_weight > 0.0 ? w.second_weight * corner_rank(w.second, n) : 0.0);
    constexpr std::size_t q = hypsometry_quantile_count;
    const double elevation = w.own * cell_elevation_m(cell, r) +
                             w.first_weight * curve_m(corner_quantiles_m_.data() + w.first * q, r) +
                             w.second_weight * curve_m(corner_quantiles_m_.data() + w.second * q, r);
    return {elevation, r, cell};
}

ReliefSample TerrainRelief::smooth_sample(const Vec3d& direction_unit, CellId hint) const {
    const CellId cell = nearest_cell(direction_unit, hint);
    const Blend w = blend(direction_unit, cell);
    const double elevation = w.own * mean_m_[cell.to_index()] + w.first_weight * corner_mean_m_[w.first] +
                             w.second_weight * corner_mean_m_[w.second];
    return {elevation, 0.5, cell};
}

}  // namespace planetsim::presentation
