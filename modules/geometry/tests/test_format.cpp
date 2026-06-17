/**
 * @file
 * @brief Tests for the geometry formatters (format.hpp): to_string, operator<<,
 *        and std::format for every shape, the poses, and the geometry_error enum.
 */

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <span>
#include <sstream>
#include <string>

#include <nexenne/geometry/error.hpp>
#include <nexenne/geometry/format.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using geo::to_string;
using nm::quaternion;
using nm::radians;
using vec2 = nm::vector2_f;
using vec3 = nm::vector3_f;

TEST_CASE("format: to_string of the basic shapes is prefixed by the type name") {
  CHECK(to_string(geo::aabb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}}).starts_with("aabb(min="));
  CHECK(to_string(geo::sphere3_f{vec3{0, 0, 0}, 1.5f}).starts_with("sphere3("));
  CHECK(to_string(geo::circle2_f{vec2{0, 0}, 2.0f}).starts_with("circle2("));
  CHECK(to_string(geo::ray3_f{vec3{0, 0, 0}, vec3{1, 0, 0}}).starts_with("ray("));
  CHECK(to_string(geo::segment3_f{vec3{0, 0, 0}, vec3{1, 0, 0}}).starts_with("segment("));
  CHECK(to_string(geo::plane3_f{vec3{0, 0, 1}, 5.0f}).starts_with("plane3("));
}

TEST_CASE("format: to_string of triangle, capsule, and the oriented boxes") {
  CHECK(to_string(geo::triangle3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0}}
  ).starts_with("triangle("));
  CHECK(to_string(geo::capsule3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, 0.5f}).starts_with("capsule("));
  CHECK(to_string(geo::obb2_f{vec2{0, 0}, vec2{1, 1}, radians<float>{0.5f}}).starts_with("obb2("));
  CHECK(to_string(geo::obb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}, quaternion<float>{}}
  ).starts_with("obb3("));
}

TEST_CASE("format: polygon and convex hull list their vertices") {
  CHECK(to_string(geo::polygon2_f{}) == "polygon2(0 vertices)");
  constexpr auto verts2{std::array{vec2{0, 0}, vec2{1, 0}, vec2{0, 1}}};
  CHECK(to_string(geo::polygon2_f{verts2}).starts_with("polygon2(3 vertices:"));

  CHECK(to_string(geo::convex_hull3_f{}) == "convex_hull3(0 vertices)");
  constexpr auto verts3{std::array{vec3{0, 0, 0}, vec3{1, 0, 0}}};
  CHECK(to_string(geo::convex_hull3_f{std::span<vec3 const>{verts3}}
  ).starts_with("convex_hull3(2 vertices:"));
}

TEST_CASE("format: frustum and the poses") {
  CHECK(to_string(geo::frustum3_f{}).starts_with("frustum3("));
  CHECK(to_string(geo::transform2d_f::identity()).starts_with("transform2d(pos="));
  CHECK(to_string(geo::transform3d_f::identity()).starts_with("transform3d(pos="));
}

TEST_CASE("format: the frustum_plane enum prints its name") {
  CHECK(to_string(geo::frustum_plane::near_plane) == "near_plane");
  CHECK(std::format("{}", geo::frustum_plane::left) == "left");
  auto stream{std::stringstream{}};
  stream << geo::frustum_plane::far_plane;
  CHECK(stream.str() == "far_plane");
}

TEST_CASE("format: the geometry_error enum prints its name") {
  CHECK(to_string(geo::geometry_error::degenerate_primitive) == "degenerate_primitive");
  CHECK(std::format("{}", geo::geometry_error::parallel) == "parallel");
  auto stream{std::stringstream{}};
  stream << geo::geometry_error::invalid_input;
  CHECK(stream.str() == "invalid_input");
}

TEST_CASE("format: operator<< matches to_string") {
  auto const a{geo::aabb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}}};
  auto stream{std::stringstream{}};
  stream << a;
  CHECK(stream.str() == to_string(a));
}

TEST_CASE("format: std::format works on the geometry types") {
  auto const a{geo::aabb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}}};
  CHECK(std::format("box={}", a) == "box=" + to_string(a));
  auto const s{geo::sphere3_f{vec3{0, 0, 0}, 1.5f}};
  CHECK(std::format("{}", s) == to_string(s));
}

}  // namespace
