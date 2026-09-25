cmake_minimum_required(VERSION 3.20)

project(
    PlanetaryCivilizationSimulator
    VERSION 0.1.0
    LANGUAGES CXX
)

option(PLANETSIM_BUILD_TESTS "Build the headless PlanetSim test suite" ON)
option(PLANETSIM_BUILD_GODOT_EXTENSION "Build the optional Godot 4 adapter" OFF)
option(PLANETSIM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PLANETSIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerWarnings.cmake)

add_library(PlanetSim STATIC
    sim/core/serialization/state_snapshot.cpp
    sim/core/scheduler/simulation_clock.cpp
    sim/planet/coordinates/local_tangent_basis.cpp
    sim/planet/mesh/icosphere.cpp
    sim/planet/mesh/mesh_diagnostics.cpp
    sim/planet/mesh/planet_mesh.cpp
    sim/planet/orbit/orbit_state.cpp
    sim/planet/orbit/solar_diagnostics.cpp
    sim/planet/orbit/solar_forcing.cpp
    sim/planet/planet_parameters.cpp
    sim/planet/planet_state.cpp
)
add_library(PlanetSim::PlanetSim ALIAS PlanetSim)

target_include_directories(PlanetSim
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
set_target_properties(PlanetSim PROPERTIES POSITION_INDEPENDENT_CODE ON)
planetsim_set_project_warnings(PlanetSim)
planetsim_enable_sanitizers(PlanetSim)

add_executable(planet_cli apps/planet_cli/main.cpp)
target_link_libraries(planet_cli PRIVATE PlanetSim::PlanetSim)
planetsim_set_project_warnings(planet_cli)
planetsim_enable_sanitizers(planet_cli)

if(PLANETSIM_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
    add_test(
        NAME planet_cli_mesh_smoke
        COMMAND planet_cli mesh --subdivision 0 --radius 6371000
    )
    add_test(
        NAME planet_cli_solar_smoke
        COMMAND planet_cli solar --subdivision 2 --time-days 0
    )
    set_tests_properties(
        planet_cli_mesh_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "cell_count: 20"
    )
    set_tests_properties(
        planet_cli_solar_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "solar_forcing_valid: true"
    )
endif()

if(PLANETSIM_BUILD_GODOT_EXTENSION)
    set(GODOT_CPP_PATH "" CACHE PATH "Path to a godot-cpp checkout")
    if(NOT EXISTS "${GODOT_CPP_PATH}/CMakeLists.txt")
        message(FATAL_ERROR
            "PLANETSIM_BUILD_GODOT_EXTENSION requires GODOT_CPP_PATH to point "
            "to a godot-cpp checkout")
    endif()
    add_subdirectory("${GODOT_CPP_PATH}" "${CMAKE_BINARY_DIR}/godot-cpp")
    add_subdirectory(godot/gdextension)
endif()
