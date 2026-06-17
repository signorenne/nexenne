/**
 * @file
 * @brief Tests for the nexenne::geometry primitive shapes (Phase 1).
 */

#include <doctest/doctest.h>

#include <optional>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec2 = nm::vector<double, 2>;
using vec3 = nm::vector<double, 3>;

constexpr double eps{1e-9};

TEST_CASE("aabb: construction, measures, and containment") {
  geo::aabb3_d const box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
  CHECK(geo::center(box) == vec3{0, 0, 0});
  CHECK(geo::size(box) == vec3{2, 2, 2});
  CHECK(geo::volume(box) == doctest::Approx(8.0));
  CHECK(geo::surface_area(box) == doctest::Approx(24.0));
  CHECK(geo::contains_point(box, vec3{0, 0, 0}));
  CHECK(geo::contains_point(box, vec3{1, 1, 1}));  // boundary inclusive
  CHECK_FALSE(geo::contains_point(box, vec3{2, 0, 0}));

  geo::aabb2_d const r{vec2{0, 0}, vec2{3, 2}};
  CHECK(geo::area(r) == doctest::Approx(6.0));
  CHECK(geo::perimeter(r) == doctest::Approx(10.0));
}

TEST_CASE("aabb: closest point, distance, union, and intersection") {
  geo::aabb3_d const box{vec3{0, 0, 0}, vec3{2, 2, 2}};
  CHECK(geo::closest_point(box, vec3{5, 1, 1}) == vec3{2, 1, 1});
  CHECK(geo::closest_point(box, vec3{1, 1, 1}) == vec3{1, 1, 1});  // inside
  CHECK(geo::distance(box, vec3{5, 1, 1}) == doctest::Approx(3.0));
  CHECK(geo::distance(box, vec3{1, 1, 1}) == doctest::Approx(0.0));

  geo::aabb3_d const a{vec3{0, 0, 0}, vec3{2, 2, 2}};
  geo::aabb3_d const b{vec3{1, 1, 1}, vec3{3, 3, 3}};
  CHECK(geo::intersects(a, b));
  CHECK(geo::intersection_of(a, b) == geo::aabb3_d{vec3{1, 1, 1}, vec3{2, 2, 2}});
  CHECK(geo::union_of(a, b) == geo::aabb3_d{vec3{0, 0, 0}, vec3{3, 3, 3}});
  CHECK(geo::empty(geo::intersection_of(a, geo::aabb3_d{vec3{5, 5, 5}, vec3{6, 6, 6}})));
}

TEST_CASE("aabb: empty_aabb seed folds to a tight box") {
  auto box{geo::empty_aabb<double, 2>()};
  CHECK(geo::empty(box));
  box = geo::expand_to_include(box, vec2{1, 2});
  box = geo::expand_to_include(box, vec2{-3, 4});
  CHECK(box.min() == vec2{-3, 2});
  CHECK(box.max() == vec2{1, 4});
}

TEST_CASE("sphere: measures, containment, closest point, intersection") {
  geo::sphere3_d const s{vec3{0, 0, 0}, 2.0};
  CHECK(geo::volume(s) == doctest::Approx(4.0 / 3.0 * nm::pi * 8.0));
  CHECK(geo::surface_area(s) == doctest::Approx(4.0 * nm::pi * 4.0));
  CHECK(geo::contains_point(s, vec3{1, 0, 0}));
  CHECK_FALSE(geo::contains_point(s, vec3{3, 0, 0}));

  CHECK(geo::closest_point(s, vec3{4, 0, 0}) == vec3{2, 0, 0});
  CHECK(geo::distance(s, vec3{5, 0, 0}) == doctest::Approx(3.0));
  CHECK(geo::distance(s, vec3{1, 0, 0}) == doctest::Approx(0.0));

  // center query falls back to the +x boundary point by convention
  CHECK(geo::closest_point_on_boundary(s, vec3{0, 0, 0}) == vec3{2, 0, 0});

  CHECK(geo::intersects(s, geo::sphere3_d{vec3{3, 0, 0}, 1.5}));
  CHECK_FALSE(geo::intersects(s, geo::sphere3_d{vec3{5, 0, 0}, 1.0}));
  CHECK(geo::bounding_aabb(s) == geo::aabb3_d{vec3{-2, -2, -2}, vec3{2, 2, 2}});
}

TEST_CASE("circle: measures, closest point, intersection, bounds") {
  geo::circle2_d const c{vec2{0, 0}, 2.0};
  CHECK(geo::area(c) == doctest::Approx(nm::pi * 4.0));
  CHECK(geo::circumference(c) == doctest::Approx(nm::tau * 2.0));
  CHECK(geo::contains_point(c, vec2{1, 1}));
  CHECK(geo::closest_point(c, vec2{4, 0}) == vec2{2, 0});
  CHECK(geo::distance(c, vec2{0, 5}) == doctest::Approx(3.0));
  CHECK(geo::intersects(c, geo::circle2_d{vec2{0, 3}, 1.5}));
  CHECK(geo::bounding_aabb(c) == geo::aabb2_d{vec2{-2, -2}, vec2{2, 2}});
}

TEST_CASE("plane: factories, distances, projection, degenerate") {
  auto const pl{geo::plane_from_point_normal(vec3{0, 0, 0}, vec3{0, 0, 2})};
  REQUIRE(pl.has_value());
  CHECK(nm::almost_equal(pl->normal(), vec3{0, 0, 1}));
  CHECK(geo::signed_distance(*pl, vec3{0, 0, 5}) == doctest::Approx(5.0));
  CHECK(geo::signed_distance(*pl, vec3{0, 0, -3}) == doctest::Approx(-3.0));
  CHECK(geo::distance(*pl, vec3{0, 0, -3}) == doctest::Approx(3.0));
  CHECK(geo::closest_point(*pl, vec3{4, 1, 7}) == vec3{4, 1, 0});
  CHECK(geo::contains_point(*pl, vec3{9, 9, 0}));

  auto const tri{geo::plane_from_three_points(vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0})};
  REQUIRE(tri.has_value());
  CHECK(nm::almost_equal(tri->normal(), vec3{0, 0, 1}));

  auto const bad{geo::plane_from_point_normal(vec3{0, 0, 0}, vec3{0, 0, 0})};
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == geo::geometry_error::degenerate_primitive);

  auto const collinear{geo::plane_from_three_points(vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{2, 0, 0})};
  REQUIRE_FALSE(collinear.has_value());
}

TEST_CASE("ray: construction, point at, closest point, degenerate") {
  auto const r{geo::ray_from_points(vec3{0, 0, 0}, vec3{10, 0, 0})};
  REQUIRE(r.has_value());
  CHECK(nm::almost_equal(r->direction(), vec3{1, 0, 0}));
  CHECK(geo::at(*r, 3.0) == vec3{3, 0, 0});

  // a point behind the origin clamps to the origin
  CHECK(geo::closest_point(*r, vec3{-5, 2, 0}) == vec3{0, 0, 0});
  CHECK(geo::closest_point(*r, vec3{4, 2, 0}) == vec3{4, 0, 0});
  CHECK(geo::distance(*r, vec3{4, 2, 0}) == doctest::Approx(2.0));

  auto const bad{geo::ray_from_points(vec3{1, 1, 1}, vec3{1, 1, 1})};
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == geo::geometry_error::degenerate_primitive);
}

TEST_CASE("segment: length, closest point, and 2D intersection") {
  geo::segment3_d const s{vec3{0, 0, 0}, vec3{4, 0, 0}};
  CHECK(geo::length(s) == doctest::Approx(4.0));
  CHECK(geo::at(s, 0.25) == vec3{1, 0, 0});
  CHECK(geo::closest_point(s, vec3{2, 3, 0}) == vec3{2, 0, 0});
  CHECK(geo::closest_point(s, vec3{-9, 0, 0}) == vec3{0, 0, 0});  // clamps to start
  CHECK(geo::distance(s, vec3{2, 3, 0}) == doctest::Approx(3.0));

  geo::segment2_d const a{vec2{0, 0}, vec2{2, 2}};
  geo::segment2_d const b{vec2{0, 2}, vec2{2, 0}};
  auto const hit{geo::intersects(a, b)};
  REQUIRE(hit.has_value());
  CHECK(hit->x() == doctest::Approx(1.0));
  CHECK(hit->y() == doctest::Approx(1.0));

  geo::segment2_d const parallel{vec2{0, 1}, vec2{2, 3}};
  CHECK_FALSE(geo::intersects(a, parallel).has_value());

  geo::segment2_d const apart{vec2{5, 5}, vec2{6, 6}};
  CHECK_FALSE(geo::intersects(a, apart).has_value());
}

TEST_CASE("triangle: centroid, area, normal, containment, bounds") {
  geo::triangle2_d const t2{vec2{0, 0}, vec2{4, 0}, vec2{0, 3}};
  CHECK(geo::area(t2) == doctest::Approx(6.0));
  CHECK(geo::signed_area(t2) == doctest::Approx(6.0));  // counter-clockwise
  CHECK(
    geo::signed_area(geo::triangle2_d{vec2{0, 0}, vec2{0, 3}, vec2{4, 0}}) == doctest::Approx(-6.0)
  );
  CHECK(geo::contains_point(t2, vec2{1, 1}));
  CHECK_FALSE(geo::contains_point(t2, vec2{3, 3}));
  CHECK(geo::centroid(t2).x() == doctest::Approx(4.0 / 3.0));

  geo::triangle3_d const t3{vec3{0, 0, 0}, vec3{2, 0, 0}, vec3{0, 2, 0}};
  CHECK(geo::area(t3) == doctest::Approx(2.0));
  auto const n{geo::normal(t3)};
  REQUIRE(n.has_value());
  CHECK(nm::almost_equal(*n, vec3{0, 0, 1}));

  auto const degenerate{geo::normal(geo::triangle3_d{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{2, 0, 0}})};
  REQUIRE_FALSE(degenerate.has_value());
  CHECK(degenerate.error() == geo::geometry_error::degenerate_primitive);

  CHECK(geo::bounding_aabb(t2) == geo::aabb2_d{vec2{0, 0}, vec2{4, 3}});
}

TEST_CASE("triangle: a degenerate triangle contains its supporting line, not off-line points") {
  // A single-point triangle reports containment for every point (documented).
  geo::triangle2_d const point{vec2{3, 3}, vec2{3, 3}, vec2{3, 3}};
  CHECK(geo::contains_point(point, vec2{100, 100}));
  // A collinear triangle contains points on its supporting line...
  geo::triangle2_d const line{vec2{0, 0}, vec2{1, 0}, vec2{2, 0}};
  CHECK(geo::contains_point(line, vec2{100, 0}));
  // ...but still excludes points off the line.
  CHECK_FALSE(geo::contains_point(line, vec2{1, 1}));
}

TEST_CASE("segment: collinear overlapping 2D segments report no crossing") {
  geo::segment2_d const a{vec2{0, 0}, vec2{4, 0}};
  geo::segment2_d const overlap{vec2{2, 0}, vec2{6, 0}};  // collinear, overlapping
  CHECK_FALSE(geo::intersects(a, overlap).has_value());
}

}  // namespace
