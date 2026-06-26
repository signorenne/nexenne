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

template <std::floating_point T>
[[nodiscard]] auto to_string(ema<T> const& f) -> std::string {
  return std::format("ema(value={}, alpha={})", f.value(), f.alpha());
}

template <std::floating_point T>
auto operator<<(std::ostream& os, ema<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T, std::size_t N>
[[nodiscard]] auto to_string(sma<T, N> const& f) -> std::string {
  return std::format("sma(value={}, count={}, window={})", f.value(), f.count(), N);
}

template <std::floating_point T, std::size_t N>
auto operator<<(std::ostream& os, sma<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(lowpass<T> const& f) -> std::string {
  return std::format("lowpass(value={}, alpha={})", f.value(), f.alpha());
}

template <std::floating_point T>
auto operator<<(std::ostream& os, lowpass<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(highpass<T> const& f) -> std::string {
  return std::format("highpass(value={}, alpha={})", f.value(), f.alpha());
}

template <std::floating_point T>
auto operator<<(std::ostream& os, highpass<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(biquad<T> const& f) -> std::string {
  return std::format("biquad(value={})", f.value());
}

template <std::floating_point T>
auto operator<<(std::ostream& os, biquad<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T, std::size_t SectionsN>
[[nodiscard]] auto to_string(butterworth<T, SectionsN> const& f) -> std::string {
  return std::format(
    "butterworth(value={}, sections={}, order={})",
    f.value(),
    butterworth<T, SectionsN>::sections,
    butterworth<T, SectionsN>::order
  );
}

template <std::floating_point T, std::size_t SectionsN>
auto operator<<(std::ostream& os, butterworth<T, SectionsN> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <typename T, std::size_t N>
[[nodiscard]] auto to_string(fir<T, N> const& f) -> std::string {
  return std::format("fir(value={}, taps={})", f.value(), N);
}

template <typename T, std::size_t N>
auto operator<<(std::ostream& os, fir<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::totally_ordered T, std::size_t N>
[[nodiscard]] auto to_string(median<T, N> const& f) -> std::string {
  return std::format("median(value={}, window={}, filled={})", f.value(), N, f.filled());
}

template <std::totally_ordered T, std::size_t N>
auto operator<<(std::ostream& os, median<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(kalman<T> const& f) -> std::string {
  return std::format(
    "kalman(value={}, covariance={}, gain={})", f.value(), f.covariance(), f.gain()
  );
}

template <std::floating_point T>
auto operator<<(std::ostream& os, kalman<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(complementary<T> const& f) -> std::string {
  return std::format("complementary(value={}, alpha={})", f.value(), f.alpha());
}

template <std::floating_point T>
auto operator<<(std::ostream& os, complementary<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T, std::size_t N>
[[nodiscard]] auto to_string(lms<T, N> const& f) -> std::string {
  return std::format("lms(value={}, taps={}, error={})", f.value(), N, f.error());
}

template <std::floating_point T, std::size_t N>
auto operator<<(std::ostream& os, lms<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(slew<T> const& f) -> std::string {
  return std::format("slew(value={}, max_rate={})", f.value(), f.max_rate());
}

template <std::floating_point T>
auto operator<<(std::ostream& os, slew<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::equality_comparable T, std::size_t Threshold>
[[nodiscard]] auto to_string(debounce<T, Threshold> const& f) -> std::string {
  return std::format("debounce(value={}, threshold={})", f.value(), Threshold);
}

template <std::equality_comparable T, std::size_t Threshold>
auto operator<<(std::ostream& os, debounce<T, Threshold> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <typename Duration>
[[nodiscard]] auto to_string(timed_debounce<Duration> const& f) -> std::string {
  return std::format(
    "timed_debounce(stable_value={}, has_stable={}, period={})",
    f.stable_value(),
    f.has_stable(),
    f.period()
  );
}

template <typename Duration>
auto operator<<(std::ostream& os, timed_debounce<Duration> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::totally_ordered T>
[[nodiscard]] auto to_string(hysteresis<T> const& f) -> std::string {
  return std::format(
    "hysteresis(value={}, low={}, high={})", f.value(), f.low_threshold(), f.high_threshold()
  );
}

template <std::totally_ordered T>
auto operator<<(std::ostream& os, hysteresis<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::equality_comparable T, std::size_t N>
[[nodiscard]] auto to_string(glitch<T, N> const& f) -> std::string {
  return std::format("glitch(value={}, hold_count={}, pending={})", f.value(), N, f.pending());
}

template <std::equality_comparable T, std::size_t N>
auto operator<<(std::ostream& os, glitch<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::totally_ordered T>
[[nodiscard]] auto to_string(range_guard<T> const& f) -> std::string {
  return std::format(
    "range_guard(value={}, lo={}, hi={}, primed={})", f.value(), f.lo(), f.hi(), f.primed()
  );
}

template <std::totally_ordered T>
auto operator<<(std::ostream& os, range_guard<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::floating_point T>
[[nodiscard]] auto to_string(rate_guard<T> const& f) -> std::string {
  return std::format(
    "rate_guard(value={}, max_delta={}, rejected_streak={})",
    f.value(),
    f.max_delta(),
    f.rejected_streak()
  );
}

template <std::floating_point T>
auto operator<<(std::ostream& os, rate_guard<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <typename T, std::predicate<T const&> Pred>
[[nodiscard]] auto to_string(validator<T, Pred> const& f) -> std::string {
  return std::format("validator(value={})", f.value());
}

template <typename T, std::predicate<T const&> Pred>
auto operator<<(std::ostream& os, validator<T, Pred> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::equality_comparable T, std::size_t N>
[[nodiscard]] auto to_string(majority<T, N> const& f) -> std::string {
  return std::format("majority(value={}, batch_size={}, filled={})", f.value(), N, f.filled());
}

template <std::equality_comparable T, std::size_t N>
auto operator<<(std::ostream& os, majority<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

template <std::equality_comparable T, std::size_t N>
[[nodiscard]] auto to_string(stale_detector<T, N> const& f) -> std::string {
  return std::format(
    "stale_detector(value={}, streak={}, stale={})", f.value(), f.streak(), f.is_stale()
  );
}

template <std::equality_comparable T, std::size_t N>
auto operator<<(std::ostream& os, stale_detector<T, N> const& f) -> std::ostream& {
  return os << to_string(f);
}

}  // namespace nexenne::filter

/// Formats an \c ema via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::ema<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::ema<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats an \c sma via \c nexenne::filter::to_string.
template <std::floating_point T, std::size_t N>
struct std::formatter<nexenne::filter::sma<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::sma<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c lowpass via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::lowpass<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::lowpass<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c highpass via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::highpass<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::highpass<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c biquad via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::biquad<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::biquad<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c butterworth via \c nexenne::filter::to_string.
template <std::floating_point T, std::size_t SectionsN>
struct std::formatter<nexenne::filter::butterworth<T, SectionsN>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::butterworth<T, SectionsN> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c fir via \c nexenne::filter::to_string.
template <typename T, std::size_t N>
struct std::formatter<nexenne::filter::fir<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::fir<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c median via \c nexenne::filter::to_string.
template <std::totally_ordered T, std::size_t N>
struct std::formatter<nexenne::filter::median<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::median<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c kalman via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::kalman<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::kalman<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c complementary via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::complementary<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::complementary<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats an \c lms via \c nexenne::filter::to_string.
template <std::floating_point T, std::size_t N>
struct std::formatter<nexenne::filter::lms<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::lms<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c slew via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::slew<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::slew<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c debounce via \c nexenne::filter::to_string.
template <std::equality_comparable T, std::size_t Threshold>
struct std::formatter<nexenne::filter::debounce<T, Threshold>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::debounce<T, Threshold> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c timed_debounce via \c nexenne::filter::to_string.
template <typename Duration>
struct std::formatter<nexenne::filter::timed_debounce<Duration>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::timed_debounce<Duration> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c hysteresis via \c nexenne::filter::to_string.
template <std::totally_ordered T>
struct std::formatter<nexenne::filter::hysteresis<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::hysteresis<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c glitch via \c nexenne::filter::to_string.
template <std::equality_comparable T, std::size_t N>
struct std::formatter<nexenne::filter::glitch<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::glitch<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c range_guard via \c nexenne::filter::to_string.
template <std::totally_ordered T>
struct std::formatter<nexenne::filter::range_guard<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::range_guard<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c rate_guard via \c nexenne::filter::to_string.
template <std::floating_point T>
struct std::formatter<nexenne::filter::rate_guard<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::rate_guard<T> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c validator via \c nexenne::filter::to_string.
template <typename T, std::predicate<T const&> Pred>
struct std::formatter<nexenne::filter::validator<T, Pred>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::validator<T, Pred> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c majority via \c nexenne::filter::to_string.
template <std::equality_comparable T, std::size_t N>
struct std::formatter<nexenne::filter::majority<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::majority<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};

/// Formats a \c stale_detector via \c nexenne::filter::to_string.
template <std::equality_comparable T, std::size_t N>
struct std::formatter<nexenne::filter::stale_detector<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::filter::stale_detector<T, N> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::filter::to_string(f));
  }
};
