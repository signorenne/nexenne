/**
 * @file
 * @brief Tests for the nexenne::geometry cross-type queries (Phase 3).
 */

#include <doctest/doctest.h>

#include <optional>

#include <nexenne/geometry/closest_point.hpp>
#include <nexenne/geometry/intersect.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec2 = nm::vector<double, 2>;
using vec3 = nm::vector<double, 3>;

// A unit-length direction for rays.
[[nodiscard]] auto unit(vec3 const v) -> vec3 {
  return *nm::normalize(v);
}

TEST_CASE("closest_points: skew, parallel, and degenerate segments") {
  // Two perpendicular skew segments: the x-axis [0,2] and a z-lifted y-segment.
  geo::segment3_d const s1{vec3{0, 0, 0}, vec3{2, 0, 0}};
  geo::segment3_d const s2{vec3{1, -1, 1}, vec3{1, 1, 1}};
  auto const [p1, p2]{geo::closest_points(s1, s2)};
  CHECK(p1 == vec3{1, 0, 0});
  CHECK(p2 == vec3{1, 0, 1});

  // A degenerate (point) segment against a real one clamps to the foot.
  geo::segment3_d const pt{vec3{5, 3, 0}, vec3{5, 3, 0}};
  auto const [q1, q2]{geo::closest_points(pt, s1)};
  CHECK(q1 == vec3{5, 3, 0});
  CHECK(q2 == vec3{2, 0, 0});  // clamped to the near end of s1
}

TEST_CASE("ray vs plane / sphere / box / triangle") {
  auto const r{geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, -1})}};

  // Plane z = -5 with +z normal: hit at t = 5.
  auto const hp{geo::intersects(r, geo::plane3_d{vec3{0, 0, 1}, 5.0})};
  REQUIRE(hp.has_value());
  CHECK(*hp == doctest::Approx(5.0));

  // Sphere centered down -z: entry at 9.
  auto const hs{geo::intersects(r, geo::sphere3_d{vec3{0, 0, -10}, 1.0})};
  REQUIRE(hs.has_value());
  CHECK(*hs == doctest::Approx(9.0));
  CHECK_FALSE(geo::intersects(r, geo::sphere3_d{vec3{5, 0, -10}, 1.0}));  // misses

  // Box straddling the -z axis: slab entry at 9.
  auto const hb{geo::intersects(r, geo::aabb3_d{vec3{-1, -1, -11}, vec3{1, 1, -9}})};
  REQUIRE(hb.has_value());
  CHECK(*hb == doctest::Approx(9.0));

  // Moller-Trumbore: a triangle facing the ray at z = -4.
  geo::triangle3_d const tri{vec3{-1, -1, -4}, vec3{1, -1, -4}, vec3{0, 2, -4}};
  auto const ht{geo::intersects(r, tri)};
  REQUIRE(ht.has_value());
  CHECK(*ht == doctest::Approx(4.0));
  // A ray pointing away from the triangle misses.
  CHECK_FALSE(geo::intersects(geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, 1})}, tri));
}

TEST_CASE("segment vs plane: crossing, parallel, same-side") {
  auto const pl{geo::plane3_d{vec3{0, 0, 1}, 0.0}};  // the z = 0 plane
  auto const hit{geo::intersects(geo::segment3_d{vec3{0, 0, -1}, vec3{0, 0, 1}}, pl)};
  REQUIRE(hit.has_value());
  CHECK(*hit == vec3{0, 0, 0});
  // Both endpoints above the plane: no crossing.
  CHECK_FALSE(geo::intersects(geo::segment3_d{vec3{0, 0, 1}, vec3{0, 0, 2}}, pl).has_value());
}

TEST_CASE("sphere/circle vs box and plane") {
  geo::aabb3_d const box{vec3{0, 0, 0}, vec3{2, 2, 2}};
  CHECK(geo::intersects(geo::sphere3_d{vec3{3, 1, 1}, 1.5}, box));  // reaches a face
  CHECK_FALSE(geo::intersects(geo::sphere3_d{vec3{5, 1, 1}, 1.0}, box));
  CHECK(geo::intersects(geo::sphere3_d{vec3{1, 1, 5}, 1.0}, geo::plane3_d{vec3{0, 0, 1}, -4.5}));
}

TEST_CASE("OBB SAT: 2D and 3D overlap and separation") {
  // Two axis-aligned-ish 2D boxes; a 45-degree box overlapping one at the origin.
  geo::obb2_d const a{vec2{0, 0}, vec2{1, 1}, nm::radians<double>{0.0}};
  geo::obb2_d const b{vec2{1.5, 0}, vec2{1, 1}, nm::radians<double>{nm::quarter_pi}};
  CHECK(geo::intersects(a, b));
  geo::obb2_d const far{vec2{5, 0}, vec2{1, 1}, nm::radians<double>{nm::quarter_pi}};
  CHECK_FALSE(geo::intersects(a, far));

  // 3D: identity-rotation boxes, overlap vs separation along x.
  geo::obb3_d const c{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  CHECK(geo::intersects(c, geo::obb3_d{vec3{1.5, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}}));
  CHECK_FALSE(
    geo::intersects(c, geo::obb3_d{vec3{3, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}})
  );

  // AABB vs OBB reuses the same SAT.
  CHECK(geo::intersects(geo::aabb3_d{vec3{-1, -1, -1}, vec3{1, 1, 1}}, c));
}

TEST_CASE("ray vs OBB reduces to the local-frame box") {
  // A 3D OBB at the origin; a ray down -z must hit it.
  geo::obb3_d const box{vec3{0, 0, -5}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  auto const hit{geo::intersects(geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, -1})}, box)};
  REQUIRE(hit.has_value());
  CHECK(*hit == doctest::Approx(4.0));
}

TEST_CASE("capsule overlaps: vs sphere and vs capsule") {
  geo::capsule3_d const cap{vec3{0, 0, 0}, vec3{0, 0, 4}, 1.0};
  CHECK(geo::intersects(cap, geo::sphere3_d{vec3{1.5, 0, 2}, 1.0}));  // 1.5 <= 1 + 1
  CHECK_FALSE(geo::intersects(cap, geo::sphere3_d{vec3{3, 0, 2}, 0.5}));

  // Two parallel capsules 1.5 apart with radii summing to 2 overlap.
  geo::capsule3_d const cap2{vec3{1.5, 0, 0}, vec3{1.5, 0, 4}, 1.0};
  CHECK(geo::intersects(cap, cap2));
  geo::capsule3_d const cap3{vec3{3, 0, 0}, vec3{3, 0, 4}, 0.5};
  CHECK_FALSE(geo::intersects(cap, cap3));
}

TEST_CASE("OBB3 SAT accounts for rotation, not just identity axes") {
  geo::obb3_d const a{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  auto const rot{*nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<double>{nm::quarter_pi})};
  // Centered at (2.2, 0, 0): an axis-aligned box reaches only x = 1.2 and stays
  // clear of a, but the 45-degree box's diamond footprint reaches x = 2.2 - sqrt(2)
  // ~ 0.79 and overlaps. Only correct rotated axes give this verdict, so it would
  // catch a transpose in detail::axes via the rotation matrix.
  CHECK_FALSE(
    geo::intersects(a, geo::obb3_d{vec3{2.2, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}})
  );
  CHECK(geo::intersects(a, geo::obb3_d{vec3{2.2, 0, 0}, vec3{1, 1, 1}, rot}));
}

}  // namespace
