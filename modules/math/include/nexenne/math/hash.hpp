#pragma once

/**
 * @file
 * @brief \c std::hash specializations for the nexenne math value types.
 *
 * Lets \c vector, \c matrix, and \c quaternion serve as keys in
 * \c std::unordered_map / \c std::unordered_set without each caller writing its
 * own hash. The element hashes are folded together with the utility module's
 * \c hash_combine recipe (the order-sensitive Boost mix), reused rather than
 * re-derived. Including this header is enough to enable hashing; the
 * specializations live at global scope as the standard requires.
 *
 * @note Hashing is bit-for-bit on the components, so two values that compare
 *       equal hash equally, but the usual floating-point caveats apply (for
 *       example +0.0 and -0.0 are equal yet hash differently).
 */

#include <cstddef>

#include <nexenne/math/concepts.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/utility/hash.hpp>

/**
 * @brief Hashes a \c vector by folding its components in index order.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::hash<nexenne::math::vector<Value, N>> {
  /**
   * @brief Combined hash of every component.
   *
   * @param v Vector to hash.
   *
   * @return The composite hash.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator()(nexenne::math::vector<Value, N> const& v
  ) const noexcept -> std::size_t {
    return nexenne::utility::hash_range(v);  // vector models a range over its components
  }
};

/**
 * @brief Hashes a \c quaternion by folding x, y, z, w.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::math::quaternion<Real>> {
  /**
   * @brief Combined hash of the four components.
   *
   * @param q Quaternion to hash.
   *
   * @return The composite hash.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator()(nexenne::math::quaternion<Real> const& q
  ) const noexcept -> std::size_t {
    return nexenne::utility::hash_args(q.x(), q.y(), q.z(), q.w());
  }
};

/**
 * @brief Hashes a \c matrix by folding every element in column-major order.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::hash<nexenne::math::matrix<Value, N>> {
  /**
   * @brief Combined hash of every element.
   *
   * @param m Matrix to hash.
   *
   * @return The composite hash.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator()(nexenne::math::matrix<Value, N> const& m
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    for (std::size_t c{0}; c < N; ++c) {
      for (std::size_t r{0}; r < N; ++r) {
        nexenne::utility::hash_combine(seed, m(r, c));
      }
    }
    return seed;
  }
};
