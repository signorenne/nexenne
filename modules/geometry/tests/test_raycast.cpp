/**
 * @file
 * @brief Tests for the ray-cast hit records (intersect.hpp raycast overloads):
 *        hit distance, world hit point, and ray-facing surface normal.
 */

#include <doctest/doctest.h>

#include <cmath>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/intersect.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;
using vec3 = nm::vector3_f;

TEST_CASE("raycast: plane returns the hit point and a ray-facing normal") {
  geo::plane3_f const pl{vec3{0, 1, 0}, 0.0f};
  auto const hit{raycast(geo::ray3_f{vec3{0, 2, 0}, vec3{0, -1, 0}}, pl)};
  REQUIRE(hit.has_value());
  CHECK(hit->t == doctest::Approx(2.0));
  CHECK(hit->point.y() == doctest::Approx(0.0));
  CHECK(hit->normal.y() == doctest::Approx(1.0));
}

TEST_CASE("raycast: sphere hit lies on the surface with an outward normal") {
  geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
  auto const hit{raycast(geo::ray3_f{vec3{-3, 0, 0}, vec3{1, 0, 0}}, s)};
  REQUIRE(hit.has_value());
  CHECK(hit->t == doctest::Approx(2.0));
  CHECK(hit->point.x() == doctest::Approx(-1.0));
  CHECK(hit->normal.x() == doctest::Approx(-1.0));
  CHECK(nm::dot(hit->normal, vec3{1, 0, 0}) < 0.0f);
}

TEST_CASE("raycast: a ray starting inside the sphere reports t == 0") {
  geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
  auto const hit{raycast(geo::ray3_f{vec3{0, 0, 0}, vec3{1, 0, 0}}, s)};
  REQUIRE(hit.has_value());
  CHECK(hit->t == doctest::Approx(0.0));
  CHECK(hit->normal.x() == doctest::Approx(-1.0));
}

TEST_CASE("raycast: aabb reports the entry face normal") {
  geo::aabb3_f const box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
  auto const hit{raycast(geo::ray3_f{vec3{-5, 0, 0}, vec3{1, 0, 0}}, box)};
  REQUIRE(hit.has_value());
  CHECK(hit->t == doctest::Approx(4.0));
  CHECK(hit->point.x() == doctest::Approx(-1.0));
  CHECK(hit->normal == vec3{-1, 0, 0});
}

TEST_CASE("raycast: obb reports a rotated face normal") {
  auto const q{nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<float>{nm::half_pi_v<float>})};
  REQUIRE(q.has_value());
  geo::obb3_f const box{vec3{0, 0, 0}, vec3{1, 1, 1}, *q};
  auto const hit{raycast(geo::ray3_f{vec3{0, 5, 0}, vec3{0, -1, 0}}, box)};
  REQUIRE(hit.has_value());
  CHECK(hit->point.y() == doctest::Approx(1.0));
  CHECK(hit->normal.y() == doctest::Approx(1.0).epsilon(1e-5));
}

TEST_CASE("raycast: triangle normal faces the ray") {
  geo::triangle3_f const tri{vec3{-1, 0, -1}, vec3{1, 0, -1}, vec3{0, 0, 1}};
  auto const hit{raycast(geo::ray3_f{vec3{0, 3, 0}, vec3{0, -1, 0}}, tri)};
  REQUIRE(hit.has_value());
  CHECK(hit->point.y() == doctest::Approx(0.0));
  CHECK(hit->normal.y() == doctest::Approx(1.0));
}

TEST_CASE("raycast: a clean miss returns nullopt") {
  geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
  CHECK_FALSE(raycast(geo::ray3_f{vec3{0, 5, 0}, vec3{1, 0, 0}}, s).has_value());
  geo::aabb3_f const box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
  CHECK_FALSE(raycast(geo::ray3_f{vec3{-5, 5, 0}, vec3{1, 0, 0}}, box).has_value());
}

TEST_CASE("raycast: the hit record is constexpr") {
  constexpr auto ok{[] {
    geo::aabb3_f const box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
    auto const hit{raycast(geo::ray3_f{vec3{-5, 0, 0}, vec3{1, 0, 0}}, box)};
    return hit.has_value() && hit->normal == vec3{-1, 0, 0};
  }()};
  static_assert(ok, "raycast must be constexpr");
  CHECK(ok);
}

TEST_CASE("raycast: a far float sphere keeps its radius") {
  geo::sphere3_f const s{vec3{0, 0, 0}, 1.0f};
  for (auto const offset : {1.5f, 2.0f}) {
    CAPTURE(offset);
    geo::ray3_f const r{vec3{-1e4f, offset, 0}, vec3{1, 0, 0}};
    CHECK_FALSE(geo::intersects(r, s).has_value());
    CHECK_FALSE(raycast(r, s).has_value());
  }
  auto const hit{raycast(geo::ray3_f{vec3{-1e4f, 0.6f, 0}, vec3{1, 0, 0}}, s)};
  REQUIRE(hit.has_value());
  CHECK((hit.has_value() && std::abs(hit->t - (1e4f - 0.8f)) <= 2e-3f));
  CHECK((hit.has_value() && std::abs(nm::length(hit->point - s.center()) - 1.0f) <= 2e-3f));
}

TEST_CASE("intersects: a far double sphere and a far float circle keep the radius") {
  using vec3d = nm::vector3_d;
  geo::sphere3_d const s{vec3d{0, 0, 0}, 1.0};
  CHECK_FALSE(geo::intersects(geo::ray3_d{vec3d{-1e8, 1.5, 0}, vec3d{1, 0, 0}}, s).has_value());
  auto const t{geo::intersects(geo::ray3_d{vec3d{-1e8, 0.6, 0}, vec3d{1, 0, 0}}, s)};
  CHECK((t.has_value() && std::abs(*t - (1e8 - 0.8)) <= 1e-7));

  using vec2 = nm::vector2_f;
  geo::circle2_f const c{vec2{0, 0}, 1.0f};
  CHECK_FALSE(geo::intersects(geo::ray2_f{vec2{-1e4f, 2.0f}, vec2{1, 0}}, c).has_value());
  auto const tc{geo::intersects(geo::ray2_f{vec2{-1e4f, 0.6f}, vec2{1, 0}}, c)};
  CHECK((tc.has_value() && std::abs(*tc - (1e4f - 0.8f)) <= 2e-3f));
}

}  // namespace
