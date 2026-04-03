#pragma once

/**
 * @file
 * @brief Strong-typed wrapper for a vector guaranteed to be unit length.
 *
 * APIs that consume directions (reflection normals, projection axes, lighting
 * directions) can take \c normalized<Real, N> instead of a raw \c vector and so
 * skip both the "must be normalized" precondition and any defensive runtime
 * re-normalization: the type carries the guarantee.
 *
 * Construction goes through one of two factories: \c make_unchecked(v) trusts the
 * caller and does no math (for known unit vectors, such as an axis basis), while
 * \c make_normalized(v) re-normalizes and returns a \c result, failing when \p v
 * is too short to normalize stably.
 *
 * The wrapped vector is reachable via \c .value(), and an implicit conversion to
 * \c vector const& lets a \c normalized be passed wherever a vector is expected.
 * One caveat: C++ template-argument deduction does not see through a user-defined
 * conversion, so a generic algorithm must be called as \c dot(n.value(), ...),
 * not \c dot(n, ...); the implicit conversion still applies in non-template
 * contexts. The value is read-only after construction: a mutable accessor would
 * let a caller store a non-unit vector and silently break the invariant every
 * consumer relies on, so only a const accessor is exposed.
 */

#include <concepts>
#include <cstddef>

#include <nexenne/math/error.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::math {

/**
 * @brief Newtype wrapper marking a vector as unit length.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 */
template <std::floating_point Real, std::size_t N>
class normalized {
public:
  using value_type = Real;                    ///< The component type.
  using vector_type = vector<value_type, N>;  ///< The wrapped vector type.

private:
  vector_type m_value{};

public:
  /**
   * @brief Constructs a zero-initialized wrapper.
   *
   * @pre None.
   * @post The wrapped vector is value-initialized to zero.
   */
  constexpr normalized() noexcept = default;

  /**
   * @brief Wraps \p v without checking or normalizing it.
   *
   * @param v Vector assumed to be unit length.
   *
   * @pre \p v has unit length.
   * @post The wrapped vector equals \p v.
   */
  constexpr explicit normalized(vector_type const& v) noexcept : m_value{v} {}

  /**
   * @brief Accesses the wrapped vector.
   *
   * @return Const reference to the underlying vector.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> vector_type const& {
    return m_value;
  }

  /**
   * @brief Implicit conversion to the wrapped vector.
   *
   * Lets a \c normalized be passed to any non-generic function taking a vector.
   * Template-deduction sites still need an explicit \c .value() (deduction does
   * not see through user-defined conversions).
   *
   * @return Const reference to the wrapped vector.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr operator vector_type const&() const noexcept {
    return m_value;
  }
};

/**
 * @brief Wraps \p v as \c normalized with no check or normalization.
 *
 * Use only when the caller has independently established that \p v is unit length
 * (for example the axis-aligned unit basis vectors).
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector assumed to be unit length.
 *
 * @return The wrapped vector.
 *
 * @pre \p v has unit length.
 * @post The wrapped vector equals \p v.
 *
 * @warning Bypasses the unit-length contract; passing a non-unit vector silently
 *          breaks every consumer of the result.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto make_unchecked(vector<Real, N> const& v
) noexcept -> normalized<Real, N> {
  return normalized<Real, N>{v};
}

/**
 * @brief Wraps \p v as \c normalized after a safe normalization.
 *
 * Re-normalizes \p v and wraps the result, returning an error when \p v is too
 * short to normalize stably (see \c normalize in vector_algorithms.hpp).
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector to normalize and wrap.
 * @param threshold Minimum allowed squared length. Default 1e-20.
 *
 * @return The wrapped unit vector, or \c math_error::zero_length_vector when too
 *         short.
 *
 * @pre Components of \p v are finite.
 * @post On success the wrapped vector has unit length.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto make_normalized(
  vector<Real, N> const& v, Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<normalized<Real, N>> {
  auto const normalized_v{normalize(v, threshold)};
  if (!normalized_v) {
    return std::unexpected{normalized_v.error()};
  }
  return normalized<Real, N>{*normalized_v};
}

}  // namespace nexenne::math
