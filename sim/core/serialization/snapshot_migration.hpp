#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace planetsim {

class PlanetMesh;
struct SlowState;

// Schema 1 (M2): hypsometry and sea level. Schema 2 (M3, ADR-0007 §4.6) adds
// the four surface-energy temperatures. Schema 3 (ADR-0007 §10) stores the
// ocean mixed layer as float64 under a new field ID. Schema 4 (ADR-0008
// §4.6) adds the snow and sea-ice reservoirs. Schema 5 (ADR-0010 §4.1, §4.7)
// adds the atmosphere, whose layer count each file states. Schema 6
// (ADR-0021 §4.1) adds the layers' humidity and the land's bucket.
inline constexpr std::uint32_t persistent_snapshot_schema_version = 6U;
inline constexpr std::uint32_t oldest_readable_snapshot_schema_version = 1U;

// How a step of the chain is carried out.
enum class MigrationStepKind : std::uint8_t {
    // The planet layer supplies an initialiser of the fields the step adds:
    // a function of the mesh and of the staged slow state, with every
    // earlier field already present (ADR-0003 §3.6: never silently zero).
    initialiser,
    // Core converts stored chunks itself: a type widening, or dropping a
    // retired field.
    core,
};

// One step vN -> vN+1 of the ordered migration chain (ADR-0003 §3.6). Steps
// live in the code permanently and are never deleted.
struct SnapshotMigrationStep {
    std::uint32_t from_schema;
    MigrationStepKind kind;
    std::string_view name;
    std::string_view decision;
};

inline constexpr std::array<SnapshotMigrationStep, 5> snapshot_migration_chain{{
    {1U, MigrationStepKind::initialiser, "surface-energy temperatures", "ADR-0007 §4.6"},
    {2U, MigrationStepKind::core, "ocean mixed layer widened to float64", "ADR-0007 §10"},
    {3U, MigrationStepKind::initialiser, "snow and sea-ice reservoirs", "ADR-0008 §4.6"},
    {4U, MigrationStepKind::initialiser, "atmosphere at hydrostatic rest", "ADR-0010 §4.3"},
    {5U, MigrationStepKind::initialiser, "water vapour and the land bucket", "ADR-0021 §4.1"},
}};

consteval bool snapshot_migration_chain_is_contiguous() {
    std::uint32_t expected = oldest_readable_snapshot_schema_version;
    for (const auto& step : snapshot_migration_chain) {
        if (step.from_schema != expected) {
            return false;
        }
        ++expected;
    }
    return expected == persistent_snapshot_schema_version;
}

static_assert(snapshot_migration_chain_is_contiguous(),
              "the migration chain must have one step from every readable schema to the "
              "current one, in order");

// "1 -> 2 surface-energy temperatures (ADR-0007 §4.6)".
[[nodiscard]] std::string migration_log_line(const SnapshotMigrationStep& step);

// The planet layer's initialisers, keyed by the schema their step leads to.
// Core code stays domain-agnostic; each initialiser receives the target mesh
// and the staged slow state after the earlier steps, and writes the current
// types.
class SnapshotMigration {
  public:
    using Initialiser = std::function<void(const PlanetMesh&, SlowState&)>;

    // Throws std::invalid_argument unless an initialiser step of the chain
    // leads to `to_schema`, or if the function is empty.
    SnapshotMigration& set_initialiser(std::uint32_t to_schema, Initialiser initialiser);

    [[nodiscard]] const Initialiser* initialiser(std::uint32_t to_schema) const noexcept;

  private:
    std::map<std::uint32_t, Initialiser> initialisers_;
};

}  // namespace planetsim
