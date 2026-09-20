/**
 * @file
 * @brief Strong angle types: typed conversions, algebra, and wrapping.
 *
 *   1. Convert degrees to radians and feed the result to the trig wrappers.
 *   2. Algebra stays in one unit. There is no radians + degrees overload, so
 *      radians_d{1.0} + degrees_d{1.0} is a compile error, which is the whole
 *      point of the strong types.
 *   3. Compound assignment accumulates a heading in place: angle-typed for +=
 *      and -=, raw-scalar for *= and /=.
 *   4. wrap_signed and wrap_unsigned keep a drifting heading bounded.
 */

#include <print>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  constexpr nm::degrees_d turn{90.0};
  constexpr auto r{nm::to_radians(turn)};
  std::println("90 deg = {} rad", r.value());
  std::println("sin(90 deg) = {}", nm::sin(r));

  constexpr auto sum{nm::degrees_d{30.0} + nm::degrees_d{15.0}};
  std::println("30 deg + 15 deg = {} deg", sum.value());

  auto heading{nm::radians_d{0.0}};
  heading += nm::radians_d{0.5};
  heading *= 2.0;
  std::println("heading after += 0.5 then *= 2 = {} rad", heading.value());

  std::println(
    "wrap_signed(7 rad)    = {} (in [-pi, pi))", nm::wrap_signed(nm::radians_d{7.0}).value()
  );
  std::println(
    "wrap_unsigned(-1 rad) = {} (in [0, 2pi))", nm::wrap_unsigned(nm::radians_d{-1.0}).value()
  );
  return 0;
}
