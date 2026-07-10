/**
 * @file
 * @brief A guided tour of nexenne::geometry through one small scene.
 *
 * Nothing is drawn; the program builds a handful of world objects and runs the
 * queries a physics step or a renderer would, printing the numbers so the pieces
 * fit together in context:
 *
 *   1. Place objects   -> transform a shape into world space (transform.hpp).
 *   2. Broad phase     -> index every world box in an aabb_tree, then region
 *                         query and raycast it (aabb_tree.hpp).
 *   3. Cull            -> build a camera frustum and keep only what it sees
 *                         (frustum.hpp).
 *   4. Narrow phase    -> GJK overlap, EPA penetration, and the contact manifold
 *                         on a real pair, via the support mappings (gjk/epa.hpp).
 *   5. Distance        -> GJK separation distance and nearest points for a pair
 *                         that does not overlap (gjk.hpp).
 *   6. Ray hit         -> a ray cast that returns the hit point and surface
 *                         normal (intersect.hpp).
 *
 * Every value is printed through format.hpp, so we never hand-roll a printer.
 * Read it top to bottom.
 */

#include <cstdint>
#include <print>
#include <utility>

#include <nexenne/geometry/aabb_tree.hpp>
#include <nexenne/geometry/epa.hpp>
#include <nexenne/geometry/format.hpp>
#include <nexenne/geometry/frustum.hpp>
#include <nexenne/geometry/gjk.hpp>
#include <nexenne/geometry/intersect.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/support.hpp>
#include <nexenne/geometry/transform.hpp>
#include <nexenne/math/projection.hpp>
#include <nexenne/math/transform.hpp>

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec3 = nm::vector3_f;

auto main() -> int {
  // 1. Place objects. A unit box authored at the origin is moved into world space
  // by a pose; because the pose rotates it, the world shape is an oriented box.
  std::println("== 1. Place objects ==");
  auto pose{geo::transform3d_f::identity()};
  pose.position() = vec3{2, 0, 0};
  pose.rotation() = *nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<float>{0.6f});
  geo::aabb3_f const authored{vec3{-1, -1, -1}, vec3{1, 1, 1}};
  geo::obb3_f const crate{transform(pose, authored)};
  geo::sphere3_f const ball{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const far_ball{vec3{20, 0, 0}, 1.0f};
  std::println("  crate    = {}", crate);
  std::println("  ball     = {}", ball);
  std::println("  far_ball = {}", far_ball);

  // 2. Broad phase. Index every object's world bounding box in the tree, keyed by
  // a small id, then ask which boxes a region overlaps and which a ray hits.
  std::println("\n== 2. Broad phase (aabb_tree) ==");
  geo::aabb_tree<std::uint32_t, 3, float> world{0.1f};
  world.insert(bounding_aabb(crate), 0u);
  world.insert(bounding_aabb(ball), 1u);
  world.insert(bounding_aabb(far_ball), 2u);
  std::println("  indexed {} objects, tree height {}", world.size(), world.height());

  geo::aabb3_f const region{vec3{-2, -2, -2}, vec3{4, 2, 2}};
  std::print("  region {} overlaps ids:", region);
  world.query(region, [](auto, std::uint32_t id) { std::print(" {}", id); });
  std::println("");

  auto const probe{geo::ray3_f{vec3{-5, 0, 0}, vec3{1, 0, 0}}};
  std::print("  ray along +x hits ids:");
  world.raycast(probe, 100.0f, [](auto, std::uint32_t id, float t) {
    std::print(" {}(t={:.1f})", id, t);
  });
  std::println("");

  // 3. Cull. A camera looking down -z keeps only the objects inside its frustum.
  std::println("\n== 3. Frustum culling ==");
  auto const view{*nm::look_at(vec3{0, 0, 8}, vec3{0, 0, 0}, vec3{0, 1, 0})};
  auto const proj{nm::perspective(nm::radians<float>{1.0f}, 1.0f, 0.1f, 50.0f)};
  auto const camera{geo::frustum_from_view_projection(proj * view)};
  for (auto const& [name, sphere] : {std::pair{"ball", ball}, std::pair{"far_ball", far_ball}}) {
    std::println("  {} visible: {}", name, intersects(camera, sphere));
  }

  // 4. Narrow phase. The broad phase flagged crate and ball as a candidate pair;
  // confirm the overlap with GJK, then recover the push-out with EPA and the
  // contact manifold. Both run on the analytic primitives directly through their
  // support mappings.
  std::println("\n== 4. Narrow phase (GJK + EPA) ==");
  auto const hit{geo::gjk<float>(ball, crate, crate.center() - ball.center())};
  std::println("  ball vs crate overlap: {}", hit.overlap);
  if (hit.overlap) {
    auto const contact{geo::epa<float>(ball, crate, hit.simplex)};
    std::println("  converged:   {}", contact.converged);
    std::println("  normal:      {}", contact.normal);
    std::println("  penetration: {:.3f}", contact.penetration_depth);
    auto const manifold{geo::contact_manifold<float>(ball, crate, contact)};
    std::println("  manifold points: {}", manifold.count);
  }

  // 5. Distance. For a separated pair GJK reports the gap and the nearest point on
  // each shape, not just a yes/no.
  std::println("\n== 5. Distance (GJK) ==");
  auto const gap{geo::gjk<float>(ball, far_ball, far_ball.center() - ball.center())};
  std::println("  ball vs far_ball overlap: {}", gap.overlap);
  std::println("  distance:  {:.3f}", gap.distance);
  std::println("  nearest on ball:     {}", gap.closest_a);
  std::println("  nearest on far_ball: {}", gap.closest_b);

  // 6. Ray hit. A ray cast returns the hit point and the surface normal there.
  std::println("\n== 6. Ray hit point and normal ==");
  auto const beam{geo::ray3_f{vec3{-5, 0, 0}, vec3{1, 0, 0}}};
  if (auto const rh{raycast(beam, crate)}) {
    std::println("  hit t={:.3f} point={} normal={}", rh->t, rh->point, rh->normal);
  }

  return 0;
}
