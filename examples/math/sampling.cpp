/**
 * @file
 * @brief Geometric random samplers: directions, points, and an angle.
 */

#include <cstddef>
#include <print>

#include <nexenne/math/random.hpp>
#include <nexenne/math/vector_algorithms.hpp>
#include <nexenne/random/pcg.hpp>

namespace nm = nexenne::math;
namespace rng = nexenne::random;

auto main() -> int {
  rng::pcg32 gen{0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

  // A few uniform directions on the sphere (each is unit length).
  for (int i{0}; i < 3; ++i) {
    auto const d{nm::unit_vector3<double>(gen)};
    std::println(
      "unit_vector3 = ({:+.4f}, {:+.4f}, {:+.4f})  |len| = {:.4f}",
      d.x(),
      d.y(),
      d.z(),
      nm::length(d)
    );
  }

  // A point inside the unit disc (squared length < 1).
  auto const p{nm::point_in_unit_disc<double>(gen)};
  std::println(
    "point_in_unit_disc = ({:+.4f}, {:+.4f})  |len^2| = {:.4f}", p.x(), p.y(), nm::length_squared(p)
  );

  // Average many directions: a uniform sampler has ~zero mean (no bias).
  constexpr std::size_t n{200000};
  nm::vector3_d sum{0, 0, 0};
  for (std::size_t i{0}; i < n; ++i) {
    sum = sum + nm::unit_vector3<double>(gen);
  }
  auto const inv{1.0 / static_cast<double>(n)};
  std::println(
    "mean of {} directions = ({:+.4f}, {:+.4f}, {:+.4f})  (~0, unbiased)",
    n,
    sum.x() * inv,
    sum.y() * inv,
    sum.z() * inv
  );

  std::println("random_angle = {:+.4f} rad", nm::random_angle<double>(gen).value());
  return 0;
}
