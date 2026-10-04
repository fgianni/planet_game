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
find_package(Threads REQUIRED)
# Snapshot compression (ADR-0003 §3.4): the system libzstd.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

add_library(PlanetSim STATIC
    sim/core/math/banded_lu.cpp
    sim/core/random/noise.cpp
    sim/core/serialization/crc32c.cpp
    sim/core/serialization/history_store.cpp
    sim/core/serialization/snapshot_file.cpp
    sim/core/serialization/snapshot_migration.cpp
    sim/core/serialization/state_snapshot.cpp
    sim/core/serialization/xxh3.cpp
    sim/core/serialization/run_manifest.cpp
    sim/core/scheduler/orbital_calendar.cpp
    sim/core/scheduler/scheduler.cpp
    sim/core/scheduler/simulation_clock.cpp
    sim/core/scheduler/worker_pool.cpp
    sim/planet/atmosphere/atmosphere.cpp
    sim/planet/climatology/monthly_climatology.cpp
    sim/planet/coordinates/local_tangent_basis.cpp
    sim/planet/dynamics/atmosphere_dynamics.cpp
    sim/planet/dynamics/balanced_circulation.cpp
    sim/planet/dynamics/orography.cpp
    sim/planet/dynamics/primitive_equations.cpp
    sim/planet/dynamics/shallow_water.cpp
    sim/planet/dynamics/williamson_cases.cpp
    sim/planet/dynamics/zonal_circulation.cpp
    sim/planet/dynamics/zonal_coupling.cpp
    sim/planet/dynamics/zonal_statistics.cpp
    sim/planet/geology/crust.cpp
    sim/planet/geology/geology_generator.cpp
    sim/planet/geology/geology_parameters.cpp
    sim/planet/geology/plates.cpp
    sim/planet/geology/structural_elevation.cpp
    sim/planet/mesh/icosphere.cpp
    sim/planet/mesh/mesh_diagnostics.cpp
    sim/planet/mesh/planet_mesh.cpp
    sim/planet/operators/c_grid.cpp
    sim/planet/operators/finite_volume.cpp
    sim/planet/operators/operator_validation.cpp
    sim/planet/orbit/climate_calendar.cpp
    sim/planet/orbit/orbit_state.cpp
    sim/planet/orbit/solar_diagnostics.cpp
    sim/planet/orbit/solar_forcing.cpp
    sim/planet/orbit/substep_forcing.cpp
    sim/planet/surface/column_step.cpp
    sim/planet/surface/land_snow.cpp
    sim/planet/surface/heat_transport.cpp
    sim/planet/surface/sea_ice.cpp
    sim/planet/surface/surface_energy.cpp
    sim/planet/surface/surface_materials.cpp
    sim/planet/planet_parameters.cpp
    sim/planet/planet_migration.cpp
    sim/planet/planet_state.cpp
    sim/planet/run/planet_run.cpp
    sim/planet/terrain/drainage.cpp
    sim/planet/terrain/hypsometry.cpp
    sim/planet/terrain/surface_fractions.cpp
    sim/planet/terrain/terrain_diagnostics.cpp
    sim/planet/terrain/terrain_generator.cpp
    sim/planet/terrain/terrain_snapshot.cpp
)
add_library(PlanetSim::PlanetSim ALIAS PlanetSim)

add_library(PlanetSimPresentation STATIC
    sim/presentation/presentation_record.cpp
    sim/presentation/visual_frame.cpp
)
add_library(PlanetSim::Presentation ALIAS PlanetSimPresentation)
target_include_directories(PlanetSimPresentation PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(PlanetSimPresentation PUBLIC PlanetSim::PlanetSim)
planetsim_set_project_warnings(PlanetSimPresentation)
planetsim_enable_sanitizers(PlanetSimPresentation)

target_include_directories(PlanetSim
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_definitions(PlanetSim PRIVATE PLANETSIM_ENGINE_VERSION="${PROJECT_VERSION}")
target_link_libraries(PlanetSim PUBLIC Threads::Threads PRIVATE PkgConfig::ZSTD)
set_target_properties(PlanetSim PROPERTIES POSITION_INDEPENDENT_CODE ON)
planetsim_set_project_warnings(PlanetSim)
planetsim_enable_sanitizers(PlanetSim)

add_executable(planet_cli apps/planet_cli/main.cpp)
target_link_libraries(planet_cli PRIVATE PlanetSim::PlanetSim PlanetSim::Presentation)
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
        PROPERTIES PASS_REGULAR_EXPRESSION "cell_count: 12"
    )
    add_test(
        NAME planet_cli_operators_smoke
        COMMAND planet_cli operators --min-subdivision 1 --max-subdivision 2
    )
    set_tests_properties(
        planet_cli_operators_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "poisson_solution_relative_l2"
    )
    set_tests_properties(
        planet_cli_solar_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "solar_forcing_valid: true"
    )
    set(PLANETSIM_SNAPSHOT_SMOKE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/planet_cli_snapshot_smoke.psnap")
    set(PLANETSIM_TERRAIN_SMOKE_DIR "${CMAKE_CURRENT_BINARY_DIR}/planet_cli_terrain_smoke")
    file(MAKE_DIRECTORY "${PLANETSIM_TERRAIN_SMOKE_DIR}")
    add_test(
        NAME planet_cli_terrain_smoke
        COMMAND planet_cli terrain --subdivision 3 --seed 7 --workers 2
                --map "${PLANETSIM_TERRAIN_SMOKE_DIR}/terrain.csv"
                --snapshot "${PLANETSIM_TERRAIN_SMOKE_DIR}/terrain.psnap"
    )
    set_tests_properties(
        planet_cli_terrain_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION
            "achieved_land_fraction: 0\\.29[^0-9].*drainage_catchment_closure_relative_error: [0-9eE+.-]+.*terrain_valid: true"
    )
    add_test(
        NAME planet_cli_terrain_aqua_smoke
        COMMAND planet_cli terrain --subdivision 2 --preset aqua_planet
    )
    set_tests_properties(
        planet_cli_terrain_aqua_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "achieved_land_fraction: 0\n"
    )
    add_test(
        NAME planet_cli_solar_substep_smoke
        COMMAND planet_cli solar --subdivision 2 --substep 3
    )
    set_tests_properties(
        planet_cli_solar_substep_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "substep_forcing_valid: true"
    )
    add_test(
        NAME planet_cli_calendar_smoke
        COMMAND planet_cli calendar --year 0
    )
    set_tests_properties(
        planet_cli_calendar_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "\n11 11 482120 525949 43829 "
    )
    add_test(
        NAME planet_cli_thermal_smoke
        COMMAND planet_cli thermal --preset dead_rock --subdivision 2 --years 3 --workers 2
    )
    set_tests_properties(
        planet_cli_thermal_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "g=0 layers=0 tau=0 D=0 mean_K=25[0-9]"
    )
    set(PLANETSIM_RUN_SMOKE_FILE "${CMAKE_CURRENT_BINARY_DIR}/planet_cli_run_smoke.prun")
    add_test(
        NAME planet_cli_run_smoke
        COMMAND planet_cli run --subdivision 2 --years 2 --workers 2
                --command 43830,set_solar_luminosity_factor,1.01
                --manifest "${PLANETSIM_RUN_SMOKE_FILE}"
                --min-years-per-minute 1
    )
    set_tests_properties(
        planet_cli_run_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "checkpoints=3 .*gate years_per_minute>=1: passed"
    )
    add_test(
        NAME planet_cli_replay_smoke
        COMMAND planet_cli replay "${PLANETSIM_RUN_SMOKE_FILE}" --workers 3
    )
    set_tests_properties(
        planet_cli_replay_smoke
        PROPERTIES
            DEPENDS planet_cli_run_smoke
            PASS_REGULAR_EXPRESSION "replay matched"
    )
    add_test(
        NAME planet_cli_registry_dump
        COMMAND planet_cli registry dump
    )
    set_tests_properties(
        planet_cli_registry_dump
        PROPERTIES PASS_REGULAR_EXPRESSION "131074\tsea_level_m\tslow\tglobal\tfloat64\t1\tm"
    )
    add_test(
        NAME test_field_registry_check
        COMMAND python3
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_field_registry_check.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/check_field_registry.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/field_registry_baseline.tsv"
                "$<TARGET_FILE:planet_cli>"
    )
    add_test(
        NAME test_channel_registry_check
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_channel_registry_check.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/check_channel_registry.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/channel_registry_baseline.tsv"
                "$<TARGET_FILE:planet_cli>"
    )
    add_test(
        NAME test_presentation_include_check
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_presentation_include_check.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/check_presentation_includes.py"
    )
    add_test(
        NAME planet_cli_snapshot_write_smoke
        COMMAND planet_cli snapshot write --subdivision 0
                --out "${PLANETSIM_SNAPSHOT_SMOKE_FILE}"
    )
    set_tests_properties(
        planet_cli_snapshot_write_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "snapshot_written:"
    )
    add_test(
        NAME planet_cli_snapshot_inspect_smoke
        COMMAND planet_cli snapshot inspect "${PLANETSIM_SNAPSHOT_SMOKE_FILE}"
    )
    set_tests_properties(
        planet_cli_snapshot_inspect_smoke
        PROPERTIES
            DEPENDS planet_cli_snapshot_write_smoke
            PASS_REGULAR_EXPRESSION "checksums_valid: true"
    )
endif()

if(PLANETSIM_BUILD_GODOT_EXTENSION)
    set(GODOT_CPP_PATH "" CACHE PATH "Path to a godot-cpp checkout")
    if(NOT EXISTS "${GODOT_CPP_PATH}/CMakeLists.txt")
        message(FATAL_ERROR
            "PLANETSIM_BUILD_GODOT_EXTENSION requires GODOT_CPP_PATH to point "
            "to a godot-cpp checkout")
    endif()
    # The adapter reports PlanetSim exceptions to Godot, and godot-cpp's
    # headers must not be held to PlanetSim's warning set.
    set(GODOTCPP_DISABLE_EXCEPTIONS OFF)
    set(GODOTCPP_SYSTEM_HEADERS ON)
    add_subdirectory("${GODOT_CPP_PATH}" "${CMAKE_BINARY_DIR}/godot-cpp")
    add_subdirectory(godot/gdextension)
endif()
