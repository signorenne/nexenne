#pragma once

/**
 * @file
 * @brief Oriented bounding box (a box with arbitrary rotation), 2D and 3D.
 *
 * Two flavours, each storing \c center, \c half_size, and a \c rotation:
 *   - \c obb2 carries the rotation as a \c radians angle.
 *   - \c obb3 carries it as a \c quaternion.
 *
 * The corners, containment test, and axis-aligned bound are provided here; full
 * OBB-OBB overlap via the Separating Axis Theorem lives in the cross-type
 * \c intersect.hpp header.
 *
 * Constexpr coverage differs between the two only because of the rotation type.
 * The 3D queries are \c constexpr: quaternion rotation is pure constexpr
 * arithmetic. The 2D queries are runtime-only because evaluating a \c radians
 * angle needs \c std::sin / \c std::cos, neither \c constexpr in C++23 (expected
 * to lift once P1383 lands; a caller needing a constexpr 2D OBB today can
 * substitute \c fast_sin / \c fast_cos from trigonometry.hpp at about 5e-7
 * accuracy). Everything is \c noexcept and nothing allocates.
 *
 * Aliases: \c obb2 / \c obb3, each with \c _f (float) and \c _d (double).
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <type_traits>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>

namespace nexenne::geometry {

/**
 * @brief Oriented 2D box: center, half-size, and a rotation angle.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class obb2 {
public:
  using value_type = Real;
  using vector_type = nexenne::math::vector<value_type, 2>;
  using rotation_type = nexenne::math::radians<value_type>;

private:
  vector_type m_center{};
  vector_type m_half_size{};
  rotation_type m_rotation{};

public:
  /**
   * @brief Constructs the degenerate point box at the origin with zero rotation.
   */
  constexpr obb2() noexcept = default;

  /**
   * @brief Constructs an oriented box from its center, half-size, and rotation.
   *
   * @param center Box center.
   * @param half_size Half-extent along each local axis.
   * @param rotation Rotation of the local frame, counter-clockwise.
   */
  constexpr obb2(vector_type center, vector_type half_size, rotation_type rotation) noexcept
      : m_center{center}, m_half_size{half_size}, m_rotation{rotation} {}

  /**
   * @brief Box center.
   *
   * @return Const reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() const noexcept -> vector_type const& {
    return m_center;
  }

  /**
   * @brief Half-extent along each local axis.
   *
   * @return Const reference to the stored half-size.
   */
  [[nodiscard]] constexpr auto half_size() const noexcept -> vector_type const& {
    return m_half_size;
  }

  /**
   * @brief Rotation of the local frame.
   *
   * @return Const reference to the stored rotation angle.
   */
  [[nodiscard]] constexpr auto rotation() const noexcept -> rotation_type const& {
    return m_rotation;
  }

  /**
   * @brief Mutable box center.
   *
   * @return Reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() noexcept -> vector_type& {
    return m_center;
  }

  /**
   * @brief Mutable half-size.
   *
   * @return Reference to the stored half-size.
   */
  [[nodiscard]] constexpr auto half_size() noexcept -> vector_type& {
    return m_half_size;
  }

  /**
   * @brief Mutable rotation.
   *
   * @return Reference to the stored rotation angle.
   */
  [[nodiscard]] constexpr auto rotation() noexcept -> rotation_type& {
    return m_rotation;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto operator<=>(obb2 const&, obb2 const&) noexcept = default;
};

using obb2_f = obb2<float>;
using obb2_d = obb2<double>;

static_assert(std::is_trivially_copyable_v<obb2_f>);
static_assert(std::is_standard_layout_v<obb2_f>);
static_assert(sizeof(obb2_f) == 5 * sizeof(float));

/**
 * @brief Area of the box: \c (2*hx) * (2*hy).
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The box area.
 *
 * @pre Both half-size components are non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto area(obb2<Real> const& box) noexcept -> Real {
  return Real{4} * box.half_size().x() * box.half_size().y();
}

/**
 * @brief Perimeter of the box: \c 2 * (2*hx + 2*hy).
 *
 * Rotation does not change the edge lengths, so this matches the axis-aligned
 * formula.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The box perimeter.
 *
 * @pre Both half-size components are non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto perimeter(obb2<Real> const& box) noexcept -> Real {
  return Real{4} * (box.half_size().x() + box.half_size().y());
}

/**
 * @brief The four corners of a 2D oriented box, counter-clockwise.
 *
 * Local order \c (-hx,-hy), \c (+hx,-hy), \c (+hx,+hy), \c (-hx,+hy), then
 * rotated and translated into world space.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The four world-space corners in counter-clockwise order.
 *
 * @pre None.
 * @post The returned corners are in counter-clockwise order.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto corners(obb2<Real> const& box
) noexcept -> std::array<nexenne::math::vector<Real, 2>, 4> {
  auto const c{std::cos(box.rotation().value())};
  auto const s{std::sin(box.rotation().value())};
  auto const hx{box.half_size().x()};
  auto const hy{box.half_size().y()};
  auto const rotate{[&](Real const lx, Real const ly) {
    return nexenne::math::vector<Real, 2>{
      box.center().x() + c * lx - s * ly, box.center().y() + s * lx + c * ly
    };
  }};
  return {rotate(-hx, -hy), rotate(hx, -hy), rotate(hx, hy), rotate(-hx, hy)};
}

/**
 * @brief Reports whether \p p is inside the oriented box (boundary inclusive).
 *
 * Rotates \p p into the box's local frame by the inverse angle and tests against
 * the half-size on each axis.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 * @param p Query point.
 *
 * @return \c true when \p p is inside the closed box.
 *
 * @pre None.
 * @post None.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto
contains_point(obb2<Real> const& box, nexenne::math::vector<Real, 2> const& p) noexcept -> bool {
  auto const c{std::cos(box.rotation().value())};
  auto const s{std::sin(box.rotation().value())};
  auto const offset{p - box.center()};
  // Rotate the offset by -rotation to bring it into the box's local frame.
  auto const lx{c * offset.x() + s * offset.y()};
  auto const ly{-s * offset.x() + c * offset.y()};
  return nexenne::math::abs(lx) <= box.half_size().x()
         && nexenne::math::abs(ly) <= box.half_size().y();
}

/**
 * @brief Closest point on or inside a 2D oriented box to \p p.
 *
 * Rotates \p p into the box's local frame by the inverse angle, clamps it to the
 * half-size on each axis, and rotates the clamped point back into world space.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 * @param p Query point.
 *
 * @return Closest point on or in \p box.
 *
 * @pre None.
 * @post The result lies in the closed box (\c contains_point is true up to
 *       rounding).
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto closest_point(
  obb2<Real> const& box, nexenne::math::vector<Real, 2> const& p
) noexcept -> nexenne::math::vector<Real, 2> {
  auto const c{std::cos(box.rotation().value())};
  auto const s{std::sin(box.rotation().value())};
  auto const offset{p - box.center()};
  // Rotate the offset by -rotation into the box's local frame.
  auto const lx{c * offset.x() + s * offset.y()};
  auto const ly{-s * offset.x() + c * offset.y()};
  auto const clx{nexenne::math::clamp(lx, -box.half_size().x(), box.half_size().x())};
  auto const cly{nexenne::math::clamp(ly, -box.half_size().y(), box.half_size().y())};
  // Rotate the clamped local point back by +rotation into world space.
  return box.center() + nexenne::math::vector<Real, 2>{c * clx - s * cly, s * clx + c * cly};
}

/**
 * @brief Smallest axis-aligned box containing a rotated 2D oriented box.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The tight axis-aligned bound of \p box.
 *
 * @pre None.
 * @post The result is well-formed and contains every corner of \p box.
 */
template <std::floating_point Real>
[[nodiscard]] auto bounding_aabb(obb2<Real> const& box) noexcept -> aabb<Real, 2> {
  auto result{empty_aabb<Real, 2>()};
  for (auto const& corner : corners(box)) {
    result = expand_to_include(result, corner);
  }
  return result;
}

/**
 * @brief Oriented 3D box: center, half-size, and a rotation quaternion.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class obb3 {
public:
  using value_type = Real;
  using vector_type = nexenne::math::vector<value_type, 3>;
  using rotation_type = nexenne::math::quaternion<value_type>;

private:
  vector_type m_center{};
  vector_type m_half_size{};
  rotation_type m_rotation{};

public:
  /**
   * @brief Constructs the degenerate point box at the origin with identity rotation.
   */
  constexpr obb3() noexcept = default;

  /**
   * @brief Constructs an oriented box from its center, half-size, and rotation.
   *
   * @param center Box center.
   * @param half_size Half-extent along each local axis.
   * @param rotation Orientation of the local frame, expected unit length.
   */
  constexpr obb3(vector_type center, vector_type half_size, rotation_type rotation) noexcept
      : m_center{center}, m_half_size{half_size}, m_rotation{rotation} {}

  /**
   * @brief Box center.
   *
   * @return Const reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() const noexcept -> vector_type const& {
    return m_center;
  }

  /**
   * @brief Half-extent along each local axis.
   *
   * @return Const reference to the stored half-size.
   */
  [[nodiscard]] constexpr auto half_size() const noexcept -> vector_type const& {
    return m_half_size;
  }

  /**
   * @brief Orientation of the local frame.
   *
   * @return Const reference to the stored rotation quaternion.
   */
  [[nodiscard]] constexpr auto rotation() const noexcept -> rotation_type const& {
    return m_rotation;
  }

  /**
   * @brief Mutable box center.
   *
   * @return Reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() noexcept -> vector_type& {
    return m_center;
  }

  /**
   * @brief Mutable half-size.
   *
   * @return Reference to the stored half-size.
   */
  [[nodiscard]] constexpr auto half_size() noexcept -> vector_type& {
    return m_half_size;
  }

  /**
   * @brief Mutable rotation.
   *
   * @return Reference to the stored rotation quaternion.
   */
  [[nodiscard]] constexpr auto rotation() noexcept -> rotation_type& {
    return m_rotation;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto operator<=>(obb3 const&, obb3 const&) noexcept = default;
};

using obb3_f = obb3<float>;
using obb3_d = obb3<double>;

static_assert(std::is_trivially_copyable_v<obb3_f>);
static_assert(std::is_standard_layout_v<obb3_f>);
static_assert(sizeof(obb3_f) == 10 * sizeof(float));

/**
 * @brief Volume of the box: \c (2*hx) * (2*hy) * (2*hz).
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The box volume.
 *
 * @pre All half-size components are non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto volume(obb3<Real> const& box) noexcept -> Real {
  return Real{8} * box.half_size().x() * box.half_size().y() * box.half_size().z();
}

/**
 * @brief Surface area of the box: \c 8 * (hx*hy + hy*hz + hz*hx).
 *
 * Rotation does not change any face area, so this matches the axis-aligned
 * formula.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The box surface area.
 *
 * @pre All half-size components are non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto surface_area(obb3<Real> const& box) noexcept -> Real {
  auto const h{box.half_size()};
  return Real{8} * (h.x() * h.y() + h.y() * h.z() + h.z() * h.x());
}

/**
 * @brief The eight corners of a 3D oriented box.
 *
 * Corner \c i carries the sign pattern \c (zyx): \c x is positive when
 * \c (i & 1), \c y when \c (i & 2), \c z when \c (i & 4), each scaled by the
 * matching half-size, rotated, and translated into world space.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The eight world-space corners.
 *
 * @pre \c box.rotation() has unit length.
 * @post Corner \c i carries the sign pattern described above.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto corners(obb3<Real> const& box
) noexcept -> std::array<nexenne::math::vector<Real, 3>, 8> {
  auto result{std::array<nexenne::math::vector<Real, 3>, 8>{}};
  auto const hx{box.half_size().x()};
  auto const hy{box.half_size().y()};
  auto const hz{box.half_size().z()};
  for (auto i{std::size_t{0}}; i < 8; ++i) {
    auto const sx{((i & 1) != 0) ? hx : -hx};
    auto const sy{((i & 2) != 0) ? hy : -hy};
    auto const sz{((i & 4) != 0) ? hz : -hz};
    result[i] = box.center()
                + nexenne::math::rotate(box.rotation(), nexenne::math::vector<Real, 3>{sx, sy, sz});
  }
  return result;
}

/**
 * @brief Reports whether \p p is inside the oriented box (boundary inclusive).
 *
 * Rotates \p p into the box's local frame with the inverse rotation (the
 * conjugate, since the quaternion is unit length) and tests against the
 * half-size on each axis.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 * @param p Query point.
 *
 * @return \c true when \p p is inside the closed box.
 *
 * @pre \c box.rotation() has unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
contains_point(obb3<Real> const& box, nexenne::math::vector<Real, 3> const& p) noexcept -> bool {
  auto const local{nexenne::math::rotate(nexenne::math::conjugate(box.rotation()), p - box.center())
  };
  return nexenne::math::abs(local.x()) <= box.half_size().x()
         && nexenne::math::abs(local.y()) <= box.half_size().y()
         && nexenne::math::abs(local.z()) <= box.half_size().z();
}

/**
 * @brief Closest point on or inside a 3D oriented box to \p p.
 *
 * Rotates \p p into the box's local frame (the conjugate rotation), clamps it to
 * the half-size on each axis, and rotates the clamped point back into world space.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 * @param p Query point.
 *
 * @return Closest point on or in \p box.
 *
 * @pre \c box.rotation() has unit length.
 * @post The result lies in the closed box (\c contains_point is true up to
 *       rounding).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto closest_point(
  obb3<Real> const& box, nexenne::math::vector<Real, 3> const& p
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const local{nexenne::math::rotate(nexenne::math::conjugate(box.rotation()), p - box.center())
  };
  auto const h{box.half_size()};
  auto const clamped{nexenne::math::vector<Real, 3>{
    nexenne::math::clamp(local.x(), -h.x(), h.x()),
    nexenne::math::clamp(local.y(), -h.y(), h.y()),
    nexenne::math::clamp(local.z(), -h.z(), h.z()),
  }};
  return box.center() + nexenne::math::rotate(box.rotation(), clamped);
}

/**
 * @brief Smallest axis-aligned box containing a rotated 3D oriented box.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The tight axis-aligned bound of \p box.
 *
 * @pre \c box.rotation() has unit length.
 * @post The result is well-formed and contains every corner of \p box.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto bounding_aabb(obb3<Real> const& box) noexcept -> aabb<Real, 3> {
  // The world half-extent of a rotated box is abs(R) * half: each world axis k
  // spans the sum over the local axes j of |R(k, j)| * half[j], because the box's
  // furthest reach along world axis k folds every local half-axis in by its
  // absolute projection. This is exact and far cheaper than rotating all eight
  // corners (one quaternion-to-matrix build plus nine multiply-adds, versus eight
  // quaternion rotations). See Arvo, "Transforming Axis-Aligned Bounding Boxes",
  // Graphics Gems (1990), or Ericson, RTCD section 4.2.6.
  auto const r{nexenne::math::to_matrix3(box.rotation())};
  auto const h{box.half_size()};
  auto world_half{nexenne::math::vector<Real, 3>{}};
  for (auto k{std::size_t{0}}; k < 3; ++k) {
    world_half[k] = nexenne::math::abs(r(k, 0)) * h.x() + nexenne::math::abs(r(k, 1)) * h.y()
                    + nexenne::math::abs(r(k, 2)) * h.z();
  }
  return aabb<Real, 3>{box.center() - world_half, box.center() + world_half};
}

}  // namespace nexenne::geometry
