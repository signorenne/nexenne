#pragma once

/**
 * @file
 * @brief Simplified one-dimensional Kalman filter.
 */

#include <concepts>

namespace nexenne::filter {

/**
 * @brief Simplified one-dimensional Kalman filter.
 *
 * Models a single scalar state with Gaussian uncertainty. The estimate is
 * \c x with error covariance \c P. Each \c push(z) of a measurement \c z runs
 * one predict-then-update cycle, where \c x_pred and \c P_pred are the
 * predicted (a priori) estimate and covariance:
 *
 * Predict (no process model, the state is assumed constant):
 * x_pred = x (the prior estimate carries over unchanged)
 * P_pred = P + Q (the uncertainty grows by the process noise)
 *
 * Update:
 * K = P_pred / (P_pred + R) (the Kalman gain)
 * x = x_pred + K * (z - x_pred) (pull the estimate toward the measurement)
 * P = (1 - K) * P_pred (the uncertainty shrinks)
 *
 * Where:
 * - \c Q is the process noise (how much the true state is
 * expected to drift between samples).
 * - \c R is the measurement noise (how noisy the sensor is).
 * - \c K is the Kalman gain (computed automatically).
 *
 * Common uses: smoothing noisy sensor readings (temperature,
 * distance, voltage) while tracking gradual real changes
 * faster than an EMA with the same smoothness.
 *
 * @tparam T Floating-point sample type. Default \c double.
 *
 * @note Statistically weighted smoothing of one noisy scalar when you can
 * characterise the sensor noise \c R and the expected drift \c Q: it tracks
 * genuine change faster than an EMA of equal smoothness because the gain
 * adapts.
 */
template <std::floating_point T = double>
class kalman {
public:
  using value_type = T;

private:
  value_type m_q{};           ///< process noise covariance Q
  value_type m_r{};           ///< measurement noise covariance R
  value_type m_estimate{};    ///< current state estimate
  value_type m_covariance{};  ///< current error covariance P
  bool m_primed{false};

public:
  /**
   * @brief Constructs a 1D Kalman filter from its noise parameters.
   *
   * The filter is unprimed at construction: the first \c push adopts
   * the measurement directly as the estimate so the output has no
   * startup transient.
   *
   * @param process_noise Process noise covariance Q, the
   * expected per-sample drift of the true
   * state.
   * @param measurement_noise Measurement noise covariance R, the
   * variance of the sensor.
   * @param initial_estimate Starting state estimate.
   * @param initial_covariance Starting error covariance.
   *
   * @pre \p process_noise and \p measurement_noise are non-negative
   * and not both zero (a zero denominator in the gain occurs
   * only when \c P + Q + R is zero).
   * @pre \p initial_covariance is non-negative.
   * @post \c value() returns \p initial_estimate and \c covariance()
   * returns \p initial_covariance until the first \c push.
   */
  constexpr kalman(
    T const process_noise,
    T const measurement_noise,
    T const initial_estimate = T{0},
    T const initial_covariance = T{1}
  ) noexcept
      : m_q{process_noise}
      , m_r{measurement_noise}
      , m_estimate{initial_estimate}
      , m_covariance{initial_covariance} {}

  /**
   * @brief Feeds one measurement and returns the updated estimate.
   *
   * Runs one predict-then-update cycle. The first call after
   * construction or \c reset() seeds the estimate with \p measurement
   * and skips the update step.
   *
   * @param measurement New scalar measurement.
   *
   * @return The filtered state estimate after incorporating
   * \p measurement.
   *
   * @pre None.
   * @post \c value() returns the value returned here; \c covariance()
   * reflects the post-update error covariance.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const measurement) noexcept -> T {
    if (!m_primed) {
      m_estimate = measurement;
      m_primed = true;
      return m_estimate;
    }

    // Predict
    auto const p_pred{m_covariance + m_q};

    // Update. Guard the gain against a zero denominator (P + Q + R == 0):
    // with no uncertainty anywhere the gain is zero, so the prediction is
    // trusted and the estimate is held rather than producing a NaN.
    auto const denom{p_pred + m_r};
    auto const k{denom > T{0} ? p_pred / denom : T{0}};
    m_estimate = m_estimate + k * (measurement - m_estimate);
    m_covariance = (T{1} - k) * p_pred;

    return m_estimate;
  }

  /**
   * @brief Returns the current state estimate without advancing.
   *
   * @return The most recent filtered estimate.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_estimate;
  }

  /**
   * @brief Returns the current error covariance.
   *
   * @return The post-update error covariance P, a measure of the
   * filter's confidence in the estimate.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto covariance() const noexcept -> T {
    return m_covariance;
  }

  /**
   * @brief Computes the Kalman gain that the next \c push would apply.
   *
   * Evaluates the gain from the current covariance and noise
   * parameters without mutating any state.
   *
   * @return The Kalman gain in \c [0, 1] for the predicted covariance.
   * When \c P + Q + R is zero the gain is zero (the prediction is
   * fully trusted) rather than an undefined division.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto gain() const noexcept -> T {
    auto const p_pred{m_covariance + m_q};
    auto const denom{p_pred + m_r};
    return denom > T{0} ? p_pred / denom : T{0};
  }

  /**
   * @brief Resets the filter to a known estimate and covariance.
   *
   * @param estimate State estimate to hold after the reset.
   * @param cov Error covariance to hold after the reset.
   *
   * @pre \p cov is non-negative.
   * @post \c value() returns \p estimate, \c covariance() returns
   * \p cov, and the next \c push reseeds the estimate from its
   * measurement.
   */
  constexpr auto reset(T const estimate = T{0}, T const cov = T{1}) noexcept -> void {
    m_estimate = estimate;
    m_covariance = cov;
    m_primed = false;
  }

  /**
   * @brief Replaces the process and measurement noise covariances.
   *
   * @param q New process noise covariance.
   * @param r New measurement noise covariance.
   *
   * @pre \p q and \p r are non-negative.
   * @post Subsequent \c push and \c gain calls use \p q and \p r; the
   * stored estimate and covariance are unchanged.
   */
  constexpr auto noise(T const q, T const r) noexcept -> void {
    m_q = q;
    m_r = r;
  }
};

}  // namespace nexenne::filter
