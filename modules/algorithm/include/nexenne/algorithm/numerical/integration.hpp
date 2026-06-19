#pragma once

/**
 * @file
 * @brief Numerical integration of f(x) over [a, b].
 *
 * Three quadrature rules at different cost/accuracy points, all returning
 * \c double and accepting any \c std::invocable<double>. \c trapezoidal is
 * second-order over n subintervals; \c simpson is fourth-order over n (rounded
 * up to even) subintervals; \c gauss_legendre_5 evaluates f at five nodes for
 * accuracy equivalent to a degree-9 polynomial, the best per-evaluation
 * accuracy on smooth integrands.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace nexenne::algorithm {

/**
 * @brief Integrates \p f over \c [a, b] by the composite trapezoidal rule.
 *
 * Splits \c [a, b] into \p n equal subintervals and sums the trapezoid areas.
 * Second-order accurate; exact for affine integrands.
 *
 * @tparam F Callable invocable as \c f(double) returning \c double.
 * @param f Integrand.
 * @param a Lower limit of integration.
 * @param b Upper limit of integration.
 * @param n Number of equal subintervals.
 *
 * @return The approximate integral; exactly \c 0.0 when \p n is zero.
 *
 * @pre None.
 * @post The result is the composite trapezoidal estimate.
 *
 * @complexity \c O(n) evaluations of \p f.
 */
template <std::invocable<double> F>
[[nodiscard]] constexpr auto trapezoidal(
  F&& f, double const a, double const b, std::size_t const n
) noexcept(std::is_nothrow_invocable_v<F, double>) -> double {
  if (n == 0) {
    return 0.0;
  }
  auto const h{(b - a) / static_cast<double>(n)};
  auto sum{0.5 * (f(a) + f(b))};
  for (auto i{std::size_t{1}}; i < n; ++i) {
    sum += f(a + static_cast<double>(i) * h);
  }
  return sum * h;
}

/**
 * @brief Integrates \p f over \c [a, b] by composite Simpson's 1/3 rule.
 *
 * Splits \c [a, b] into an even number of subintervals and applies the
 * parabolic Simpson weights; an odd \p n is rounded up. Fourth-order accurate,
 * exact for integrands up to cubic degree.
 *
 * @tparam F Callable invocable as \c f(double) returning \c double.
 * @param f Integrand.
 * @param a Lower limit of integration.
 * @param b Upper limit of integration.
 * @param n Requested number of subintervals; rounded up to even.
 *
 * @return The approximate integral; exactly \c 0.0 when \p n is zero.
 *
 * @pre None.
 * @post The result is the composite Simpson estimate.
 *
 * @complexity \c O(n) evaluations of \p f.
 */
template <std::invocable<double> F>
[[nodiscard]] constexpr auto simpson(
  F&& f, double const a, double const b, std::size_t n
) noexcept(std::is_nothrow_invocable_v<F, double>) -> double {
  if (n == 0) {
    return 0.0;
  }
  if (n % 2 != 0) {
    ++n;
  }
  auto const h{(b - a) / static_cast<double>(n)};
  auto sum{f(a) + f(b)};
  for (auto i{std::size_t{1}}; i < n; ++i) {
    auto const x{a + static_cast<double>(i) * h};
    sum += ((i % 2 == 0) ? 2.0 : 4.0) * f(x);
  }
  return sum * h / 3.0;
}

namespace detail {

// Abscissae and weights for 5-point Gauss-Legendre on [-1, 1].
inline constexpr auto gl5_nodes{std::array<double, 5>{
  -0.906179845938663992797626878299,
  -0.538469310105683091036314420700,
  0.0,
  0.538469310105683091036314420700,
  0.906179845938663992797626878299,
}};
inline constexpr auto gl5_weights{std::array<double, 5>{
  0.236926885056189087514264040720,
  0.478628670499366468041291514836,
  0.568888888888888888888888888889,
  0.478628670499366468041291514836,
  0.236926885056189087514264040720,
}};

}  // namespace detail

/**
 * @brief Integrates \p f over \c [a, b] by 5-point Gauss-Legendre quadrature.
 *
 * Evaluates \p f at the five Gauss-Legendre nodes mapped onto \c [a, b] with no
 * subdivision. Exact for polynomials up to degree 9 and the most accurate of
 * the three rules per evaluation on smooth integrands.
 *
 * @tparam F Callable invocable as \c f(double) returning \c double.
 * @param f Integrand.
 * @param a Lower limit of integration.
 * @param b Upper limit of integration.
 *
 * @return The approximate integral, from exactly five evaluations of \p f.
 *
 * @pre None.
 * @post The result is exact for integrands of polynomial degree at most 9.
 *
 * @complexity \c O(1): exactly five evaluations of \p f.
 */
template <std::invocable<double> F>
[[nodiscard]] constexpr auto gauss_legendre_5(
  F&& f, double const a, double const b
) noexcept(std::is_nothrow_invocable_v<F, double>) -> double {
  auto const half{0.5 * (b - a)};
  auto const mid{0.5 * (b + a)};
  auto sum{0.0};
  for (auto i{std::size_t{0}}; i < 5; ++i) {
    sum += detail::gl5_weights[i] * f(mid + half * detail::gl5_nodes[i]);
  }
  return half * sum;
}

}  // namespace nexenne::algorithm
