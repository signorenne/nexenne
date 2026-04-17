/**
 * @file
 * @brief Tests for the pose aggregates and shape transforms (transform.hpp):
 *        to_matrix / point / direction, decompose round-trips, and the per-shape
 *        transforms (sphere, aabb -> obb, obb, triangle, circle).
 */

#include <doctest/doctest.h>

#include <nexenne/geometry/transform.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using geo::transform2d_f;
using geo::transform3d_f;
using nm::half_pi_v;
using nm::radians;
using vec2 = nm::vector2_f;
using vec3 = nm::vector3_f;

TEST_CASE("transform2d: identity to_matrix is the identity") {
  auto const m{to_matrix(transform2d_f::identity())};
  CHECK(m(0, 0) == doctest::Approx{1.0f});
  CHECK(m(1, 1) == doctest::Approx{1.0f});
  CHECK(m(2, 2) == doctest::Approx{1.0f});
  CHECK(m(0, 1) == doctest::Approx{0.0f});
}

TEST_CASE("transform2d: composes scale, then rotation, then translation") {
  auto t{transform2d_f::identity()};
  t.position() = vec2{10.0f, 20.0f};
  t.rotation() = radians<float>{half_pi_v<float>};  // 90 CCW.
  t.scale() = vec2{2.0f, 2.0f};

  // (1, 0): scale -> (2, 0), rotate 90 -> (0, 2), translate -> (10, 22).
  auto const out{transform_point(t, vec2{1.0f, 0.0f})};
  CHECK(out.x() == doctest::Approx{10.0f}.epsilon(1e-6f));
  CHECK(out.y() == doctest::Approx{22.0f}.epsilon(1e-6f));
}

TEST_CASE("transform2d: transform_direction skips the translation") {
  auto t{transform2d_f::identity()};
  t.position() = vec2{100.0f, 200.0f};
  t.rotation() = radians<float>{half_pi_v<float>};

  auto const out{transform_direction(t, vec2{1.0f, 0.0f})};  // (1,0) -> (0,1).
  CHECK(out.x() == doctest::Approx{0.0f}.epsilon(1e-6f));
  CHECK(out.y() == doctest::Approx{1.0f}.epsilon(1e-6f));
}

TEST_CASE("transform3d: identity to_matrix is the identity") {
  CHECK(to_matrix(transform3d_f::identity()) == nm::matrix4_f::identity());
}

TEST_CASE("transform3d: composes scale, then rotation, then translation") {
  auto t{transform3d_f::identity()};
  t.position() = vec3{5.0f, 0.0f, 0.0f};
  t.rotation() = *nm::from_axis_angle(vec3{0.0f, 0.0f, 1.0f}, radians<float>{half_pi_v<float>});
  t.scale() = vec3{2.0f, 2.0f, 2.0f};

  // (1,0,0): scale -> (2,0,0), rotate 90 about z -> (0,2,0), translate -> (5,2,0).
  auto const out{transform_point(t, vec3{1.0f, 0.0f, 0.0f})};
  CHECK(out.x() == doctest::Approx{5.0f}.epsilon(1e-6f));
  CHECK(out.y() == doctest::Approx{2.0f}.epsilon(1e-6f));
  CHECK(out.z() == doctest::Approx{0.0f}.epsilon(1e-6f));
}

TEST_CASE("transform: decompose_2 round-trips a 2D pose") {
  auto t{transform2d_f::identity()};
  t.position() = vec2{5.0f, 7.0f};
  t.rotation() = radians<float>{0.5f};
  t.scale() = vec2{2.0f, 3.0f};

  auto const back{*geo::decompose_2(to_matrix(t))};
  CHECK(nm::almost_equal(back.position(), t.position(), 1e-5f, 1e-5f));
  CHECK(back.rotation().value() == doctest::Approx{t.rotation().value()}.epsilon(1e-5f));
  CHECK(nm::almost_equal(back.scale(), t.scale(), 1e-5f, 1e-5f));
}

TEST_CASE("transform: decompose_3 round-trips a 3D pose") {
  auto t{transform3d_f::identity()};
  t.position() = vec3{1.0f, 2.0f, 3.0f};
  t.rotation() = *nm::from_axis_angle(vec3{0.0f, 0.0f, 1.0f}, radians<float>{half_pi_v<float>});
  t.scale() = vec3{2.0f, 3.0f, 4.0f};

  auto const back{*geo::decompose_3(to_matrix(t))};
  CHECK(nm::almost_equal(back.position(), t.position(), 1e-5f, 1e-5f));
  CHECK(nm::almost_equal(back.scale(), t.scale(), 1e-5f, 1e-5f));
}

TEST_CASE("transform: decompose_3 reports a degenerate (zero-scale) matrix") {
  auto t{transform3d_f::identity()};
  t.scale() = vec3{0.0f, 1.0f, 1.0f};
  auto const r{geo::decompose_3(to_matrix(t))};
  CHECK_FALSE(r.has_value());
  CHECK(r.error() == geo::geometry_error::degenerate_primitive);
}

TEST_CASE("transform: a 3D pose moves and grows a sphere") {
  auto t{transform3d_f::identity()};
  t.position() = vec3{1.0f, 2.0f, 3.0f};
  t.scale() = vec3{2.0f, 2.0f, 2.0f};

  auto const s{transform(t, geo::sphere3_f{vec3{0, 0, 0}, 1.0f})};
  CHECK(s.center() == vec3{1, 2, 3});
  CHECK(s.radius() == doctest::Approx{2.0f});
}

TEST_CASE("transform: a non-uniform scale grows a sphere by the largest factor") {
  auto t{transform3d_f::identity()};
  t.scale() = vec3{2.0f, 3.0f, 1.0f};
  auto const s{transform(t, geo::sphere3_f{vec3{0, 0, 0}, 1.0f})};
  CHECK(s.radius() == doctest::Approx{3.0f});  // enclosing, conservative.
}

TEST_CASE("transform: an axis-aligned box transforms into an oriented box") {
  auto t{transform3d_f::identity()};
  t.position() = vec3{10.0f, 0.0f, 0.0f};
  t.rotation() = *nm::from_axis_angle(vec3{0, 0, 1}, radians<float>{half_pi_v<float>});
  t.scale() = vec3{2.0f, 2.0f, 2.0f};

  geo::aabb<float, 3> const box{vec3{-1, -1, -1}, vec3{1, 1, 1}};
  auto const o{transform(t, box)};
  CHECK(o.center() == vec3{10, 0, 0});
  CHECK(o.half_size().x() == doctest::Approx{2.0f});  // half 1 * scale 2.
  // contains_point uses the obb orientation; the transformed box still holds its
  // own (transformed) center.
  CHECK(contains_point(o, o.center()));
}

TEST_CASE("transform: a triangle transforms vertex by vertex") {
  auto t{transform3d_f::identity()};
  t.position() = vec3{1.0f, 0.0f, 0.0f};
  auto const tri{transform(t, geo::triangle3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0}})};
  CHECK(tri.a() == vec3{1, 0, 0});
  CHECK(tri.b() == vec3{2, 0, 0});
  CHECK(tri.c() == vec3{1, 1, 0});
}

TEST_CASE("transform: an obb composes the pose rotation onto its own") {
  auto t{transform3d_f::identity()};
  t.rotation() = *nm::from_axis_angle(vec3{0, 0, 1}, radians<float>{half_pi_v<float>});
  geo::obb3_f const box{vec3{1, 0, 0}, vec3{1, 1, 1}, nm::quaternion<float>{}};
  auto const o{transform(t, box)};
  // The box center (1,0,0) rotates 90 about z to (0,1,0).
  CHECK(o.center().x() == doctest::Approx{0.0f}.epsilon(1e-6f));
  CHECK(o.center().y() == doctest::Approx{1.0f}.epsilon(1e-6f));
}

TEST_CASE("transform: the 3D shape transforms are constexpr") {
  constexpr auto ok{[] {
    auto t{transform3d_f::identity()};
    t.scale() = vec3{2, 2, 2};
    auto const s{transform(t, geo::sphere3_f{vec3{0, 0, 0}, 1.0f})};
    return s.radius() == 2.0f;
  }()};
  static_assert(ok, "3D shape transforms must be constexpr");
}

TEST_CASE("transform: a 2D pose transforms a circle and an aabb") {
  auto t{transform2d_f::identity()};
  t.position() = vec2{5.0f, 0.0f};
  t.scale() = vec2{2.0f, 2.0f};

  auto const c{transform(t, geo::circle2_f{vec2{0, 0}, 1.0f})};
  CHECK(c.center().x() == doctest::Approx{5.0f});
  CHECK(c.radius() == doctest::Approx{2.0f});

  auto const o{transform(t, geo::aabb<float, 2>{vec2{-1, -1}, vec2{1, 1}})};
  CHECK(o.half_size().x() == doctest::Approx{2.0f});
}

}  // namespace
