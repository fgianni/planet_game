#pragma once

#include "sim/planet/mesh/cell_id.hpp"

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace planetsim {

template <typename T>
class Field {
    static_assert(!std::is_same_v<T, bool>,
                  "Field<bool> is intentionally unsupported; use an explicit byte field");

public:
    using value_type = T;
    using size_type = std::size_t;

    Field() = default;
    explicit Field(size_type size) : values_(size) {}
    Field(size_type size, const T& initial_value) : values_(size, initial_value) {}
    explicit Field(std::vector<T> values) : values_(std::move(values)) {}

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return values_.size(); }

    [[nodiscard]] T& at(size_type index) { return values_.at(index); }
    [[nodiscard]] const T& at(size_type index) const { return values_.at(index); }
    [[nodiscard]] T& at(CellId id) { return values_.at(id.to_index()); }
    [[nodiscard]] const T& at(CellId id) const { return values_.at(id.to_index()); }

    [[nodiscard]] T& operator[](size_type index) { return at(index); }
    [[nodiscard]] const T& operator[](size_type index) const { return at(index); }
    [[nodiscard]] T& operator[](CellId id) { return at(id); }
    [[nodiscard]] const T& operator[](CellId id) const { return at(id); }

    [[nodiscard]] std::span<T> values() noexcept { return values_; }
    [[nodiscard]] std::span<const T> values() const noexcept { return values_; }

private:
    std::vector<T> values_;
};

}  // namespace planetsim
