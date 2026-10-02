/**
 * @file
 * @brief Tests for the nexenne::geometry composite shapes (Phase 2).
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>

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
  geo::obb2_d const box{vec2{0, 0}, vec2{2, 1}, nm::radians<double>{nm::half_pi}};
  CHECK(geo::area(box) == doctest::Approx(8.0));
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
  CHECK(geo::signed_area(poly) == doctest::Approx(16.0));
  CHECK(geo::area(poly) == doctest::Approx(16.0));
  CHECK(geo::perimeter(poly) == doctest::Approx(16.0));
  CHECK(geo::centroid(poly) == vec2{2, 2});
  CHECK(geo::contains_point(poly, vec2{2, 2}));
  CHECK_FALSE(geo::contains_point(poly, vec2{5, 2}));
  CHECK(geo::convex(poly));

  std::array const concave{vec2{0, 0}, vec2{4, 2}, vec2{0, 4}, vec2{1, 2}};
  CHECK_FALSE(geo::convex(geo::polygon2_d{concave}));

  std::array const line{vec2{0, 0}, vec2{1, 0}, vec2{2, 0}};
  CHECK_FALSE(geo::convex(geo::polygon2_d{line}));
}

TEST_CASE("frustum3: planes extracted from a perspective matrix cull correctly") {
  auto const proj{nm::perspective(nm::radians{nm::half_pi * 0.5}, 1.0, 0.5, 100.0)};
  auto const f{geo::frustum_from_view_projection(proj)};

  CHECK(geo::intersects(f, geo::sphere3_d{vec3{0, 0, -10}, 1.0}));
  CHECK_FALSE(geo::intersects(f, geo::sphere3_d{vec3{0, 0, 10}, 1.0}));
  CHECK(geo::intersects(f, geo::aabb3_d{vec3{-1, -1, -11}, vec3{1, 1, -9}}));
  CHECK_FALSE(geo::intersects(f, geo::aabb3_d{vec3{-1, -1, 200}, vec3{1, 1, 202}}));

  auto const near{geo::plane_of(f, geo::frustum_plane::near_plane)};
  CHECK(nm::length(near.normal()) == doctest::Approx(1.0));
}

TEST_CASE("frustum3: the near plane sits at z = -near for a GL matrix, not for ZO") {
  auto const near_z{0.5};
  auto const far_z{100.0};
  auto const gl{nm::perspective(nm::radians{nm::half_pi * 0.5}, 1.0, near_z, far_z)};
  auto const fg{geo::frustum_from_view_projection(gl)};
  auto const near_pl{geo::plane_of(fg, geo::frustum_plane::near_plane)};

  CHECK(geo::signed_distance(near_pl, vec3{0, 0, -near_z}) == doctest::Approx(0.0));
  CHECK(geo::signed_distance(near_pl, vec3{0, 0, -near_z * 0.5}) < 0.0);
  CHECK(geo::signed_distance(near_pl, vec3{0, 0, -1.0}) > 0.0);

  auto const zo{nm::perspective_zo(nm::radians{nm::half_pi * 0.5}, 1.0, near_z, far_z)};
  auto const fz{geo::frustum_from_view_projection(zo)};
  auto const near_zo{geo::plane_of(fz, geo::frustum_plane::near_plane)};
  CHECK(nm::abs(geo::signed_distance(near_zo, vec3{0, 0, -near_z})) > 0.1);
}

TEST_CASE("obb: perimeter (2D) and surface_area (3D) mirror the aabb formulas") {
  geo::obb2_d const b2{vec2{0, 0}, vec2{2, 3}, nm::radians<double>{0.7}};
  CHECK(geo::perimeter(b2) == doctest::Approx(4.0 * (2.0 + 3.0)));
  geo::obb3_d const b3{vec3{0, 0, 0}, vec3{1, 2, 3}, nm::quaternion<double>{}};
  CHECK(geo::surface_area(b3) == doctest::Approx(8.0 * (1.0 * 2.0 + 2.0 * 3.0 + 3.0 * 1.0)));
}

TEST_CASE("polygon2: concave containment and degenerate centroid fallback") {
  std::array const arrow{vec2{0, 0}, vec2{4, 2}, vec2{0, 4}, vec2{1, 2}};
  geo::polygon2_d const poly{arrow};
  CHECK(geo::contains_point(poly, vec2{2, 2}));
  CHECK_FALSE(geo::contains_point(poly, vec2{0.5, 2}));

  std::array const line{vec2{0, 0}, vec2{2, 0}, vec2{4, 0}};
  CHECK(geo::centroid(geo::polygon2_d{line}) == vec2{2, 0});
}

TEST_CASE("obb: rotated 3D containment and 2D closest point (conjugate-rotation path)") {
  auto const rot{*nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<double>{nm::quarter_pi})};
  geo::obb3_d const b{vec3{0, 0, 0}, vec3{2, 1, 1}, rot};
  CHECK(geo::contains_point(b, nm::rotate(rot, vec3{1.9, 0, 0})));
  CHECK_FALSE(geo::contains_point(b, nm::rotate(rot, vec3{2.1, 0, 0})));

  geo::obb2_d const q{vec2{0, 0}, vec2{2, 1}, nm::radians<double>{nm::half_pi}};
  auto const cp{geo::closest_point(q, vec2{0, 5})};
  CHECK(cp.x() == doctest::Approx(0.0).epsilon(1e-9));
  CHECK(cp.y() == doctest::Approx(2.0));
}

TEST_CASE("obb3: corner-free bounding box of a rotated box") {
  auto const rot{*nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<double>{nm::quarter_pi})};
  geo::obb3_d const box{vec3{0, 0, 0}, vec3{1, 1, 1}, rot};
  auto const b{geo::bounding_aabb(box)};
  CHECK(b.max().x() == doctest::Approx(nm::sqrt_two));
  CHECK(b.max().y() == doctest::Approx(nm::sqrt_two));
  CHECK(b.max().z() == doctest::Approx(1.0));
  CHECK(b.min().x() == doctest::Approx(-nm::sqrt_two));
}

TEST_CASE("polygon2: area and centroid far from the origin") {
  using vec2f = nm::vector2_f;
  for (auto const o : {3000.0f, 1e4f}) {
    CAPTURE(o);
    std::array<vec2f, 4> const square{
      vec2f{o, o}, vec2f{o + 1, o}, vec2f{o + 1, o + 1}, vec2f{o, o + 1}
    };
    geo::polygon2_f const poly{square};
    CHECK(geo::signed_area(poly) == 1.0f);
    CHECK(geo::centroid(poly) == vec2f{o + 0.5f, o + 0.5f});
  }
  auto const o{1e8};
  std::array<vec2, 4> const square{vec2{o, o}, vec2{o + 1, o}, vec2{o + 1, o + 1}, vec2{o, o + 1}};
  CHECK(geo::signed_area(geo::polygon2_d{square}) == 1.0);
  CHECK(geo::centroid(geo::polygon2_d{square}) == vec2{o + 0.5, o + 0.5});
}

TEST_CASE("polygon2: a collinear loop with a rounding-noise area uses the vertex mean") {
  auto const mean_of{[](auto const& pts) {
    auto sum{vec2{}};
    for (auto const& p : pts) {
      sum = sum + p;
    }
    return sum * (1.0 / static_cast<double>(pts.size()));
  }};
  auto const near{[](vec2 const& p, vec2 const& q) { return nm::length(p - q) <= 1e-12; }};

  auto const a{vec2{0x1.c99263aa6b19p-2, 0x1.ff90e79e76dfp-1}};
  auto const d{vec2{-0x1.30a60bcad2314p-1, 0x1.bc6a53f6585cp-1}};
  std::array<vec2, 3> const line3{a, a + d * 0.3, a + d * 0.9};
  CHECK(near(geo::centroid(geo::polygon2_d{line3}), mean_of(line3)));

  auto const b{vec2{0x1.e7b13587acecp-2, -0x1.f0050961917dp-2}};
  auto const e{vec2{0x1.06ffbfd08465p-3, -0x1.78645db129966p-1}};
  std::array<vec2, 4> const line4{b, b + e * 0.3, b + e * 0.9, b + e * 0.55};
  CHECK(near(geo::centroid(geo::polygon2_d{line4}), mean_of(line4)));
}

TEST_CASE("polygon2: a pentagram is not convex though every turn has one sign") {
  auto star{std::array<vec2, 5>{}};
  auto pentagon{std::array<vec2, 5>{}};
  for (auto i{std::size_t{0}}; i < 5; ++i) {
    auto const step{2.0 * nm::pi_v<double> / 5.0};
    star[i] = vec2{
      std::cos(step * static_cast<double>(2 * i)), std::sin(step * static_cast<double>(2 * i))
    };
    pentagon[i] =
      vec2{std::cos(step * static_cast<double>(i)), std::sin(step * static_cast<double>(i))};
  }
  CHECK_FALSE(geo::convex(geo::polygon2_d{star}));
  CHECK(geo::convex(geo::polygon2_d{pentagon}));

  std::array<vec2, 4> const ccw{vec2{0, 0}, vec2{1, 0}, vec2{1, 1}, vec2{0, 1}};
  std::array<vec2, 4> const cw{vec2{0, 0}, vec2{0, 1}, vec2{1, 1}, vec2{1, 0}};
  std::array<vec2, 6> const repeated{
    vec2{0, 0}, vec2{1, 0}, vec2{1, 0}, vec2{1, 1}, vec2{0, 1}, vec2{0, 0}
  };
  std::array<vec2, 5> const midpoint{vec2{0, 0}, vec2{0.5, 0}, vec2{1, 0}, vec2{1, 1}, vec2{0, 1}};
  CHECK(geo::convex(geo::polygon2_d{ccw}));
  CHECK(geo::convex(geo::polygon2_d{cw}));
  CHECK(geo::convex(geo::polygon2_d{repeated}));
  CHECK(geo::convex(geo::polygon2_d{midpoint}));
  static_assert([] {
    std::array<vec2, 4> const square{vec2{0, 0}, vec2{1, 0}, vec2{1, 1}, vec2{0, 1}};
    return geo::convex(geo::polygon2_d{square});
  }());
}

}  // namespace
