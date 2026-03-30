#pragma once

/**
 * @file
 * @brief Cascaded Butterworth IIR filter built from biquad sections.
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <numbers>

#include <nexenne/filter/biquad.hpp>

namespace nexenne::filter {

/**
 * @brief Cascaded Butterworth IIR filter built from biquad sections.
 *
 * Butterworth = maximally flat magnitude in passband, monotonic
 * rolloff. Higher orders give steeper rolloff at the cost of more
 * compute. Implemented as a cascade of biquad sections (Direct Form I);
 * each biquad realises one complex-conjugate pole pair, so order
 * \c 2*SectionsN needs \c SectionsN biquads.
 *
 * Currently provides low-pass and high-pass designs. For band-pass /
 * band-stop, cascade an LP and an HP at the appropriate corners.
 *
 * @tparam T Floating-point sample type
 * @tparam SectionsN Number of biquad sections (filter order is 2*SectionsN)
 *
 * @note Reach for this when you need a steeper yet ripple-free rolloff than
 * one biquad gives. More sections give a steeper rolloff at the cost
 * of latency and compute.
 */
template <std::floating_point T, std::size_t SectionsN>
  requires(SectionsN > 0)
class butterworth {
public:
  using value_type = T;
  static constexpr std::size_t sections{SectionsN};
  static constexpr std::size_t order{2 * SectionsN};

  /**
   * @brief Selects the response shape of a Butterworth design.
   *
   * @pre None.
   * @post None.
   */
  enum class kind {
    low_pass,  ///< passes frequencies below the cutoff
    high_pass  ///< passes frequencies above the cutoff
  };

private:
  std::array<biquad<T>, SectionsN> m_sections{};
  value_type m_last{};

  /**
   * @brief Per-section Q for an order-2N Butterworth cascade.
   *
   * Q_k = 1 / (2 * sin((2k+1) * pi / (4N))).
   */
  [[nodiscard]] static auto section_q(std::size_t const section_idx) noexcept -> T {
    auto const n{static_cast<T>(SectionsN)};
    auto const theta{(static_cast<T>(2 * section_idx + 1) * std::numbers::pi_v<T>) / (T{4} * n)};
    return T{1} / (T{2} * std::sin(theta));
  }

public:
  /**
   * @brief Constructs an undesigned cascade with pass-through sections.
   *
   * Each biquad section starts with default coefficients, so the
   * cascade passes its input unchanged until a design function is
   * called.
   *
   * @pre None.
   * @post All sections are zeroed and \c value() returns zero.
   */
  constexpr butterworth() noexcept = default;

  /**
   * @brief Configures the cascade as a low-pass Butterworth filter.
   *
   * Assigns each biquad section the cutoff and the Butterworth
   * per-section Q so the cascade realises an order-2N maximally flat
   * low-pass.
   *
   * @param cutoff_hz Cutoff frequency in Hz (the -3 dB point).
   * @param sample_rate_hz Sample rate in Hz.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @post Every section holds low-pass coefficients; section state is
   * not cleared, so call \c reset() to remove any transient.
   *
   * @complexity \c O(SectionsN).
   */
  auto design_low_pass(T const cutoff_hz, T const sample_rate_hz) noexcept -> void {
    for (std::size_t i{0}; i < SectionsN; ++i) {
      m_sections[i] = biquad<T>::make_lowpass(cutoff_hz, sample_rate_hz, section_q(i));
    }
  }

  /**
   * @brief Configures the cascade as a high-pass Butterworth filter.
   *
   * @param cutoff_hz Cutoff frequency in Hz (the -3 dB point).
   * @param sample_rate_hz Sample rate in Hz.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @post Every section holds high-pass coefficients; section state is
   * not cleared, so call \c reset() to remove any transient.
   *
   * @complexity \c O(SectionsN).
   */
  auto design_high_pass(T const cutoff_hz, T const sample_rate_hz) noexcept -> void {
    for (std::size_t i{0}; i < SectionsN; ++i) {
      m_sections[i] = biquad<T>::make_highpass(cutoff_hz, sample_rate_hz, section_q(i));
    }
  }

  /**
   * @brief Feeds one sample through every biquad section in series.
   *
   * @param sample New input sample.
   *
   * @return The output of the final section.
   *
   * @pre None.
   * @post Each section advances by one sample and \c value() returns
   * the value returned here.
   *
   * @complexity \c O(SectionsN).
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept -> T {
    value_type x{sample};
    for (auto& bq : m_sections) {
      x = bq.push(x);
    }
    m_last = x;
    return x;
  }

  /**
   * @brief Returns the most recent cascade output without advancing.
   *
   * @return The last value produced by \c push, or zero before the
   * first \c push or after \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_last;
  }

  /**
   * @brief Clears the delay lines of every section.
   *
   * @pre None.
   * @post All section states and \c value() are zero; the designed
   * coefficients are preserved.
   *
   * @complexity \c O(SectionsN).
   */
  constexpr auto reset() noexcept -> void {
    for (auto& bq : m_sections) {
      bq.reset();
    }
    m_last = value_type{};
  }
};

}  // namespace nexenne::filter
