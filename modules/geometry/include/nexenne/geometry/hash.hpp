#pragma once

/**
 * @file
 * @brief \c std::hash specializations for the concrete geometry value types.
 *
 * Lets every trivially-copyable geometry primitive be a key in
 * \c std::unordered_map / \c std::unordered_set without each caller writing its
 * own hash. Every specialization folds the shape's fields through the math
 * module's vector hash via \c nexenne::utility::hash_combine, so a vector's hash
 * is the single source of truth and equal shapes hash equal.
 *
 * \c polygon2 and \c convex_hull3 are intentionally NOT hashable: they hold a
 * \c std::span, so their identity is the backing storage, not the value. A
 * caller who wants one can hash the underlying vertex range directly.
 *
 * Including this header is all that is needed; the specializations live at global
 * scope as the standard requires.
 */

#include <cstddef>
#include <functional>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/transform.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/concepts.hpp>
#include <nexenne/math/hash.hpp>
#include <nexenne/utility/hash.hpp>

/**
 * @brief Hashes an axis-aligned box by its \c min and \c max corners.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::hash<nexenne::geometry::aabb<Value, N>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param a Box to hash.
   *
   * @return Hash combining \c min and \c max.
   *
   * @pre None.
   * @post Equal boxes hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::aabb<Value, N> const& a
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, a.min());
    nexenne::utility::hash_combine(seed, a.max());
    return seed;
  }
};

/**
 * @brief Hashes a 2D circle by its center and radius.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::circle2<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param c Circle to hash.
   *
   * @return Hash combining \c center and \c radius.
   *
   * @pre None.
   * @post Equal circles hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::circle2<Real> const& c
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, c.center());
    nexenne::utility::hash_combine(seed, c.radius());
    return seed;
  }
};

/**
 * @brief Hashes a 3D sphere by its center and radius.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::sphere3<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param s Sphere to hash.
   *
   * @return Hash combining \c center and \c radius.
   *
   * @pre None.
   * @post Equal spheres hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::sphere3<Real> const& s
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, s.center());
    nexenne::utility::hash_combine(seed, s.radius());
    return seed;
  }
};

/**
 * @brief Hashes a ray by its origin and direction.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::hash<nexenne::geometry::ray<Real, N>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param r Ray to hash.
   *
   * @return Hash combining \c origin and \c direction.
   *
   * @pre None.
   * @post Equal rays hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::ray<Real, N> const& r
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, r.origin());
    nexenne::utility::hash_combine(seed, r.direction());
    return seed;
  }
};

/**
 * @brief Hashes a segment by its two endpoints.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::hash<nexenne::geometry::segment<Real, N>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param s Segment to hash.
   *
   * @return Hash combining \c start and \c end.
   *
   * @pre None.
   * @post Equal segments hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::segment<Real, N> const& s
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, s.start());
    nexenne::utility::hash_combine(seed, s.end());
    return seed;
  }
};

/**
 * @brief Hashes a plane by its normal and offset.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::plane3<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param p Plane to hash.
   *
   * @return Hash combining \c normal and \c d.
   *
   * @pre None.
   * @post Equal planes hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::plane3<Real> const& p
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, p.normal());
    nexenne::utility::hash_combine(seed, p.d());
    return seed;
  }
};

/**
 * @brief Hashes a triangle by its three vertices in order.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::hash<nexenne::geometry::triangle<Real, N>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param t Triangle to hash.
   *
   * @return Hash combining \c a, \c b, and \c c.
   *
   * @pre None.
   * @post Equal triangles hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::triangle<Real, N> const& t
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, t.a());
    nexenne::utility::hash_combine(seed, t.b());
    nexenne::utility::hash_combine(seed, t.c());
    return seed;
  }
};

/**
 * @brief Hashes a capsule by its two spine endpoints and radius.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::hash<nexenne::geometry::capsule<Real, N>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param c Capsule to hash.
   *
   * @return Hash combining \c start, \c end, and \c radius.
   *
   * @pre None.
   * @post Equal capsules hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::capsule<Real, N> const& c
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, c.start());
    nexenne::utility::hash_combine(seed, c.end());
    nexenne::utility::hash_combine(seed, c.radius());
    return seed;
  }
};

/**
 * @brief Hashes a 2D oriented box by its center, half-size, and angle.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::obb2<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param o Box to hash.
   *
   * @return Hash combining \c center, \c half_size, and the rotation angle.
   *
   * @pre None.
   * @post Equal boxes hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::obb2<Real> const& o
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, o.center());
    nexenne::utility::hash_combine(seed, o.half_size());
    nexenne::utility::hash_combine(seed, o.rotation().value());
    return seed;
  }
};

/**
 * @brief Hashes a 3D oriented box by its center, half-size, and rotation.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::obb3<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param o Box to hash.
   *
   * @return Hash combining \c center, \c half_size, and the rotation quaternion.
   *
   * @pre None.
   * @post Equal boxes hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::obb3<Real> const& o
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, o.center());
    nexenne::utility::hash_combine(seed, o.half_size());
    nexenne::utility::hash_combine(seed, o.rotation());
    return seed;
  }
};

/**
 * @brief Hashes a 2D pose by its position, angle, and scale.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::transform2d<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param t Pose to hash.
   *
   * @return Hash combining \c position, the rotation angle, and \c scale.
   *
   * @pre None.
   * @post Equal poses hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::transform2d<Real> const& t
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, t.position());
    nexenne::utility::hash_combine(seed, t.rotation().value());
    nexenne::utility::hash_combine(seed, t.scale());
    return seed;
  }
};

/**
 * @brief Hashes a 3D pose by its position, rotation, and scale.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::hash<nexenne::geometry::transform3d<Real>> {
  /**
   * @brief Computes the composite hash.
   *
   * @param t Pose to hash.
   *
   * @return Hash combining \c position, the rotation quaternion, and \c scale.
   *
   * @pre None.
   * @post Equal poses hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::geometry::transform3d<Real> const& t
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, t.position());
    nexenne::utility::hash_combine(seed, t.rotation());
    nexenne::utility::hash_combine(seed, t.scale());
    return seed;
  }
};
