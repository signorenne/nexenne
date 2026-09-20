/**
 * @file
 * @brief A guided tour of nexenne::math through one realistic task: a tiny,
 *        console-only 3D pipeline.
 *
 * This program does not draw anything: it computes everything a renderer or a
 * physics step would, and prints the numbers, so you can see how the pieces of
 * the module fit together in context. The object is a unit cube at an animation
 * time t in [0, 1].
 *
 *   1. Build a camera    -> the eye flies a Catmull-Rom spline, which passes
 *                           through its waypoints (a Bezier's inner handles only
 *                           pull toward them). look_at returns a result because a
 *                           degenerate setup has no view matrix; perspective
 *                           takes the vertical field of view and keeps clip z in
 *                           [-1, 1], the OpenGL convention.
 *   2. Animate an object -> a gimbal-lock-free quaternion spin about an orbit
 *                           angle wrapped to [-pi, pi), composed right to left:
 *                           scale, then spin, then move.
 *   3. Project vertices  -> one precomputed model-view-projection matrix;
 *                           transform_point applies the perspective divide, so
 *                           its result is already in NDC.
 *   4. Shade a face      -> Lambert diffuse max(0, dot(N, L)) plus a reflect
 *                           specular direction; transform_direction suits the
 *                           normal because the scale is uniform.
 *   5. Sample a sphere   -> unit_vector3 draws uniform directions by Marsaglia's
 *                           method, with no pole bias, from a fixed-seed engine.
 *   6. Stay deterministic-> floating-point sums are not bit-reproducible across
 *                           compilers, so a lockstep checksum folds the projected
 *                           x coordinates into Q16.16 fixed point.
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

  constexpr double t{0.35};

  std::println("== 1. Camera ==");
  nm::vector3_d const w0{-6, 2, 6}, w1{0, 3, 7}, w2{6, 2, 6}, w3{8, 4, 2};
  auto const eye{nm::catmull_rom(w0, w1, w2, w3, t)};
  std::println("  eye (on spline)            {:+.3f}", eye);

  auto const view{nm::look_at(eye, nm::vector3_d{0, 0, 0}, nm::vector3_d{0, 1, 0})};
  if (!view) {
    std::println("  degenerate camera; aborting");
    return 1;
  }

  auto const proj{nm::perspective(nm::radians{nm::half_pi * 0.5}, 16.0 / 9.0, 0.1, 100.0)};

  std::println("== 2. Model transform ==");
  auto const orbit{nm::wrap_signed(nm::radians_d{nm::tau * 3.0 * t})};
  std::println("  orbit angle (wrapped)      {:+.4f} rad", orbit.value());

  auto const spin{nm::from_axis_angle(nm::vector3_d{0, 1, 0}, orbit)};
  if (!spin) {
    return 1;
  }
  std::println("  spin quaternion            {:+.3f}", *spin);
  auto const model{
    nm::translation3(nm::vector3_d{0, 0, 0}) * nm::rotation3(*spin)
    * nm::scale3(nm::vector3_d{1.5, 1.5, 1.5})
  };

  auto const mvp{proj * *view * model};

  std::println("== 3. Projected corners (NDC) ==");
  for (std::size_t i{0}; i < 3; ++i) {
    std::println("  corner                     {:+.3f}", nm::transform_point(mvp, cube[i]));
  }

  std::println("== 4. Shading ==");
  auto const face_normal{nm::transform_direction(model, nm::vector3_d{0, 0, 1})};
  auto const to_light{nm::normalize(nm::vector3_d{0.5, 1.0, 0.8})};
  if (auto const n = nm::normalize(face_normal); n && to_light) {
    std::println("  diffuse intensity          {:.4f}", nm::max(0.0, nm::dot(*n, *to_light)));
    std::println("  specular reflect dir       {:+.3f}", nm::reflect(-*to_light, *n));
  }

  std::println("== 5. Hemisphere samples ==");
  rng::pcg32 gen{0xC0FFEEu, 0x1234u};
  double occlusion{0.0};
  constexpr int samples{8};
  for (int i{0}; i < samples; ++i) {
    auto const dir{nm::unit_vector3<double>(gen)};
    if (auto const n = nm::normalize(face_normal)) {
      occlusion += nm::abs(nm::dot(dir, *n));
    }
  }
  std::println("  mean |cos| over {} samples  {:.4f}", samples, occlusion / samples);

  std::println("== 6. Deterministic checksum ==");
  nm::q16_16 checksum{0};
  for (auto const& corner : cube) {
    checksum += nm::q16_16{nm::transform_point(mvp, corner).x()};
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
