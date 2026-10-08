#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/dynamics/balanced_circulation.hpp"
#include "sim/planet/dynamics/pressure_redistribution.hpp"
#include "sim/planet/dynamics/zonal_circulation.hpp"
#include "sim/planet/dynamics/zonal_coupling.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace planetsim {

// The climate mode's circulation (ADR-0011 §4.4; task M6-04 step D): each
// climate sub-step, the column heating at the slow state, the bands' means,
// the zonal-mean circulation (§4.5, §14) and the azonal balance with the
// balanced surface pressure (§4.6) on the coarse mesh.
//
// Its outputs are derived (ADR-0001 §4.1). Until M6-05 couples the
// transport, nothing reads them back: the slow state is the same with or
// without this process. Every month is solved from the slow state alone
// (the zonal model starts from its own initial guess, never from the last
// month's solution), so a run continued from a snapshot computes the same
// circulation as an uninterrupted one.
//
// A month whose solve fails (the zonal model's iteration caps, or BiCGSTAB)
// keeps the last solved month's circulation and is counted; it never stops
// the run, because nothing in the slow state depends on it yet.
//
// On the cells (§4.4 step 5), each solved month writes the CirculationState:
//  - the balanced p_s: the balance's p_s rests on the groups' smoothed
//    heights, far flatter than the cells' terrain, so it is reduced to sea
//    level at the groups' heights, interpolated, and brought back up to each
//    cell's true height; one global factor holds the atmosphere's mass;
//  - the winds per layer: the zonal-mean ū and the overturning v̄,
//    interpolated in latitude (v̄ is zero at the poles), plus the azonal
//    flow reconstructed on the coarse cells (Perot) and interpolated as a
//    3-D vector;
//  - the vertical mass flux through the top of each layer, interpolated;
//  - the sea-level pressure, reduced from the balanced p_s through the cell's
//    true height at Γ_c, from the surface air temperature T_0 σ_0^(−RΓ_c/g),
//    with the ECMWF rule for the fictitious column below high ground;
//  - the surface stress ρ C_D |V| V of the bottom-layer wind V, with C_D
//    from the land fraction as the balance's drag.
// "Interpolated" means barycentrically from the three coarse cell centres
// whose dual triangle holds the cell's centre.
struct ClimateCirculationParameters {
    std::size_t bands = 36;   // the zonal model's bands (ADR-0011 §4.4 step 1)
    double orography_max_step_m = 800.0;   // ADR-0011 §13, as reference mode
    // c_E, when not the Earth-like fit (the calibration's candidates).
    std::optional<double> eddy_generation_m4_s2_K2;
    // BiCGSTAB's relative residual is 1e-6 here, not the balance's 1e-10:
    // the transport carries only the overturning, whose column fluxes cancel
    // exactly (ADR-0011 §17.6), so the balance's residual reaches only the
    // pressure and the winds, as a column divergence near 1e-6 of the
    // layers'; the 250-year gates need the fewer iterations.
    BalancedCirculationParameters balance = [] {
        BalancedCirculationParameters p;
        p.relative_tolerance = 1.0e-6;
        return p;
    }();
};

struct ClimateCirculationDiagnostics {
    std::size_t months = 0;          // climate sub-steps attempted
    std::size_t failed_months = 0;   // of them, those that kept the previous circulation
    bool last_solved = false;
    std::string last_failure;        // the last failure's message
    // The last solved month:
    ZonalSolutionMethod method = ZonalSolutionMethod::newton;
    std::size_t jacobians = 0;
    std::size_t balance_iterations = 0;
    double relative_column_divergence = 0.0;
    double total_torque_N_m = 0.0;
    double gross_torque_N_m = 0.0;
    // Wall-clock seconds over all months, for performance records only:
    // never read by the simulation.
    double heating_s = 0.0;
    double zonal_s = 0.0;
    double balance_s = 0.0;
    double outputs_s = 0.0;   // the cells' fields
    // The last balanced p_s written to the slow state (coupled runs only,
    // ADR-0011 §17.3).
    std::size_t pressure_writes = 0;
    PressureRedistributionResult last_pressure;
    double worst_pressure_energy_ratio = 0.0;   // max |ΔE| / E over the writes
    double pressure_s = 0.0;   // wall-clock of the writes, performance records only
};

class ClimateCirculation {
  public:
    // Needs an initialised atmosphere with at least one layer.
    ClimateCirculation(const PlanetMesh& mesh, const SlowState& slow,
                       const PlanetParameters& planet, const SurfaceEnergyParameters& surface,
                       const SurfaceFractions& fractions,
                       ClimateCirculationParameters parameters = {});
    ClimateCirculation(const ClimateCirculation&) = delete;
    ClimateCirculation& operator=(const ClimateCirculation&) = delete;
    ClimateCirculation(ClimateCirculation&&) = delete;
    ClimateCirculation& operator=(ClimateCirculation&&) = delete;

    // Solves the month the slow state has just reached, with `insolation`
    // the sub-step's mean (the column heating's shortwave), and writes the
    // cells' circulation to `outputs` (left as it was if the month fails).
    void step(const PlanetState& state, const PlanetParameters& planet,
              const SurfaceEnergyParameters& surface, const Field2D<float>& insolation_W_m2,
              CirculationState& outputs, std::size_t worker_count = 1U);

    [[nodiscard]] const ZonalCirculation& zonal_model() const noexcept { return zonal_model_; }
    [[nodiscard]] const BalancedCirculation& balance_model() const noexcept { return balance_; }
    [[nodiscard]] const Field2D<double>& dynamics_height_m() const noexcept {
        return dynamics_height_m_;
    }
    // Empty until a month has been solved.
    [[nodiscard]] const std::optional<ZonalCirculationSolution>& zonal() const noexcept {
        return zonal_;
    }
    [[nodiscard]] const std::optional<BalancedCirculationResult>& balance() const noexcept {
        return azonal_;
    }
    [[nodiscard]] const ClimateCirculationDiagnostics& diagnostics() const noexcept {
        return diagnostics_;
    }
    // Writes the last solved month's balanced p_s into the slow state, with
    // the energy of the moved air (ADR-0011 §17.3). Coupled runs call it
    // after step().
    void write_balanced_pressure(PlanetState& state, std::size_t worker_count = 1U);

    // The month's transport for the surface step (ADR-0011 §17.1): the
    // zonal-mean overturning's layer mass fluxes and the eddies' conductance
    // (the azonal flow carries no heat, §17.6); active only when this month's
    // circulation was solved (§17.4).
    [[nodiscard]] const CirculationTransport& transport() const noexcept { return transport_; }

  private:
    // The three coarse cells and weights of each cell's interpolation.
    struct Interpolation {
        std::array<std::size_t, 3> group{};
        std::array<double, 3> weight{};
    };

    void write_outputs(const PlanetState& state, const ZonalCirculationSolution& zonal,
                       const BalancedCirculationResult& azonal, CirculationState& outputs,
                       std::size_t worker_count) const;
    void write_transport(const ZonalCirculationSolution& zonal,
                         const BalancedCirculationResult& azonal);

    // Per CSR entry of the coarse graph: its coarse edge, the sign of the
    // edge's normal out of the entry's node, and the edge's latitude.
    struct Face {
        std::size_t edge = 0;
        double sign = 1.0;
        double latitude_deg = 0.0;
    };

    const PlanetMesh* mesh_;
    const SurfaceFractions* fractions_;
    ClimateCirculationParameters parameters_;
    double lapse_rate_K_m_;
    Field2D<double> surface_height_m_;   // the cells' true height (ADR-0010)
    Field2D<double> dynamics_height_m_;
    ZonalCirculation zonal_model_;
    BalancedCirculation balance_;
    PressureRedistribution pressure_;
    std::vector<Interpolation> interpolation_;   // per cell
    std::vector<Face> faces_;                    // per coarse graph CSR entry
    CirculationTransport transport_;
    AtmosphereHeating heating_;
    std::optional<ZonalCirculationSolution> zonal_;
    std::optional<BalancedCirculationResult> azonal_;
    ClimateCirculationDiagnostics diagnostics_;
};

// The pressure p_s at height z reduced to sea level through a fictitious
// column of lapse rate Γ from the surface air temperature T_s, with the
// ECMWF rule for that column's temperature (Trenberth, Berry and Buja, 1993):
// over cold ground (T_s < 255 K) it is warmed halfway to 255 K, and where
// its sea-level temperature would pass 290.5 K the lapse rate is reduced so
// that it does not. Without the rule, cold high plateaus reduce to more than
// 1,300 hPa. Linear in p_s.
[[nodiscard]] double reduced_to_sea_level(double ps, double surface_air_K, double z,
                                          double lapse_rate_K_m, double g, double gas);

// The surface air temperature from the bottom equal-mass layer's, along
// Γ_c from the layer's mid-σ to the surface.
[[nodiscard]] double surface_air_temperature(double bottom_layer_K, std::size_t layers,
                                             double lapse_rate_K_m, double g, double gas);

// Whether the mesh resolves the zonal model's bands: each must hold a cell
// centre (ADR-0011 §4.4 keeps the band count independent of the mesh, so
// the coarsest meshes, L0–L2 for 36 bands, have no climate circulation).
[[nodiscard]] bool climate_circulation_resolves(const PlanetMesh& mesh,
                                                const ClimateCirculationParameters& parameters = {});

// Climate mode runs `circulation` at the start of each climate step, from
// the slow state the step starts from (ADR-0011 §17.2): register it before
// the surface. It sets the sub-step's mean insolation first. `planet` and
// `surface` are read at each step, so commands that change the star's
// luminosity take effect.
// With `first` false it runs after the processes already registered, on the
// month they have just stepped, and only diagnoses (task M6-04).
void register_climate_circulation(Scheduler& scheduler, PlanetState& state,
                                  const PlanetParameters& planet,
                                  const SurfaceEnergyParameters& surface,
                                  ClimateCirculation& circulation, std::size_t worker_count,
                                  bool first);

}  // namespace planetsim
