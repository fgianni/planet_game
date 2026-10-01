#include "sim/core/serialization/snapshot_migration.hpp"

#include <stdexcept>
#include <utility>

namespace planetsim {

std::string migration_log_line(const SnapshotMigrationStep& step) {
    std::string line = std::to_string(step.from_schema);
    line += " -> ";
    line += std::to_string(step.from_schema + 1U);
    line += ' ';
    line += step.name;
    line += " (";
    line += step.decision;
    line += ')';
    return line;
}

SnapshotMigration& SnapshotMigration::set_initialiser(std::uint32_t to_schema,
                                                      Initialiser initialiser) {
    bool found = false;
    for (const auto& step : snapshot_migration_chain) {
        found = found || (step.from_schema + 1U == to_schema &&
                          step.kind == MigrationStepKind::initialiser);
    }
    if (!found) {
        throw std::invalid_argument("no migration step with an initialiser leads to schema " +
                                    std::to_string(to_schema));
    }
    if (!initialiser) {
        throw std::invalid_argument("empty migration initialiser for schema " +
                                    std::to_string(to_schema));
    }
    initialisers_[to_schema] = std::move(initialiser);
    return *this;
}

const SnapshotMigration::Initialiser*
SnapshotMigration::initialiser(std::uint32_t to_schema) const noexcept {
    const auto found = initialisers_.find(to_schema);
    return found == initialisers_.end() ? nullptr : &found->second;
}

}  // namespace planetsim
