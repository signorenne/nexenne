/**
 * @file
 * @brief Tests for the geometry std::hash specializations (hash.hpp): equal
 *        shapes hash equal, and the shapes work as unordered-container keys.
 */

#include <doctest/doctest.h>

#include <unordered_map>
#include <unordered_set>

#include <nexenne/geometry/hash.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec2 = nm::vector2_f;
using vec3 = nm::vector3_f;
using nm::quaternion;
using nm::radians;

TEST_CASE("hash: equal aabbs hash equal, different ones differ") {
  geo::aabb3_f const a{vec3{0, 0, 0}, vec3{1, 1, 1}};
  geo::aabb3_f const b{vec3{0, 0, 0}, vec3{1, 1, 1}};
  geo::aabb3_f const c{vec3{0, 0, 0}, vec3{2, 2, 2}};
  CHECK(std::hash<geo::aabb3_f>{}(a) == std::hash<geo::aabb3_f>{}(b));
  CHECK(std::hash<geo::aabb3_f>{}(a) != std::hash<geo::aabb3_f>{}(c));
}

TEST_CASE("hash: every primitive specialization is stable on equal values") {
  CHECK(
    std::hash<geo::circle2_f>{}(geo::circle2_f{vec2{1, 2}, 3.0f})
    == std::hash<geo::circle2_f>{}(geo::circle2_f{vec2{1, 2}, 3.0f})
  );
  CHECK(
    std::hash<geo::sphere3_f>{}(geo::sphere3_f{vec3{1, 2, 3}, 4.0f})
    == std::hash<geo::sphere3_f>{}(geo::sphere3_f{vec3{1, 2, 3}, 4.0f})
  );
  CHECK(
    std::hash<geo::ray3_f>{}(geo::ray3_f{vec3{0, 0, 0}, vec3{1, 0, 0}})
    == std::hash<geo::ray3_f>{}(geo::ray3_f{vec3{0, 0, 0}, vec3{1, 0, 0}})
  );
  CHECK(
    std::hash<geo::segment3_f>{}(geo::segment3_f{vec3{0, 0, 0}, vec3{1, 0, 0}})
    == std::hash<geo::segment3_f>{}(geo::segment3_f{vec3{0, 0, 0}, vec3{1, 0, 0}})
  );
  CHECK(
    std::hash<geo::plane3_f>{}(geo::plane3_f{vec3{0, 0, 1}, 5.0f})
    == std::hash<geo::plane3_f>{}(geo::plane3_f{vec3{0, 0, 1}, 5.0f})
  );
  CHECK(
    std::hash<geo::triangle3_f>{}(geo::triangle3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0}})
    == std::hash<geo::triangle3_f>{}(geo::triangle3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, vec3{0, 1, 0}})
  );
  CHECK(
    std::hash<geo::capsule3_f>{}(geo::capsule3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, 0.5f})
    == std::hash<geo::capsule3_f>{}(geo::capsule3_f{vec3{0, 0, 0}, vec3{1, 0, 0}, 0.5f})
  );
  CHECK(
    std::hash<geo::obb2_f>{}(geo::obb2_f{vec2{0, 0}, vec2{1, 1}, radians<float>{0.5f}})
    == std::hash<geo::obb2_f>{}(geo::obb2_f{vec2{0, 0}, vec2{1, 1}, radians<float>{0.5f}})
  );
  CHECK(
    std::hash<geo::obb3_f>{}(geo::obb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}, quaternion<float>{}})
    == std::hash<geo::obb3_f>{}(geo::obb3_f{vec3{0, 0, 0}, vec3{1, 1, 1}, quaternion<float>{}})
  );
}

TEST_CASE("hash: a frustum hashes by its six planes") {
  geo::frustum3_f const a{};
  geo::frustum3_f const b{};
  CHECK(std::hash<geo::frustum3_f>{}(a) == std::hash<geo::frustum3_f>{}(b));

  auto planes{a.planes()};
  planes[0] = geo::plane3_f{vec3{1, 0, 0}, 2.0f};
  geo::frustum3_f const c{planes};
  CHECK(std::hash<geo::frustum3_f>{}(a) != std::hash<geo::frustum3_f>{}(c));
}

TEST_CASE("hash: the pose aggregates are hashable too") {
  geo::transform3d_f const a{vec3{1, 2, 3}, quaternion<float>{}, vec3{1, 1, 1}};
  geo::transform3d_f const b{vec3{1, 2, 3}, quaternion<float>{}, vec3{1, 1, 1}};
  CHECK(std::hash<geo::transform3d_f>{}(a) == std::hash<geo::transform3d_f>{}(b));

  geo::transform2d_f const c{vec2{1, 2}, radians<float>{0.5f}, vec2{1, 1}};
  CHECK(std::hash<geo::transform2d_f>{}(c) == std::hash<geo::transform2d_f>{}(c));
}

TEST_CASE("hash: shapes work as keys in unordered containers") {
  auto map{std::unordered_map<geo::aabb2_f, int>{}};
  map[geo::aabb2_f{vec2{0, 0}, vec2{1, 1}}] = 42;
  CHECK(map.at(geo::aabb2_f{vec2{0, 0}, vec2{1, 1}}) == 42);

  auto set{std::unordered_set<geo::sphere3_f>{}};
  set.insert(geo::sphere3_f{vec3{0, 0, 0}, 1.0f});
  set.insert(geo::sphere3_f{vec3{0, 0, 0}, 1.0f});
  set.insert(geo::sphere3_f{vec3{1, 0, 0}, 1.0f});
  CHECK(set.size() == 2);
}

}  // namespace
