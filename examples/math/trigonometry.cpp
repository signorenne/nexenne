/**
 * @file
 * @brief The three trig speeds side by side, and short-way angle blending.
 */

#include <cmath>
#include <print>

#include <nexenne/math/trigonometry.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  std::println("{:<10} {:>12} {:>12} {:>12} {:>12}", "angle", "libm", "fast", "lut", "error(fast)");
  for (double a : {0.3, 1.0, 2.5, 4.0}) {
    auto const ref{std::sin(a)};
    auto const f{nm::fast_sin(nm::radians_d{a})};
    auto const l{nm::lut_sin(nm::radians_d{a})};
    std::println(
      "{:<10.3f} {:>12.8f} {:>12.8f} {:>12.8f} {:>12.2e}", a, ref, f, l, std::abs(f - ref)
    );
  }

  // sincos shares the work when both are needed (rotation matrix row).
  auto const sc{nm::sincos(nm::radians_d{nm::quarter_pi})};
  std::println("");
  std::println("sincos(pi/4): sin={:.6f} cos={:.6f}", sc.sin(), sc.cos());

  // Short-way interpolation: 350 deg to 10 deg passes through 0, not 180.
  auto const a350{nm::radians_d{nm::to_radians(350.0).value()}};
  auto const a10{nm::radians_d{nm::to_radians(10.0).value()}};
  std::println(
    "lerp_angle(350 deg, 10 deg, 0.5) = {:.3f} rad (= 0)", nm::lerp_angle(a350, a10, 0.5).value()
  );
  return 0;
}
