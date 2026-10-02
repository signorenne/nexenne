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

#include <nexenne/geometry/aabb_tree.hpp>
#include <nexenne/geometry/error.hpp>
#include <nexenne/geometry/format.hpp>
#include <nexenne/geometry/gjk.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using geo::to_string;
using nm::quaternion;
using namespace std::string_literals;
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
  CHECK(to_string(geo::triangle3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0}})
          .starts_with("triangle("));
  CHECK(to_string(geo::capsule3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, 0.5f}).starts_with("capsule("));
  CHECK(to_string(geo::obb2_f{vec2{0, 0}, vec2{1, 1}, radians<float>{0.5f}}).starts_with("obb2("));
  CHECK(
    to_string(geo::obb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}, quaternion<float>{}}).starts_with("obb3(")
  );
}

TEST_CASE("format: polygon and convex hull list their vertices") {
  CHECK(to_string(geo::polygon2_f{}) == "polygon2(0 vertices)");
  constexpr auto verts2{std::array{vec2{0, 0}, vec2{1, 0}, vec2{0, 1}}};
  CHECK(to_string(geo::polygon2_f{verts2}).starts_with("polygon2(3 vertices:"));

  CHECK(to_string(geo::convex_hull3_f{}) == "convex_hull3(0 vertices)");
  constexpr auto verts3{std::array{vec3{0, 0, 0}, vec3{1, 0, 0}}};
  CHECK(to_string(
          geo::convex_hull3_f{std::span<vec3 const>{verts3}}
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
  CHECK(std::format("{}", geo::geometry_error::degenerate_primitive) == "degenerate_primitive");
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

template <typename T>
auto three_layers_agree(T const& value) -> bool {
  auto stream{std::stringstream{}};
  stream << value;
  auto const formatted{std::format("{}", value)};
  return to_string(value) == formatted && stream.str() == formatted;
}

TEST_CASE("format: the query results print through every layer") {
  auto gjk{geo::gjk_result3<float>{}};
  gjk.distance = 1.5F;
  gjk.closest_a = vec3{1, 0, 0};
  gjk.closest_b = vec3{2.5F, 0, 0};
  gjk.iterations = 3;
  CHECK(three_layers_agree(gjk));
  CHECK(to_string(gjk).starts_with("gjk_result3(overlap=false, distance=1.5,"));
  CHECK(to_string(gjk).ends_with("simplex=0, iterations=3)"));

  auto epa{geo::epa_result3<float>{}};
  epa.converged = true;
  epa.penetration_depth = 0.25F;
  CHECK(three_layers_agree(epa));
  CHECK(to_string(epa).starts_with("epa_result3(converged=true,"));

  auto const hit{geo::ray_hit3<float>{.t = 2.0F, .point = vec3{0, 0, 2}, .normal = vec3{0, 0, -1}}};
  CHECK(three_layers_agree(hit));
  CHECK(to_string(hit).starts_with("ray_hit3(t=2, point="));

  auto manifold{geo::contact_manifold3<float>{}};
  manifold.points[0] = vec3{1, 0, 0};
  manifold.points[1] = vec3{0, 1, 0};
  manifold.count = 2;
  manifold.normal = vec3{0, 0, 1};
  CHECK(three_layers_agree(manifold));
  CHECK(to_string(manifold).find(to_string(manifold.points[1])) != std::string::npos);
  CHECK(to_string(manifold).find(to_string(manifold.points[2])) == std::string::npos);
}

TEST_CASE("format: the GJK simplex prints its valid vertices") {
  auto const vertex{geo::gjk_minkowski_point3<float>{
    .difference = vec3{1, 0, 0}, .support_a = vec3{2, 0, 0}, .support_b = vec3{1, 0, 0}
  }};
  auto const vertex_text{
    "gjk_minkowski_point3(difference=vector3(1, 0, 0), support_a=vector3(2, 0, 0), "
    "support_b=vector3(1, 0, 0))"s
  };
  CHECK(std::format("{}", vertex) == vertex_text);
  CHECK(three_layers_agree(vertex));

  auto simplex{geo::gjk_simplex3<float>{}};
  CHECK(std::format("{}", simplex) == "gjk_simplex3(count=0, points=[])");
  simplex.points[0] = vertex;
  simplex.points[1].difference = vec3{0, 5, 0};
  simplex.count = 1;
  CHECK(std::format("{}", simplex) == "gjk_simplex3(count=1, points=[" + vertex_text + "])");
  CHECK(three_layers_agree(simplex));
}

TEST_CASE("format: the aabb_tree prints its size, height and root box") {
  auto tree{geo::aabb_tree<int, 3, float>{0.5F}};
  CHECK(std::format("{}", tree) == "aabb_tree(size=0, height=0)");

  tree.insert(geo::aabb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}}, 1);
  tree.insert(geo::aabb3_f{vec3{3, 0, 0}, vec3{4, 1, 1}}, 2);
  CHECK(
    std::format("{}", tree)
    == "aabb_tree(size=2, height=1, root_bounds=aabb(min=vector3(-0.5, -0.5, -0.5), "
       "max=vector3(4.5, 1.5, 1.5)))"
  );
  CHECK(three_layers_agree(tree));
}

}  // namespace
