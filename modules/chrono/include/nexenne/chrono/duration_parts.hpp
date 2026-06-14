#pragma once

/**
 * @file
 * @brief Human-readable breakdown of a duration plus a small
 *        replace-based formatter.
 *
 * \c extract_parts splits a \c std::chrono::duration into
 * days / hours / minutes / seconds / milliseconds with a sign,
 * optionally rounding away the millisecond component (ties go away
 * from zero).
 *
 * \c format renders the parts using a tokenised format string. The
 * placeholders are deliberately unlike \c std::format positional
 * specifiers so they don't fight with it:
 *
 *   - \c {s+} positive sign character (only when value > 0)
 *   - \c {s-} negative sign character (only when value < 0)
 *   - \c {d}  days, zero-padded width 2
 *   - \c {h}  hours, zero-padded width 2
 *   - \c {m}  minutes, zero-padded width 2
 *   - \c {s}  seconds, zero-padded width 2
 *   - \c {ms} milliseconds, zero-padded width 3
 *
 * When the format string contains no \c {ms}, the value is rounded
 * to the nearest second (ties away from zero). With
 * \c suppress_zero = true, leading zero components are dropped and
 * the surviving components are joined with \c ':'.
 *
 * \c format_scaled is the complementary formatter for the other end of the
 * range: it renders a single auto-scaled SI unit (ns / us / ms / s), keeping
 * sub-millisecond resolution that the breakdown above deliberately drops, which
 * is what micro-timing reports want (for example "4.17 us").
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <ranges>
#include <ratio>
#include <string>
#include <string_view>
#include <type_traits>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Signed days / hours / minutes / seconds / milliseconds breakdown.
 *
 * Holds the components of a duration with a single overall \c sign. The
 * magnitude components are always non-negative; the \c sign field carries the
 * direction.
 *
 * @pre None.
 * @post None.
 */
struct duration_parts {
  int sign{0};              ///< -1, 0, or +1.
  std::int64_t days{0};     ///< Whole days, non-negative.
  std::int64_t hours{0};    ///< Hours within the day, 0 to 23.
  std::int64_t minutes{0};  ///< Minutes within the hour, 0 to 59.
  std::int64_t seconds{0};  ///< Seconds within the minute, 0 to 59.
  std::int64_t millis{0};   ///< Milliseconds within the second, 0 to 999.
};

/**
 * @brief Split a millisecond duration into signed time components.
 *
 * Decomposes \p ms into days, hours, minutes, seconds, and milliseconds with
 * an overall sign. When \p round_to_seconds is true the value is first rounded
 * to the nearest second, ties away from zero, and the millisecond component
 * comes out zero.
 *
 * @param ms Duration to decompose.
 * @param round_to_seconds Whether to round to whole seconds first.
 *
 * @return The component breakdown of \p ms.
 *
 * @pre None.
 * @post The magnitude components are non-negative; \c sign is -1, 0, or +1 and
 *       is zero exactly when \p ms is zero.
 */
[[nodiscard]] constexpr auto extract_parts(
  std::chrono::milliseconds ms, bool const round_to_seconds = false
) noexcept -> duration_parts {
  using rep_type = std::chrono::milliseconds::rep;
  using urep_type = std::make_unsigned_t<rep_type>;

  if (round_to_seconds) {
    // Round to the nearest second within the millisecond domain. Doing this via
    // duration_cast<seconds> then back would multiply by 1000 and could overflow
    // int64 near milliseconds::max(); the guarded integer add stays in range and
    // saturates at the extreme instead of invoking undefined behaviour.
    using ms_t = std::chrono::milliseconds;
    auto total{ms.count()};
    auto const rem{total % 1000};
    total -= rem;  // toward zero to a whole second; magnitude shrinks, no overflow
    if (rem >= 500 && total <= ms_t::max().count() - 1000) {
      total += 1000;
    } else if (rem <= -500 && total >= ms_t::min().count() + 1000) {
      total -= 1000;
    }
    ms = ms_t{total};
  }

  auto const total{ms.count()};
  auto const sign{total > 0 ? 1 : (total < 0 ? -1 : 0)};
  auto const abs_ms{
    total >= 0 ? static_cast<urep_type>(total) : urep_type{} - static_cast<urep_type>(total)
  };

  constexpr auto day_ms{urep_type{86'400'000u}};
  constexpr auto hour_ms{urep_type{3'600'000u}};
  constexpr auto min_ms{urep_type{60'000u}};
  constexpr auto sec_ms{urep_type{1'000u}};

  auto const days{abs_ms / day_ms};
  auto rem{abs_ms % day_ms};
  auto const hrs{rem / hour_ms};
  rem %= hour_ms;
  auto const mins{rem / min_ms};
  rem %= min_ms;
  auto const secs{rem / sec_ms};
  auto const msec{rem % sec_ms};

  return duration_parts{
    .sign = sign,
    .days = static_cast<std::int64_t>(days),
    .hours = static_cast<std::int64_t>(hrs),
    .minutes = static_cast<std::int64_t>(mins),
    .seconds = static_cast<std::int64_t>(secs),
    .millis = static_cast<std::int64_t>(msec),
  };
}

namespace detail {

// Cast d to milliseconds, clamping to the representable range first. When D is
// coarser than a millisecond the plain cast multiplies and could overflow
// int64 (undefined), so values beyond the millisecond range saturate instead.
template <chrono_duration D>
[[nodiscard]] constexpr auto to_millis_clamped(D d) noexcept -> std::chrono::milliseconds {
  using ms = std::chrono::milliseconds;
  if constexpr (std::ratio_greater_v<typename D::period, typename ms::period>) {
    constexpr auto hi{std::chrono::duration_cast<D>(ms::max())};
    constexpr auto lo{std::chrono::duration_cast<D>(ms::min())};
    if (d > hi) {
      d = hi;
    } else if (d < lo) {
      d = lo;
    }
  }
  return std::chrono::duration_cast<ms>(d);
}

}  // namespace detail

/**
 * @brief Split any duration into signed time components.
 *
 * Casts \p d to milliseconds (saturating if \p d is too coarse and large to fit),
 * then delegates to the millisecond overload.
 *
 * @tparam D Source duration type, deduced from \p d.
 * @param d Duration to decompose.
 * @param round_to_seconds Whether to round to whole seconds first.
 *
 * @return The component breakdown of \p d.
 *
 * @pre None.
 * @post The magnitude components are non-negative; \c sign is -1, 0, or +1.
 */
template <chrono_duration D>
[[nodiscard]] constexpr auto
extract_parts(D const d, bool const round_to_seconds = false) noexcept -> duration_parts {
  return extract_parts(detail::to_millis_clamped(d), round_to_seconds);
}

namespace detail {

inline auto replace_all(
  std::string& inout, std::string_view const token, std::string_view const value
) -> void {
  if (token.empty()) {
    return;
  }
  for (auto pos{inout.find(token)}; pos != std::string::npos;
       pos = inout.find(token, pos + value.size())) {
    inout.replace(pos, token.size(), value);
  }
}

}  // namespace detail

/**
 * @brief Render a millisecond duration through a token format string.
 *
 * Substitutes the placeholders \c {s+}, \c {s-}, \c {d}, \c {h}, \c {m},
 * \c {s}, and \c {ms} in \p fmt with the corresponding zero-padded
 * components. When \p fmt contains no \c {ms}, the value is rounded to the
 * nearest second, ties away from zero. With \p suppress_zero, leading zero
 * components are dropped and the survivors are joined with \c ':'.
 *
 * @param ms Duration to render.
 * @param fmt Token format string.
 * @param suppress_zero Whether to drop leading zero components.
 * @param pos_sign Text emitted for a positive value at \c {s+}.
 * @param neg_sign Text emitted for a negative value at \c {s-}.
 *
 * @return The formatted string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if string construction fails.
 *
 * @par Example
 * \code
 *   auto const s{nexenne::chrono::format(std::chrono::milliseconds{90061500})};
 *   // "01d:01h:01m:01s:500ms"  (suppress-zero joins every piece with ':')
 * \endcode
 */
[[nodiscard]] inline auto format(
  std::chrono::milliseconds const ms,
  std::string_view const fmt = "{s-}{d}d:{h}h:{m}m:{s}s.{ms}",
  bool const suppress_zero = true,
  std::string_view const pos_sign = "+",
  std::string_view const neg_sign = "-"
) -> std::string {
  auto const want_ms{fmt.find("{ms}") != std::string_view::npos};
  auto const want_plus{fmt.find("{s+}") != std::string_view::npos};
  auto const want_minus{fmt.find("{s-}") != std::string_view::npos};

  auto const parts{extract_parts(ms, !want_ms)};

  auto const s_d{std::format("{:02}", parts.days)};
  auto const s_h{std::format("{:02}", parts.hours)};
  auto const s_m{std::format("{:02}", parts.minutes)};
  auto const s_s{std::format("{:02}", parts.seconds)};
  auto const s_ms{std::format("{:03}", parts.millis)};

  if (!suppress_zero) {
    auto out{std::string{fmt}};
    detail::replace_all(out, "{s+}", parts.sign > 0 ? pos_sign : std::string_view{});
    detail::replace_all(out, "{s-}", parts.sign < 0 ? neg_sign : std::string_view{});
    detail::replace_all(out, "{d}", s_d);
    detail::replace_all(out, "{h}", s_h);
    detail::replace_all(out, "{m}", s_m);
    detail::replace_all(out, "{s}", s_s);
    detail::replace_all(out, "{ms}", want_ms ? std::string_view{s_ms} : std::string_view{});
    return out;
  }

  auto sign_out{std::string{}};
  if (parts.sign > 0 && want_plus) {
    sign_out = std::string{pos_sign};
  }
  if (parts.sign < 0 && want_minus) {
    sign_out = std::string{neg_sign};
  }

  auto const part_d{parts.days != 0 ? std::format("{}d", s_d) : std::string{}};
  auto const part_h{
    (parts.hours != 0 || !part_d.empty())
      ? (parts.hours != 0 ? std::format("{}h", s_h) : std::string{})
      : std::string{}
  };
  auto const part_m{
    (parts.minutes != 0 || !part_d.empty() || !part_h.empty())
      ? (parts.minutes != 0 ? std::format("{}m", s_m) : std::string{})
      : std::string{}
  };
  auto const part_s{std::format("{}s", s_s)};
  auto const part_ms{(want_ms && parts.millis != 0) ? std::format("{}ms", s_ms) : std::string{}};

  auto body{std::string{}};
  body.reserve(part_d.size() + part_h.size() + part_m.size() + part_s.size() + part_ms.size() + 4);
  auto add{[&](std::string const& piece) {
    if (piece.empty()) {
      return;
    }
    if (!body.empty()) {
      body.push_back(':');
    }
    body += piece;
  }};
  add(part_d);
  add(part_h);
  add(part_m);
  add(part_s);
  add(part_ms);

  return sign_out + body;
}

/**
 * @brief Render any duration through a token format string.
 *
 * Casts \p d to milliseconds (saturating if too coarse and large to fit), then
 * delegates to the millisecond overload.
 *
 * @tparam D Source duration type, deduced from \p d.
 * @param d Duration to render.
 * @param fmt Token format string.
 * @param suppress_zero Whether to drop leading zero components.
 * @param pos_sign Text emitted for a positive value at \c {s+}.
 * @param neg_sign Text emitted for a negative value at \c {s-}.
 *
 * @return The formatted string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if string construction fails.
 */
template <chrono_duration D>
[[nodiscard]] inline auto format(
  D const d,
  std::string_view const fmt = "{s-}{d}d:{h}h:{m}m:{s}s.{ms}",
  bool const suppress_zero = true,
  std::string_view const pos_sign = "+",
  std::string_view const neg_sign = "-"
) -> std::string {
  return format(detail::to_millis_clamped(d), fmt, suppress_zero, pos_sign, neg_sign);
}

/**
 * @brief Render a duration as a human-readable auto-scaled single unit.
 *
 * Picks the largest of \c ns, \c us, \c ms, \c s whose magnitude is at least one
 * and formats it with \p precision fractional digits, so 4170 ns becomes
 * "4.17 us". Unlike \c format (a days/hours/minutes/seconds/ms breakdown) this
 * keeps sub-millisecond resolution, which is what micro-timing reports want. A
 * negative duration keeps its sign. Give it a floating-point rep (for example
 * \c std::chrono::duration<double,std::nano>) to preserve fractional units.
 *
 * @tparam D Source duration type, deduced from \p d.
 * @param d Duration to render.
 * @param precision Number of fractional digits.
 *
 * @return The scaled magnitude with a unit suffix (\c ns, \c us, \c ms, or \c s).
 *
 * @pre \p precision is non-negative.
 * @post None.
 * @throws std::bad_alloc if string construction fails.
 *
 * @par Example
 * \code
 *   using ns_d = std::chrono::duration<double, std::nano>;
 *   nexenne::chrono::format_scaled(ns_d{4170.0});  // "4.17 us"
 *   nexenne::chrono::format_scaled(std::chrono::milliseconds{5});  // "5.00 ms"
 * \endcode
 */
template <chrono_duration D>
[[nodiscard]] auto format_scaled(D const d, int const precision = 2) -> std::string {
  auto const raw{std::chrono::duration_cast<std::chrono::duration<double, std::nano>>(d).count()};
  auto const ns{raw == 0.0 ? 0.0 : raw};  // normalise -0.0 so a zero never reads "-0.00"
  auto const magnitude{ns < 0.0 ? -ns : ns};
  if (magnitude < 1e3) {
    return std::format("{:.{}f} ns", ns, precision);
  }
  if (magnitude < 1e6) {
    return std::format("{:.{}f} us", ns / 1e3, precision);
  }
  if (magnitude < 1e9) {
    return std::format("{:.{}f} ms", ns / 1e6, precision);
  }
  return std::format("{:.{}f} s", ns / 1e9, precision);
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
  auto format(nexenne::chrono::duration_parts const& p, std::basic_format_context<Out, char>& ctx)
    const {
    using std::chrono::days;
    using std::chrono::hours;
    using std::chrono::milliseconds;
    using std::chrono::minutes;
    using std::chrono::seconds;
    auto total{
      days{p.days} + hours{p.hours} + minutes{p.minutes} + seconds{p.seconds}
      + milliseconds{p.millis}
    };
    if (p.sign < 0) {
      total = -total;
    }
    auto const s{nexenne::chrono::format(total, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};
