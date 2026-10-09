#include "sim/planet/dynamics/climate_circulation.hpp"

#include "sim/core/math/vec3d.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/climatology/monthly_climatology.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/dynamics/orography.hpp"
#include "sim/planet/operators/c_grid.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

[[nodiscard]] Field2D<double> true_height(const PlanetMesh& mesh, const SlowState& slow,
                                          const SurfaceFractions& fractions) {
    Field2D<double> height;
    compute_surface_height(mesh, slow, fractions, height);
    return height;
}

[[nodiscard]] ZonalCirculationParameters checked_zonal_parameters(
    const PlanetParameters& planet, const SurfaceEnergyParameters& surface,
    const ClimateCirculationParameters& parameters) {
    if (surface.atmosphere.layer_count == 0U) {
        throw std::invalid_argument("the climate circulation needs an atmosphere");
    }
    if (parameters.bands < 2U || !(parameters.orography_max_step_m > 0.0)) {
        throw std::invalid_argument("invalid climate circulation parameters");
    }
    auto zonal = zonal_circulation_parameters(planet, surface.atmosphere, parameters.bands);
    if (parameters.eddy_generation_m4_s2_K2) {
        if (!(*parameters.eddy_generation_m4_s2_K2 > 0.0)) {
            throw std::invalid_argument("c_E must be positive (ADR-0011 §4.8)");
        }
        zonal.eddy_generation_m4_s2_K2 = *parameters.eddy_generation_m4_s2_K2;
    }
    return zonal;
}

// Linear interpolation in latitude of values at increasing latitudes, held
// constant beyond the ends.
[[nodiscard]] double interpolate_latitude(std::span<const double> latitude_deg,
                                          std::span<const double> values, double at_deg) {
    if (at_deg <= latitude_deg.front()) {
        return values.front();
    }
    if (at_deg >= latitude_deg.back()) {
        return values.back();
    }
    const auto upper = static_cast<std::size_t>(
        std::upper_bound(latitude_deg.begin(), latitude_deg.end(), at_deg) - latitude_deg.begin());
    const std::size_t lower = upper - 1U;
    const double fraction =
        (at_deg - latitude_deg[lower]) / (latitude_deg[upper] - latitude_deg[lower]);
    return values[lower] + fraction * (values[upper] - values[lower]);
}

[[nodiscard]] EdgeId edge_id(std::size_t index) noexcept {
    return EdgeId{static_cast<EdgeId::value_type>(index)};
}

}  // namespace

double reduced_to_sea_level(double ps, double surface_air_K, double z,
                                          double lapse_rate_K_m, double g, double gas) {
    constexpr double cold_K = 255.0;
    constexpr double warm_K = 290.5;
    double t = surface_air_K;
    double lapse = lapse_rate_K_m;
    if (t < cold_K) {
        t = 0.5 * (t + cold_K);
    }
    if (z > 0.0 && t + lapse * z > warm_K) {
        if (t <= warm_K) {
            lapse = (warm_K - t) / z;
        } else {
            t = 0.5 * (t + warm_K);
            lapse = 0.0;
        }
    }
    if (lapse > 0.0) {
        return ps * std::pow(1.0 + lapse * z / t, g / (gas * lapse));
    }
    return ps * std::exp(g * z / (gas * t));
}


double surface_air_temperature(double bottom_layer_K, std::size_t layers, double lapse_rate_K_m,
                               double g, double gas) {
    const double bottom_sigma = 1.0 - 0.5 / static_cast<double>(layers);   // equal-mass layers
    return bottom_layer_K * std::pow(bottom_sigma, -gas * lapse_rate_K_m / g);
}

ClimateCirculation::ClimateCirculation(const PlanetMesh& mesh, const SlowState& slow,
                                       const PlanetParameters& planet,
                                       const SurfaceEnergyParameters& surface,
                                       const SurfaceFractions& fractions,
                                       ClimateCirculationParameters parameters)
    : mesh_(&mesh), fractions_(&fractions), parameters_(parameters),
      lapse_rate_K_m_(surface.atmosphere.critical_lapse_rate_K_m),
      surface_height_m_(true_height(mesh, slow, fractions)),
      dynamics_height_m_(std::move(
          limit_dynamics_orography_steps(mesh, surface_height_m_, parameters.orography_max_step_m)
              .height_m)),
      zonal_model_(checked_zonal_parameters(planet, surface, parameters)),
      balance_(mesh, zonal_model_.parameters(), parameters.balance),
      pressure_(mesh, balance_.graph(), balance_.group_of_cell(), surface_height_m_,
                zonal_model_.parameters().gravity_m_s2,
                zonal_model_.parameters().gas_constant_J_kg_K,
                zonal_model_.parameters().heat_capacity_J_kg_K) {
    if (slow.atmosphere_layer_count() != surface.atmosphere.layer_count) {
        throw std::invalid_argument("the climate circulation needs an initialised atmosphere");
    }
    if (!(lapse_rate_K_m_ >= 0.0)) {
        throw std::invalid_argument("the climate circulation needs Γ_c ≥ 0");
    }
    // Each cell's coarse dual triangle: among the corners of its group, the
    // one whose three centres hold the cell's centre (the largest smallest
    // barycentric weight, which is ≥ 0 inside; negative weights are clipped
    // only where rounding or the agglomeration puts a centre just outside).
    const PlanetMesh& coarse = balance_.coarse_mesh();
    const CGridGeometry& grid = balance_.coarse_grid();
    const auto& group_of = balance_.group_of_cell();
    interpolation_.resize(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        const std::size_t i = cell.id.to_index();
        const std::size_t g = group_of[i];
        const Vec3d& x = cell.center_unit;
        Interpolation best;
        best.group = {g, g, g};
        best.weight = {1.0, 0.0, 0.0};
        double best_minimum = -std::numeric_limits<double>::infinity();
        for (const CornerIndex v : coarse.cell_corners(CellId{static_cast<CellId::value_type>(g)})) {
            const auto& corner = grid.corners()[v];
            const Vec3d& a = coarse.cells()[corner.cell[0].to_index()].center_unit;
            const Vec3d& b = coarse.cells()[corner.cell[1].to_index()].center_unit;
            const Vec3d& c = coarse.cells()[corner.cell[2].to_index()].center_unit;
            std::array<double, 3> lambda{dot(x, cross(b, c)), dot(x, cross(c, a)),
                                         dot(x, cross(a, b))};
            const double sum = lambda[0] + lambda[1] + lambda[2];
            if (sum == 0.0) {
                continue;
            }
            for (double& value : lambda) {
                value /= sum;
            }
            const double minimum = std::min({lambda[0], lambda[1], lambda[2]});
            if (minimum > best_minimum) {
                best_minimum = minimum;
                for (std::size_t m = 0; m < 3U; ++m) {
                    best.group[m] = corner.cell[m].to_index();
                    best.weight[m] = std::max(0.0, lambda[m]);
                }
            }
        }
        const double total = best.weight[0] + best.weight[1] + best.weight[2];
        for (double& value : best.weight) {
            value /= total;
        }
        interpolation_[i] = best;
    }
    // The coarse graph's faces: its CSR entries follow each coarse cell's
    // edges in order (mesh_transport_graph).
    const TransportGraph& graph = balance_.graph();
    faces_.resize(graph.neighbour.size());
    for (std::size_t a = 0; a < graph.size(); ++a) {
        const auto cell_edges = coarse.cell_edges(CellId{static_cast<CellId::value_type>(a)});
        if (cell_edges.size() != graph.offset[a + 1U] - graph.offset[a]) {
            throw std::logic_error("the coarse graph does not follow the coarse mesh's edges");
        }
        for (std::size_t q = 0; q < cell_edges.size(); ++q) {
            const std::size_t k = graph.offset[a] + q;
            const std::size_t e = cell_edges[q].edge.to_index();
            if (cell_edges[q].neighbor.to_index() != graph.neighbour[k]) {
                throw std::logic_error("the coarse graph does not follow the coarse mesh's edges");
            }
            faces_[k].edge = e;
            faces_[k].sign = coarse.edge(edge_id(e)).first_cell.to_index() == a ? 1.0 : -1.0;
            faces_[k].latitude_deg = latitude_rad(grid.edges()[e].midpoint_unit) * 180.0 /
                                     std::numbers::pi_v<double>;
        }
    }
    const double g = zonal_model_.parameters().gravity_m_s2;
    transport_.transport.layers = zonal_model_.parameters().layer_count;
    transport_.surface_geopotential_m2_s2.resize(mesh.cell_count());
    for (std::size_t i = 0; i < mesh.cell_count(); ++i) {
        transport_.surface_geopotential_m2_s2[i] = g * dynamics_height_m_[i];
    }
}

void ClimateCirculation::write_balanced_pressure(PlanetState& state, std::size_t worker_count) {
    const auto& circulation = state.circulation();
    if (!circulation.available()) {
        throw std::logic_error("no balanced surface pressure to write");
    }
    const auto start = std::chrono::steady_clock::now();
    const auto result = pressure_.apply(state.slow(), circulation.balanced_surface_pressure_Pa,
                                        worker_count);
    diagnostics_.pressure_s +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ++diagnostics_.pressure_writes;
    diagnostics_.last_pressure = result;
    diagnostics_.worst_pressure_energy_ratio =
        std::max(diagnostics_.worst_pressure_energy_ratio,
                 std::abs(result.energy_change_J) / result.energy_J);
}

void ClimateCirculation::write_transport(const ZonalCirculationSolution& zonal,
                                         const BalancedCirculationResult& azonal) {
    const TransportGraph& graph = balance_.graph();
    const PlanetMesh& coarse = balance_.coarse_mesh();
    const auto& p = zonal_model_.parameters();
    const std::size_t n = p.layer_count;
    const std::size_t entries = graph.neighbour.size();
    const std::size_t edges = coarse.edge_count();
    // D̄: the layers' mean of the zonal model's D_k at each boundary.
    std::vector<double> diffusivity(zonal.bands - 1U, 0.0);
    for (std::size_t b = 0; b + 1U < zonal.bands; ++b) {
        for (std::size_t k = 0; k < n; ++k) {
            diffusivity[b] += zonal.at_boundary(zonal.heat_diffusivity_m2_s, k, b) /
                              static_cast<double>(n);
        }
    }
    auto& out = transport_.transport;
    out.conductance_W_K.resize(entries);
    out.outflow_kg_s.resize(n * entries);
    out.heat_capacity_J_kg_K = p.heat_capacity_J_kg_K;
    for (std::size_t a = 0; a < graph.size(); ++a) {
        for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
            const Face& face = faces_[k];
            const std::size_t b = graph.neighbour[k];
            const double column_mass =
                0.5 * (azonal.surface_pressure_Pa[a] + azonal.surface_pressure_Pa[b]) /
                p.gravity_m_s2;
            const double d = std::max(
                0.0, interpolate_latitude(zonal.boundary_latitude_deg, diffusivity,
                                          face.latitude_deg));
            out.conductance_W_K[k] = p.heat_capacity_J_kg_K * column_mass * d * graph.weight[k];
            // The overturning only (ADR-0011 §17.6): the azonal balance has no
            // thermodynamic equation, so its divergent flow is not bounded by
            // any heating and carries no heat.
            const double length = coarse.edge(edge_id(face.edge)).length_m;
            for (std::size_t l = 0; l < n; ++l) {
                const std::size_t at = l * edges + face.edge;
                out.outflow_kg_s[l * entries + k] =
                    face.sign * (azonal.mass_flux_kg_m_s[at] - azonal.azonal_mass_flux_kg_m_s[at]) *
                    length;
            }
        }
    }
    transport_.active = true;
}

void ClimateCirculation::step(const PlanetState& state, const PlanetParameters& planet,
                              const SurfaceEnergyParameters& surface,
                              const Field2D<float>& insolation_W_m2, CirculationState& outputs,
                              std::size_t worker_count) {
    ++diagnostics_.months;
    try {
        using clock = std::chrono::steady_clock;
        const auto seconds = [](clock::time_point from, clock::time_point to) {
            return std::chrono::duration<double>(to - from).count();
        };
        const auto start = clock::now();
        compute_atmosphere_heating(state, planet, surface, *fractions_, insolation_W_m2, heating_,
                                   worker_count);
        const auto forcing =
            zonal_forcing_from_state(*mesh_, state.slow(), heating_, dynamics_height_m_,
                                     *fractions_, parameters_.bands, parameters_.balance.drag);
        const auto heated = clock::now();
        auto zonal = zonal_model_.solve(forcing, worker_count);
        const auto solved = clock::now();
        auto azonal = balance_.solve(state.slow(), dynamics_height_m_, *fractions_, zonal,
                                     worker_count);
        const auto balanced = clock::now();
        write_outputs(state, zonal, azonal, outputs, worker_count);
        write_transport(zonal, azonal);
        const auto written = clock::now();
        diagnostics_.heating_s += seconds(start, heated);
        diagnostics_.zonal_s += seconds(heated, solved);
        diagnostics_.balance_s += seconds(solved, balanced);
        diagnostics_.outputs_s += seconds(balanced, written);
        diagnostics_.method = zonal.method;
        diagnostics_.jacobians = zonal.statistics.jacobians;
        diagnostics_.balance_iterations = azonal.iterations;
        diagnostics_.relative_column_divergence = azonal.relative_column_divergence;
        diagnostics_.total_torque_N_m = zonal.total_torque_N_m;
        diagnostics_.gross_torque_N_m = zonal.gross_torque_N_m;
        zonal_ = std::move(zonal);
        azonal_ = std::move(azonal);
        diagnostics_.last_solved = true;
    } catch (const std::runtime_error& error) {
        transport_.active = false;
        ++diagnostics_.failed_months;
        diagnostics_.last_solved = false;
        diagnostics_.last_failure = error.what();
    }
}

void ClimateCirculation::write_outputs(const PlanetState& state,
                                       const ZonalCirculationSolution& zonal,
                                       const BalancedCirculationResult& azonal,
                                       CirculationState& outputs, std::size_t worker_count) const {
    const PlanetMesh& mesh = *mesh_;
    const PlanetMesh& coarse = balance_.coarse_mesh();
    const CGridGeometry& grid = balance_.coarse_grid();
    const TransportGraph& graph = balance_.graph();
    const auto& group_of = balance_.group_of_cell();
    const SlowState& slow = state.slow();
    const auto& p = zonal_model_.parameters();
    const std::size_t n = p.layer_count;
    const std::size_t cells = mesh.cell_count();
    const std::size_t groups = coarse.cell_count();
    const std::size_t edges = coarse.edge_count();
    const double g = p.gravity_m_s2;
    const double gas = p.gas_constant_J_kg_K;
    const double n_d = static_cast<double>(n);
    constexpr double degrees = 180.0 / std::numbers::pi_v<double>;

    // 1. The azonal flow as 3-D vectors on the coarse cells, per layer.
    std::vector<Vec3d> azonal_vector(n * groups);
    {
        EdgeField<double> velocity(edges, 0.0);
        Field2D<double> east(groups, 0.0);
        Field2D<double> north(groups, 0.0);
        for (std::size_t k = 0; k < n; ++k) {
            for (std::size_t e = 0; e < edges; ++e) {
                const auto& edge = coarse.edge(edge_id(e));
                const double mu = 0.5 *
                                  (azonal.surface_pressure_Pa[edge.first_cell.to_index()] +
                                   azonal.surface_pressure_Pa[edge.second_cell.to_index()]) /
                                  (g * n_d);
                velocity[edge_id(e)] = azonal.azonal_mass_flux_kg_m_s[k * edges + e] / mu;
            }
            reconstruct_cell_vector(coarse, grid, velocity, east, north, worker_count);
            for (std::size_t c = 0; c < groups; ++c) {
                const auto& cell = coarse.cells()[c];
                azonal_vector[k * groups + c] = cell.east_unit * east[c] + cell.north_unit * north[c];
            }
        }
    }

    // 2. The balanced p_s. The balance's p_s rests on the coarse groups'
    // smoothed (dynamics) heights, which are far flatter than the cells' own
    // terrain, so it cannot be shared among a group's cells directly: a
    // mountain group would keep the air of its lowland neighbours. Instead
    // each group's p_s is reduced to sea level at its own height and
    // temperature, that pressure is interpolated to the cells and brought
    // back up to each cell's true height, and the atmosphere's mass is held
    // exactly by one global factor.
    const auto surface_air = [&](double bottom_layer_K) {
        return surface_air_temperature(bottom_layer_K, n, lapse_rate_K_m_, g, gas);
    };
    std::vector<double> group_height(groups, 0.0);
    std::vector<double> group_temperature(groups, 0.0);
    std::vector<double> group_mass(groups, 0.0);
    double slow_mass = 0.0;
    for (std::size_t i = 0; i < cells; ++i) {
        const std::size_t c = group_of[i];
        const double a = mesh.cells()[i].area_m2;
        const double ps = slow.atmosphere_surface_pressure_Pa[i];
        group_height[c] += a * dynamics_height_m_[i];
        group_temperature[c] += a * ps * slow.atmosphere_temperature_K.layer(0)[i];
        group_mass[c] += a * ps;
        slow_mass += a * ps;
    }
    std::vector<double> group_sea_level(groups, 0.0);
    for (std::size_t c = 0; c < groups; ++c) {
        group_sea_level[c] = reduced_to_sea_level(
            azonal.surface_pressure_Pa[c], surface_air(group_temperature[c] / group_mass[c]),
            group_height[c] / graph.area_m2[c], lapse_rate_K_m_, g, gas);
    }
    Field2D<double> balanced(cells, 0.0);
    const double balanced_mass = reduce_deterministic_blocks<double>(
        mesh.blocks(), worker_count, 0.0,
        [&](std::size_t, const CellBlock& block) {
            double sum = 0.0;
            for (std::size_t i = block.begin; i < block.end; ++i) {
                const auto& at = interpolation_[i];
                double sea_level = 0.0;
                for (std::size_t m = 0; m < 3U; ++m) {
                    sea_level += at.weight[m] * group_sea_level[at.group[m]];
                }
                const double factor = reduced_to_sea_level(
                    1.0, surface_air(slow.atmosphere_temperature_K.layer(0)[i]),
                    surface_height_m_[i], lapse_rate_K_m_, g, gas);
                balanced[i] = sea_level / factor;
                sum += mesh.cells()[i].area_m2 * balanced[i];
            }
            return sum;
        },
        [](double x, double y) { return x + y; });
    const double scale = slow_mass / balanced_mass;
    for (std::size_t i = 0; i < cells; ++i) {
        balanced[i] *= scale;
    }

    // 3. The zonal means' latitudes: ū at the band centres, v̄ at the
    // boundaries and zero at the poles.
    std::vector<double> boundary_latitude;
    boundary_latitude.reserve(zonal.bands + 1U);
    boundary_latitude.push_back(-90.0);
    boundary_latitude.insert(boundary_latitude.end(), zonal.boundary_latitude_deg.begin(),
                             zonal.boundary_latitude_deg.end());
    boundary_latitude.push_back(90.0);
    std::vector<double> northward((zonal.bands + 1U) * n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t b = 0; b + 1U < zonal.bands; ++b) {
            northward[k * (zonal.bands + 1U) + b + 1U] = zonal.at_boundary(zonal.northward_m_s, k, b);
        }
    }

    // 4. Every cell: winds, vertical flux, sea-level pressure and stress.
    if (outputs.eastward_wind_m_s.layer_count() != n || outputs.eastward_wind_m_s.cell_count() != cells) {
        outputs.eastward_wind_m_s = Field3D<float>(n, cells, 0.0F);
        outputs.northward_wind_m_s = Field3D<float>(n, cells, 0.0F);
        outputs.vertical_mass_flux_kg_m2_s = Field3D<float>(n, cells, 0.0F);
        outputs.sea_level_pressure_Pa = Field2D<float>(cells, 0.0F);
        outputs.surface_stress_east_N_m2 = Field2D<float>(cells, 0.0F);
        outputs.surface_stress_north_N_m2 = Field2D<float>(cells, 0.0F);
    }
    const auto& drag = parameters_.balance.drag;
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t i = block.begin; i < block.end; ++i) {
                const auto& cell = mesh.cells()[i];
                const auto& at = interpolation_[i];
                const double latitude = latitude_rad(cell.center_unit) * degrees;
                double bottom_east = 0.0;
                double bottom_north = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    Vec3d v{0.0, 0.0, 0.0};
                    for (std::size_t m = 0; m < 3U; ++m) {
                        v = v + azonal_vector[k * groups + at.group[m]] * at.weight[m];
                    }
                    const double east =
                        interpolate_latitude(zonal.latitude_deg,
                                             std::span<const double>(zonal.eastward_m_s)
                                                 .subspan(k * zonal.bands, zonal.bands),
                                             latitude) +
                        dot(v, cell.east_unit);
                    const double north =
                        interpolate_latitude(boundary_latitude,
                                             std::span<const double>(northward)
                                                 .subspan(k * (zonal.bands + 1U), zonal.bands + 1U),
                                             latitude) +
                        dot(v, cell.north_unit);
                    outputs.eastward_wind_m_s.layer(k)[i] = static_cast<float>(east);
                    outputs.northward_wind_m_s.layer(k)[i] = static_cast<float>(north);
                    double w = 0.0;
                    for (std::size_t m = 0; m < 3U; ++m) {
                        w += at.weight[m] * azonal.vertical_mass_flux_kg_m2_s[(k + 1U) * groups +
                                                                              at.group[m]];
                    }
                    outputs.vertical_mass_flux_kg_m2_s.layer(k)[i] = static_cast<float>(w);
                    if (k == 0U) {
                        bottom_east = east;
                        bottom_north = north;
                    }
                }
                const double ps = balanced[i];
                const double t_air = surface_air(slow.atmosphere_temperature_K.layer(0)[i]);
                const double z = surface_height_m_[i];
                const double sea_level = reduced_to_sea_level(ps, t_air, z, lapse_rate_K_m_, g, gas);
                outputs.sea_level_pressure_Pa[i] = static_cast<float>(sea_level);
                const double land = std::clamp(static_cast<double>(fractions_->land_fraction[i]),
                                               0.0, 1.0);
                const double coefficient = land * drag.land + (1.0 - land) * drag.ocean;
                const double density = ps / (gas * t_air);
                const double speed = std::hypot(bottom_east, bottom_north);
                outputs.surface_stress_east_N_m2[i] =
                    static_cast<float>(density * coefficient * speed * bottom_east);
                outputs.surface_stress_north_N_m2[i] =
                    static_cast<float>(density * coefficient * speed * bottom_north);
            }
        });
    outputs.balanced_surface_pressure_Pa = std::move(balanced);
}

bool climate_circulation_resolves(const PlanetMesh& mesh,
                                  const ClimateCirculationParameters& parameters) {
    if (parameters.bands < 2U) {
        return false;
    }
    std::vector<bool> held(parameters.bands, false);
    for (const std::size_t band : zonal_band_of_cells(mesh, parameters.bands)) {
        held[band] = true;
    }
    return std::find(held.begin(), held.end(), false) == held.end();
}

void register_climate_circulation(Scheduler& scheduler, PlanetState& state,
                                  const PlanetParameters& planet,
                                  const SurfaceEnergyParameters& surface,
                                  ClimateCirculation& circulation, std::size_t worker_count,
                                  bool first) {
    scheduler.register_process(
        {"climate_circulation", SimulationMode::climate, 0},
        [&state, &planet, &surface, &circulation, worker_count, first](const StepContext& context) {
            if (first) {
                // For the surface step too, which then does not set it.
                update_substep_mean_insolation(state, planet, *context.substep, worker_count);
            }
            circulation.step(state, planet, surface, state.forcing().substep_mean_insolation_W_m2,
                             state.circulation(), worker_count);
            if (first && circulation.diagnostics().last_solved) {
                // The balanced p_s in the slow state (ADR-0011 §17.3); a failed
                // month keeps its p_s (§17.4).
                circulation.write_balanced_pressure(state, worker_count);
            }
            if (circulation.diagnostics().last_solved) {
                accumulate_circulation_climatology(state, context.substep->index, worker_count);
            }
        });
}

}  // namespace planetsim
