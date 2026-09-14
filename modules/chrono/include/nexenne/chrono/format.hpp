#pragma once

/**
 * @file
 * @brief The three formatting layers for the nexenne::chrono types.
 *
 * Kept out of the type headers because the \c format standard header is heavy
 * and \c static_stopwatch, \c deadline and \c interval target small
 * microcontrollers. Include this header (or the \c chrono.hpp umbrella)
 * wherever a \c duration_parts, \c stopwatch, \c static_stopwatch,
 * \c deadline, \c interval or \c countdown is printed. Each gets a
 * \c std::formatter, which renders through \c nexenne::chrono::format (a
 * leading \c '!' in the spec disables suppress-zero), and a \c to_string and
 * an \c operator<< that give the formatter's default text. The enums
 * \c stopwatch_state, \c countdown_state and \c alarm_mode print their names
 * through all three, their \c to_string a \c constexpr \c std::string_view.
 *
 * The stateful engines print a one-line summary of their observable state
 * through the same three layers: \c alarm, \c basic_manual_clock,
 * \c frame_timer, \c profiler and its \c profiler_stats buckets,
 * \c rate_limiter and \c scope_timer, plus the \c hertz frequency type. Their
 * formatters inherit the string formatter, so a width or alignment spec applies
 * to the whole summary.
 */

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/chrono/alarm.hpp>
#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/frame_timer.hpp>
#include <nexenne/chrono/frequency.hpp>
#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/profiler.hpp>
#include <nexenne/chrono/rate_limiter.hpp>
#include <nexenne/chrono/scope_timer.hpp>
#include <nexenne/chrono/static_stopwatch.hpp>
#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/chrono/timer_state.hpp>

namespace nexenne::chrono {

/**
 * @brief Returns the name of a stopwatch state.
 *
 * @param s State to name.
 *
 * @return The enumerator's name, or \c "?" for a value outside the enum.
 *
 * @pre None.
 * @post The returned view refers to a static string.
 */
[[nodiscard]] constexpr auto to_string(stopwatch_state const s) noexcept -> std::string_view {
  switch (s) {
    case stopwatch_state::idle:
      return "idle";
    case stopwatch_state::running:
      return "running";
    case stopwatch_state::paused:
      return "paused";
  }
  return "?";
}

/**
 * @brief Returns the name of a countdown state.
 *
 * @param s State to name.
 *
 * @return The enumerator's name, or \c "?" for a value outside the enum.
 *
 * @pre None.
 * @post The returned view refers to a static string.
 */
[[nodiscard]] constexpr auto to_string(countdown_state const s) noexcept -> std::string_view {
  switch (s) {
    case countdown_state::idle:
      return "idle";
    case countdown_state::running:
      return "running";
    case countdown_state::paused:
      return "paused";
    case countdown_state::expired:
      return "expired";
  }
  return "?";
}

/**
 * @brief Returns the name of an alarm mode.
 *
 * @param mode Mode to name.
 *
 * @return The enumerator's name, or \c "?" for a value outside the enum.
 *
 * @pre None.
 * @post The returned view refers to a static string.
 */
[[nodiscard]] constexpr auto to_string(alarm_mode const mode) noexcept -> std::string_view {
  switch (mode) {
    case alarm_mode::one_shot:
      return "one_shot";
    case alarm_mode::periodic:
      return "periodic";
  }
  return "?";
}

/**
 * @brief Debug string for an \c alarm: its armed flag, mode and next fire time.
 *
 * Prints \c "alarm(armed, mode=periodic, next_fire_time=02s)" while armed and
 * \c "alarm(disarmed, mode=one_shot)" otherwise, since \c next_fire_time() is
 * meaningful only while armed. The fire time is the absolute time since the
 * \p Clock epoch, rendered through \c nexenne::chrono::format. The stored
 * callback is not printed.
 *
 * @tparam Clock Clock of the alarm.
 * @tparam CallbackBytes Inline callback storage of the alarm.
 * @param a Alarm to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <clock_like Clock, std::size_t CallbackBytes>
[[nodiscard]] auto to_string(alarm<Clock, CallbackBytes> const& a) -> std::string {
  if (!a.is_armed()) {
    return std::format("alarm(disarmed, mode={})", to_string(a.mode()));
  }
  return std::format(
    "alarm(armed, mode={}, next_fire_time={})",
    to_string(a.mode()),
    nexenne::chrono::format(a.next_fire_time().time_since_epoch())
  );
}

/**
 * @brief Debug string for a \c basic_manual_clock: its current virtual time.
 *
 * Prints \c "manual_clock(now=01s:250ms)", the time since the clock epoch
 * rendered through \c nexenne::chrono::format. The time is static state shared
 * by every instance with the same \p Tag.
 *
 * @tparam Tag Tag type that selects the clock.
 * @param clock Clock to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Tag>
[[nodiscard]] auto to_string(basic_manual_clock<Tag> const& clock) -> std::string {
  return std::format(
    "manual_clock(now={})", nexenne::chrono::format(clock.now().time_since_epoch())
  );
}

/**
 * @brief Debug string for a \c frame_timer: its frame count and recent FPS.
 *
 * Prints \c "frame_timer(frames=3, fps=10)".
 *
 * @tparam WindowSize Moving-average window of the timer.
 * @tparam Clock Steady clock of the timer.
 * @param t Frame timer to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <std::size_t WindowSize, steady_clock_like Clock>
  requires(WindowSize > 0)
[[nodiscard]] auto to_string(frame_timer<WindowSize, Clock> const& t) -> std::string {
  return std::format("frame_timer(frames={}, fps={})", t.frame_count(), t.fps());
}

/**
 * @brief Debug string for a \c hertz frequency.
 *
 * Prints \c "hertz(1000)".
 *
 * @tparam Hz The frequency in hertz.
 * @param f Frequency to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <std::uint64_t Hz>
[[nodiscard]] auto to_string(hertz<Hz> const f) -> std::string {
  return std::format("hertz({})", f.value);
}

/**
 * @brief Debug string for a \c profiler_stats bucket.
 *
 * Prints \c "profiler_stats(count=2, total=3.00 ms, min=1.00 ms, max=2.00 ms,
 * mean=1.50 ms)", each duration through \c nexenne::chrono::format_scaled so a
 * sub-millisecond sample keeps its resolution. An empty bucket prints
 * \c "profiler_stats(count=0)", leaving out its \c min and \c max sentinels.
 *
 * @tparam Duration Duration type of the samples.
 * @param s Bucket to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <chrono_duration Duration>
[[nodiscard]] auto to_string(profiler_stats<Duration> const& s) -> std::string {
  if (s.count == 0) {
    return std::string{"profiler_stats(count=0)"};
  }
  return std::format(
    "profiler_stats(count={}, total={}, min={}, max={}, mean={})",
    s.count,
    nexenne::chrono::format_scaled(s.total),
    nexenne::chrono::format_scaled(s.min),
    nexenne::chrono::format_scaled(s.max),
    nexenne::chrono::format_scaled(s.mean())
  );
}

/**
 * @brief Debug string for a \c profiler: every bucket by name.
 *
 * Map-like, in name order: each quoted bucket name maps to its
 * \c profiler_stats text, and an empty profiler prints \c "profiler{}".
 *
 * @tparam Clock Steady clock of the profiler.
 * @param p Profiler to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <steady_clock_like Clock>
[[nodiscard]] auto to_string(profiler<Clock> const& p) -> std::string {
  auto body{std::string{}};
  for (auto const& [name, s] : p.buckets()) {
    if (!body.empty()) {
      body += ", ";
    }
    body += std::format("{:?}: {}", name, to_string(s));
  }
  return std::format("profiler{{{}}}", body);
}

/**
 * @brief Debug string for a \c rate_limiter: its capacity and refill rate.
 *
 * Prints \c "rate_limiter(capacity=10, refill_rate=2)". The token count is
 * left out: reading it refills the bucket, so it has no \c const accessor.
 *
 * @tparam Clock Steady clock of the limiter.
 * @param r Limiter to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <steady_clock_like Clock>
[[nodiscard]] auto to_string(rate_limiter<Clock> const& r) -> std::string {
  return std::format("rate_limiter(capacity={}, refill_rate={})", r.capacity(), r.refill_rate());
}

/**
 * @brief Debug string for a \c scope_timer: the time elapsed so far.
 *
 * Prints \c "scope_timer(elapsed=01s:250ms)", rendered through
 * \c nexenne::chrono::format. Reads \c Clock::now(); the callback is not
 * printed.
 *
 * @tparam Callback Callback type of the timer.
 * @tparam Clock Steady clock of the timer.
 * @param t Scope timer to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Callback, steady_clock_like Clock>
  requires std::invocable<Callback&, typename Clock::duration>
[[nodiscard]] auto to_string(scope_timer<Callback, Clock> const& t) -> std::string {
  return std::format("scope_timer(elapsed={})", nexenne::chrono::format(t.elapsed()));
}

}  // namespace nexenne::chrono

/**
 * @brief \c std::format support for \c duration_parts.
 *
 * Produces the same human-readable string as \c nexenne::chrono::format. The
 * spec accepts a leading \c '!' that disables suppress-zero, showing every
 * component including zeros.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::chrono::duration_parts, char> {
  bool suppress_zero{true};  ///< Drop leading zero components; the \c '!' spec flag clears it.

  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' flag, if present, has been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted breakdown to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param p The component breakdown to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::duration_parts const& p, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const s{nexenne::chrono::detail::format_parts(
      p, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero, "+", "-"
    )};
    return std::ranges::copy(s, ctx.out()).out;
  }
};

/**
 * @brief \c std::format support for \c stopwatch.
 *
 * Formats the current elapsed time using the same token layout as
 * \c nexenne::chrono::format. A leading \c '!' disables suppress-zero.
 *
 * @tparam Clock Steady clock of the formatted stopwatch.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::stopwatch<Clock>, char> {
private:
  bool suppress_zero{true};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' flag, if present, has been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted stopwatch to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param sw The stopwatch to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::stopwatch<Clock> const& sw, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const ms{sw.template elapsed<std::chrono::milliseconds>()};
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};

/**
 * @brief \c std::format support for \c static_stopwatch.
 *
 * Formats the current elapsed time using the same token layout as
 * \c nexenne::chrono::format. A leading \c '!' disables suppress-zero.
 *
 * @tparam N Lap-buffer capacity of the formatted stopwatch.
 * @tparam Clock Steady clock of the formatted stopwatch.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N, nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::static_stopwatch<N, Clock>, char> {
private:
  bool suppress_zero{true};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' flag, if present, has been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted stopwatch to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param sw The stopwatch to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::static_stopwatch<N, Clock> const& sw, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const ms{sw.template elapsed<std::chrono::milliseconds>()};
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};

/**
 * @brief \c std::format support for \c deadline.
 *
 * Formats the time remaining until the deadline (clamped at zero) using the
 * same token layout as \c nexenne::chrono::format. A leading \c '!' disables
 * suppress-zero. Reads \c Clock::now() at format time.
 *
 * @tparam Clock Steady clock of the formatted deadline.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::deadline<Clock>, char> {
private:
  bool suppress_zero{true};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' flag, if present, has been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted remaining time to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param dl The deadline to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::deadline<Clock> const& dl, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const ms{dl.template remaining<std::chrono::milliseconds>()};
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};

/**
 * @brief \c std::format support for \c interval.
 *
 * Formats the time remaining until the next tick. A leading \c '!' disables
 * suppress-zero.
 *
 * @tparam Clock Steady clock of the formatted interval.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::interval<Clock>, char> {
private:
  bool suppress_zero{true};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' flag, if present, has been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted interval to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param iv The interval to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::interval<Clock> const& iv, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const ms{iv.template remaining<std::chrono::milliseconds>()};
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};

/**
 * @brief \c std::format support for \c countdown.
 *
 * Formats the remaining time by default, or the elapsed time with the \c 'e'
 * flag. A leading \c '!' disables the suppress-zero behaviour, showing every
 * component including zeros.
 *
 * @tparam Clock Steady clock of the formatted countdown.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::countdown<Clock>, char> {
private:
  bool suppress_zero{true};
  bool show_elapsed{false};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' and \c 'e' flags, if present, have been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    if (it != end && *it == 'e') {
      show_elapsed = true;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted countdown to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param c The countdown to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(
    nexenne::chrono::countdown<Clock> const& c, std::basic_format_context<Out, char>& ctx
  ) const {
    auto const ms{
      show_elapsed ? c.template elapsed<std::chrono::milliseconds>()
                   : c.template remaining<std::chrono::milliseconds>()
    };
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};

/**
 * @brief \c std::format support for a \c stopwatch_state: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::chrono::stopwatch_state> : std::formatter<std::string_view> {
  /**
   * @brief Formats the enumerator's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param state Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::stopwatch_state const state, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(state), ctx);
  }
};

/**
 * @brief \c std::format support for a \c countdown_state: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::chrono::countdown_state> : std::formatter<std::string_view> {
  /**
   * @brief Formats the enumerator's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param state Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::countdown_state const state, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(state), ctx);
  }
};

/**
 * @brief \c std::format support for an \c alarm_mode: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::chrono::alarm_mode> : std::formatter<std::string_view> {
  /**
   * @brief Formats the enumerator's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param mode Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::alarm_mode const mode, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(mode), ctx);
  }
};

/**
 * @brief \c std::format support for an \c alarm: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Clock Clock of the alarm.
 * @tparam CallbackBytes Inline callback storage of the alarm.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::clock_like Clock, std::size_t CallbackBytes>
struct std::formatter<nexenne::chrono::alarm<Clock, CallbackBytes>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param a Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p a has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::alarm<Clock, CallbackBytes> const& a, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(a), ctx);
  }
};

/**
 * @brief \c std::format support for a \c basic_manual_clock: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Tag Tag type that selects the clock.
 *
 * @pre None.
 * @post None.
 */
template <typename Tag>
struct std::formatter<nexenne::chrono::basic_manual_clock<Tag>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param clock Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p clock has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::basic_manual_clock<Tag> const& clock, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(clock), ctx);
  }
};

/**
 * @brief \c std::format support for a \c frame_timer: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam WindowSize Moving-average window of the timer.
 * @tparam Clock Steady clock of the timer.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t WindowSize, nexenne::chrono::steady_clock_like Clock>
  requires(WindowSize > 0)
struct std::formatter<nexenne::chrono::frame_timer<WindowSize, Clock>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p t has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::frame_timer<WindowSize, Clock> const& t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(t), ctx);
  }
};

/**
 * @brief \c std::format support for a \c hertz: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Hz The frequency in hertz.
 *
 * @pre None.
 * @post None.
 */
template <std::uint64_t Hz>
struct std::formatter<nexenne::chrono::hertz<Hz>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param f Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p f has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::hertz<Hz> const f, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(f), ctx);
  }
};

/**
 * @brief \c std::format support for a \c profiler_stats: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Duration Duration type of the samples.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::chrono_duration Duration>
struct std::formatter<nexenne::chrono::profiler_stats<Duration>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p s has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::profiler_stats<Duration> const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for a \c profiler: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Clock Steady clock of the profiler.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::profiler<Clock>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param p Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p p has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::profiler<Clock> const& p, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(p), ctx);
  }
};

/**
 * @brief \c std::format support for a \c rate_limiter: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Clock Steady clock of the limiter.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::rate_limiter<Clock>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p r has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::rate_limiter<Clock> const& r, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(r), ctx);
  }
};

/**
 * @brief \c std::format support for a \c scope_timer: prints its \c to_string summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the text.
 *
 * @tparam Callback Callback type of the timer.
 * @tparam Clock Steady clock of the timer.
 *
 * @pre None.
 * @post None.
 */
template <typename Callback, nexenne::chrono::steady_clock_like Clock>
  requires std::invocable<Callback&, typename Clock::duration>
struct std::formatter<nexenne::chrono::scope_timer<Callback, Clock>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary of \p t has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::chrono::scope_timer<Callback, Clock> const& t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::chrono::to_string(t), ctx);
  }
};

namespace nexenne::chrono {

/**
 * @brief Debug string for a \c duration_parts.
 *
 * The same text \c std::format("{}") prints.
 *
 * @param parts Components to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(duration_parts const& parts) -> std::string {
  return std::format("{}", parts);
}

/**
 * @brief Streams a \c duration_parts via its \c to_string.
 *
 * @param os Output stream.
 * @param parts Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p parts has been written to \p os.
 */
inline auto operator<<(std::ostream& os, duration_parts const& parts) -> std::ostream& {
  return os << to_string(parts);
}

/**
 * @brief Debug string for a \c stopwatch: its elapsed time.
 *
 * The same text \c std::format("{}") prints.
 *
 * @tparam Clock Steady clock of the stopwatch.
 * @param sw Stopwatch to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <steady_clock_like Clock>
[[nodiscard]] auto to_string(stopwatch<Clock> const& sw) -> std::string {
  return std::format("{}", sw);
}

/**
 * @brief Streams a \c stopwatch via its \c to_string.
 *
 * @tparam Clock Steady clock of the stopwatch.
 * @param os Output stream.
 * @param sw Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p sw has been written to \p os.
 */
template <steady_clock_like Clock>
auto operator<<(std::ostream& os, stopwatch<Clock> const& sw) -> std::ostream& {
  return os << to_string(sw);
}

/**
 * @brief Debug string for a \c static_stopwatch: its elapsed time.
 *
 * The same text \c std::format("{}") prints.
 *
 * @tparam N Lap-buffer capacity of the stopwatch.
 * @tparam Clock Steady clock of the stopwatch.
 * @param sw Stopwatch to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <std::size_t N, steady_clock_like Clock>
[[nodiscard]] auto to_string(static_stopwatch<N, Clock> const& sw) -> std::string {
  return std::format("{}", sw);
}

/**
 * @brief Streams a \c static_stopwatch via its \c to_string.
 *
 * @tparam N Lap-buffer capacity of the stopwatch.
 * @tparam Clock Steady clock of the stopwatch.
 * @param os Output stream.
 * @param sw Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p sw has been written to \p os.
 */
template <std::size_t N, steady_clock_like Clock>
auto operator<<(std::ostream& os, static_stopwatch<N, Clock> const& sw) -> std::ostream& {
  return os << to_string(sw);
}

/**
 * @brief Debug string for a \c deadline: the time remaining.
 *
 * The same text \c std::format("{}") prints.
 *
 * @tparam Clock Steady clock of the deadline.
 * @param d Deadline to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <steady_clock_like Clock>
[[nodiscard]] auto to_string(deadline<Clock> const& d) -> std::string {
  return std::format("{}", d);
}

/**
 * @brief Streams a \c deadline via its \c to_string.
 *
 * @tparam Clock Steady clock of the deadline.
 * @param os Output stream.
 * @param d Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p d has been written to \p os.
 */
template <steady_clock_like Clock>
auto operator<<(std::ostream& os, deadline<Clock> const& d) -> std::ostream& {
  return os << to_string(d);
}

/**
 * @brief Debug string for an \c interval: the time until the next tick.
 *
 * The same text \c std::format("{}") prints.
 *
 * @tparam Clock Steady clock of the interval.
 * @param iv Interval to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <steady_clock_like Clock>
[[nodiscard]] auto to_string(interval<Clock> const& iv) -> std::string {
  return std::format("{}", iv);
}

/**
 * @brief Streams an \c interval via its \c to_string.
 *
 * @tparam Clock Steady clock of the interval.
 * @param os Output stream.
 * @param iv Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p iv has been written to \p os.
 */
template <steady_clock_like Clock>
auto operator<<(std::ostream& os, interval<Clock> const& iv) -> std::ostream& {
  return os << to_string(iv);
}

/**
 * @brief Debug string for a \c countdown: the time remaining.
 *
 * The same text \c std::format("{}") prints.
 *
 * @tparam Clock Steady clock of the countdown.
 * @param cd Countdown to describe.
 *
 * @return The formatted text.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <steady_clock_like Clock>
[[nodiscard]] auto to_string(countdown<Clock> const& cd) -> std::string {
  return std::format("{}", cd);
}

/**
 * @brief Streams a \c countdown via its \c to_string.
 *
 * @tparam Clock Steady clock of the countdown.
 * @param os Output stream.
 * @param cd Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p cd has been written to \p os.
 */
template <steady_clock_like Clock>
auto operator<<(std::ostream& os, countdown<Clock> const& cd) -> std::ostream& {
  return os << to_string(cd);
}

/**
 * @brief Streams a \c stopwatch_state by its name.
 *
 * @param os Output stream.
 * @param state Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p state has been written to \p os.
 */
inline auto operator<<(std::ostream& os, stopwatch_state const state) -> std::ostream& {
  return os << to_string(state);
}

/**
 * @brief Streams a \c countdown_state by its name.
 *
 * @param os Output stream.
 * @param state Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p state has been written to \p os.
 */
inline auto operator<<(std::ostream& os, countdown_state const state) -> std::ostream& {
  return os << to_string(state);
}

/**
 * @brief Streams an \c alarm_mode by its name.
 *
 * @param os Output stream.
 * @param mode Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p mode has been written to \p os.
 */
inline auto operator<<(std::ostream& os, alarm_mode const mode) -> std::ostream& {
  return os << to_string(mode);
}

/**
 * @brief Streams an \c alarm via its \c to_string.
 *
 * @tparam Clock Clock of the alarm.
 * @tparam CallbackBytes Inline callback storage of the alarm.
 * @param os Output stream.
 * @param a Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p a has been written to \p os.
 */
template <clock_like Clock, std::size_t CallbackBytes>
auto operator<<(std::ostream& os, alarm<Clock, CallbackBytes> const& a) -> std::ostream& {
  return os << to_string(a);
}

/**
 * @brief Streams a \c basic_manual_clock via its \c to_string.
 *
 * @tparam Tag Tag type that selects the clock.
 * @param os Output stream.
 * @param clock Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p clock has been written to \p os.
 */
template <typename Tag>
auto operator<<(std::ostream& os, basic_manual_clock<Tag> const& clock) -> std::ostream& {
  return os << to_string(clock);
}

/**
 * @brief Streams a \c frame_timer via its \c to_string.
 *
 * @tparam WindowSize Moving-average window of the timer.
 * @tparam Clock Steady clock of the timer.
 * @param os Output stream.
 * @param t Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p t has been written to \p os.
 */
template <std::size_t WindowSize, steady_clock_like Clock>
  requires(WindowSize > 0)
auto operator<<(std::ostream& os, frame_timer<WindowSize, Clock> const& t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Streams a \c hertz via its \c to_string.
 *
 * @tparam Hz The frequency in hertz.
 * @param os Output stream.
 * @param f Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p f has been written to \p os.
 */
template <std::uint64_t Hz>
auto operator<<(std::ostream& os, hertz<Hz> const f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Streams a \c profiler_stats via its \c to_string.
 *
 * @tparam Duration Duration type of the samples.
 * @param os Output stream.
 * @param s Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p s has been written to \p os.
 */
template <chrono_duration Duration>
auto operator<<(std::ostream& os, profiler_stats<Duration> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Streams a \c profiler via its \c to_string.
 *
 * @tparam Clock Steady clock of the profiler.
 * @param os Output stream.
 * @param p Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p p has been written to \p os.
 */
template <steady_clock_like Clock>
auto operator<<(std::ostream& os, profiler<Clock> const& p) -> std::ostream& {
  return os << to_string(p);
}

/**
 * @brief Streams a \c rate_limiter via its \c to_string.
 *
 * @tparam Clock Steady clock of the limiter.
 * @param os Output stream.
 * @param r Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p r has been written to \p os.
 */
template <steady_clock_like Clock>
auto operator<<(std::ostream& os, rate_limiter<Clock> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Streams a \c scope_timer via its \c to_string.
 *
 * @tparam Callback Callback type of the timer.
 * @tparam Clock Steady clock of the timer.
 * @param os Output stream.
 * @param t Value to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p t has been written to \p os.
 */
template <typename Callback, steady_clock_like Clock>
  requires std::invocable<Callback&, typename Clock::duration>
auto operator<<(std::ostream& os, scope_timer<Callback, Clock> const& t) -> std::ostream& {
  return os << to_string(t);
}

}  // namespace nexenne::chrono
