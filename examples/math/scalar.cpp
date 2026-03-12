/**
 * @file
 * @brief Scalar utilities: clamping, interpolation, easing, and wrapping.
 */

#include <print>

#include <nexenne/math/scalar.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  // Interpolation and easing.
  std::println("lerp(0, 10, 0.5)        = {}", nm::lerp(0.0, 10.0, 0.5));
  std::println("smoothstep(0, 1, 0.25)  = {}", nm::smoothstep(0.0, 1.0, 0.25));
  std::println("remap(5, 0..10, 0..100) = {}", nm::remap(5.0, 0.0, 10.0, 0.0, 100.0));

  // Approach a target at a capped speed (frame-rate independent).
  double current{0.0};
  for (int frame{0}; frame < 4; ++frame) {
    current = nm::move_toward(current, 10.0, 3.0);
    std::println("move_toward frame {}: {}", frame, current);
  }

  // Floor-based modulo and wrapping take the sign of the divisor.
  std::println("mod(-1, 3)              = {}", nm::mod(-1.0, 3.0));
  std::println("wrap(22, 10, 20)        = {}", nm::wrap(22.0, 10.0, 20.0));

  // ping_pong is a triangle wave in [0, length].
  for (double t : {0.0, 3.0, 5.0, 7.0, 10.0}) {
    std::println("ping_pong({}, 5)        = {}", t, nm::ping_pong(t, 5.0));
  }
  return 0;
}
