#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <vector>

namespace planetsim {

// Time- and zonal-mean statistics of a run of the winds on equal-mass σ
// layers (ADR-0011 V5, V6 and the climate-mode comparison of §4.5): each
// sample adds every cell's eastward and northward wind, temperature and
// surface pressure; the result reduces the time means to latitude bands of
// equal width, area-weighted. "Eddy" means the departure from the band's
// time and zonal mean, transient and stationary together:
//   [u'v'] = [uv] − [u][v],  [v'T'] = [vT] − [v][T].
// Diagnostics only: sequential, in cell order.
struct ZonalProfile {
    std::size_t bands = 0;
    std::size_t layers = 0;
    std::size_t samples = 0;
    std::vector<double> latitude_deg;          // band centres, from the south
    std::vector<double> surface_pressure_Pa;   // per band
    // layer × band, layer 0 at the bottom:
    std::vector<double> eastward_m_s;
    std::vector<double> northward_m_s;
    std::vector<double> temperature_K;
    // ½ of the time variance of u and v, band-averaged: transient eddies only
    std::vector<double> eddy_kinetic_m2_s2;
    std::vector<double> eddy_momentum_flux_m2_s2;  // [u'v']
    std::vector<double> eddy_heat_flux_K_m_s;      // [v'T']
    // Meridional mass streamfunction at the band centres, (layers + 1) ×
    // band: ψ_m = 2π a cos φ Σ_{k<m} [μ_k v_k] (kg/s), northward mass flux
    // below interface m; ψ_0 = 0, and ψ_N ≈ 0 when mass is steady.
    std::vector<double> streamfunction_kg_s;

    [[nodiscard]] double at(const std::vector<double>& field, std::size_t layer,
                            std::size_t band) const {
        return field[layer * bands + band];
    }
};

class ZonalStatistics {
  public:
    ZonalStatistics(const PlanetMesh& mesh, std::size_t layers, std::size_t bands,
                    double gravity_m_s2);

    // east, north and temperature are layer × cell.
    void add(const Field3D<double>& eastward_m_s, const Field3D<double>& northward_m_s,
             const Field3D<double>& temperature_K, const Field2D<double>& surface_pressure_Pa);

    [[nodiscard]] std::size_t samples() const noexcept { return samples_; }
    [[nodiscard]] ZonalProfile profile() const;

  private:
    const PlanetMesh* mesh_;
    std::size_t layers_;
    std::size_t bands_;
    double gravity_m_s2_;
    std::vector<std::size_t> band_of_cell_;
    std::size_t samples_ = 0;
    // Time sums per layer × cell.
    std::vector<double> u_, v_, t_, uu_, vv_, uv_, vt_, mass_v_;
    std::vector<double> surface_pressure_;   // per cell
};

}  // namespace planetsim
