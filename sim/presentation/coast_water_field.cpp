#include "sim/presentation/coast_water_field.hpp"

#include "sim/planet/terrain/hypsometry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace planetsim::presentation {

CoastWaterField::CoastWaterField(const TerrainRelief& relief, const TerrainSnapshot& terrain)
    : relief_(relief), sea_level_m_(terrain.sea_level_m) {
    const std::size_t cells = relief.mesh().cell_count();
    if (!std::isfinite(sea_level_m_) || terrain.land_fraction.size() != cells ||
        terrain.hypsometry_m.size() != cells * hypsometry_quantile_count)
        throw std::invalid_argument("coast water field requires matching schema-3 terrain");
    connectivity_.assign(cells, 1.0);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const double lowest = terrain.hypsometry_m[cell * hypsometry_quantile_count];
        if (!std::isfinite(lowest) || !std::isfinite(terrain.land_fraction[cell]) ||
            terrain.land_fraction[cell] < 0.0F || terrain.land_fraction[cell] > 1.0F)
            throw std::invalid_argument("coast water field has invalid terrain values");
        if (terrain.land_fraction[cell] == 1.0F && lowest < sea_level_m_) {
            connectivity_[cell] = 0.0;
            depth_m_ = std::max(depth_m_, sea_level_m_ - lowest + 1.0);
            ++depression_cells_;
        }
    }
}

double CoastWaterField::connectivity(const Vec3d& direction_unit, CellId hint) const {
    const CellId cell = relief_.nearest_cell(direction_unit, hint);
    return relief_.blended_cell_scalar(direction_unit, cell, connectivity_);
}

CoastWaterSample CoastWaterField::sample(const Vec3d& direction_unit, CellId hint) const {
    const ReliefSample height = relief_.sample(direction_unit, hint);
    const double kappa = relief_.blended_cell_scalar(direction_unit, height.cell, connectivity_);
    return {(sea_level_m_ - height.elevation_m) - (1.0 - kappa) * depth_m_, height.cell};
}

} // namespace planetsim::presentation
