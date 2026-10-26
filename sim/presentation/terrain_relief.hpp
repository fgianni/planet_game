#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace planetsim::presentation {

// Sub-cell terrain for the presentation tiles (ADR-0022 §4.9, as amended
// 2026-10-15). Elevation is built by rank from each place's elevation
// curve, so that the terrain follows the cells' hypsometry and is smooth
// everywhere:
//
//   n(x)  one fixed noise field, keyed by (world_seed, terrain) and sampled
//         by position on the sphere, with all its octaves;
//   r(x)  its rank: the cell's own distribution of n, F_cell (estimated
//         once from fixed points over the cell, so ranks are uniform over
//         the cell, not only on average), handing over towards the edges to
//         the corners' distributions (the mean of their three cells' F) with
//         the same weights as the curves below, so it is continuous;
//   Q_x   the elevation curve at x: the cell's own nine quantiles in the
//         cell's interior, handing over smoothly, towards its edges, to the
//         curves of its corners (each the mean of its three cells' curves),
//         weighted by x's place in the cell mesh's fan triangle, as the
//         cell mesh interpolates; on an edge only the shared corners count,
//         so neighbouring cells meet exactly and without a step;
//   h(x)  = Q_x(r(x)).
//
// Everything is a pure function of the position, the world seed and the
// snapshot: no tile, level or cell seeds anything, and a point gives the
// same answer however it is reached. Without hypsometry (a schema-2
// terrain) a cell's curve is flat at its mean elevation, the neutral
// picture.

// The relief is a band-limited landscape, not new full-height bumps at every
// zoom level. The finest octave is roughly a few hundred metres at L6;
// finer tiles resolve this same surface rather than inventing new coasts.
inline constexpr double relief_features_per_cell = 1.5; // lowest octave, per cell spacing
inline constexpr int relief_octaves = 8;
// The share of the cell barycentric weight over which a place hands over
// from its cell's curve to its corners' (0: the cell mesh's linear blend).
inline constexpr double relief_handover = 0.15;
inline constexpr std::size_t relief_rank_knots = 33;
inline constexpr std::size_t relief_rank_samples = 4096; // per cell, for F_cell

struct ReliefSample {
    double elevation_m = 0.0;
    double rank = 0.0; // r(x): band b is [b/8, (b+1)/8)
    CellId cell;       // the nearest cell; a good hint for the next nearby point
};

class TerrainRelief {
public:
    TerrainRelief(std::shared_ptr<const PlanetMesh> mesh, const TerrainSnapshot& terrain,
                  std::uint64_t world_seed);

    // The terrain at a direction on the unit sphere. `hint` is any cell near
    // the point (the previous sample's cell); it changes only the search time.
    [[nodiscard]] ReliefSample sample(const Vec3d& direction_unit, CellId hint = CellId{0}) const;

    // The filtered terrain coarse tiles draw (ADR-0022 §4.9): the same
    // blend of curves, read at their means instead of at the point's rank.
    [[nodiscard]] ReliefSample smooth_sample(const Vec3d& direction_unit, CellId hint = CellId{0}) const;

    // The parts, exposed for tests.
    [[nodiscard]] double noise(const Vec3d& direction_unit) const;
    [[nodiscard]] double cell_rank(CellId cell, double noise_value) const;
    [[nodiscard]] CellId nearest_cell(const Vec3d& direction_unit, CellId hint) const;
    [[nodiscard]] double cell_elevation_m(CellId cell, double rank) const;
    [[nodiscard]] double cell_spacing_rad() const noexcept { return spacing_rad_; }
    [[nodiscard]] const PlanetMesh& mesh() const noexcept { return *mesh_; }

private:
    struct Blend {
        CellId cell;
        CornerIndex first = 0;
        CornerIndex second = 0;
        double own = 1.0; // weight of the cell's curve; the rest goes to the two corners
        double first_weight = 0.0;
        double second_weight = 0.0;
    };

    struct RankTable {
        std::once_flag once;
        std::array<double, relief_rank_knots> knots{};
    };

    [[nodiscard]] Blend blend(const Vec3d& direction_unit, CellId cell) const;
    [[nodiscard]] double corner_rank(CornerIndex corner, double noise_value) const;
    [[nodiscard]] const std::array<double, relief_rank_knots>& rank_table(CellId cell) const;
    void build_rank_table(CellId cell, std::array<double, relief_rank_knots>& knots) const;
    [[nodiscard]] static double curve_m(const float* quantiles, double rank) noexcept;

    std::shared_ptr<const PlanetMesh> mesh_;
    std::vector<float> quantiles_m_;        // cell-major, hypsometry_quantile_count per cell
    std::vector<float> corner_quantiles_m_; // corner-major, the mean of its cells' curves
    std::vector<double> mean_m_;            // per cell
    std::vector<double> corner_mean_m_;     // per corner
    std::vector<std::array<std::uint32_t, 3>> corner_cells_; // the cells meeting at each corner
    std::unique_ptr<RankTable[]> ranks_;                      // per cell, built on first use
    double spacing_rad_ = 0.0;
    double frequency_ = 0.0; // lattice units per radian
    std::array<double, 3> offset_{};
    std::uint64_t lattice_key_ = 0U;
};

}  // namespace planetsim::presentation
