/**
 * @file
 * @brief A guided tour of nexenne::math through one realistic task: a tiny,
 *        console-only 3D pipeline.
 *
 * This program does not draw anything - it computes everything a renderer or a
 * physics step would, and prints the numbers, so you can see how the pieces of
 * the module fit together in context:
 *
 *   1. Build a camera    -> look_at (view matrix) + perspective (projection).
 *   2. Animate an object -> a Catmull-Rom path for position, a quaternion spin,
 *                           composed into a model matrix.
 *   3. Project vertices  -> model-view-projection, transform_point, the divide.
 *   4. Shade a face      -> normalize / dot / reflect (Lambert + a specular dir).
 *   5. Sample a sphere   -> the rng-backed geometric samplers.
 *   6. Stay deterministic-> a Q16.16 fixed-point checksum.
 *
 * Note we never hand-roll a print helper: format.hpp makes the math types
 * formattable, and the spec forwards to each component, so "{:+.3f}" prints a
 * vector with three signed decimals. Read it top to bottom.
 */

#include <array>
#include <cstdint>
#include <print>

#include <nexenne/math/curve.hpp>
#include <nexenne/math/fixed.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/projection.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/random.hpp>
#include <nexenne/math/transform.hpp>
#include <nexenne/math/vector_algorithms.hpp>
#include <nexenne/random/pcg.hpp>

namespace nm = nexenne::math;
namespace rng = nexenne::random;

auto main() -> int {
  // A unit cube's eight corners, the object we will transform and project.
  std::array<nm::vector3_d, 8> const cube{
    nm::vector3_d{-1, -1, -1},
    nm::vector3_d{1, -1, -1},
    nm::vector3_d{1, 1, -1},
    nm::vector3_d{-1, 1, -1},
    nm::vector3_d{-1, -1, 1},
    nm::vector3_d{1, -1, 1},
    nm::vector3_d{1, 1, 1},
    nm::vector3_d{-1, 1, 1},
  };

  // A normalized animation parameter; pretend this is "time" in [0, 1].
  constexpr double t{0.35};

  // 1. The camera. It flies along a Catmull-Rom spline through four waypoints.
  // Catmull-Rom passes *through* its control points (unlike a Bezier, whose inner
  // handles are only pulled toward), so the waypoints are real positions visited.
  std::println("== 1. Camera ==");
  nm::vector3_d const w0{-6, 2, 6}, w1{0, 3, 7}, w2{6, 2, 6}, w3{8, 4, 2};
  auto const eye{nm::catmull_rom(w0, w1, w2, w3, t)};
  std::println("  eye (on spline)            {:+.3f}", eye);

  // look_at gives the world->view matrix; it returns a result<> because a
  // degenerate setup (eye == target, or up parallel to the view) has no answer.
  auto const view{nm::look_at(eye, nm::vector3_d{0, 0, 0}, nm::vector3_d{0, 1, 0})};
  if (!view) {
    std::println("  degenerate camera; aborting");
    return 1;
  }

  // perspective maps the view frustum to clip space; it takes the vertical field
  // of view in radians and keeps clip z in [-1, 1] (the OpenGL convention).
  auto const proj{nm::perspective(nm::half_pi * 0.5, 16.0 / 9.0, 0.1, 100.0)};

  // 2. The model transform, composed right-to-left: scale, then spin, then move.
  // The spin is a quaternion (gimbal-lock-free, cheap to compose); rotation3 turns
  // it into a 4x4. The orbit angle is wrapped to [-pi, pi) so a long animation
  // never accumulates an unbounded angle.
  std::println("== 2. Model transform ==");
  auto const orbit{nm::wrap_signed(nm::radians_d{nm::tau * 3.0 * t})};  // 3 turns over [0,1]
  std::println("  orbit angle (wrapped)      {:+.4f} rad", orbit.value());

  auto const spin{nm::from_axis_angle(nm::vector3_d{0, 1, 0}, orbit)};
  if (!spin) {
    return 1;  // a zero axis has no rotation defined (cannot happen here)
  }
  std::println("  spin quaternion            {:+.3f}", *spin);
  auto const model{
    nm::translation3(nm::vector3_d{0, 0, 0})  // sitting at the origin
    * nm::rotation3(*spin) * nm::scale3(nm::vector3_d{1.5, 1.5, 1.5})
  };

  // Matrix multiply is associative, so precompute one transform per object/frame.
  auto const mvp{proj * *view * model};

  // 3. Project the cube's corners to normalized device coordinates. transform_point
  // treats the input as a homogeneous point and applies the perspective divide, so
  // the result is already in NDC (x, y in [-1, 1] when on screen, z the depth).
  std::println("== 3. Projected corners (NDC) ==");
  for (std::size_t i{0}; i < 3; ++i) {
    std::println("  corner                     {:+.3f}", nm::transform_point(mvp, cube[i]));
  }

  // 4. Shade one face. Lambert (diffuse) shading is max(0, dot(N, L)) for a unit
  // surface normal N and a unit direction-to-light L. We also bounce the light off
  // the surface with reflect for a specular direction. transform_direction moves
  // the normal by the model's rotation (fine here: the model has uniform scale).
  std::println("== 4. Shading ==");
  auto const face_normal{nm::transform_direction(model, nm::vector3_d{0, 0, 1})};  // +Z face
  auto const to_light{nm::normalize(nm::vector3_d{0.5, 1.0, 0.8})};
  if (auto const n = nm::normalize(face_normal); n && to_light) {
    std::println("  diffuse intensity          {:.4f}", nm::max(0.0, nm::dot(*n, *to_light)));
    std::println("  specular reflect dir       {:+.3f}", nm::reflect(-*to_light, *n));
  }

  // 5. Sample directions over the sphere (e.g. for ambient occlusion). unit_vector3
  // draws a uniform direction via Marsaglia's method - no pole bias - seeded by any
  // rng engine; a fixed seed keeps the demo reproducible.
  std::println("== 5. Hemisphere samples ==");
  rng::pcg32 gen{0xC0FFEEu, 0x1234u};
  double occlusion{0.0};
  constexpr int samples{8};
  for (int i{0}; i < samples; ++i) {
    auto const dir{nm::unit_vector3<double>(gen)};
    if (auto const n = nm::normalize(face_normal)) {
      occlusion += nm::abs(nm::dot(dir, *n));  // cosine weight into the normal's hemisphere
    }
  }
  std::println("  mean |cos| over {} samples  {:.4f}", samples, occlusion / samples);

  // 6. A deterministic checksum in Q16.16 fixed-point. Floating-point sums are not
  // bit-reproducible across compilers/optimization; for a lockstep simulation you
  // want fixed-point: integer math, identical everywhere. We fold the projected
  // x-coordinates into a Q16.16 accumulator.
  std::println("== 6. Deterministic checksum ==");
  nm::q16_16 checksum{0};
  for (auto const& corner : cube) {
    checksum += nm::q16_16{nm::transform_point(mvp, corner).x()};  // float -> fixed grid
  }
  std::println(
    "  Q16.16 checksum            {:.5f}  (raw bits {})",
    checksum.to_float(),
    static_cast<std::int64_t>(checksum.raw())
  );

  std::println("\nThat is the whole module in one frame: curves, matrices,");
  std::println("quaternions, projection, shading vectors, sampling, and fixed-point -");
  std::println("and every value printed straight through the math formatter.");
  return 0;
}
