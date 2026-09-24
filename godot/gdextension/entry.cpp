#include "planet_mesh_node.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize_planetsim(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    godot::ClassDB::register_class<planetsim::godot_bridge::PlanetMeshNode>();
}

void uninitialize_planetsim(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

}  // namespace

extern "C" {

GDExtensionBool GDE_EXPORT planetsim_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    const GDExtensionClassLibraryPtr library,
    GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init_object(
        get_proc_address, library, initialization);
    init_object.register_initializer(initialize_planetsim);
    init_object.register_terminator(uninitialize_planetsim);
    init_object.set_minimum_library_initialization_level(
        godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_object.init();
}

}
