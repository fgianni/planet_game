#include "sim/core/serialization/run_manifest.hpp"
#include "tests/test_support.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] planetsim::RunManifest sample_manifest() {
    planetsim::RunManifest manifest;
    manifest.engine_version = "0.1.0";
    manifest.mesh_generator_version = 2U;
    manifest.scenario = {{"preset", "earth_like"}, {"seed", "7"}, {"note", ""}};
    manifest.commands = {{100, "user", "set_mode", "reference"},
                         {100, "user", "set_mode", "climate"},
                         {250, "test", "set_solar_luminosity_factor", "1.02"}};
    manifest.checkpoints = {{0, 0x0123'4567'89AB'CDEFULL}, {100, 1U}, {300, 0xFFFF'FFFF'FFFF'FFFFULL}};
    manifest.end_tick = 300;
    return manifest;
}

[[nodiscard]] bool same(const planetsim::RunManifest& a, const planetsim::RunManifest& b) {
    if (a.engine_version != b.engine_version ||
        a.mesh_generator_version != b.mesh_generator_version || a.scenario != b.scenario ||
        a.commands.size() != b.commands.size() || a.checkpoints.size() != b.checkpoints.size() ||
        a.end_tick != b.end_tick) {
        return false;
    }
    for (std::size_t index = 0; index < a.commands.size(); ++index) {
        const auto& x = a.commands[index];
        const auto& y = b.commands[index];
        if (x.tick != y.tick || x.actor != y.actor || x.type != y.type || x.payload != y.payload) {
            return false;
        }
    }
    for (std::size_t index = 0; index < a.checkpoints.size(); ++index) {
        if (a.checkpoints[index].tick != b.checkpoints[index].tick ||
            a.checkpoints[index].state_hash != b.checkpoints[index].state_hash) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool parse_fails(const std::string& text, std::string_view expected) {
    try {
        static_cast<void>(planetsim::parse_run_manifest(text));
    } catch (const std::runtime_error& error) {
        return std::string_view{error.what()}.find(expected) != std::string_view::npos;
    }
    return false;
}

[[nodiscard]] std::string replace_once(std::string text, std::string_view from,
                                       std::string_view to) {
    const auto position = text.find(from);
    if (position == std::string::npos) {
        throw std::logic_error("test pattern not found");
    }
    return text.replace(position, from.size(), to);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto manifest = sample_manifest();
    const std::string text = planetsim::format_run_manifest(manifest);

    // Records merge in tick order, a checkpoint before a command at its tick.
    const auto checkpoint_100 = text.find("checkpoint\t100\t");
    const auto command_100 = text.find("command\t100\t");
    PLANETSIM_EXPECT(test, text.starts_with("format\tPRUNv1\n"));
    PLANETSIM_EXPECT(test, checkpoint_100 != std::string::npos && checkpoint_100 < command_100);
    PLANETSIM_EXPECT(test, text.ends_with("end_tick\t300\n"));
    PLANETSIM_EXPECT(test, same(planetsim::parse_run_manifest(text), manifest));
    PLANETSIM_EXPECT(test, planetsim::format_run_manifest(planetsim::parse_run_manifest(text)) ==
                               text);

    const auto path = std::filesystem::temp_directory_path() / "planetsim_test_run.prun";
    planetsim::write_run_manifest(path, manifest);
    PLANETSIM_EXPECT(test, same(planetsim::read_run_manifest(path), manifest));
    PLANETSIM_EXPECT(test, !std::filesystem::exists(path.string() + ".partial"));
    std::filesystem::remove(path);

    PLANETSIM_EXPECT(test, planetsim::scenario_value(manifest.scenario, "seed") == "7");
    PLANETSIM_EXPECT(test, !planetsim::scenario_value(manifest.scenario, "absent"));
    PLANETSIM_EXPECT(test, planetsim::scenario_hash(manifest.scenario) !=
                               planetsim::scenario_hash({{"preset", "earth_like"}, {"seed", "8"},
                                                         {"note", ""}}));

    // An edited scenario no longer matches its hash.
    PLANETSIM_EXPECT(test, parse_fails(replace_once(text, "seed\t7", "seed\t8"), "scenario_hash"));
    PLANETSIM_EXPECT(test, parse_fails(replace_once(text, "PRUNv1", "PRUNv9"), "PRUNv1"));
    PLANETSIM_EXPECT(test, parse_fails(replace_once(text, "end_tick", "end_tock"), "unknown record"));
    PLANETSIM_EXPECT(test, parse_fails(replace_once(text, "checkpoint\t300\tffffffffffffffff",
                                                    "checkpoint\t300\tfff"),
                                       "16 hex digits"));
    PLANETSIM_EXPECT(test, parse_fails(replace_once(text, "checkpoint\t300", "checkpoint\t50"),
                                       "increasing tick order"));
    PLANETSIM_EXPECT(test, parse_fails(text.substr(0, text.size() - 1U), "line break"));

    auto tabbed = manifest;
    tabbed.commands[0].payload = "a\tb";
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            static_cast<void>(planetsim::format_run_manifest(tabbed)));
    auto no_actor = manifest;
    no_actor.commands[0].actor.clear();
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            static_cast<void>(planetsim::format_run_manifest(no_actor)));
    return test.result();
}
