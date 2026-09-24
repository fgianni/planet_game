#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace planetsim {

class CellId {
public:
    using value_type = std::uint32_t;

    constexpr CellId() noexcept = default;
    explicit constexpr CellId(value_type value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr CellId invalid() noexcept {
        return CellId{std::numeric_limits<value_type>::max()};
    }

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr std::size_t to_index() const noexcept {
        return static_cast<std::size_t>(value_);
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return *this != invalid(); }

    friend constexpr bool operator==(CellId, CellId) noexcept = default;
    friend constexpr auto operator<=>(CellId, CellId) noexcept = default;

private:
    value_type value_ = std::numeric_limits<value_type>::max();
};

}  // namespace planetsim
