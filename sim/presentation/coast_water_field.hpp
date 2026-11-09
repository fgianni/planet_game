#pragma once

#include "sim/presentation/terrain_relief.hpp"

#include <cstddef>
#include <vector>

namespace planetsim::presentation {

struct CoastWaterSample {
    double signed_water_m = 0.0;
    CellId cell;
};

// ADR-0030 Amendment 1's continuous signed canonical water field. Positive
// means connected ocean. Read-only presentation data; no tile or shader uses
// it yet. The relief and its mesh must outlive this object.
class CoastWaterField {
public:
    CoastWaterField(const TerrainRelief& relief, const TerrainSnapshot& terrain);

    [[nodiscard]] CoastWaterSample sample(const Vec3d& direction_unit,
                                          CellId hint = CellId{0}) const;
    [[nodiscard]] double signed_water_m(const Vec3d& direction_unit,
                                        CellId hint = CellId{0}) const {
        return sample(direction_unit, hint).signed_water_m;
    }
    [[nodiscard]] double connectivity(const Vec3d& direction_unit,
                                      CellId hint = CellId{0}) const;
    [[nodiscard]] double depression_depth_m() const noexcept { return depth_m_; }
    [[nodiscard]] std::size_t depression_cells() const noexcept { return depression_cells_; }

private:
    const TerrainRelief& relief_;
    std::vector<double> connectivity_;
    double sea_level_m_ = 0.0;
    double depth_m_ = 1.0;
    std::size_t depression_cells_ = 0;
};

} // namespace planetsim::presentation
