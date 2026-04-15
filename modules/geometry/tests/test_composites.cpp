/**
 * @file
 * @brief Tests for the nexenne::geometry composite shapes (Phase 2).
 */

#include <doctest/doctest.h>

#include <array>

#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/frustum.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/polygon.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/projection.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec2 = nm::vector<double, 2>;
using vec3 = nm::vector<double, 3>;

TEST_CASE("obb2: area, contains, and a 90-degree rotation") {
  // A unit-half-size box at the origin rotated 90 degrees is still axis-aligned.
  geo::obb2_d const box{vec2{0, 0}, vec2{2, 1}, nm::radians<double>{nm::half_pi}};
  CHECK(geo::area(box) == doctest::Approx(8.0));
  // After a quarter turn the local x half-extent (2) lies along world y.
  CHECK(geo::contains_point(box, vec2{0.9, 1.9}));
  CHECK_FALSE(geo::contains_point(box, vec2{1.1, 0}));
  auto const bound{geo::bounding_aabb(box)};
  CHECK(bound.min().x() == doctest::Approx(-1.0));
  CHECK(bound.max().y() == doctest::Approx(2.0));
}

TEST_CASE("obb3: volume, containment, and corners are constexpr") {
  constexpr geo::obb3_d box{vec3{0, 0, 0}, vec3{1, 2, 3}, nm::quaternion<double>{}};
  static_assert(geo::volume(box) == 48.0);
  static_assert(geo::contains_point(box, vec3{1, 2, 3}));
  static_assert(!geo::contains_point(box, vec3{1.01, 0, 0}));
  // Identity rotation: the axis-aligned bound matches the half-size extents.
  constexpr auto bound{geo::bounding_aabb(box)};
  static_assert(bound.max().z() == 3.0);
  CHECK(geo::corners(box).size() == 8);
}

TEST_CASE("capsule: length, area/volume, containment, closest point, bounds") {
  geo::capsule3_d const c{vec3{0, 0, 0}, vec3{0, 0, 4}, 1.0};
  CHECK(geo::length(c) == doctest::Approx(4.0));
  CHECK(geo::volume(c) == doctest::Approx(nm::pi * 4.0 + 4.0 / 3.0 * nm::pi));
  CHECK(geo::contains_point(c, vec3{0.5, 0, 2}));
  CHECK_FALSE(geo::contains_point(c, vec3{1.5, 0, 2}));
  // A point beside the spine projects to the spine then pushes out by radius.
  CHECK(geo::closest_point(c, vec3{3, 0, 2}) == vec3{1, 0, 2});
  auto const b{geo::bounding_aabb(c)};
  CHECK(b.min() == vec3{-1, -1, -1});
  CHECK(b.max() == vec3{1, 1, 5});

  geo::capsule2_d const c2{vec2{0, 0}, vec2{4, 0}, 1.0};
  CHECK(geo::area(c2) == doctest::Approx(8.0 + nm::pi));
}

TEST_CASE("polygon2: area, perimeter, centroid, containment, convexity") {
  std::array const square{vec2{0, 0}, vec2{4, 0}, vec2{4, 4}, vec2{0, 4}};
  geo::polygon2_d const poly{square};
  CHECK(geo::signed_area(poly) == doctest::Approx(16.0));  // counter-clockwise
  CHECK(geo::area(poly) == doctest::Approx(16.0));
  CHECK(geo::perimeter(poly) == doctest::Approx(16.0));
  CHECK(geo::centroid(poly) == vec2{2, 2});
  CHECK(geo::contains_point(poly, vec2{2, 2}));
  CHECK_FALSE(geo::contains_point(poly, vec2{5, 2}));
  CHECK(geo::convex(poly));

  // An arrow-head (concave) quad is not convex.
  std::array const concave{vec2{0, 0}, vec2{4, 2}, vec2{0, 4}, vec2{1, 2}};
  CHECK_FALSE(geo::convex(geo::polygon2_d{concave}));

  // Fully collinear vertices are degenerate, not convex (the audited fix).
  std::array const line{vec2{0, 0}, vec2{1, 0}, vec2{2, 0}};
  CHECK_FALSE(geo::convex(geo::polygon2_d{line}));
}

TEST_CASE("frustum3: planes extracted from a perspective matrix cull correctly") {
  // A standard perspective camera looking down -z.
  auto const proj{nm::perspective(nm::half_pi * 0.5, 1.0, 0.5, 100.0)};
  auto const f{geo::frustum_from_view_projection(proj)};

  // A sphere a little down -z, inside the frustum, is visible.
  CHECK(geo::intersects(f, geo::sphere3_d{vec3{0, 0, -10}, 1.0}));
  // A sphere behind the camera is culled.
  CHECK_FALSE(geo::intersects(f, geo::sphere3_d{vec3{0, 0, 10}, 1.0}));
  // A small box on the -z axis is visible; one far behind is culled.
  CHECK(geo::intersects(f, geo::aabb3_d{vec3{-1, -1, -11}, vec3{1, 1, -9}}));
  CHECK_FALSE(geo::intersects(f, geo::aabb3_d{vec3{-1, -1, 200}, vec3{1, 1, 202}}));

  // The named accessor returns a unit-normal plane.
  auto const near{geo::plane_of(f, geo::frustum_plane::near_plane)};
  CHECK(nm::length(near.normal()) == doctest::Approx(1.0));
}

TEST_CASE("obb3: corner-free bounding box of a rotated box") {
  // A unit cube turned 45 degrees about z: its xy footprint is a diamond reaching
  // sqrt(2) on each axis, z unchanged. This exercises the abs(R) * half formula
  // (no corner enumeration) and would catch a transpose in the rotation matrix.
  auto const rot{*nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<double>{nm::quarter_pi})};
  geo::obb3_d const box{vec3{0, 0, 0}, vec3{1, 1, 1}, rot};
  auto const b{geo::bounding_aabb(box)};
  CHECK(b.max().x() == doctest::Approx(nm::sqrt_two));
  CHECK(b.max().y() == doctest::Approx(nm::sqrt_two));
  CHECK(b.max().z() == doctest::Approx(1.0));
  CHECK(b.min().x() == doctest::Approx(-nm::sqrt_two));
}

}  // namespace
