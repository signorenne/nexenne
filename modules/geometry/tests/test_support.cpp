/**
 * @file
 * @brief Tests for the analytic-primitive support mappings (support.hpp): each
 *        shape's furthest-point query and a GJK/EPA collision on primitives.
 */

#include <doctest/doctest.h>

#include <array>
#include <span>

#include <nexenne/geometry/concepts.hpp>
#include <nexenne/geometry/epa.hpp>
#include <nexenne/geometry/gjk.hpp>
#include <nexenne/geometry/support.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec3 = nm::vector<float, 3>;

// Every analytic primitive must model convex_shape so GJK and EPA accept it.
static_assert(geo::convex_shape<geo::sphere3_f, float>);
static_assert(geo::convex_shape<geo::obb3_f, float>);
static_assert(geo::convex_shape<geo::capsule3_f, float>);
static_assert(geo::convex_shape<geo::aabb<float, 3>, float>);
static_assert(geo::convex_shape<geo::triangle3_f, float>);
static_assert(geo::convex_shape<geo::segment3_f, float>);

TEST_CASE("support: sphere returns the boundary cap along the direction") {
  geo::sphere3_f const s{vec3{1, 2, 3}, 2.0f};
  CHECK(support(s, vec3{1, 0, 0}) == vec3{3, 2, 3});
  CHECK(support(s, vec3{0, -1, 0}) == vec3{1, 0, 3});
  // A zero direction is degenerate; the +x cap is returned by convention.
  CHECK(support(s, vec3{0, 0, 0}) == vec3{3, 2, 3});
}

TEST_CASE("support: capsule grows its furthest spine endpoint by the radius") {
  geo::capsule3_f const c{vec3{0, 0, 0}, vec3{4, 0, 0}, 1.0f};
  // +x picks the far endpoint, then pushes out by the radius along +x.
  CHECK(support(c, vec3{1, 0, 0}) == vec3{5, 0, 0});
  // -x picks the near endpoint, then pushes out by the radius along -x.
  CHECK(support(c, vec3{-1, 0, 0}) == vec3{-1, 0, 0});
  // +y ties on the spine projection, so an endpoint is chosen and lifted by r.
  CHECK(support(c, vec3{0, 1, 0}).y() == doctest::Approx(1.0f));
}

TEST_CASE("support: an identity obb supports to its corner") {
  geo::obb3_f const box{vec3{0, 0, 0}, vec3{1, 2, 3}, nm::quaternion<float>{}};
  CHECK(support(box, vec3{1, 1, 1}) == vec3{1, 2, 3});
  CHECK(support(box, vec3{-1, -1, -1}) == vec3{-1, -2, -3});
  CHECK(support(box, vec3{1, -1, 0}).x() == doctest::Approx(1.0f));
  CHECK(support(box, vec3{1, -1, 0}).y() == doctest::Approx(-2.0f));
}

TEST_CASE("support: a rotated obb supports to a rotated corner") {
  // 90 degrees about +z maps local +x onto world +y, so a half-extent of
  // (2, 1, 1) reaches y == 2 along +y.
  auto const q{nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<float>{nm::pi_v<float> / 2.0f})};
  REQUIRE(q.has_value());
  geo::obb3_f const box{vec3{0, 0, 0}, vec3{2, 1, 1}, *q};
  CHECK(support(box, vec3{0, 1, 0}).y() == doctest::Approx(2.0f));
}

TEST_CASE("support: aabb picks the corner by the sign of each direction component") {
  geo::aabb<float, 3> const box{vec3{-1, -1, -1}, vec3{2, 3, 4}};
  CHECK(support(box, vec3{1, 1, 1}) == vec3{2, 3, 4});
  CHECK(support(box, vec3{-1, 1, -1}) == vec3{-1, 3, -1});
}

TEST_CASE("support: triangle and segment return the furthest vertex") {
  geo::triangle3_f const t{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0}};
  CHECK(support(t, vec3{1, 0, 0}) == vec3{1, 0, 0});
  CHECK(support(t, vec3{0, 1, 0}) == vec3{0, 1, 0});
  CHECK(support(t, vec3{-1, -1, 0}) == vec3{0, 0, 0});

  geo::segment3_f const s{vec3{-2, 0, 0}, vec3{5, 0, 0}};
  CHECK(support(s, vec3{1, 0, 0}) == vec3{5, 0, 0});
  CHECK(support(s, vec3{-1, 0, 0}) == vec3{-2, 0, 0});
}

TEST_CASE("support: the closed-form mappings are constexpr") {
  constexpr auto ok{[] {
    geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
    geo::obb3_f const box{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<float>{}};
    geo::aabb<float, 3> const ax{vec3{-1, -1, -1}, vec3{1, 1, 1}};
    return support(s, vec3{1, 0, 0}) == vec3{1, 0, 0}
           && support(box, vec3{1, 1, 1}) == vec3{1, 1, 1}
           && support(ax, vec3{-1, -1, -1}) == vec3{-1, -1, -1};
  }()};
  static_assert(ok, "primitive support must be constexpr");
}

TEST_CASE("support: GJK and EPA run on a sphere-vs-obb overlap") {
  // A unit sphere at the origin and a box centered at (1,0,0) overlap: the box's
  // near face sits at x == 0, well inside the sphere of radius 1.
  geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
  geo::obb3_f const box{vec3{1, 0, 0}, vec3{1, 1, 1}, nm::quaternion<float>{}};

  auto const hit{geo::gjk<float>(s, box, vec3{1, 0, 0})};
  REQUIRE(hit.overlap);

  auto const contact{geo::epa<float>(s, box, hit.simplex)};
  CHECK(contact.converged);
  // The shapes separate along x; the minimum push-out is the overlap on that
  // axis: sphere reaches x == 1, box near face is at x == 0, so depth ~ 1.
  CHECK(contact.penetration_depth == doctest::Approx(1.0f).epsilon(0.05));
  CHECK(nm::abs(contact.normal.x()) == doctest::Approx(1.0f).epsilon(0.05));
}

TEST_CASE("support: GJK reports separation for disjoint primitives") {
  geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
  geo::obb3_f const box{vec3{5, 0, 0}, vec3{1, 1, 1}, nm::quaternion<float>{}};
  auto const hit{geo::gjk<float>(s, box, vec3{1, 0, 0})};
  CHECK_FALSE(hit.overlap);
}

}  // namespace
