#pragma once

/**
 * @file
 * @brief Scalar root-finders: robust bisection and fast Newton-Raphson.
 *
 * \c bisection is derivative-free and always converges given a bracketing
 * interval, halving it each step. \c newton converges quadratically near a
 * simple root but needs a derivative and a good starting guess. Both report
 * failure through \c std::expected<T, numerical_error>.
 */

#include <concepts>
#include <cstddef>
#include <expected>

#include <nexenne/algorithm/numerical/numerical_error.hpp>

namespace nexenne::algorithm {

/**
 * @brief Finds a root of \p f in \c [lo, hi] by interval bisection.
 *
 * Requires \c f(lo) and \c f(hi) to have opposite signs so the intermediate
 * value theorem guarantees a root. Each step halves the interval, keeping the
 * half whose endpoints still bracket a sign change, until the interval is
 * narrower than \p tol, a midpoint evaluates to zero, or \p max_iter steps
 * elapse.
 *
 * @tparam T Floating-point type.
 * @tparam Fn Callable invocable as \c f(T) returning \c T.
 * @param f Function whose root is sought.
 * @param lo Lower bracket endpoint.
 * @param hi Upper bracket endpoint.
 * @param tol Absolute interval-width tolerance.
 * @param max_iter Maximum number of bisection steps.
 *
 * @return The approximate root on success; \c numerical_error::not_bracketed
 *         when \c f(lo) and \c f(hi) share a sign; \c numerical_error::no_convergence
 *         when \p max_iter steps run without meeting \p tol.
 *
 * @pre \p f is continuous on \c [lo, hi]; \p tol is positive.
 * @post On success the returned value lies in \c [lo, hi] and the bracket it
 *       was found in is narrower than \p tol or had a zero midpoint.
 *
 * @complexity \c O(log((hi - lo) / tol)) evaluations of \p f.
 */
template <std::floating_point T, typename Fn>
[[nodiscard]] constexpr auto bisection(
  Fn&& f, T const lo, T const hi, T const tol = T{1e-9}, std::size_t const max_iter = 100
) noexcept -> std::expected<T, numerical_error> {
  auto a{lo};
  auto b{hi};
  auto const fa_0{f(a)};
  auto const fb_0{f(b)};
  auto fa{fa_0};

  // A non-finite endpoint value cannot bracket a root: every sign test below is
  // false for a NaN, so guard it explicitly (NaN compares unequal to itself)
  // rather than burn the whole iteration budget and report no_convergence.
  if (!(fa_0 == fa_0) || !(fb_0 == fb_0)) {
    return std::unexpected{numerical_error::not_bracketed};
  }
  if ((fa_0 > T{0} && fb_0 > T{0}) || (fa_0 < T{0} && fb_0 < T{0})) {
    return std::unexpected{numerical_error::not_bracketed};
  }
  if (fa_0 == T{0}) {
    return a;
  }
  if (fb_0 == T{0}) {
    return b;
  }

  for (auto i{std::size_t{0}}; i < max_iter; ++i) {
    auto const mid{a + (b - a) / T{2}};  // numerically safer than (a + b) / 2
    auto const fm{f(mid)};
    if (fm == T{0} || (b - a) < tol) {
      return mid;
    }
    if ((fa > T{0} && fm > T{0}) || (fa < T{0} && fm < T{0})) {
      a = mid;
      fa = fm;
    } else {
      b = mid;
    }
  }
  return std::unexpected{numerical_error::no_convergence};
}

/**
 * @brief Finds a root of \p f from \p x0 by Newton-Raphson iteration.
 *
 * Steps \c x by \c -f(x) / df(x) each iteration, converging quadratically near
 * a simple root. Much faster than \c bisection when it converges, but may
 * diverge when the derivative is small or \p x0 is far from a root. The caller
 * supplies the derivative \p df.
 *
 * @tparam T Floating-point type.
 * @tparam Fn Callable invocable as \c f(T) returning \c T.
 * @tparam Dfn Callable invocable as \c df(T) returning \c T.
 * @param f Function whose root is sought.
 * @param df Derivative of \p f.
 * @param x0 Starting guess.
 * @param tol Absolute tolerance on \c |f(x)| and on the step size.
 * @param max_iter Maximum number of iterations.
 *
 * @return The approximate root on success; \c numerical_error::no_convergence
 *         when \p max_iter iterations elapse or the derivative reaches zero.
 *
 * @pre \p df is the derivative of \p f; \p tol is positive.
 * @post On success either \c |f(result)| or the final step is below \p tol.
 *
 * @complexity \c O(max_iter) evaluations of \p f and \p df; quadratic
 *             convergence near a simple root.
 */
template <std::floating_point T, typename Fn, typename Dfn>
[[nodiscard]] constexpr auto newton(
  Fn&& f, Dfn&& df, T const x0, T const tol = T{1e-9}, std::size_t const max_iter = 50
) noexcept -> std::expected<T, numerical_error> {
  auto x{x0};
  for (auto i{std::size_t{0}}; i < max_iter; ++i) {
    auto const fx{f(x)};
    if ((fx < T{0} ? -fx : fx) < tol) {
      return x;
    }
    auto const dfx{df(x)};
    if (dfx == T{0}) {
      return std::unexpected{numerical_error::no_convergence};
    }
    auto const step{fx / dfx};
    x -= step;
    if ((step < T{0} ? -step : step) < tol) {
      return x;
    }
  }
  return std::unexpected{numerical_error::no_convergence};
}

}  // namespace nexenne::algorithm
