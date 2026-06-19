#pragma once

/**
 * @file
 * @brief Interpolation primitives, stateless and stateful.
 *
 * Stateless per-segment functions: \c lerp (linear), \c smoothstep (Hermite
 * ease, 3t^2 - 2t^3), \c smootherstep (Perlin quintic ease,
 * 6t^5 - 15t^4 + 10t^3), \c bilinear (2x2 grid), and \c catmull_rom (a cubic
 * segment between p1 and p2 with p0/p3 as tangent neighbours). Stateful
 * multi-knot tables: \c linear_interpolator (piecewise linear, binary-search
 * lookup) and \c cubic_spline (natural cubic spline, C2-continuous with zero
 * second derivative at both ends). Both stateful types take strictly increasing
 * x knots; out-of-range queries clamp to the nearest endpoint.
 */

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Linear interpolation between \p a and \p b at parameter \p t.
 *
 * Returns \c a + (b - a) * t. \p t is not clamped, so values outside \c [0, 1]
 * extrapolate.
 *
 * @tparam T Floating-point type.
 * @param a Value at \c t == 0.
 * @param b Value at \c t == 1.
 * @param t Interpolation parameter.
 *
 * @return The interpolated value.
 *
 * @pre None.
 * @post For \p t in \c [0, 1] the result lies between \p a and \p b.
 *
 * @complexity \c O(1).
 */
template <std::floating_point T>
[[nodiscard]] constexpr auto lerp(T const a, T const b, T const t) noexcept -> T {
  return a + (b - a) * t;
}

/**
 * @brief Cubic Hermite smoothstep of \p t over the edges \c [a, b].
 *
 * Maps \p t to \c x = clamp((t - a) / (b - a), 0, 1) and returns
 * \c x*x*(3 - 2*x), which has zero first derivative at both edges.
 *
 * @tparam T Floating-point type.
 * @param a Lower edge.
 * @param b Upper edge.
 * @param t Value to smooth.
 *
 * @return A value in \c [0, 1]: 0 at or below \p a, 1 at or above \p b, eased
 *         in between.
 *
 * @pre \p a is not equal to \p b.
 * @post The result lies in \c [0, 1] and is non-decreasing in \p t.
 *
 * @complexity \c O(1).
 */
template <std::floating_point T>
[[nodiscard]] constexpr auto smoothstep(T const a, T const b, T const t) noexcept -> T {
  auto const x{std::clamp((t - a) / (b - a), T{0}, T{1})};
  return x * x * (T{3} - T{2} * x);
}

/**
 * @brief Quintic smootherstep of \p t over the edges \c [a, b].
 *
 * Maps \p t to \c x = clamp((t - a) / (b - a), 0, 1) and returns Perlin's
 * \c x*x*x*(x*(6*x - 15) + 10), which has zero first and second derivatives at
 * both edges, removing the kink \c smoothstep leaves in shading and noise.
 *
 * @tparam T Floating-point type.
 * @param a Lower edge.
 * @param b Upper edge.
 * @param t Value to smooth.
 *
 * @return A value in \c [0, 1]: 0 at or below \p a, 1 at or above \p b, eased
 *         in between.
 *
 * @pre \p a is not equal to \p b.
 * @post The result lies in \c [0, 1] and is non-decreasing in \p t.
 *
 * @complexity \c O(1).
 */
template <std::floating_point T>
[[nodiscard]] constexpr auto smootherstep(T const a, T const b, T const t) noexcept -> T {
  auto const x{std::clamp((t - a) / (b - a), T{0}, T{1})};
  return x * x * x * (x * (x * T{6} - T{15}) + T{10});
}

/**
 * @brief Bilinear interpolation of the four corner values of a unit cell.
 *
 * Interpolates along \c x first, then \c y, by composing three \c lerp calls.
 * The parameters are not clamped, so values outside \c [0, 1] extrapolate.
 *
 * @tparam T Floating-point type.
 * @param v00 Value at corner \c (0, 0).
 * @param v10 Value at corner \c (1, 0).
 * @param v01 Value at corner \c (0, 1).
 * @param v11 Value at corner \c (1, 1).
 * @param tx Interpolation parameter along \c x.
 * @param ty Interpolation parameter along \c y.
 *
 * @return The bilinearly interpolated value.
 *
 * @pre None.
 * @post For \p tx and \p ty in \c [0, 1] the result lies within the range of
 *       the four corner values.
 *
 * @complexity \c O(1).
 */
template <std::floating_point T>
[[nodiscard]] constexpr auto
bilinear(T const v00, T const v10, T const v01, T const v11, T const tx, T const ty) noexcept -> T {
  return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

/**
 * @brief Evaluates a Catmull-Rom spline segment between \p p1 and \p p2.
 *
 * Returns the point at parameter \p t on the cubic Catmull-Rom segment whose
 * tangents come from the neighbouring control points \p p0 and \p p3. Passes
 * through every control point with a continuous first derivative.
 *
 * @tparam T Floating-point type.
 * @param p0 Control point before the segment, a tangent neighbour.
 * @param p1 Start of the segment, reached at \c t == 0.
 * @param p2 End of the segment, reached at \c t == 1.
 * @param p3 Control point after the segment, a tangent neighbour.
 * @param t Parameter sweeping from \p p1 to \p p2.
 *
 * @return The interpolated point on the segment.
 *
 * @pre None.
 * @post At \c t == 0 the result equals \p p1 and at \c t == 1 it equals \p p2.
 *
 * @complexity \c O(1).
 */
template <std::floating_point T>
[[nodiscard]] constexpr auto
catmull_rom(T const p0, T const p1, T const p2, T const p3, T const t) noexcept -> T {
  auto const t2{t * t};
  auto const t3{t2 * t};
  return T{0.5}
         * ((T{2} * p1) + (-p0 + p2) * t + (T{2} * p0 - T{5} * p1 + T{4} * p2 - p3) * t2 + (-p0 + T{3} * p1 - T{3} * p2 + p3) * t3);
}

/**
 * @brief Piecewise-linear interpolator over a table of \c (x, y) knots.
 *
 * Stores parallel knot arrays and evaluates by binary search for the bracketing
 * segment, then a linear blend. Out-of-range queries clamp to the nearest
 * endpoint.
 *
 * @tparam T Floating-point value type; defaults to \c double.
 */
template <std::floating_point T = double>
class linear_interpolator {
public:
  using value_type = T;  ///< Floating-point value type of the knots.

private:
  std::vector<T> m_x{};
  std::vector<T> m_y{};

public:
  /**
   * @brief Builds an interpolator from parallel \c x and \c y knot arrays.
   *
   * Copies the knots verbatim; no sorting is performed.
   *
   * @param xs Strictly increasing knot abscissae.
   * @param ys Knot ordinates, parallel to \p xs.
   *
   * @pre \p xs is strictly increasing and \p ys has the same length.
   * @post \c size() equals \c xs.size() and the interpolator is ready.
   */
  linear_interpolator(std::span<T const> const xs, std::span<T const> const ys)
      : m_x{xs.begin(), xs.end()}, m_y{ys.begin(), ys.end()} {}

  /**
   * @brief Number of knots in the table.
   *
   * @return The knot count.
   *
   * @pre None.
   * @post The interpolator is unchanged.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_x.size();
  }

  /**
   * @brief Evaluates the interpolant at \p x.
   *
   * Locates the bracketing segment by binary search and linearly blends its
   * endpoints. Queries outside the knot range clamp to the nearest endpoint;
   * an empty table yields zero.
   *
   * @param x Query abscissa.
   *
   * @return The interpolated ordinate, or 0 when the table is empty.
   *
   * @pre The table was built from strictly increasing \c x knots.
   * @post The result lies within the surrounding knot ordinates; the
   *       interpolator is unchanged.
   *
   * @complexity \c O(log N) in the knot count.
   */
  [[nodiscard]] auto operator()(T const x) const noexcept -> T {
    if (m_x.empty()) {
      return T{0};
    }
    if (x <= m_x.front()) {
      return m_y.front();
    }
    if (x >= m_x.back()) {
      return m_y.back();
    }
    auto const it{std::ranges::lower_bound(m_x, x)};
    auto const i{static_cast<std::size_t>(std::distance(m_x.begin(), it))};
    // Now m_x[i - 1] < x <= m_x[i].
    auto const x0{m_x[i - 1]};
    auto const x1{m_x[i]};
    auto const t{(x - x0) / (x1 - x0)};
    return lerp(m_y[i - 1], m_y[i], t);
  }
};

/**
 * @brief Natural cubic spline through arbitrary knots.
 *
 * Builds a C2 piecewise-cubic curve through every \c (x[i], y[i]) point, with
 * the second derivative set to zero at both ends (the natural boundary). The
 * knot second derivatives are solved by the Thomas tridiagonal algorithm in
 * \c O(N); queries are \c O(log N).
 *
 * @tparam T Floating-point value type; defaults to \c double.
 */
template <std::floating_point T = double>
class cubic_spline {
public:
  using value_type = T;  ///< Floating-point value type of the knots.

private:
  std::vector<T> m_x{};
  std::vector<T> m_y{};
  std::vector<T> m_m{};  ///< Second derivatives at each knot.

public:
  /**
   * @brief Builds a natural cubic spline through the given knots.
   *
   * Solves the tridiagonal system for the knot second derivatives by the Thomas
   * algorithm. Fewer than three knots leave the second derivatives zero, so
   * evaluation degrades gracefully to linear-and-clamp behaviour.
   *
   * @param xs Strictly increasing knot abscissae.
   * @param ys Knot ordinates, parallel to \p xs.
   *
   * @pre \p xs is strictly increasing and \p ys has the same length.
   * @post \c size() equals \c xs.size() and the spline is ready.
   *
   * @complexity \c O(N) in the knot count.
   */
  cubic_spline(std::span<T const> const xs, std::span<T const> const ys)
      : m_x{xs.begin(), xs.end()}, m_y{ys.begin(), ys.end()}, m_m(xs.size(), T{0}) {
    auto const n{m_x.size()};
    if (n < 3) {
      return;  // degenerate; falls back to linear behaviour
    }

    // Tridiagonal system for the second derivatives (Thomas algorithm).
    auto h{std::vector<T>(n - 1)};
    auto alpha{std::vector<T>(n - 1)};
    for (auto i{std::size_t{0}}; i + 1 < n; ++i) {
      h[i] = m_x[i + 1] - m_x[i];
    }
    for (auto i{std::size_t{1}}; i + 1 < n; ++i) {
      alpha[i] = T{3} / h[i] * (m_y[i + 1] - m_y[i]) - T{3} / h[i - 1] * (m_y[i] - m_y[i - 1]);
    }

    auto l{std::vector<T>(n, T{1})};
    auto mu{std::vector<T>(n, T{0})};
    auto z{std::vector<T>(n, T{0})};
    for (auto i{std::size_t{1}}; i + 1 < n; ++i) {
      l[i] = T{2} * (m_x[i + 1] - m_x[i - 1]) - h[i - 1] * mu[i - 1];
      mu[i] = h[i] / l[i];
      z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }
    // Back-substitution from the second-to-last knot down to the first.
    for (auto j{n - 1}; j > 0; j -= 1) {
      m_m[j - 1] = z[j - 1] - mu[j - 1] * m_m[j];
    }
  }

  /**
   * @brief Number of knots in the spline.
   *
   * @return The knot count.
   *
   * @pre None.
   * @post The spline is unchanged.
   */
  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_x.size();
  }

  /**
   * @brief Evaluates the spline at \p x.
   *
   * Locates the bracketing segment by binary search and evaluates the piecewise
   * cubic. Queries outside the knot range, or a table of fewer than three
   * knots, clamp to the nearest endpoint; an empty table yields zero.
   *
   * @param x Query abscissa.
   *
   * @return The interpolated ordinate, or 0 when the table is empty.
   *
   * @pre The spline was built from strictly increasing \c x knots.
   * @post The spline is unchanged.
   *
   * @complexity \c O(log N) in the knot count.
   */
  [[nodiscard]] auto operator()(T const x) const noexcept -> T {
    if (m_x.empty()) {
      return T{0};
    }
    if (m_x.size() < 3 || x <= m_x.front() || x >= m_x.back()) {
      if (x <= m_x.front()) {
        return m_y.front();
      }
      if (x >= m_x.back()) {
        return m_y.back();
      }
    }
    auto const it{std::ranges::lower_bound(m_x, x)};
    auto const i{static_cast<std::size_t>(std::distance(m_x.begin(), it))};
    auto const j{i - 1};
    auto const h{m_x[i] - m_x[j]};
    // Cubic on [j, j + 1] with second-derivative boundary m_m.
    auto const a{m_y[j]};
    auto const b{(m_y[i] - m_y[j]) / h - h * (T{2} * m_m[j] + m_m[i]) / T{3}};
    auto const c{m_m[j]};
    auto const d{(m_m[i] - m_m[j]) / (T{3} * h)};
    auto const dx{x - m_x[j]};
    return a + b * dx + c * dx * dx + d * dx * dx * dx;
  }
};

}  // namespace nexenne::algorithm
