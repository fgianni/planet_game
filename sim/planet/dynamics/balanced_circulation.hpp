#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/dynamics/zonal_circulation.hpp"
#include "sim/planet/dynamics/zonal_coupling.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/operators/c_grid.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <cstddef>
#include <vector>

namespace planetsim {

// The climate mode's azonal circulation and balanced surface pressure
// (ADR-0011 §4.4 steps 3–4, §4.6; task M6-04 step C), on the coarse mesh of
// ADR-0009 §12 (the mesh one level down, whose cells are the transport's
// groups).
//
// The zonal means on the coarse mesh are band means (the most of 36, 18,
// 12, 9 or 6 equal bands that each hold three cells) interpolated linearly
// in latitude between the band centres.
//
// On every edge and layer the departures from the zonal mean are in
// frictional-geostrophic balance,
//   r_k u_n − f u_t = G_n,   r_k u_t + f u_n = G_t,
//   G = −∇Φ'_k − R T_k ∇s,   s = ln p_s' (the azonal surface pressure),
// with the normal derivatives from the two cells and the tangential ones
// from the barycentric corner values. r_k is a damping of a few days aloft
// (Gill, 1980) and adds the linearised bulk drag in the bottom layer. The
// column mass divergence vanishes, Σ_k div(μ̂ u_n) = 0: one equation per
// cell for s, a weighted Laplacian (friction) plus a small non-symmetric
// Coriolis part, solved by BiCGSTAB preconditioned with the transport's
// multigrid on its symmetric part. Its null space, a constant, is fixed by
// holding the atmosphere's mass. The balanced surface pressure is the zonal
// circulation's profile times exp(s).
//
// The layer mass fluxes are the zonal circulation's overturning mapped to
// the edges (whose column sum is zero on every edge) plus the azonal part;
// the vertical mass fluxes follow from continuity cell by cell, so a
// layer's mass closes exactly and the column's to the solver's tolerance.
// Deterministic: parallel work runs over fixed blocks with block-ordered
// reductions.
struct BalancedCirculationParameters {
    double free_damping_s = 5.0 * 86'400.0;   // 1 / r aloft
    // Added in the deep tropics, where f vanishes: r_eq exp(−(φ/φ_eq)²).
    // 0 turns it off. (Task M6-04 step C: with 5 days alone, the slow state's
    // 4 K tropical departures drive 100 m/s upper winds.)
    double equatorial_damping_s = 0.25 * 86'400.0;   // 1 / r_eq
    double equatorial_width_deg = 10.0;  // φ_eq
    // The bottom layer adds C_D V g N / (R T): the bulk drag linearised at
    // this wind speed (mean wind and gusts together).
    double drag_speed_m_s = 8.0;
    ZonalDragCoefficients drag;
    double relative_tolerance = 1.0e-10;   // BiCGSTAB: ‖residual‖ / ‖right-hand side‖
    std::size_t max_iterations = 500;
};

struct BalancedCirculationResult {
    std::size_t layers = 0;
    // Per coarse cell:
    std::vector<double> surface_pressure_Pa;   // balanced, the atmosphere's mass held
    std::vector<double> log_departure;         // s, mean zero over the area
    // Layer × coarse edge, kg/s per metre of edge, positive along n_e:
    std::vector<double> mass_flux_kg_m_s;      // overturning + azonal
    std::vector<double> azonal_mass_flux_kg_m_s;
    // (layers + 1) × coarse cell, upward, kg/m²/s: W_0 = 0, and W_N is the
    // column's divergence left by the solver.
    std::vector<double> vertical_mass_flux_kg_m2_s;

    std::size_t iterations = 0;
    double relative_residual = 0.0;
    // max |Σ_k div F_k| over max_k,cell |div F_k|.
    double relative_column_divergence = 0.0;
    std::size_t bands = 0;                 // of the zonal means on the coarse mesh
    double max_band_mean_departure = 0.0;  // max over bands of |band mean of s|
};

class BalancedCirculation {
  public:
    // `zonal` supplies the planet's constants (gravity, gas constant, c_p,
    // rotation, reference pressure) and the layer count.
    BalancedCirculation(const PlanetMesh& mesh, const ZonalCirculationParameters& zonal,
                        BalancedCirculationParameters parameters = {});

    [[nodiscard]] const PlanetMesh& coarse_mesh() const noexcept { return *coarse_; }
    [[nodiscard]] const CGridGeometry& coarse_grid() const noexcept { return grid_; }
    [[nodiscard]] const TransportGraph& graph() const noexcept { return *graph_; }
    [[nodiscard]] const std::vector<std::size_t>& group_of_cell() const noexcept {
        return *group_of_cell_;
    }

    // The slow state's layer temperatures and surface pressure, the
    // dynamics' surface height (fine cells) and the month's zonal
    // circulation.
    [[nodiscard]] BalancedCirculationResult solve(const SlowState& slow,
                                                  const Field2D<double>& dynamics_height_m,
                                                  const SurfaceFractions& fractions,
                                                  const ZonalCirculationSolution& zonal,
                                                  std::size_t worker_count = 1U) const;

  private:
    const PlanetMesh* mesh_;
    const PlanetMesh* coarse_;
    const TransportGraph* graph_;
    const std::vector<std::size_t>* group_of_cell_;
    CGridGeometry grid_;
    ZonalCirculationParameters zonal_;
    BalancedCirculationParameters parameters_;
    std::vector<double> latitude_rad_;   // per coarse cell
};

}  // namespace planetsim
