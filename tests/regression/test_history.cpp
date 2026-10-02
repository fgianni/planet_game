#include "sim/core/serialization/history_store.hpp"
#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using planetsim::PlanetPreset;

struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface =
        planetsim::surface_energy_parameters_for(PlanetPreset::earth_like);
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    explicit Planet(std::shared_ptr<const planetsim::PlanetMesh> shared_mesh)
        : mesh(std::move(shared_mesh)), state(mesh) {
        static_cast<void>(planetsim::generate_terrain(
            state, 1U, planetsim::geology_parameters_for(PlanetPreset::earth_like), 4U));
        fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                         state.slow().sea_level_m, 4U);
        planetsim::initialise_climate(*mesh, state.slow(), parameters, surface, 4U);
    }

    void years(int count) {
        static_cast<void>(planetsim::spin_up_surface_energy(state, parameters, surface,
                                                            fractions, count, 4U));
    }
};

[[nodiscard]] std::filesystem::path fresh_directory(const std::string& name) {
    const auto directory =
        std::filesystem::temp_directory_path() /
        (name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    return directory;
}

// ADR-0003 V6: every snapshot of a history, full or delta, main line or
// fork, reconstructs to the saved state bit for bit, also after reopening
// the store from its directory; deltas store only the changed fields; a
// full snapshot is rewritten after `base_interval` deltas.
void check_chain_and_fork(planetsim::test::Context& test) {
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(3U, 6'371'000.0));
    const auto directory = fresh_directory("planetsim_history");
    std::map<std::string, std::uint64_t> expected_hash;
    std::vector<std::string> main_line;
    std::string fork_tip;
    {
        planetsim::HistoryStore store(directory, 4U);
        Planet planet(mesh);
        std::string parent;
        for (int save = 0; save < 11; ++save) {
            parent = store.save(planet.state, save * 1'000, parent);
            expected_hash[parent] = planetsim::slow_state_hash(planet.state);
            main_line.push_back(parent);
            planet.years(1);
        }
        // A fork at the third save, with a stronger greenhouse.
        Planet fork(mesh);
        static_cast<void>(store.load(main_line[2], fork.state));
        fork.surface.atmosphere.longwave_optical_depth += 0.2;
        std::string fork_parent = main_line[2];
        for (int save = 0; save < 6; ++save) {
            fork.years(1);
            fork_parent = store.save(fork.state, 100'000 + save * 1'000, fork_parent);
            expected_hash[fork_parent] = planetsim::slow_state_hash(fork.state);
        }
        fork_tip = fork_parent;

        // Kinds and depths: full every base_interval + 1 snapshots along a
        // chain; deltas omit the unchanging terrain.
        bool depths = true;
        for (std::size_t index = 0; index < main_line.size(); ++index) {
            const auto& entry = store.entry(main_line[index]);
            depths = depths && entry.depth == index % 5U && entry.delta == (index % 5U != 0U);
        }
        PLANETSIM_EXPECT(test, depths);
        const auto delta_manifest = planetsim::inspect_snapshot(store.path_of(main_line[1]));
        bool terrain_omitted = delta_manifest.delta;
        for (const auto& field : delta_manifest.fields) {
            terrain_omitted =
                terrain_omitted &&
                field.field_id != static_cast<std::uint32_t>(planetsim::FieldId::hypsometry_m) &&
                field.compression == "xor-shuffle-zstd";
        }
        PLANETSIM_EXPECT(test, terrain_omitted);

        std::uint64_t full_bytes = 0;
        std::uint64_t delta_bytes = 0;
        int deltas = 0;
        for (const auto& entry : store.entries()) {
            if (entry.delta) {
                delta_bytes += entry.file_bytes;
                ++deltas;
            } else {
                full_bytes = entry.file_bytes;
            }
        }
        std::cout << "history entries=" << store.entries().size() << " full_bytes=" << full_bytes
                  << " mean_delta_bytes=" << delta_bytes / static_cast<std::uint64_t>(deltas)
                  << " delta_ratio="
                  << static_cast<double>(delta_bytes) / deltas / static_cast<double>(full_bytes)
                  << '\n';
    }

    // Reopen from disk and reconstruct everything.
    planetsim::HistoryStore reopened(directory, 4U);
    PLANETSIM_EXPECT(test, reopened.entries().size() == expected_hash.size());
    bool identical = true;
    for (const auto& [id, hash] : expected_hash) {
        planetsim::PlanetState loaded(mesh);
        static_cast<void>(reopened.load(id, loaded));
        identical = identical && planetsim::slow_state_hash(loaded) == hash;
    }
    PLANETSIM_EXPECT(test, identical);
    PLANETSIM_EXPECT(test, reopened.entry(fork_tip).parent_id != main_line.back());

    // A delta is refused by read_snapshot, which has no chain.
    planetsim::PlanetState lone(mesh);
    PLANETSIM_EXPECT_THROWS(test, std::runtime_error,
                            planetsim::read_snapshot(reopened.path_of(main_line[1]), lone));
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_chain_and_fork(test);
    return test.result();
}
