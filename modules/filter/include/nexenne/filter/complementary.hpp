#pragma once

/**
 * @file
 * @brief Complementary filter for two-sensor fusion.
 */

#include <concepts>

namespace nexenne::filter {

/**
 * @brief Complementary filter for two-sensor fusion.
 *
 * Blends a "fast but drifty" sensor with a "slow but stable"
 * sensor using a single tuning parameter alpha:
 *
 * y = alpha * fast + (1 - alpha) * slow
 *
 * The canonical use case is IMU tilt estimation: blend the
 * gyroscope (fast, drifts over time) with the accelerometer
 * (noisy but drift-free) to get a stable angle reading.
 *
 * Unlike a Kalman filter, the complementary filter has no
 * internal state model: it is a single weighted sum per sample.
 * Choose it when simplicity and determinism matter more than
 * optimal noise rejection.
 *
 * @tparam T Floating-point sample type. Default \c double.
 *
 * @note Fusing two sensors each good in a different frequency band (the classic
 * case is IMU tilt from a drifting gyro and a noisy accelerometer): pick it over
 * a Kalman filter when determinism and one obvious tuning knob matter more than
 * optimality.
 */
template <std::floating_point T = double>
class complementary {
public:
  using value_type = T;

private:
  value_type m_alpha{};
  value_type m_value{};

public:
  /**
   * @brief Constructs a complementary filter with blend weight \p alpha.
   *
   * @param alpha Weight of the fast sensor in \c (0, 1). Larger
   * values trust the fast sensor more; the slow sensor
   * receives weight \c 1 - alpha.
   *
   * @pre \p alpha lies in \c (0, 1).
   * @post \c alpha() returns \p alpha and \c value() returns zero.
   */
  constexpr explicit complementary(T const alpha) noexcept : m_alpha{alpha} {}

  /**
   * @brief Fuses a fast and a slow sensor reading.
   *
   * Computes the weighted blend \c alpha * fast + (1 - alpha) * slow
   * and stores it as the current output.
   *
   * @param fast Reading from the fast, drift-prone sensor.
   * @param slow Reading from the slow, drift-free sensor.
   *
   * @return The fused output.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const fast, T const slow) noexcept -> T {
    m_value = m_alpha * fast + (T{1} - m_alpha) * slow;
    return m_value;
  }

  /**
   * @brief Single-sensor overload that blends a sample with the
   * previous output.
   *
   * Provided so the type satisfies \c filter_like. The sample is
   * treated as the fast input and the previous output as the slow
   * input, giving a first-order low-pass response.
   *
   * @param sample New input sample.
   *
   * @return The fused output.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept -> T {
    m_value = m_alpha * sample + (T{1} - m_alpha) * m_value;
    return m_value;
  }

  /**
   * @brief Returns the most recent fused output without advancing.
   *
   * @return The last value produced by a \c push overload, or zero
   * before the first \c push or after \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_value;
  }

  /**
   * @brief Clears the stored output to zero.
   *
   * @pre None.
   * @post \c value() returns zero.
   */
  constexpr auto reset() noexcept -> void {
    m_value = T{0};
  }

  /**
   * @brief Resets the stored output to a known value.
   *
   * @param initial Value the filter holds after the reset.
   *
   * @pre None.
   * @post \c value() returns \p initial.
   */
  constexpr auto reset(T const initial) noexcept -> void {
    m_value = initial;
  }

  /**
   * @brief Returns the current blend weight of the fast sensor.
   *
   * @return The blend weight in use.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto alpha() const noexcept -> T {
    return m_alpha;
  }

  /**
   * @brief Replaces the blend weight for subsequent samples.
   *
   * @param a New blend weight in \c (0, 1).
   *
   * @pre \p a lies in \c (0, 1).
   * @post \c alpha() returns \p a; the stored output is unchanged.
   */
  constexpr auto alpha(value_type const a) noexcept -> void {
    m_alpha = a;
  }
};

}  // namespace nexenne::filter
