#pragma once

/**
 * @file
 * @brief Math constants as floating-point variable templates.
 *
 * Every constant has the suffix \c _v and is parameterized by the target
 * floating-point type. Non-suffixed aliases bind to \c double, matching the
 * convention used by \c std::numbers.
 *
 * Grouped sections: the pi family, common pi fractions, exact sin/cos at nice
 * angles, roots, logarithms, the golden ratio family, and limits and
 * conversion factors.
 */

#include <concepts>
#include <limits>
#include <numbers>

namespace nexenne::math {

/// @brief Pi.
template <std::floating_point Real>
inline constexpr Real pi_v = std::numbers::pi_v<Real>;

/// @brief Two pi, the period of sin and cos.
template <std::floating_point Real>
inline constexpr Real tau_v = Real{2} * std::numbers::pi_v<Real>;

/// @brief Pi squared.
template <std::floating_point Real>
inline constexpr Real pi_squared_v = std::numbers::pi_v<Real> * std::numbers::pi_v<Real>;

/// @brief The reciprocal of pi.
template <std::floating_point Real>
inline constexpr Real inv_pi_v = std::numbers::inv_pi_v<Real>;

/// @brief The reciprocal of the square root of pi.
template <std::floating_point Real>
inline constexpr Real inv_sqrt_pi_v = std::numbers::inv_sqrtpi_v<Real>;

/// @brief Pi divided by 2 (90 degrees).
template <std::floating_point Real>
inline constexpr Real half_pi_v = std::numbers::pi_v<Real> / Real{2};

/// @brief Pi divided by 3 (60 degrees).
template <std::floating_point Real>
inline constexpr Real third_pi_v = std::numbers::pi_v<Real> / Real{3};

/// @brief Pi divided by 4 (45 degrees).
template <std::floating_point Real>
inline constexpr Real quarter_pi_v = std::numbers::pi_v<Real> / Real{4};

/// @brief Pi divided by 6 (30 degrees).
template <std::floating_point Real>
inline constexpr Real sixth_pi_v = std::numbers::pi_v<Real> / Real{6};

/// @brief Two thirds of pi (120 degrees).
template <std::floating_point Real>
inline constexpr Real two_thirds_pi_v = Real{2} * std::numbers::pi_v<Real> / Real{3};

/// @brief Three quarters of pi (135 degrees).
template <std::floating_point Real>
inline constexpr Real three_quarters_pi_v = Real{3} * std::numbers::pi_v<Real> / Real{4};

/// @brief Five sixths of pi (150 degrees).
template <std::floating_point Real>
inline constexpr Real five_sixths_pi_v = Real{5} * std::numbers::pi_v<Real> / Real{6};

/// @brief Square root of two.
template <std::floating_point Real>
inline constexpr Real sqrt_two_v = std::numbers::sqrt2_v<Real>;

/// @brief Square root of three.
template <std::floating_point Real>
inline constexpr Real sqrt_three_v = std::numbers::sqrt3_v<Real>;

/// @brief Reciprocal of the square root of two.
template <std::floating_point Real>
inline constexpr Real inv_sqrt_two_v = Real{1} / std::numbers::sqrt2_v<Real>;

/// @brief Reciprocal of the square root of three.
template <std::floating_point Real>
inline constexpr Real inv_sqrt_three_v = std::numbers::inv_sqrt3_v<Real>;

//
// Each of the following is an exact closed-form value, computed from the roots
// above at compile time. Sign symmetries (sin is odd, cos is even) and quadrant
// identities cover the rest of the unit circle.

/// @brief sin(pi/6) = 1/2.
template <std::floating_point Real>
inline constexpr Real sin_pi_6_v = Real{1} / Real{2};

/// @brief cos(pi/6) = sqrt(3)/2.
template <std::floating_point Real>
inline constexpr Real cos_pi_6_v = std::numbers::sqrt3_v<Real> / Real{2};

/// @brief sin(pi/4) = cos(pi/4) = sqrt(2)/2.
template <std::floating_point Real>
inline constexpr Real sin_pi_4_v = std::numbers::sqrt2_v<Real> / Real{2};

/// @brief cos(pi/4) = sin(pi/4) = sqrt(2)/2.
template <std::floating_point Real>
inline constexpr Real cos_pi_4_v = sin_pi_4_v<Real>;

/// @brief sin(pi/3) = sqrt(3)/2.
template <std::floating_point Real>
inline constexpr Real sin_pi_3_v = std::numbers::sqrt3_v<Real> / Real{2};

/// @brief cos(pi/3) = 1/2.
template <std::floating_point Real>
inline constexpr Real cos_pi_3_v = Real{1} / Real{2};

/// @brief Euler's number, the base of the natural logarithm.
template <std::floating_point Real>
inline constexpr Real e_v = std::numbers::e_v<Real>;

/// @brief Natural log of 2.
template <std::floating_point Real>
inline constexpr Real ln_two_v = std::numbers::ln2_v<Real>;

/// @brief Natural log of 10.
template <std::floating_point Real>
inline constexpr Real ln_ten_v = std::numbers::ln10_v<Real>;

/// @brief Log base 2 of e.
template <std::floating_point Real>
inline constexpr Real log2_e_v = std::numbers::log2e_v<Real>;

/// @brief Log base 10 of e.
template <std::floating_point Real>
inline constexpr Real log10_e_v = std::numbers::log10e_v<Real>;

/// @brief The golden ratio, phi = (1 + sqrt(5)) / 2.
template <std::floating_point Real>
inline constexpr Real golden_ratio_v = std::numbers::phi_v<Real>;

/// @brief Reciprocal of the golden ratio, 1/phi = phi - 1.
template <std::floating_point Real>
inline constexpr Real inv_golden_ratio_v = Real{1} / std::numbers::phi_v<Real>;

/// @brief Golden angle in radians: 2 * pi * (1 - 1/phi), about 137.508 degrees.
///
/// Used in sunflower-spiral and other quasi-random radial layouts.
template <std::floating_point Real>
inline constexpr Real golden_angle_v =
  Real{2} * std::numbers::pi_v<Real> * (Real{1} - Real{1} / std::numbers::phi_v<Real>);

/// @brief Multiply degrees by this to get radians.
template <std::floating_point Real>
inline constexpr Real deg_to_rad_v = std::numbers::pi_v<Real> / Real{180};

/// @brief Multiply radians by this to get degrees.
template <std::floating_point Real>
inline constexpr Real rad_to_deg_v = Real{180} / std::numbers::pi_v<Real>;

/// @brief Machine epsilon: smallest representable difference between 1 and the
///        next larger representable value.
template <std::floating_point Real>
inline constexpr Real epsilon_v = std::numeric_limits<Real>::epsilon();

/// @brief Positive infinity.
template <std::floating_point Real>
inline constexpr Real infinity_v = std::numeric_limits<Real>::infinity();

/// @brief Smallest positive normal value.
template <std::floating_point Real>
inline constexpr Real min_normal_v = std::numeric_limits<Real>::min();

/// @brief Largest finite value.
template <std::floating_point Real>
inline constexpr Real max_finite_v = std::numeric_limits<Real>::max();

/// @brief Pi as \c double.
inline constexpr double pi = pi_v<double>;
/// @brief Two pi as \c double.
inline constexpr double tau = tau_v<double>;
/// @brief Pi squared as \c double.
inline constexpr double pi_squared = pi_squared_v<double>;
/// @brief Reciprocal of pi as \c double.
inline constexpr double inv_pi = inv_pi_v<double>;
/// @brief Reciprocal of the square root of pi as \c double.
inline constexpr double inv_sqrt_pi = inv_sqrt_pi_v<double>;
/// @brief Pi divided by 2 as \c double.
inline constexpr double half_pi = half_pi_v<double>;
/// @brief Pi divided by 3 as \c double.
inline constexpr double third_pi = third_pi_v<double>;
/// @brief Pi divided by 4 as \c double.
inline constexpr double quarter_pi = quarter_pi_v<double>;
/// @brief Pi divided by 6 as \c double.
inline constexpr double sixth_pi = sixth_pi_v<double>;
/// @brief Two thirds of pi as \c double.
inline constexpr double two_thirds_pi = two_thirds_pi_v<double>;
/// @brief Three quarters of pi as \c double.
inline constexpr double three_quarters_pi = three_quarters_pi_v<double>;
/// @brief Five sixths of pi as \c double.
inline constexpr double five_sixths_pi = five_sixths_pi_v<double>;
/// @brief Square root of two as \c double.
inline constexpr double sqrt_two = sqrt_two_v<double>;
/// @brief Square root of three as \c double.
inline constexpr double sqrt_three = sqrt_three_v<double>;
/// @brief Reciprocal of the square root of two as \c double.
inline constexpr double inv_sqrt_two = inv_sqrt_two_v<double>;
/// @brief Reciprocal of the square root of three as \c double.
inline constexpr double inv_sqrt_three = inv_sqrt_three_v<double>;
/// @brief Euler's number as \c double.
inline constexpr double e = e_v<double>;
/// @brief Natural log of 2 as \c double.
inline constexpr double ln_two = ln_two_v<double>;
/// @brief Natural log of 10 as \c double.
inline constexpr double ln_ten = ln_ten_v<double>;
/// @brief Log base 2 of e as \c double.
inline constexpr double log2_e = log2_e_v<double>;
/// @brief Log base 10 of e as \c double.
inline constexpr double log10_e = log10_e_v<double>;
/// @brief The golden ratio as \c double.
inline constexpr double golden_ratio = golden_ratio_v<double>;
/// @brief Reciprocal of the golden ratio as \c double.
inline constexpr double inv_golden_ratio = inv_golden_ratio_v<double>;
/// @brief Golden angle in radians as \c double.
inline constexpr double golden_angle = golden_angle_v<double>;
/// @brief Degrees-to-radians factor as \c double.
inline constexpr double deg_to_rad = deg_to_rad_v<double>;
/// @brief Radians-to-degrees factor as \c double.
inline constexpr double rad_to_deg = rad_to_deg_v<double>;

}  // namespace nexenne::math
