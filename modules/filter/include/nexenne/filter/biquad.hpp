#pragma once

/**
 * @file
 * @brief General second-order IIR (biquad) filter.
 */

#include <cmath>
#include <concepts>
#include <numbers>

namespace nexenne::filter {

/**
 * @brief General second-order IIR (biquad) filter.
 *
 * The biquad is the Swiss-army knife of digital filtering: by
 * choosing different coefficient sets you get low-pass,
 * high-pass, band-pass, notch (band-reject), all-pass, peaking
 * EQ, or shelving responses, all from the same 5-coefficient
 * difference equation:
 *
 * y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
 * - a1*y[n-1] - a2*y[n-2]
 *
 * (a0 is normalised to 1.)
 *
 * Static named constructors (\c lowpass, \c highpass, \c bandpass,
 * \c notch) compute the standard Audio EQ Cookbook coefficients
 * from cutoff frequency, sample rate, and Q factor.
 *
 * Two delay elements (Direct Form I), zero allocation.
 *
 * @tparam T Floating-point sample type. Default \c double.
 *
 * @note Reach for this when a single-pole slope is too gentle or you need
 * a resonant peak, a band-pass, or a notch (for example rejecting
 * 50 / 60 Hz mains hum). A \c q of \c 0.7071 gives the flat
 * Butterworth corner; higher \c q peaks.
 */
template <std::floating_point T = double>
class biquad {
public:
  using value_type = T;

  /**
   * @brief Normalised Direct Form I biquad coefficients.
   *
   * The feedback coefficient \c a0 is assumed normalised to \c 1, so
   * only the two feedforward taps after \c b0 and the two feedback
   * taps are stored.
   *
   * @pre None.
   * @post None.
   */
  struct coefficients {
    value_type b0{value_type{1}};  ///< feedforward tap for x[n]
    value_type b1{value_type{0}};  ///< feedforward tap for x[n-1]
    value_type b2{value_type{0}};  ///< feedforward tap for x[n-2]
    value_type a1{value_type{0}};  ///< feedback tap for y[n-1]
    value_type a2{value_type{0}};  ///< feedback tap for y[n-2]
  };

private:
  coefficients m_c{};
  value_type m_x1{value_type{0}};  ///< x[n-1]
  value_type m_x2{value_type{0}};  ///< x[n-2]
  value_type m_y1{value_type{0}};  ///< y[n-1]
  value_type m_y2{value_type{0}};  ///< y[n-2]
  value_type m_value{value_type{0}};

public:
  /**
   * @brief Constructs a pass-through biquad with default coefficients.
   *
   * All delay elements and feedback taps are zero; \c b0 is one, so
   * the filter passes its input unchanged until coefficients are
   * supplied via a named constructor or \c coefs.
   *
   * @pre None.
   * @post All delay elements are zero and \c coefs() returns a
   * default-constructed \c coefficients (pass-through).
   */
  constexpr biquad() noexcept = default;

  /**
   * @brief Constructs a biquad with the given coefficient set.
   *
   * @param c Normalised coefficients defining the response.
   *
   * @pre \p c describes a stable filter if recursion stability is
   * required (poles inside the unit circle).
   * @post All delay elements are zero; \c coefs() returns \p c.
   */
  constexpr explicit biquad(coefficients const c) noexcept : m_c{c} {}

  /**
   * @brief Feeds one sample through the difference equation.
   *
   * Evaluates the Direct Form I recurrence and shifts the two input
   * and two output delay elements.
   *
   * @param x New input sample.
   *
   * @return The filtered output for \p x.
   *
   * @pre None.
   * @post The delay line advances by one sample and \c value()
   * returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const x) noexcept -> T {
    auto const y{m_c.b0 * x + m_c.b1 * m_x1 + m_c.b2 * m_x2 - m_c.a1 * m_y1 - m_c.a2 * m_y2};
    m_x2 = m_x1;
    m_x1 = x;
    m_y2 = m_y1;
    m_y1 = y;
    m_value = y;
    return y;
  }

  /**
   * @brief Returns the most recent output without advancing.
   *
   * @return The last value produced by \c push, or zero before the
   * first \c push or after \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_value;
  }

  /**
   * @brief Clears the delay line back to silence.
   *
   * @pre None.
   * @post All input and output delay elements are zero and
   * \c value() returns zero; the coefficients are unchanged.
   */
  constexpr auto reset() noexcept -> void {
    m_x1 = m_x2 = m_y1 = m_y2 = m_value = T{0};
  }

  /**
   * @brief Replaces the coefficient set without touching the state.
   *
   * @param c New normalised coefficients.
   *
   * @pre None.
   * @post \c coefs() returns \p c; the delay line is unchanged, so a
   * large coefficient jump may produce a transient.
   */
  constexpr auto coefs(coefficients const c) noexcept -> void {
    m_c = c;
  }

  /**
   * @brief Returns the coefficient set currently in use.
   *
   * @return The active normalised coefficients.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto coefs() const noexcept -> coefficients {
    return m_c;
  }

  /**
   * @brief Designs a second-order low-pass biquad.
   *
   * Computes Audio EQ Cookbook coefficients for a resonant low-pass
   * whose -3 dB corner sits at \p cutoff_hz. A Q of \c 0.7071 gives a
   * maximally flat Butterworth response; higher Q adds resonance.
   *
   * @param cutoff_hz Cutoff frequency in Hz (the -3 dB point).
   * @param sample_rate_hz Sample rate in Hz.
   * @param q Quality factor.
   *
   * @return A biquad configured as a low-pass with zeroed state.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @pre \p q is positive.
   * @post The returned filter has all delay elements at zero.
   */
  [[nodiscard]] static auto make_lowpass(
    T const cutoff_hz, T const sample_rate_hz, T const q = T{0.7071}
  ) noexcept -> biquad {
    auto const w0{T{2} * std::numbers::pi_v<T> * cutoff_hz / sample_rate_hz};
    auto const sin_w{std::sin(w0)};
    auto const cos_w{std::cos(w0)};
    auto const a{sin_w / (T{2} * q)};

    auto const a0{T{1} + a};
    return biquad{coefficients{
      .b0 = ((T{1} - cos_w) / T{2}) / a0,
      .b1 = (T{1} - cos_w) / a0,
      .b2 = ((T{1} - cos_w) / T{2}) / a0,
      .a1 = (T{-2} * cos_w) / a0,
      .a2 = (T{1} - a) / a0,
    }};
  }

  /**
   * @brief Designs a second-order high-pass biquad.
   *
   * Audio EQ Cookbook high-pass complementary to \c make_lowpass: it
   * attenuates below \p cutoff_hz and passes above it.
   *
   * @param cutoff_hz Cutoff frequency in Hz (the -3 dB point).
   * @param sample_rate_hz Sample rate in Hz.
   * @param q Quality factor.
   *
   * @return A biquad configured as a high-pass with zeroed state.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @pre \p q is positive.
   * @post The returned filter has all delay elements at zero.
   */
  [[nodiscard]] static auto make_highpass(
    T const cutoff_hz, T const sample_rate_hz, T const q = T{0.7071}
  ) noexcept -> biquad {
    auto const w0{T{2} * std::numbers::pi_v<T> * cutoff_hz / sample_rate_hz};
    auto const sin_w{std::sin(w0)};
    auto const cos_w{std::cos(w0)};
    auto const a{sin_w / (T{2} * q)};

    auto const a0{T{1} + a};
    return biquad{coefficients{
      .b0 = ((T{1} + cos_w) / T{2}) / a0,
      .b1 = (-(T{1} + cos_w)) / a0,
      .b2 = ((T{1} + cos_w) / T{2}) / a0,
      .a1 = (T{-2} * cos_w) / a0,
      .a2 = (T{1} - a) / a0,
    }};
  }

  /**
   * @brief Designs a second-order band-pass biquad.
   *
   * Audio EQ Cookbook constant-skirt-gain band-pass centred on
   * \p center_hz. The bandwidth narrows as \p q rises.
   *
   * @param center_hz Centre frequency in Hz.
   * @param sample_rate_hz Sample rate in Hz.
   * @param q Quality factor; larger values give a
   * narrower passband.
   *
   * @return A biquad configured as a band-pass with zeroed state.
   *
   * @pre \p sample_rate_hz is positive and \p center_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @pre \p q is positive.
   * @post The returned filter has all delay elements at zero.
   */
  [[nodiscard]] static auto
  make_bandpass(T const center_hz, T const sample_rate_hz, T const q = T{1}) noexcept -> biquad {
    auto const w0{T{2} * std::numbers::pi_v<T> * center_hz / sample_rate_hz};
    auto const sin_w{std::sin(w0)};
    auto const cos_w{std::cos(w0)};
    auto const a{sin_w / (T{2} * q)};

    auto const a0{T{1} + a};
    return biquad{coefficients{
      .b0 = (sin_w / T{2}) / a0,
      .b1 = T{0},
      .b2 = -(sin_w / T{2}) / a0,
      .a1 = (T{-2} * cos_w) / a0,
      .a2 = (T{1} - a) / a0,
    }};
  }

  /**
   * @brief Designs a second-order notch (band-reject) biquad.
   *
   * Audio EQ Cookbook notch that strongly attenuates a narrow band
   * around \p center_hz while passing the rest of the spectrum. A
   * common use is rejecting mains hum at 50 or 60 Hz.
   *
   * @param center_hz Centre frequency of the rejected band, Hz.
   * @param sample_rate_hz Sample rate in Hz.
   * @param q Quality factor; larger values give a
   * narrower, deeper notch.
   *
   * @return A biquad configured as a notch with zeroed state.
   *
   * @pre \p sample_rate_hz is positive and \p center_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @pre \p q is positive.
   * @post The returned filter has all delay elements at zero.
   */
  [[nodiscard]] static auto
  make_notch(T const center_hz, T const sample_rate_hz, T const q = T{1}) noexcept -> biquad {
    auto const w0{T{2} * std::numbers::pi_v<T> * center_hz / sample_rate_hz};
    auto const sin_w{std::sin(w0)};
    auto const cos_w{std::cos(w0)};
    auto const a{sin_w / (T{2} * q)};

    auto const a0{T{1} + a};
    return biquad{coefficients{
      .b0 = T{1} / a0,
      .b1 = (T{-2} * cos_w) / a0,
      .b2 = T{1} / a0,
      .a1 = (T{-2} * cos_w) / a0,
      .a2 = (T{1} - a) / a0,
    }};
  }
};

}  // namespace nexenne::filter
