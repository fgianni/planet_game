#pragma once

#include <algorithm>

namespace planetsim {

// Sub-grid condensation (ADR-0025 §4.1; Smith 1990): a layer's saturation
// deficit s = q_t − q_sat varies about its mean with a triangular
// distribution of half-width b. With Q = s / b the equilibrium cloud water
// and the cloudy area are
//   Q ≤ −1:      q_l* = 0,                    area 0
//   −1 < Q ≤ 0:  q_l* = b (1 + Q)³ / 6,        area (1 + Q)² / 2
//   0 < Q < 1:   q_l* = b (Q + (1 − Q)³ / 6),  area 1 − (1 − Q)² / 2
//   Q ≥ 1:       q_l* = b Q = s,               area 1.
// dq_l*/ds is the area, and dq_l*/db = (q_l* − s · area) / b. b ≤ 0 is
// saturation adjustment: q_l* = max(0, s), the area a step at s = 0.
// q_l* is continuous and once differentiable in s and b.
struct SubgridCloud {
    double water_kg_kg = 0.0;   // q_l*
    double area = 0.0;          // the cloudy share of the layer, 0–1
    double d_water_d_width = 0.0;
};

[[nodiscard]] constexpr SubgridCloud subgrid_cloud(double deficit_kg_kg,
                                                   double width_kg_kg) noexcept {
    SubgridCloud cloud;
    if (!(width_kg_kg > 0.0)) {
        cloud.water_kg_kg = std::max(0.0, deficit_kg_kg);
        cloud.area = deficit_kg_kg > 0.0 ? 1.0 : 0.0;
        return cloud;
    }
    const double b = width_kg_kg;
    const double q = deficit_kg_kg / b;
    if (q <= -1.0) {
        return cloud;
    }
    if (q <= 0.0) {
        const double p = 1.0 + q;
        cloud.water_kg_kg = b * p * p * p / 6.0;
        cloud.area = 0.5 * p * p;
    } else if (q < 1.0) {
        const double m = 1.0 - q;
        cloud.water_kg_kg = b * (q + m * m * m / 6.0);
        cloud.area = 1.0 - 0.5 * m * m;
    } else {
        cloud.water_kg_kg = deficit_kg_kg;
        cloud.area = 1.0;
    }
    cloud.d_water_d_width = (cloud.water_kg_kg - deficit_kg_kg * cloud.area) / b;
    return cloud;
}

}  // namespace planetsim
