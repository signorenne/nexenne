#pragma once

/**
 * @file
 * @brief Single-step ODE / initial-value-problem integrators.
 *
 * Each function advances one step of size \c dt from the current state using
 * the supplied derivative or acceleration functor; the caller drives the time
 * loop. For first-order problems (dy/dt = f(t, y)): \c euler_step (explicit
 * Euler, first order, a baseline) and \c rk4_step (classic 4th-order
 * Runge-Kutta, the non-stiff workhorse). For second-order mechanics:
 * \c verlet_step (position Verlet, symplectic, needs the previous position) and
 * \c velocity_verlet_step (the common physics-engine form, returns updated
 * position and velocity). All are templated on the state type, so the same code
 * works for scalars, vectors, or any type with the needed arithmetic.
 */

#include <concepts>
#include <utility>

namespace nexenne::algorithm {

/**
 * @brief Advances one explicit Euler step of a first-order ODE.
 *
 * Computes \c y + dt * f(t, y). First-order accurate; not for stiff or
 * stability-sensitive systems.
 *
 * @tparam T State and time type supporting the required arithmetic.
 * @tparam F Callable invocable as \c f(t, y) returning the derivative.
 * @param f Derivative function \c f(t, y) = dy/dt.
 * @param t Current time.
 * @param y Current state.
 * @param dt Step size.
 *
 * @return The state after one step of size \p dt.
 *
 * @pre None.
 * @post The returned state is the first-order Euler estimate at \c t + dt.
 *
 * @complexity \c O(1): one evaluation of \p f.
 */
template <typename T, std::invocable<T, T> F>
[[nodiscard]] constexpr auto euler_step(F&& f, T const t, T const y, T const dt) -> T {
  return y + dt * f(t, y);
}

/**
 * @brief Advances one classic 4th-order Runge-Kutta step of a first-order ODE.
 *
 * Combines four derivative samples into a weighted estimate. The workhorse for
 * non-stiff problems: excellent accuracy per cost at a fixed step size.
 *
 * @tparam T State and time type supporting the required arithmetic.
 * @tparam F Callable invocable as \c f(t, y) returning the derivative.
 * @param f Derivative function \c f(t, y) = dy/dt.
 * @param t Current time.
 * @param y Current state.
 * @param dt Step size.
 *
 * @return The state after one step of size \p dt.
 *
 * @pre None.
 * @post The returned state is the fourth-order RK estimate at \c t + dt.
 *
 * @complexity \c O(1): four evaluations of \p f.
 */
template <typename T, std::invocable<T, T> F>
[[nodiscard]] constexpr auto rk4_step(F&& f, T const t, T const y, T const dt) -> T {
  auto const half{dt / T{2}};
  auto const k1{f(t, y)};
  auto const k2{f(t + half, y + half * k1)};
  auto const k3{f(t + half, y + half * k2)};
  auto const k4{f(t + dt, y + dt * k3)};
  return y + (dt / T{6}) * (k1 + T{2} * k2 + T{2} * k3 + k4);
}

/**
 * @brief Advances one position-Verlet step of a second-order system.
 *
 * Computes \c 2*x - x_prev + a*dt*dt from the current position, the previous
 * position, and the acceleration at the current position. Symplectic, so it
 * conserves energy over long integrations; the caller maintains \p x_prev.
 *
 * @tparam T State type supporting the required arithmetic.
 * @param x Current position.
 * @param x_prev Position one step earlier.
 * @param a Acceleration at the current position.
 * @param dt Step size.
 *
 * @return The next position.
 *
 * @pre \p x_prev is the position from the preceding step at the same \p dt.
 * @post The returned position advances the trajectory by one step of \p dt.
 *
 * @complexity \c O(1).
 */
template <typename T>
[[nodiscard]] constexpr auto
verlet_step(T const x, T const x_prev, T const a, T const dt) noexcept -> T {
  return T{2} * x - x_prev + a * dt * dt;
}

/**
 * @brief Advances one velocity-Verlet step of a second-order system.
 *
 * Updates position and velocity in lockstep using an acceleration functor
 * \c accel(x), evaluating it once at the current position and once at the new
 * position. The symplectic integrator most physics engines use; it conserves
 * energy without the previous-position bookkeeping of position Verlet.
 *
 * @tparam T State type supporting the required arithmetic.
 * @tparam Accel Callable invocable as \c accel(x) returning the acceleration.
 * @param accel Acceleration as a function of position.
 * @param x Current position.
 * @param v Current velocity.
 * @param dt Step size.
 *
 * @return The updated \c (position, velocity) pair after one step of \p dt.
 *
 * @pre None.
 * @post The returned pair advances the trajectory by one step of \p dt and
 *       evaluates \p accel exactly twice.
 *
 * @complexity \c O(1): two evaluations of \p accel.
 */
template <typename T, std::invocable<T> Accel>
[[nodiscard]] constexpr auto
velocity_verlet_step(Accel&& accel, T const x, T const v, T const dt) -> std::pair<T, T> {
  auto const a{accel(x)};
  auto const x_new{x + v * dt + T{0.5} * a * dt * dt};
  auto const a_new{accel(x_new)};
  auto const v_new{v + T{0.5} * (a + a_new) * dt};
  return {x_new, v_new};
}

}  // namespace nexenne::algorithm
