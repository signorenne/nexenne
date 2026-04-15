#pragma once

/**
 * @file
 * @brief View frustum (frustum3): a convex volume bounded by six planes.
 *
 * Stored as six planes in Hessian form with inward-facing normals, so a point is
 * inside when its signed distance to every plane is non-negative. Planes follow
 * the \c frustum_plane order: left, right, bottom, top, near, far.
 *
 * \c frustum_from_view_projection extracts the planes from a view-projection
 * matrix with the Gribb-Hartmann method: each plane is a sum or difference of
 * the matrix's fourth row and one of the first three, normalized so the normals
 * are unit length and signed distances are metric. The \c intersects overloads
 * (vs sphere, vs box) are conservative culling tests: they never reject a
 * visible primitive, but may keep one that lies just outside a frustum edge.
 * Everything is \c constexpr and \c noexcept and nothing allocates.
 *
 * Aliases: \c frustum3, \c frustum3_f (float), \c frustum3_d (double).
 */

#include <array>
#include <concepts>
#include <cstddef>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Canonical plane order inside a \c frustum3.
 */
enum class frustum_plane : std::size_t {
  left = 0,        ///< Left clipping plane.
  right = 1,       ///< Right clipping plane.
  bottom = 2,      ///< Bottom clipping plane.
  top = 3,         ///< Top clipping plane.
  near_plane = 4,  ///< Near clipping plane.
  far_plane = 5,   ///< Far clipping plane.
};

/**
 * @brief View frustum stored as six inward-facing planes.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class frustum3 {
public:
  using value_type = Real;
  using plane_type = plane3<value_type>;

private:
  std::array<plane_type, 6> m_planes{};

public:
  /**
   * @brief Constructs a frustum with six default (XY) planes.
   */
  constexpr frustum3() noexcept = default;

  /**
   * @brief Constructs a frustum from six inward-facing planes.
   *
   * @param planes The six planes in \c frustum_plane order.
   */
  constexpr explicit frustum3(std::array<plane_type, 6> const& planes) noexcept
      : m_planes{planes} {}

  /**
   * @brief The six planes.
   *
   * @return Const reference to the stored planes.
   */
  [[nodiscard]] constexpr auto planes() const noexcept -> std::array<plane_type, 6> const& {
    return m_planes;
  }

  /**
   * @brief Mutable access to the six planes.
   *
   * @return Reference to the stored planes.
   */
  [[nodiscard]] constexpr auto planes() noexcept -> std::array<plane_type, 6>& {
    return m_planes;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(frustum3 const&, frustum3 const&) noexcept = default;
};

using frustum3_f = frustum3<float>;
using frustum3_d = frustum3<double>;

/**
 * @brief Extracts the six frustum planes from a view-projection matrix.
 *
 * Uses the Gribb-Hartmann method. Works for any clip convention whose x and y
 * inequality is \c -w <= c <= w with either OpenGL or D3D depth, since the
 * convention is absorbed into the matrix passed in. Each plane normal is unit
 * length on return so \c signed_distance is the true Euclidean distance.
 *
 * @tparam Real Component type.
 * @param vp Combined view-projection matrix (or projection alone for a
 *           view-space frustum).
 *
 * @return A frustum whose planes face inward.
 *
 * @pre None.
 * @post Every plane has a unit-length normal (so \c signed_distance is metric),
 *       except a plane extracted as degenerate, which is left unnormalized.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto frustum_from_view_projection(nexenne::math::matrix<Real, 4> const& vp
) noexcept -> frustum3<Real> {
  auto raw{std::array<plane3<Real>, 6>{}};

  // Gribb-Hartmann. A point is in clip space when -w <= c <= w on each clip
  // coordinate c, and both c and w are rows of vp applied to the point: c is row
  // `axis` (x = 0, y = 1, z = 2), w is row 3. Rearranging w + c >= 0 and w - c >= 0
  // into (plane . point) >= 0 shows each frustum plane is exactly row 3 plus or
  // minus the axis row. The plus form gives the left/bottom/near planes, the minus
  // form the right/top/far. The (x, y, z) coefficients are the normal, the 4th the
  // offset d; the loop below normalizes so signed_distance is metric.
  auto const make_plane{[&](Real const sign, std::size_t const axis) {
    return plane3<Real>{
      nexenne::math::vector<Real, 3>{
        vp(3, 0) + sign * vp(axis, 0),
        vp(3, 1) + sign * vp(axis, 1),
        vp(3, 2) + sign * vp(axis, 2),
      },
      vp(3, 3) + sign * vp(axis, 3)
    };
  }};

  raw[static_cast<std::size_t>(frustum_plane::left)] = make_plane(Real{1}, 0);
  raw[static_cast<std::size_t>(frustum_plane::right)] = make_plane(Real{-1}, 0);
  raw[static_cast<std::size_t>(frustum_plane::bottom)] = make_plane(Real{1}, 1);
  raw[static_cast<std::size_t>(frustum_plane::top)] = make_plane(Real{-1}, 1);
  raw[static_cast<std::size_t>(frustum_plane::near_plane)] = make_plane(Real{1}, 2);
  raw[static_cast<std::size_t>(frustum_plane::far_plane)] = make_plane(Real{-1}, 2);

  for (auto i{std::size_t{0}}; i < 6; ++i) {
    auto const len{nexenne::math::length(raw[i].normal())};
    if (len > Real{0}) {
      auto const inv{Real{1} / len};
      raw[i] = plane3<Real>{raw[i].normal() * inv, raw[i].d() * inv};
    }
  }
  return frustum3<Real>{raw};
}

/**
 * @brief One named plane of the frustum.
 *
 * @tparam Real Component type.
 * @param f Frustum.
 * @param which Which plane to fetch.
 *
 * @return Const reference to the chosen plane.
 *
 * @pre None.
 * @post The frustum is unchanged.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
plane_of(frustum3<Real> const& f, frustum_plane const which) noexcept -> plane3<Real> const& {
  return f.planes()[static_cast<std::size_t>(which)];
}

/**
 * @brief Conservative frustum-versus-sphere overlap test.
 *
 * Returns \c true when the sphere lies at least partly inside. The sphere is
 * fully outside only when its center is farther than its radius beyond some
 * plane's outside half-space.
 *
 * @tparam Real Component type.
 * @param f Frustum.
 * @param s Sphere.
 *
 * @return \c true when the sphere is potentially visible.
 *
 * @pre Each plane of \p f has a unit-length normal.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(frustum3<Real> const& f, sphere3<Real> const& s) noexcept -> bool {
  for (auto const& pl : f.planes()) {
    if (signed_distance(pl, s.center()) < -s.radius()) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Conservative frustum-versus-box overlap test.
 *
 * Uses the projected-radius trick: for each plane, project the box's half-extent
 * onto the plane normal and reject when even the nearest corner is outside. It
 * can keep a box that straddles a frustum edge, which is acceptable for
 * broad-phase culling.
 *
 * @tparam Real Component type.
 * @param f Frustum.
 * @param box 3D box.
 *
 * @return \c true when the box is potentially visible.
 *
 * @pre Each plane of \p f has a unit-length normal and \p box is well-formed.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(frustum3<Real> const& f, aabb<Real, 3> const& box) noexcept -> bool {
  auto const c{center(box)};
  auto const half{half_size(box)};
  for (auto const& pl : f.planes()) {
    auto const r{
      half.x() * nexenne::math::abs(pl.normal().x())
      + half.y() * nexenne::math::abs(pl.normal().y())
      + half.z() * nexenne::math::abs(pl.normal().z())
    };
    if (signed_distance(pl, c) + r < Real{0}) {
      return false;
    }
  }
  return true;
}

}  // namespace nexenne::geometry
