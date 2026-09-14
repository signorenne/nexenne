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
 * an \c operator<< that give the formatter's default text.
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <ostream>
#include <string>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/static_stopwatch.hpp>
#include <nexenne/chrono/stopwatch.hpp>

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
  bool suppress_zero{true};

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

}  // namespace nexenne::chrono
