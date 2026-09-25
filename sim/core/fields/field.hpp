#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace planetsim {

inline constexpr std::size_t field_alignment_bytes = 64U;

template <typename T, std::size_t Alignment> class AlignedAllocator {
  public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    static_assert(Alignment >= alignof(T));
    static_assert((Alignment & (Alignment - 1U)) == 0U);

    constexpr AlignedAllocator() noexcept = default;

    template <typename U>
    constexpr AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    template <typename U> struct rebind { using other = AlignedAllocator<U, Alignment>; };

    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        return static_cast<T*>(::operator new (count * sizeof(T), std::align_val_t{Alignment}));
    }

    void deallocate(T* pointer, std::size_t) noexcept {
        ::operator delete (pointer, std::align_val_t{Alignment});
    }

    template <typename U>
    friend constexpr bool operator==(const AlignedAllocator&,
                                     const AlignedAllocator<U, Alignment>&) noexcept {
        return true;
    }
};

template <typename T>
using AlignedVector = std::vector<T, AlignedAllocator<T, field_alignment_bytes>>;

template <typename T> class Field2D {
    static_assert(!std::is_same_v<T, bool>,
                  "Field2D<bool> is intentionally unsupported; use an explicit byte field");

  public:
    using value_type = T;
    using size_type = std::size_t;

    Field2D() = default;
    explicit Field2D(size_type size) : values_(size) {}
    Field2D(size_type size, const T& initial_value) : values_(size, initial_value) {}
    explicit Field2D(const std::vector<T>& values) : values_(values.begin(), values.end()) {}

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return values_.size(); }
    [[nodiscard]] size_type allocated_bytes() const noexcept {
        return values_.capacity() * sizeof(T);
    }

    [[nodiscard]] T& at(size_type index) { return values_.at(index); }
    [[nodiscard]] const T& at(size_type index) const { return values_.at(index); }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] T& at(StrongIndex id) {
        return values_.at(id.to_index());
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] const T& at(StrongIndex id) const {
        return values_.at(id.to_index());
    }

    [[nodiscard]] T& operator[](size_type index) { return at(index); }
    [[nodiscard]] const T& operator[](size_type index) const { return at(index); }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] T& operator[](StrongIndex id) {
        return at(id);
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] const T&
    operator[](StrongIndex id) const {
        return at(id);
    }

    [[nodiscard]] std::span<T> values() noexcept { return values_; }
    [[nodiscard]] std::span<const T> values() const noexcept { return values_; }

  private:
    AlignedVector<T> values_;
};

template <typename T> using Field = Field2D<T>;

template <typename T> class Field3D {
    static_assert(!std::is_same_v<T, bool>,
                  "Field3D<bool> is intentionally unsupported; use an explicit byte field");

  public:
    using size_type = std::size_t;

    Field3D() = default;
    Field3D(size_type layer_count, size_type cell_count) : Field3D(layer_count, cell_count, T{}) {}
    Field3D(size_type layer_count, size_type cell_count, const T& initial_value)
        : layer_count_(layer_count), cell_count_(cell_count) {
        if (cell_count != 0U && layer_count > std::numeric_limits<size_type>::max() / cell_count) {
            throw std::length_error("Field3D dimensions overflow");
        }
        values_.assign(layer_count * cell_count, initial_value);
    }

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return values_.size(); }
    [[nodiscard]] size_type layer_count() const noexcept { return layer_count_; }
    [[nodiscard]] size_type cell_count() const noexcept { return cell_count_; }
    [[nodiscard]] size_type allocated_bytes() const noexcept {
        return values_.capacity() * sizeof(T);
    }

    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>)
        [[nodiscard]] T& at(size_type layer, StrongIndex cell) {
        return values_.at(flat_index(layer, cell));
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>)
        [[nodiscard]] const T& at(size_type layer, StrongIndex cell) const {
        return values_.at(flat_index(layer, cell));
    }
    [[nodiscard]] std::span<T> layer(size_type layer_index) {
        if (layer_index >= layer_count_) {
            throw std::out_of_range("Field3D layer index out of range");
        }
        return {values_.data() + layer_index * cell_count_, cell_count_};
    }
    [[nodiscard]] std::span<const T> layer(size_type layer_index) const {
        if (layer_index >= layer_count_) {
            throw std::out_of_range("Field3D layer index out of range");
        }
        return {values_.data() + layer_index * cell_count_, cell_count_};
    }

  private:
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] size_type
        flat_index(size_type layer, StrongIndex cell) const {
        if (layer >= layer_count_ || cell.to_index() >= cell_count_) {
            throw std::out_of_range("Field3D index out of range");
        }
        return layer * cell_count_ + cell.to_index();
    }

    size_type layer_count_ = 0;
    size_type cell_count_ = 0;
    AlignedVector<T> values_;
};

template <typename T> class EdgeField {
    static_assert(!std::is_same_v<T, bool>,
                  "EdgeField<bool> is intentionally unsupported; use an explicit byte field");

  public:
    using size_type = std::size_t;

    EdgeField() = default;
    explicit EdgeField(size_type size) : values_(size) {}
    EdgeField(size_type size, const T& initial_value) : values_(size, initial_value) {}

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return values_.size(); }
    [[nodiscard]] size_type allocated_bytes() const noexcept {
        return values_.capacity() * sizeof(T);
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] T& at(StrongIndex id) {
        return values_.at(id.to_index());
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] const T& at(StrongIndex id) const {
        return values_.at(id.to_index());
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] T& operator[](StrongIndex id) {
        return at(id);
    }
    template <typename StrongIndex>
    requires(!std::is_integral_v<StrongIndex>) [[nodiscard]] const T&
    operator[](StrongIndex id) const {
        return at(id);
    }
    [[nodiscard]] std::span<T> values() noexcept { return values_; }
    [[nodiscard]] std::span<const T> values() const noexcept { return values_; }

  private:
    AlignedVector<T> values_;
};

}  // namespace planetsim
