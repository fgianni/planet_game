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

// Sub-cell terrain for the presentation tiles (ADR-0022 §4.9). Elevation is
// built by rank so that every cell's area-elevation distribution follows
// its nine hypsometry quantiles at any resolution:
//
//   n(x)  one fixed noise field, keyed by (world_seed, terrain) and sampled
//         by position on the sphere, with all its octaves;
//   r(x)  = F_cell(n(x)), the point's rank among the cell's own noise values
//         (F_cell estimated once from fixed sample points over the cell, so
//         the rank is uniform over every cell, not only on average);
//   h(x)  = Q_cell^-1(r(x)), interpolating the quantiles at ranks 0..1.
//
// Within a fixed width of a cell edge, h blends towards the neighbours'
// curves so the surface is continuous. Everything is a pure function of the
// position, the world seed and the snapshot: no tile, level or cell seeds
// anything, and a point gives the same answer however it is reached.
// Without hypsometry (a schema-2 terrain) a cell is flat at its mean
// elevation, the neutral picture.

inline constexpr double relief_features_per_cell = 3.0; // lowest octave, per cell spacing
inline constexpr int relief_octaves = 14;               // down to metres at L6
inline constexpr double relief_blend_width = 0.002; // of the cell spacing, each side (V4)
inline constexpr std::size_t relief_rank_knots = 33;
inline constexpr std::size_t relief_rank_samples = 4096; // per cell, for F_cell

struct ReliefSample {
    double elevation_m = 0.0;
    double rank = 0.0; // in the nearest cell, unblended: band b is [b/8, (b+1)/8)
    CellId cell;       // the nearest cell; a good hint for the next nearby point
};

class TerrainRelief {
public:
    TerrainRelief(std::shared_ptr<const PlanetMesh> mesh, const TerrainSnapshot& terrain,
                  std::uint64_t world_seed);

    // The terrain at a direction on the unit sphere. `hint` is any cell near
    // the point (the previous sample's cell); it changes only the search time.
    [[nodiscard]] ReliefSample sample(const Vec3d& direction_unit, CellId hint = CellId{0}) const;

    // The filtered terrain coarse tiles draw (ADR-0022 §4.9): each cell's
    // mean elevation, interpolated across the cell mesh's fan triangles as
    // the cell mesh draws it (corners average their three cells); rank 0.5.
    // It needs no rank tables, so far, coarse tiles stay cheap.
    [[nodiscard]] ReliefSample smooth_sample(const Vec3d& direction_unit, CellId hint = CellId{0}) const;

    // The parts, exposed for tests.
    [[nodiscard]] double noise(const Vec3d& direction_unit) const;
    [[nodiscard]] CellId nearest_cell(const Vec3d& direction_unit, CellId hint) const;
    [[nodiscard]] double cell_rank(CellId cell, double noise_value) const;
    [[nodiscard]] double cell_elevation_m(CellId cell, double rank) const;
    [[nodiscard]] double cell_spacing_rad() const noexcept { return spacing_rad_; }
    [[nodiscard]] const PlanetMesh& mesh() const noexcept { return *mesh_; }

private:
    struct RankTable {
        std::once_flag once;
        std::array<double, relief_rank_knots> knots{};
    };

    [[nodiscard]] const std::array<double, relief_rank_knots>& rank_table(CellId cell) const;
    void build_rank_table(CellId cell, std::array<double, relief_rank_knots>& knots) const;

    std::shared_ptr<const PlanetMesh> mesh_;
    std::vector<float> quantiles_m_; // cell-major, hypsometry_quantile_count per cell
    std::vector<double> mean_m_;          // per cell, of the quantiles' curve
    std::vector<double> corner_mean_m_;   // per corner, the mean of its cells' means
    double spacing_rad_ = 0.0;
    double frequency_ = 0.0; // lattice units per radian
    std::array<double, 3> offset_{};
    std::uint64_t lattice_key_ = 0U;
    std::unique_ptr<RankTable[]> ranks_;
};

}  // namespace planetsim::presentation
