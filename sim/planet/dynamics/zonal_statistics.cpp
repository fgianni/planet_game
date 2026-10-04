#include "sim/planet/dynamics/zonal_statistics.hpp"

#include "sim/planet/coordinates/local_tangent_basis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {

ZonalStatistics::ZonalStatistics(const PlanetMesh& mesh, std::size_t layers, std::size_t bands,
                                 double gravity_m_s2)
    : mesh_(&mesh), layers_(layers), bands_(bands), gravity_m_s2_(gravity_m_s2),
      band_of_cell_(mesh.cell_count()) {
    if (layers == 0U || bands == 0U || !(gravity_m_s2 > 0.0)) {
        throw std::invalid_argument("zonal statistics need layers, bands and gravity");
    }
    constexpr double pi = std::numbers::pi_v<double>;
    for (const auto& cell : mesh.cells()) {
        const double latitude = latitude_rad(cell.center_unit);
        const auto band = static_cast<std::ptrdiff_t>((latitude + pi / 2.0) /
                                                      (pi / static_cast<double>(bands)));
        band_of_cell_[cell.id.to_index()] = static_cast<std::size_t>(
            std::clamp<std::ptrdiff_t>(band, 0, static_cast<std::ptrdiff_t>(bands) - 1));
    }
    const std::size_t size = layers * mesh.cell_count();
    for (auto* sums : {&u_, &v_, &t_, &uu_, &vv_, &uv_, &vt_, &mass_v_}) {
        sums->assign(size, 0.0);
    }
    surface_pressure_.assign(mesh.cell_count(), 0.0);
}

void ZonalStatistics::add(const Field3D<double>& eastward_m_s,
                          const Field3D<double>& northward_m_s,
                          const Field3D<double>& temperature_K,
                          const Field2D<double>& surface_pressure_Pa) {
    const std::size_t cells = mesh_->cell_count();
    for (const auto* field : {&eastward_m_s, &northward_m_s, &temperature_K}) {
        if (field->layer_count() != layers_ || field->cell_count() != cells) {
            throw std::invalid_argument("zonal statistics: field shape mismatch");
        }
    }
    const double layer_fraction = 1.0 / static_cast<double>(layers_);
    for (std::size_t k = 0; k < layers_; ++k) {
        const auto u = eastward_m_s.layer(k);
        const auto v = northward_m_s.layer(k);
        const auto t = temperature_K.layer(k);
        for (std::size_t i = 0; i < cells; ++i) {
            const std::size_t index = k * cells + i;
            const double mass = layer_fraction * surface_pressure_Pa.values()[i] / gravity_m_s2_;
            u_[index] += u[i];
            v_[index] += v[i];
            t_[index] += t[i];
            uu_[index] += u[i] * u[i];
            vv_[index] += v[i] * v[i];
            uv_[index] += u[i] * v[i];
            vt_[index] += v[i] * t[i];
            mass_v_[index] += mass * v[i];
        }
    }
    for (std::size_t i = 0; i < cells; ++i) {
        surface_pressure_[i] += surface_pressure_Pa.values()[i];
    }
    ++samples_;
}

ZonalProfile ZonalStatistics::profile() const {
    ZonalProfile result;
    result.bands = bands_;
    result.layers = layers_;
    result.samples = samples_;
    const std::size_t size = layers_ * bands_;
    for (auto* field : {&result.eastward_m_s, &result.northward_m_s, &result.temperature_K,
                        &result.eddy_kinetic_m2_s2, &result.eddy_momentum_flux_m2_s2,
                        &result.eddy_heat_flux_K_m_s}) {
        field->assign(size, 0.0);
    }
    result.surface_pressure_Pa.assign(bands_, 0.0);
    result.streamfunction_kg_s.assign((layers_ + 1U) * bands_, 0.0);
    result.latitude_deg.resize(bands_);
    const double width = 180.0 / static_cast<double>(bands_);
    for (std::size_t b = 0; b < bands_; ++b) {
        result.latitude_deg[b] = -90.0 + width * (static_cast<double>(b) + 0.5);
    }
    if (samples_ == 0U) {
        return result;
    }

    // Band means of the time means (area-weighted).
    const std::size_t cells = mesh_->cell_count();
    const double n = static_cast<double>(samples_);
    std::vector<double> area(bands_, 0.0);
    std::vector<double> uu(size, 0.0), vv(size, 0.0), uv(size, 0.0), vt(size, 0.0);
    std::vector<double> mass_v(size, 0.0), eddy_square(size, 0.0);
    for (const auto& cell : mesh_->cells()) {
        const std::size_t i = cell.id.to_index();
        const std::size_t b = band_of_cell_[i];
        const double a = cell.area_m2;
        area[b] += a;
        result.surface_pressure_Pa[b] += a * surface_pressure_[i] / n;
        for (std::size_t k = 0; k < layers_; ++k) {
            const std::size_t index = k * cells + i;
            const std::size_t out = k * bands_ + b;
            const double mean_u = u_[index] / n;
            const double mean_v = v_[index] / n;
            result.eastward_m_s[out] += a * mean_u;
            result.northward_m_s[out] += a * mean_v;
            result.temperature_K[out] += a * t_[index] / n;
            uu[out] += a * uu_[index] / n;
            vv[out] += a * vv_[index] / n;
            uv[out] += a * uv_[index] / n;
            vt[out] += a * vt_[index] / n;
            mass_v[out] += a * mass_v_[index] / n;
            // Transient: the time variance at each cell.
            eddy_square[out] += a * (uu_[index] / n - mean_u * mean_u + vv_[index] / n -
                                     mean_v * mean_v);
        }
    }
    constexpr double pi = std::numbers::pi_v<double>;
    for (std::size_t b = 0; b < bands_; ++b) {
        const double a = area[b];
        if (!(a > 0.0)) {
            continue;
        }
        result.surface_pressure_Pa[b] /= a;
        const double circumference =
            2.0 * pi * mesh_->radius_m() * std::cos(result.latitude_deg[b] * pi / 180.0);
        double psi = 0.0;
        for (std::size_t k = 0; k < layers_; ++k) {
            const std::size_t out = k * bands_ + b;
            for (auto* field : {&result.eastward_m_s, &result.northward_m_s,
                                &result.temperature_K}) {
                (*field)[out] /= a;
            }
            const double u = result.eastward_m_s[out];
            const double v = result.northward_m_s[out];
            result.eddy_kinetic_m2_s2[out] = 0.5 * eddy_square[out] / a;
            result.eddy_momentum_flux_m2_s2[out] = uv[out] / a - u * v;
            result.eddy_heat_flux_K_m_s[out] = vt[out] / a - v * result.temperature_K[out];
            psi += circumference * mass_v[out] / a;
            result.streamfunction_kg_s[(k + 1U) * bands_ + b] = psi;
        }
    }
    return result;
}

}  // namespace planetsim
