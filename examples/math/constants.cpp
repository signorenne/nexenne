/**
 * @file
 * @brief Math constants at a chosen precision, conversion factors, and limits.
 *
 * Shows the two-level naming: the =_v<Real>= variable templates follow the
 * caller's type, and the unsuffixed aliases bind to double.
 */

#include <print>

#include <nexenne/math/constants.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  // Variable templates: the precision follows the type parameter.
  constexpr float pi_f{nm::pi_v<float>};
  constexpr double tau_d{nm::tau_v<double>};
  static_assert(tau_d == 2.0 * nm::pi_v<double>);
  static_assert(nm::half_pi_v<double> == nm::pi_v<double> / 2.0);

  // Degrees to radians, computed once as pi/180.
  constexpr float deg{45.0F};
  constexpr float rad{deg * nm::deg_to_rad_v<float>};  // pi/4 in single precision
  static_assert(180.0 * nm::deg_to_rad_v<double> == nm::pi_v<double>);

  // Exact closed-form sin/cos at nice angles, no libm call.
  static_assert(nm::sin_pi_6_v<double> == 0.5);

  // Limits forwarded under math-style names; min_normal says what it means.
  constexpr float eps{nm::epsilon_v<float>};
  constexpr double inf{nm::infinity_v<double>};

  // Untemplated double aliases for callers that do not care about precision.
  static_assert(nm::pi == nm::pi_v<double>);

  std::println("pi (float):   {}", pi_f);
  std::println("tau (double): {}", tau_d);
  std::println("45 deg in rad: {}", rad);
  std::println("float epsilon: {}", eps);
  std::println("double inf:    {}", inf);
  std::println("golden angle:  {} rad", nm::golden_angle);
  return 0;
}
