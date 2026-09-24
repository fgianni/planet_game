add_library(planetsim_gdextension SHARED
    entry.cpp
    planet_mesh_node.cpp
)

target_link_libraries(planetsim_gdextension
    PRIVATE
        PlanetSim::PlanetSim
        godot-cpp
)
target_include_directories(planetsim_gdextension PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
planetsim_set_project_warnings(planetsim_gdextension)
planetsim_enable_sanitizers(planetsim_gdextension)

set_target_properties(planetsim_gdextension PROPERTIES
    OUTPUT_NAME planetsim_gdextension
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin"
)
