/**
 * @file
 * @brief The shape primitives the showcase does not exercise, one query each.
 *
 * showcase.cpp walks a whole scene (transforms, broad phase, culling, GJK/EPA,
 * raycast). This companion fills the four-places gap for the plain primitives it
 * skips, running one representative query per shape and printing it through
 * format.hpp:
 *
 *   1. segment   -> closest point and length.
 *   2. plane3    -> signed distance and projection.
 *   3. triangle  -> area, centroid, and 2D containment.
 *   4. circle2   -> area and containment.
 *   5. capsule   -> volume and containment.
 *   6. polygon2  -> shoelace area, centroid, and point-in-polygon.
 *   7. hash      -> a shape as an unordered_map key.
 *   8. decompose -> recover a pose from its matrix.
 *
 * Every value is printed through format.hpp, so we never hand-roll a printer.
 */

#include <array>
#include <print>
#include <unordered_map>

#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/format.hpp>
#include <nexenne/geometry/hash.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/polygon.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/transform.hpp>
#include <nexenne/geometry/triangle.hpp>

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec2 = nm::vector2_d;
using vec3 = nm::vector3_d;

auto main() -> int {
  // 1. segment: the closest point on a finite segment clamps to its endpoints.
  std::println("== 1. segment ==");
  geo::segment3_d const spine{vec3{0, 0, 0}, vec3{4, 0, 0}};
  std::println("  segment      = {}", spine);
  std::println("  length       = {:.3f}", geo::length(spine));
  std::println("  closest to (2,3,0) = {}", geo::closest_point(spine, vec3{2, 3, 0}));

  // 2. plane3: signed distance is positive on the normal's side; closest_point is
  // the orthogonal projection onto the plane.
  std::println("\n== 2. plane3 ==");
  auto const ground{geo::plane_from_point_normal(vec3{0, 0, 0}, vec3{0, 0, 1})};
  if (ground) {
    std::println("  plane        = {}", *ground);
    std::println("  signed dist of (0,0,5) = {:.3f}", geo::signed_distance(*ground, vec3{0, 0, 5}));
    std::println("  projection of (4,1,7)  = {}", geo::closest_point(*ground, vec3{4, 1, 7}));
  }

  // 3. triangle: area, centroid, and the winding-independent 2D containment test.
  std::println("\n== 3. triangle ==");
  geo::triangle2_d const tri{vec2{0, 0}, vec2{4, 0}, vec2{0, 3}};
  std::println("  triangle     = {}", tri);
  std::println("  area         = {:.3f}", geo::area(tri));
  std::println("  centroid     = {}", geo::centroid(tri));
  std::println("  contains (1,1): {}", geo::contains_point(tri, vec2{1, 1}));

  // 4. circle2: the 2D disc, area and containment.
  std::println("\n== 4. circle2 ==");
  geo::circle2_d const disc{vec2{0, 0}, 2.0};
  std::println("  circle       = {}", disc);
  std::println("  area         = {:.3f}", geo::area(disc));
  std::println("  contains (1,1): {}", geo::contains_point(disc, vec2{1, 1}));

  // 5. capsule: a segment swept by a radius; volume is the cylinder plus caps.
  std::println("\n== 5. capsule ==");
  geo::capsule3_d const pill{vec3{0, 0, 0}, vec3{0, 0, 4}, 1.0};
  std::println("  capsule      = {}", pill);
  std::println("  volume       = {:.3f}", geo::volume(pill));
  std::println("  contains (0.5,0,2): {}", geo::contains_point(pill, vec3{0.5, 0, 2}));

  // 6. polygon2: a non-owning view over caller-owned vertices; shoelace area,
  // area-weighted centroid, and the crossing-number containment test.
  std::println("\n== 6. polygon2 ==");
  std::array const square{vec2{0, 0}, vec2{4, 0}, vec2{4, 4}, vec2{0, 4}};
  geo::polygon2_d const poly{square};
  std::println("  polygon      = {}", poly);
  std::println("  area         = {:.3f}", geo::area(poly));
  std::println("  centroid     = {}", geo::centroid(poly));
  std::println("  contains (2,2): {}", geo::contains_point(poly, vec2{2, 2}));

  // 7. hash: any concrete shape is a ready-made unordered-container key.
  std::println("\n== 7. hash ==");
  auto labels{std::unordered_map<geo::circle2_d, char const*>{}};
  labels[disc] = "origin disc";
  std::println("  map lookup   = {}", labels.at(geo::circle2_d{vec2{0, 0}, 2.0}));

  // 8. decompose: recover a pose (position, angle, scale) from its 3x3 matrix.
  std::println("\n== 8. decompose ==");
  auto pose{geo::transform2d_d::identity()};
  pose.position() = vec2{5, 7};
  pose.rotation() = nm::radians<double>{0.5};
  pose.scale() = vec2{2, 3};
  if (auto const back{geo::decompose_2(to_matrix(pose))}) {
    std::println("  original     = {}", pose);
    std::println("  recovered    = {}", *back);
  }

  return 0;
}
