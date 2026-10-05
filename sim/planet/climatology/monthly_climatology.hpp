#pragma once

#include <cstddef>
#include <cstdint>

namespace planetsim {

class PlanetState;

// Monthly climatology (ADR-0001 §4.1, ADR-0006 §7, ADR-0008 §4.6): per cell
// and per sub-step k mod 12, the running mean and population variance of the
// surface temperature (state.forcing().surface_temperature_K) and the means
// of land snow and sea ice, over the climate steps since the last reset.
// Derived and never persisted; spin-up does not contribute.

void reset_climatology(PlanetState& state);

// Adds the state after climate sub-step `substep_index` to its month
// (Welford's update in double). Bit-identical for any worker count.
void accumulate_climatology(PlanetState& state, std::int64_t substep_index,
                            std::size_t worker_count = 1U);

// Adds the circulation of climate sub-step `substep_index` (the bottom-layer
// wind and the sea-level pressure of state.circulation()) to its month, with
// its own sample count (ADR-0011 §4.4 step 5). The circulation must be
// available. Bit-identical for any worker count.
void accumulate_circulation_climatology(PlanetState& state, std::int64_t substep_index,
                                        std::size_t worker_count = 1U);

}  // namespace planetsim
