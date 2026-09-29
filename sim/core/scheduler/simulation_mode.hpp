#pragma once

#include <cstdint>
#include <string_view>

namespace planetsim {

// The three simulation modes of ADR-0001 §4.2. Every mode shares the physical
// clock, coordinates, units and diagnostics; they differ in step and physics.
enum class SimulationMode : std::uint8_t {
    reference,       // explicit weather, short steps; validation and calibration
    climate,         // one orbital sub-step per step (ADR-0006); the mode of play
    weather_window,  // bounded explicit window seeded from climate mode (M10-M12)
};

[[nodiscard]] constexpr std::string_view simulation_mode_name(SimulationMode mode) noexcept {
    switch (mode) {
    case SimulationMode::reference:
        return "reference";
    case SimulationMode::climate:
        return "climate";
    case SimulationMode::weather_window:
        return "weather_window";
    }
    return {};
}

}  // namespace planetsim
