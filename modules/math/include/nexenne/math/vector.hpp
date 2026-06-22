#pragma once

/**
 * @file
 * @brief Fixed-size vectors with named \c .x() / \c .y() / \c .z() / \c .w()
 *        accessors for the common dimensions.
 *
 * \c vector<Value, N> stores its components in a \c std::array. The
 * specializations for \c N = 2, 3, 4 add named accessors and swizzles for the
 * canonical components while keeping \c operator[], \c data(), and iteration, so
 * generic code works at any dimension. The shared interface (indexing, the data
 * pointer, iteration, size, comparison) lives once in \c detail::vector_base and
 * the four class definitions only add the per-dimension ergonomics.
 *
 * Layout and SIMD. The single data member is a \c std::array, so a vector is
 * standard-layout, trivially copyable, and exactly \c N*sizeof(Value) with no
 * padding (verified by the static_asserts below). That tight, contiguous layout
 * is what lets the compiler auto-vectorize the element-wise operators: a
 * \c vector<float,4> add compiles to a single SSE \c addps at \c -O2, with no
 * intrinsics and no per-platform code. We deliberately keep the natural
 * alignment rather than force a 16-byte alignment: padding would break the
 * \c sizeof guarantee (a \c vector<float,3> would grow to 16 bytes), cost memory
 * on the embedded targets, and buy nothing on modern hardware, where an
 * unaligned packed load (\c movups) is as fast as an aligned one. Wider SIMD
 * (AVX) needs a \c -march flag the build does not assume.
 *
 * Pass-by-value is fine for these small types; the element-wise operators take
 * \c const& so a \c vector<float,4> loads its four components contiguously into
 * one SSE register, which is what feeds the packed op.
 *
 * Construction. Deduction guides cover the 2, 3, and 4 component forms, so
 * \c vector{1.0f, 2.0f, 3.0f} deduces \c vector<float, 3>. The guides require all
 * arguments to share one arithmetic type; mixed types (\c vector{1, 2.0f}) are
 * rejected on purpose, to avoid surprise integer-promotion dispatch in templated
 * algorithms.
 *
 * The algorithms (dot, cross, length, normalize) live in
 * \c vector_algorithms.hpp to keep this header focused on storage and the
 * element-wise algebra.
 */

#include <array>
#include <compare>
#include <cstddef>
#include <type_traits>

#include <nexenne/math/concepts.hpp>

namespace nexenne::math {

namespace detail {

/**
 * @brief Shared storage and interface for every \c vector<Value, N>.
 *
 * Holds the one data member (a \c std::array) and the dimension-independent
 * interface: indexing, the contiguous data pointer, iteration, size, and the
 * defaulted lexicographic comparison. The \c vector primary template and its 2D,
 * 3D, and 4D specializations all derive from this and add only their named
 * accessors, so the common code exists once. Because this base holds the only
 * non-static data member and the derived types add none, a \c vector stays
 * standard-layout and trivially copyable.
 *
 * @tparam Value Arithmetic component type.
 * @tparam N Component count.
 */
template <arithmetic Value, std::size_t N>
class vector_base {
public:
  using value_type = Value;  ///< The component type.

protected:
  std::array<value_type, N> m_components{};

public:
  /**
   * @brief Constructs a zero-initialized vector.
   *
   * @pre None.
   * @post Every component is value-initialized to zero.
   */
  constexpr vector_base() noexcept = default;

  /**
   * @brief Constructs from an array of components.
   *
   * @param components Source components copied into the vector.
   *
   * @pre None.
   * @post Each component equals the corresponding element of \p components.
   */
  constexpr explicit vector_base(std::array<value_type, N> const& components) noexcept
      : m_components{components} {}

  /**
   * @brief Accesses the component at index \p i.
   *
   * @param i Zero-based component index.
   *
   * @return Mutable reference to the component at index \p i.
   *
   * @pre \p i is less than \c N.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](std::size_t const i) noexcept -> value_type& {
    return m_components[i];
  }

  /**
   * @brief Accesses the component at index \p i.
   *
   * @param i Zero-based component index.
   *
   * @return Const reference to the component at index \p i.
   *
   * @pre \p i is less than \c N.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](std::size_t const i) const noexcept -> value_type const& {
    return m_components[i];
  }

  /**
   * @brief Returns a pointer to the contiguous component storage.
   *
   * @return Mutable pointer to the first component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto data() noexcept -> value_type* {
    return m_components.data();
  }

  /**
   * @brief Returns a pointer to the contiguous component storage.
   *
   * @return Const pointer to the first component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto data() const noexcept -> value_type const* {
    return m_components.data();
  }

  /**
   * @brief Returns an iterator to the first component.
   *
   * @return Mutable iterator to the first component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() noexcept {
    return m_components.begin();
  }

  /**
   * @brief Returns an iterator one past the last component.
   *
   * @return Mutable iterator one past the last component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() noexcept {
    return m_components.end();
  }

  /**
   * @brief Returns a const iterator to the first component.
   *
   * @return Const iterator to the first component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept {
    return m_components.begin();
  }

  /**
   * @brief Returns a const iterator one past the last component.
   *
   * @return Const iterator one past the last component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept {
    return m_components.end();
  }

  /**
   * @brief Returns a const iterator to the first component.
   *
   * @return Const iterator to the first component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto cbegin() const noexcept {
    return m_components.cbegin();
  }

  /**
   * @brief Returns a const iterator one past the last component.
   *
   * @return Const iterator one past the last component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto cend() const noexcept {
    return m_components.cend();
  }

  /**
   * @brief Returns the number of components.
   *
   * @return The component count \c N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto size() noexcept -> std::size_t {
    return N;
  }

  /**
   * @brief Lexicographically compares two vectors component by component.
   *
   * Defaulted, so it also yields \c ==. Derived vectors compare through this base
   * (they add no data members), so the comparison sees every component.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return Three-way comparison result over the components in order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(vector_base const& lhs, vector_base const& rhs) noexcept = default;
};

}  // namespace detail

/**
 * @brief Fixed-size vector of \p N components of type \p Value.
 *
 * Generic form. The specializations for \c N = 2, 3, 4 add \c .x() / \c .y() /
 * \c .z() / \c .w() accessors and swizzles; this primary template carries only
 * the shared interface from \c detail::vector_base.
 *
 * @tparam Value Arithmetic component type.
 * @tparam N Component count.
 */
template <arithmetic Value, std::size_t N>
class vector : public detail::vector_base<Value, N> {
private:
  using base = detail::vector_base<Value, N>;

public:
  using value_type = typename base::value_type;
  using base::base;
};

/**
 * @brief Two-component vector with \c x and \c y accessors.
 *
 * @tparam Value Arithmetic component type.
 */
template <arithmetic Value>
class vector<Value, 2> : public detail::vector_base<Value, 2> {
private:
  using base = detail::vector_base<Value, 2>;

public:
  using value_type = typename base::value_type;
  using base::base;

  /**
   * @brief Constructs a 2D vector from its two components.
   *
   * @param a Value for the x component.
   * @param b Value for the y component.
   *
   * @pre None.
   * @post The x and y components equal \p a and \p b respectively.
   */
  constexpr vector(value_type const a, value_type const b) noexcept
      : base{std::array<value_type, 2>{a, b}} {}

  /**
   * @brief Accesses the x component.
   *
   * @return Mutable reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() noexcept -> value_type& {
    return this->m_components[0];
  }

  /**
   * @brief Accesses the y component.
   *
   * @return Mutable reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() noexcept -> value_type& {
    return this->m_components[1];
  }

  /**
   * @brief Accesses the x component.
   *
   * @return Const reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() const noexcept -> value_type const& {
    return this->m_components[0];
  }

  /**
   * @brief Accesses the y component.
   *
   * @return Const reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() const noexcept -> value_type const& {
    return this->m_components[1];
  }
};

/**
 * @brief Three-component vector with \c x, \c y, \c z accessors and swizzles.
 *
 * @tparam Value Arithmetic component type.
 */
template <arithmetic Value>
class vector<Value, 3> : public detail::vector_base<Value, 3> {
private:
  using base = detail::vector_base<Value, 3>;

public:
  using value_type = typename base::value_type;
  using base::base;

  /**
   * @brief Constructs a 3D vector from its three components.
   *
   * @param a Value for the x component.
   * @param b Value for the y component.
   * @param c Value for the z component.
   *
   * @pre None.
   * @post The x, y, and z components equal \p a, \p b, and \p c respectively.
   */
  constexpr vector(value_type const a, value_type const b, value_type const c) noexcept
      : base{std::array<value_type, 3>{a, b, c}} {}

  /**
   * @brief Accesses the x component.
   *
   * @return Mutable reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() noexcept -> value_type& {
    return this->m_components[0];
  }

  /**
   * @brief Accesses the y component.
   *
   * @return Mutable reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() noexcept -> value_type& {
    return this->m_components[1];
  }

  /**
   * @brief Accesses the z component.
   *
   * @return Mutable reference to the z component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto z() noexcept -> value_type& {
    return this->m_components[2];
  }

  /**
   * @brief Accesses the x component.
   *
   * @return Const reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() const noexcept -> value_type const& {
    return this->m_components[0];
  }

  /**
   * @brief Accesses the y component.
   *
   * @return Const reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() const noexcept -> value_type const& {
    return this->m_components[1];
  }

  /**
   * @brief Accesses the z component.
   *
   * @return Const reference to the z component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto z() const noexcept -> value_type const& {
    return this->m_components[2];
  }

  /**
   * @brief Extracts the x and y components as a 2D vector.
   *
   * @return A 2D vector holding the x and y components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xy() const noexcept -> vector<value_type, 2> {
    return vector<value_type, 2>{this->m_components[0], this->m_components[1]};
  }

  /**
   * @brief Extracts the x and z components as a 2D vector.
   *
   * @return A 2D vector holding the x and z components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xz() const noexcept -> vector<value_type, 2> {
    return vector<value_type, 2>{this->m_components[0], this->m_components[2]};
  }

  /**
   * @brief Extracts the y and z components as a 2D vector.
   *
   * @return A 2D vector holding the y and z components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto yz() const noexcept -> vector<value_type, 2> {
    return vector<value_type, 2>{this->m_components[1], this->m_components[2]};
  }
};

/**
 * @brief Four-component vector with \c x, \c y, \c z, \c w accessors and swizzles.
 *
 * @tparam Value Arithmetic component type.
 */
template <arithmetic Value>
class vector<Value, 4> : public detail::vector_base<Value, 4> {
private:
  using base = detail::vector_base<Value, 4>;

public:
  using value_type = typename base::value_type;
  using base::base;

  /**
   * @brief Constructs a 4D vector from its four components.
   *
   * @param a Value for the x component.
   * @param b Value for the y component.
   * @param c Value for the z component.
   * @param d Value for the w component.
   *
   * @pre None.
   * @post The x, y, z, and w components equal \p a, \p b, \p c, and \p d.
   */
  constexpr vector(
    value_type const a, value_type const b, value_type const c, value_type const d
  ) noexcept
      : base{std::array<value_type, 4>{a, b, c, d}} {}

  /**
   * @brief Accesses the x component.
   *
   * @return Mutable reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() noexcept -> value_type& {
    return this->m_components[0];
  }

  /**
   * @brief Accesses the y component.
   *
   * @return Mutable reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() noexcept -> value_type& {
    return this->m_components[1];
  }

  /**
   * @brief Accesses the z component.
   *
   * @return Mutable reference to the z component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto z() noexcept -> value_type& {
    return this->m_components[2];
  }

  /**
   * @brief Accesses the w component.
   *
   * @return Mutable reference to the w component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto w() noexcept -> value_type& {
    return this->m_components[3];
  }

  /**
   * @brief Accesses the x component.
   *
   * @return Const reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() const noexcept -> value_type const& {
    return this->m_components[0];
  }

  /**
   * @brief Accesses the y component.
   *
   * @return Const reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() const noexcept -> value_type const& {
    return this->m_components[1];
  }

  /**
   * @brief Accesses the z component.
   *
   * @return Const reference to the z component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto z() const noexcept -> value_type const& {
    return this->m_components[2];
  }

  /**
   * @brief Accesses the w component.
   *
   * @return Const reference to the w component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto w() const noexcept -> value_type const& {
    return this->m_components[3];
  }

  /**
   * @brief Extracts the x, y, and z components as a 3D vector.
   *
   * @return A 3D vector holding the x, y, and z components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xyz() const noexcept -> vector<value_type, 3> {
    return vector<value_type, 3>{
      this->m_components[0], this->m_components[1], this->m_components[2]
    };
  }

  /**
   * @brief Extracts the x, y, and w components as a 3D vector.
   *
   * @return A 3D vector holding the x, y, and w components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xyw() const noexcept -> vector<value_type, 3> {
    return vector<value_type, 3>{
      this->m_components[0], this->m_components[1], this->m_components[3]
    };
  }

  /**
   * @brief Extracts the x, z, and w components as a 3D vector.
   *
   * @return A 3D vector holding the x, z, and w components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xzw() const noexcept -> vector<value_type, 3> {
    return vector<value_type, 3>{
      this->m_components[0], this->m_components[2], this->m_components[3]
    };
  }

  /**
   * @brief Extracts the y, z, and w components as a 3D vector.
   *
   * @return A 3D vector holding the y, z, and w components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto yzw() const noexcept -> vector<value_type, 3> {
    return vector<value_type, 3>{
      this->m_components[1], this->m_components[2], this->m_components[3]
    };
  }

  /**
   * @brief Extracts the x and y components as a 2D vector.
   *
   * @return A 2D vector holding the x and y components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xy() const noexcept -> vector<value_type, 2> {
    return vector<value_type, 2>{this->m_components[0], this->m_components[1]};
  }

  /**
   * @brief Extracts the x and z components as a 2D vector.
   *
   * @return A 2D vector holding the x and z components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto xz() const noexcept -> vector<value_type, 2> {
    return vector<value_type, 2>{this->m_components[0], this->m_components[2]};
  }

  /**
   * @brief Extracts the y and z components as a 2D vector.
   *
   * @return A 2D vector holding the y and z components.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto yz() const noexcept -> vector<value_type, 2> {
    return vector<value_type, 2>{this->m_components[1], this->m_components[2]};
  }
};

// Deduction guides: let callers write vector{1.0f, 2.0f, 3.0f} and have the size
// deduced from the argument count. All arguments must share one arithmetic type.

/// @brief Deduces \c vector<Value, 2> from two same-typed components.
template <arithmetic Value>
vector(Value, Value) -> vector<Value, 2>;
/// @brief Deduces \c vector<Value, 3> from three same-typed components.
template <arithmetic Value>
vector(Value, Value, Value) -> vector<Value, 3>;
/// @brief Deduces \c vector<Value, 4> from four same-typed components.
template <arithmetic Value>
vector(Value, Value, Value, Value) -> vector<Value, 4>;

// The layout guarantees the SIMD-friendly storage and the contiguous data()
// pointer rely on: one std::array member, no padding, no vtable.
static_assert(std::is_standard_layout_v<vector<float, 2>>);
static_assert(std::is_standard_layout_v<vector<float, 3>>);
static_assert(std::is_standard_layout_v<vector<float, 4>>);
static_assert(sizeof(vector<float, 2>) == 2 * sizeof(float));
static_assert(sizeof(vector<float, 3>) == 3 * sizeof(float));
static_assert(sizeof(vector<float, 4>) == 4 * sizeof(float));
static_assert(std::is_trivially_copyable_v<vector<float, 2>>);
static_assert(std::is_trivially_copyable_v<vector<float, 3>>);
static_assert(std::is_trivially_copyable_v<vector<float, 4>>);

/// @brief Two-component vector of arbitrary component type.
template <arithmetic Value>
using vector2 = vector<Value, 2>;
/// @brief Three-component vector of arbitrary component type.
template <arithmetic Value>
using vector3 = vector<Value, 3>;
/// @brief Four-component vector of arbitrary component type.
template <arithmetic Value>
using vector4 = vector<Value, 4>;

/// @brief Two-component \c float vector.
using vector2_f = vector2<float>;
/// @brief Three-component \c float vector.
using vector3_f = vector3<float>;
/// @brief Four-component \c float vector.
using vector4_f = vector4<float>;

/// @brief Two-component \c double vector.
using vector2_d = vector2<double>;
/// @brief Three-component \c double vector.
using vector3_d = vector3<double>;
/// @brief Four-component \c double vector.
using vector4_d = vector4<double>;

/// @brief Two-component \c int vector.
using vector2_i = vector2<int>;
/// @brief Three-component \c int vector.
using vector3_i = vector3<int>;
/// @brief Four-component \c int vector.
using vector4_i = vector4<int>;

// The element-wise operators below are plain for loops over the fixed N. For
// N = 2, 3, 4 the compiler unrolls them and emits one packed SIMD instruction
// per operation (for example a vector<float,4> add becomes a single SSE addps);
// the loop form is what the auto-vectorizer recognizes, so no intrinsics are
// needed. Each builds a zero vector and overwrites every lane, and the dead
// zero-initialization is elided.

/**
 * @brief Element-wise vector addition.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return Component-wise sum.
 *
 * @pre None.
 * @post Each component equals the sum of the corresponding components.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator+(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = a[i] + b[i];
  }
  return result;
}

/**
 * @brief Element-wise vector subtraction.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return Component-wise difference.
 *
 * @pre None.
 * @post Each component equals the difference of the corresponding components.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator-(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = a[i] - b[i];
  }
  return result;
}

/**
 * @brief Element-wise (Hadamard) vector multiplication.
 *
 * The component-wise product, not the dot or cross product. Use \c dot or
 * \c cross from vector_algorithms.hpp for those.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return Component-wise product.
 *
 * @pre None.
 * @post Each component equals the product of the corresponding components.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = a[i] * b[i];
  }
  return result;
}

/**
 * @brief Element-wise vector division.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Dividend.
 * @param b Divisor.
 *
 * @return Component-wise quotient.
 *
 * @pre No component of \p b is zero.
 * @post Each component equals the quotient of the corresponding components.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator/(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = a[i] / b[i];
  }
  return result;
}

/**
 * @brief Unary minus, component-wise.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector to negate.
 *
 * @return Component-wise negation.
 *
 * @pre None.
 * @post Each component is the negation of the corresponding component of \p v.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto operator-(vector<Value, N> const& v) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = -v[i];
  }
  return result;
}

/**
 * @brief Scalar multiplication (vector * scalar).
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector.
 * @param scalar Scaling factor.
 *
 * @return \p v with each component multiplied by \p scalar.
 *
 * @pre None.
 * @post Each component equals the corresponding component of \p v times \p scalar.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(vector<Value, N> const& v, Value const scalar) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = v[i] * scalar;
  }
  return result;
}

/**
 * @brief Scalar multiplication (scalar * vector).
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param scalar Scaling factor.
 * @param v Vector.
 *
 * @return \p v with each component multiplied by \p scalar.
 *
 * @pre None.
 * @post Each component equals the corresponding component of \p v times \p scalar.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(Value const scalar, vector<Value, N> const& v) noexcept -> vector<Value, N> {
  return v * scalar;
}

/**
 * @brief Scalar division (vector / scalar).
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector.
 * @param scalar Divisor.
 *
 * @return \p v with each component divided by \p scalar.
 *
 * @pre \p scalar is non-zero.
 * @post Each component equals the corresponding component of \p v divided by
 *       \p scalar.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator/(vector<Value, N> const& v, Value const scalar) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = v[i] / scalar;
  }
  return result;
}

/**
 * @brief Element-wise in-place addition.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Vector to add into.
 * @param b Right operand.
 *
 * @return Reference to \p a.
 *
 * @pre None.
 * @post \p a holds the component-wise sum of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator+=(vector<Value, N>& a, vector<Value, N> const& b) noexcept -> vector<Value, N>& {
  for (std::size_t i{0}; i < N; ++i) {
    a[i] += b[i];
  }
  return a;
}

/**
 * @brief Element-wise in-place subtraction.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Vector to subtract from.
 * @param b Right operand.
 *
 * @return Reference to \p a.
 *
 * @pre None.
 * @post \p a holds the component-wise difference of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator-=(vector<Value, N>& a, vector<Value, N> const& b) noexcept -> vector<Value, N>& {
  for (std::size_t i{0}; i < N; ++i) {
    a[i] -= b[i];
  }
  return a;
}

/**
 * @brief Element-wise in-place multiplication.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Vector to multiply into.
 * @param b Right operand.
 *
 * @return Reference to \p a.
 *
 * @pre None.
 * @post \p a holds the component-wise product of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator*=(vector<Value, N>& a, vector<Value, N> const& b) noexcept -> vector<Value, N>& {
  for (std::size_t i{0}; i < N; ++i) {
    a[i] *= b[i];
  }
  return a;
}

/**
 * @brief Element-wise in-place division.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a Vector to divide.
 * @param b Divisor.
 *
 * @return Reference to \p a.
 *
 * @pre No component of \p b is zero.
 * @post \p a holds the component-wise quotient of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator/=(vector<Value, N>& a, vector<Value, N> const& b) noexcept -> vector<Value, N>& {
  for (std::size_t i{0}; i < N; ++i) {
    a[i] /= b[i];
  }
  return a;
}

/**
 * @brief In-place scalar multiplication.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector to scale.
 * @param scalar Scaling factor.
 *
 * @return Reference to \p v.
 *
 * @pre None.
 * @post Each component of \p v is multiplied by \p scalar.
 */
template <arithmetic Value, std::size_t N>
constexpr auto operator*=(vector<Value, N>& v, Value const scalar) noexcept -> vector<Value, N>& {
  for (std::size_t i{0}; i < N; ++i) {
    v[i] *= scalar;
  }
  return v;
}

/**
 * @brief In-place scalar division.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector to divide.
 * @param scalar Divisor.
 *
 * @return Reference to \p v.
 *
 * @pre \p scalar is non-zero.
 * @post Each component of \p v is divided by \p scalar.
 */
template <arithmetic Value, std::size_t N>
constexpr auto operator/=(vector<Value, N>& v, Value const scalar) noexcept -> vector<Value, N>& {
  for (std::size_t i{0}; i < N; ++i) {
    v[i] /= scalar;
  }
  return v;
}

}  // namespace nexenne::math
