#pragma once

/**
 * @file
 * @brief Debug printing and formatting for nexenne filters.
 *
 * Three layers, like the rest of the library's format headers: \c to_string(x)
 * builds a readable string, \c operator<<(std::ostream&, x) streams it, and a
 * \c std::formatter specialization makes \c std::format("{}", x) work. The output
 * is for diagnostics (logging while tuning), not serialisation, and is not stable
 * across versions.
 *
 * Each filter prints its current value plus the knobs and status flags that
 * matter while tuning, for example \c "range_guard(value=1.5, lo=0, hi=10,
 * primed=true)". The standard \c format header is heavy, so this header is opt-in: include it only
 * where you actually print a filter.
 */

#include <concepts>
#include <cstddef>
#include <format>
#include <ostream>
#include <string>

#include <nexenne/filter/filter.hpp>

namespace nexenne::filter {

/**
 * @brief Debug string for an \c ema filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(ema<T> const& f) -> std::string {
  return std::format("ema(value={}, alpha={})", f.value(), f.alpha());
}

/**
 * @brief Streams an \c ema filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, ema<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c sma filter.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Window size.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T, std::size_t N>
[[nodiscard]] auto to_string(sma<T, N> const& f) -> std::string {
  return std::format("sma(value={}, count={}, window={})", f.value(), f.count(), N);
}

/**
 * @brief Streams a \c sma filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Window size.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T, std::size_t N>
auto operator<<(std::ostream& os, sma<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c lowpass filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(lowpass<T> const& f) -> std::string {
  return std::format("lowpass(value={}, alpha={})", f.value(), f.alpha());
}

/**
 * @brief Streams a \c lowpass filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, lowpass<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c highpass filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(highpass<T> const& f) -> std::string {
  return std::format("highpass(value={}, alpha={})", f.value(), f.alpha());
}

/**
 * @brief Streams a \c highpass filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, highpass<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c biquad filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(biquad<T> const& f) -> std::string {
  return std::format("biquad(value={})", f.value());
}

/**
 * @brief Streams a \c biquad filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, biquad<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c butterworth filter.
 *
 * @tparam T Floating-point sample type.
 * @tparam SectionsN Number of biquad sections.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T, std::size_t SectionsN>
[[nodiscard]] auto to_string(butterworth<T, SectionsN> const& f) -> std::string {
  return std::format(
    "butterworth(value={}, sections={}, order={})",
    f.value(),
    butterworth<T, SectionsN>::sections,
    butterworth<T, SectionsN>::order
  );
}

/**
 * @brief Streams a \c butterworth filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @tparam SectionsN Number of biquad sections.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T, std::size_t SectionsN>
auto operator<<(std::ostream& os, butterworth<T, SectionsN> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c fir filter.
 *
 * @tparam T Sample type.
 * @tparam N Tap count.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::size_t N>
[[nodiscard]] auto to_string(fir<T, N> const& f) -> std::string {
  return std::format("fir(value={}, taps={})", f.value(), N);
}

/**
 * @brief Streams a \c fir filter via its debug string.
 *
 * @tparam T Sample type.
 * @tparam N Tap count.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <typename T, std::size_t N>
auto operator<<(std::ostream& os, fir<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c median filter.
 *
 * @tparam T Ordered sample type.
 * @tparam N Window size.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::totally_ordered T, std::size_t N>
[[nodiscard]] auto to_string(median<T, N> const& f) -> std::string {
  return std::format("median(value={}, window={}, filled={})", f.value(), N, f.filled());
}

/**
 * @brief Streams a \c median filter via its debug string.
 *
 * @tparam T Ordered sample type.
 * @tparam N Window size.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::totally_ordered T, std::size_t N>
auto operator<<(std::ostream& os, median<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c kalman filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(kalman<T> const& f) -> std::string {
  return std::format(
    "kalman(value={}, covariance={}, gain={})", f.value(), f.covariance(), f.gain()
  );
}

/**
 * @brief Streams a \c kalman filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, kalman<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c complementary filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(complementary<T> const& f) -> std::string {
  return std::format("complementary(value={}, alpha={})", f.value(), f.alpha());
}

/**
 * @brief Streams a \c complementary filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, complementary<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c lms filter.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Tap count.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T, std::size_t N>
[[nodiscard]] auto to_string(lms<T, N> const& f) -> std::string {
  return std::format("lms(value={}, taps={}, error={})", f.value(), N, f.error());
}

/**
 * @brief Streams a \c lms filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Tap count.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T, std::size_t N>
auto operator<<(std::ostream& os, lms<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c slew filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(slew<T> const& f) -> std::string {
  return std::format("slew(value={}, max_rate={})", f.value(), f.max_rate());
}

/**
 * @brief Streams a \c slew filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, slew<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c debounce filter.
 *
 * @tparam T Comparable sample type.
 * @tparam Threshold Consecutive-agreement threshold.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::equality_comparable T, std::size_t Threshold>
[[nodiscard]] auto to_string(debounce<T, Threshold> const& f) -> std::string {
  return std::format("debounce(value={}, threshold={})", f.value(), Threshold);
}

/**
 * @brief Streams a \c debounce filter via its debug string.
 *
 * @tparam T Comparable sample type.
 * @tparam Threshold Consecutive-agreement threshold.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::equality_comparable T, std::size_t Threshold>
auto operator<<(std::ostream& os, debounce<T, Threshold> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c timed_debounce filter.
 *
 * @tparam Duration Chrono duration type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <typename Duration>
[[nodiscard]] auto to_string(timed_debounce<Duration> const& f) -> std::string {
  return std::format(
    "timed_debounce(stable_value={}, has_stable={}, period={})",
    f.stable_value(),
    f.has_stable(),
    f.period()
  );
}

/**
 * @brief Streams a \c timed_debounce filter via its debug string.
 *
 * @tparam Duration Chrono duration type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <typename Duration>
auto operator<<(std::ostream& os, timed_debounce<Duration> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c hysteresis filter.
 *
 * @tparam T Ordered sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::totally_ordered T>
[[nodiscard]] auto to_string(hysteresis<T> const& f) -> std::string {
  return std::format(
    "hysteresis(value={}, low={}, high={})", f.value(), f.low_threshold(), f.high_threshold()
  );
}

/**
 * @brief Streams a \c hysteresis filter via its debug string.
 *
 * @tparam T Ordered sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::totally_ordered T>
auto operator<<(std::ostream& os, hysteresis<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c glitch filter.
 *
 * @tparam T Comparable sample type.
 * @tparam N Hold count.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::equality_comparable T, std::size_t N>
[[nodiscard]] auto to_string(glitch<T, N> const& f) -> std::string {
  return std::format("glitch(value={}, hold_count={}, pending={})", f.value(), N, f.pending());
}

/**
 * @brief Streams a \c glitch filter via its debug string.
 *
 * @tparam T Comparable sample type.
 * @tparam N Hold count.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::equality_comparable T, std::size_t N>
auto operator<<(std::ostream& os, glitch<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c range_guard filter.
 *
 * @tparam T Ordered sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::totally_ordered T>
[[nodiscard]] auto to_string(range_guard<T> const& f) -> std::string {
  return std::format(
    "range_guard(value={}, lo={}, hi={}, primed={})", f.value(), f.lo(), f.hi(), f.primed()
  );
}

/**
 * @brief Streams a \c range_guard filter via its debug string.
 *
 * @tparam T Ordered sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::totally_ordered T>
auto operator<<(std::ostream& os, range_guard<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c rate_guard filter.
 *
 * @tparam T Floating-point sample type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(rate_guard<T> const& f) -> std::string {
  return std::format(
    "rate_guard(value={}, max_delta={}, rejected_streak={})",
    f.value(),
    f.max_delta(),
    f.rejected_streak()
  );
}

/**
 * @brief Streams a \c rate_guard filter via its debug string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, rate_guard<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c validator filter.
 *
 * @tparam T Validated value type.
 * @tparam Pred Predicate type.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::predicate<T const&> Pred>
[[nodiscard]] auto to_string(validator<T, Pred> const& f) -> std::string {
  return std::format("validator(value={})", f.value());
}

/**
 * @brief Streams a \c validator filter via its debug string.
 *
 * @tparam T Validated value type.
 * @tparam Pred Predicate type.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <typename T, std::predicate<T const&> Pred>
auto operator<<(std::ostream& os, validator<T, Pred> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c majority filter.
 *
 * @tparam T Comparable sample type.
 * @tparam N Batch size.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::equality_comparable T, std::size_t N>
[[nodiscard]] auto to_string(majority<T, N> const& f) -> std::string {
  return std::format("majority(value={}, batch_size={}, filled={})", f.value(), N, f.filled());
}

/**
 * @brief Streams a \c majority filter via its debug string.
 *
 * @tparam T Comparable sample type.
 * @tparam N Batch size.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::equality_comparable T, std::size_t N>
auto operator<<(std::ostream& os, majority<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string for a \c stale_detector filter.
 *
 * @tparam T Comparable sample type.
 * @tparam N Repeat count before it reports stale.
 * @param f Filter to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::equality_comparable T, std::size_t N>
[[nodiscard]] auto to_string(stale_detector<T, N> const& f) -> std::string {
  return std::format(
    "stale_detector(value={}, streak={}, stale={})", f.value(), f.streak(), f.is_stale()
  );
}

/**
 * @brief Streams a \c stale_detector filter via its debug string.
 *
 * @tparam T Comparable sample type.
 * @tparam N Repeat count before it reports stale.
 * @param os Output stream to write to.
 * @param f Filter to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p f has been written to \p os.
 */
template <std::equality_comparable T, std::size_t N>
auto operator<<(std::ostream& os, stale_detector<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

}  // namespace nexenne::filter

/**
 * @brief \c std::format support for \c ema, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::ema<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::ema<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c sma, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Window size.
 */
template <std::floating_point T, std::size_t N>
struct std::formatter<nexenne::filter::sma<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::sma<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c lowpass, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::lowpass<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::lowpass<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c highpass, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::highpass<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::highpass<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c biquad, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::biquad<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::biquad<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c butterworth, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @tparam SectionsN Number of biquad sections.
 */
template <std::floating_point T, std::size_t SectionsN>
struct std::formatter<nexenne::filter::butterworth<T, SectionsN>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::butterworth<T, SectionsN> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c fir, forwarding to \c to_string.
 *
 * @tparam T Sample type.
 * @tparam N Tap count.
 */
template <typename T, std::size_t N>
struct std::formatter<nexenne::filter::fir<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::fir<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c median, forwarding to \c to_string.
 *
 * @tparam T Ordered sample type.
 * @tparam N Window size.
 */
template <std::totally_ordered T, std::size_t N>
struct std::formatter<nexenne::filter::median<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::median<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c kalman, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::kalman<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::kalman<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c complementary, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::complementary<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::complementary<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c lms, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Tap count.
 */
template <std::floating_point T, std::size_t N>
struct std::formatter<nexenne::filter::lms<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::lms<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c slew, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::slew<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::slew<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c debounce, forwarding to \c to_string.
 *
 * @tparam T Comparable sample type.
 * @tparam Threshold Consecutive-agreement threshold.
 */
template <std::equality_comparable T, std::size_t Threshold>
struct std::formatter<nexenne::filter::debounce<T, Threshold>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::debounce<T, Threshold> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c timed_debounce, forwarding to \c to_string.
 *
 * @tparam Duration Chrono duration type.
 */
template <typename Duration>
struct std::formatter<nexenne::filter::timed_debounce<Duration>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::timed_debounce<Duration> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c hysteresis, forwarding to \c to_string.
 *
 * @tparam T Ordered sample type.
 */
template <std::totally_ordered T>
struct std::formatter<nexenne::filter::hysteresis<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::hysteresis<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c glitch, forwarding to \c to_string.
 *
 * @tparam T Comparable sample type.
 * @tparam N Hold count.
 */
template <std::equality_comparable T, std::size_t N>
struct std::formatter<nexenne::filter::glitch<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::glitch<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c range_guard, forwarding to \c to_string.
 *
 * @tparam T Ordered sample type.
 */
template <std::totally_ordered T>
struct std::formatter<nexenne::filter::range_guard<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::range_guard<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c rate_guard, forwarding to \c to_string.
 *
 * @tparam T Floating-point sample type.
 */
template <std::floating_point T>
struct std::formatter<nexenne::filter::rate_guard<T>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::rate_guard<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c validator, forwarding to \c to_string.
 *
 * @tparam T Validated value type.
 * @tparam Pred Predicate type.
 */
template <typename T, std::predicate<T const&> Pred>
struct std::formatter<nexenne::filter::validator<T, Pred>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::validator<T, Pred> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c majority, forwarding to \c to_string.
 *
 * @tparam T Comparable sample type.
 * @tparam N Batch size.
 */
template <std::equality_comparable T, std::size_t N>
struct std::formatter<nexenne::filter::majority<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::majority<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c stale_detector, forwarding to \c to_string.
 *
 * @tparam T Comparable sample type.
 * @tparam N Repeat count before it reports stale.
 */
template <std::equality_comparable T, std::size_t N>
struct std::formatter<nexenne::filter::stale_detector<T, N>> {
  /**
   * @brief Accepts the format spec (only the empty spec is used).
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the filter's debug string to the output context.
   *
   * @param f Filter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted filter has been written to \p ctx.
   */
  static auto format(nexenne::filter::stale_detector<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};
