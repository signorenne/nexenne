#pragma once

/**
 * @file
 * @brief An absolute time point with \c reached() / \c remaining()
 *        helpers.
 *
 * Useful as the "must be done by" companion to retries, polling
 * loops, and cancellable I/O. A \c deadline holds a stored
 * \c time_point and answers questions about it relative to the
 * current \c Clock::now().
 *
 * \c remaining() clamps negative differences to zero so that
 * callers don't have to guard the common case of "already overdue".
 *
 * @tparam Clock Steady clock the deadline is anchored against.
 */

#include <chrono>
#include <compare>
#include <format>
#include <ranges>
#include <string>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/duration_parts.hpp>

namespace nexenne::chrono {

/**
 * @brief Absolute time point with \c reached() and \c remaining() helpers.
 *
 * Stores a target \c time_point and answers questions about it relative to
 * \c Clock::now(). \c remaining() clamps overdue deadlines to zero so callers
 * need not special-case the already-past case.
 *
 * @tparam Clock Steady clock the deadline is anchored against.
 *
 * @pre None.
 * @post A default-constructed deadline targets the \p Clock epoch.
 */
template <steady_clock_like Clock = std::chrono::steady_clock>
class deadline {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

private:
  time_point m_when{};

public:
  /**
   * @brief Construct a deadline at the \p Clock epoch.
   *
   * @pre None.
   * @post \c when() equals a default-constructed \c time_point.
   */
  constexpr deadline() noexcept = default;

  /**
   * @brief Construct a deadline at the absolute time \p when.
   *
   * @param when Absolute target time on \p Clock.
   *
   * @pre None.
   * @post \c when() equals \p when.
   */
  constexpr explicit deadline(time_point const when) noexcept : m_when{when} {}

  /**
   * @brief Make a deadline anchored at the absolute time \p when.
   *
   * @param when Absolute target time on \p Clock.
   *
   * @return A deadline whose target is \p when.
   *
   * @pre None.
   * @post The returned deadline has \c when() equal to \p when.
   */
  [[nodiscard]] static constexpr auto at(time_point const when) noexcept -> deadline {
    return deadline{when};
  }

  /**
   * @brief Make a deadline \p d after the current time.
   *
   * Reads \c Clock::now() once and offsets it by \p d.
   *
   * @tparam D Source duration type, deduced from \p d.
   * @param d Offset from the current time to the target.
   *
   * @return A deadline whose target is \c Clock::now() plus \p d, saturated to
   *         the representable \c time_point range.
   *
   * @pre None.
   * @post The returned deadline has \c when() equal to the \c now() read at
   *       the call plus \p d, clamped to \c time_point::max() or
   *       \c time_point::min() when that sum is not representable.
   *
   * @note An offset near \c duration::max (a common "never expires" idiom)
   *       saturates to \c time_point::max() instead of overflowing the
   *       time-point addition and wrapping into the past.
   */
  template <chrono_duration D>
  [[nodiscard]] static auto after(D const d) noexcept -> deadline {
    auto const now{Clock::now()};
    auto const off{std::chrono::duration_cast<duration>(d)};
    // Test representability in the duration domain: the headroom bounds
    // (duration::max() - off with off positive, duration::min() - off with off
    // negative) never overflow themselves, unlike time_point::max() - now.
    auto const now_d{now.time_since_epoch()};
    if (off > duration::zero() && now_d > duration::max() - off) {
      return deadline{time_point::max()};
    }
    if (off < duration::zero() && now_d < duration::min() - off) {
      return deadline{time_point::min()};
    }
    return deadline{now + off};
  }

  /**
   * @brief Absolute target time of this deadline.
   *
   * @return The stored target \c time_point.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto when() const noexcept -> time_point {
    return m_when;
  }

  /**
   * @brief Whether the deadline has been reached.
   *
   * Reads \c Clock::now() and compares it against the target.
   *
   * @return \c true if the current time is at or past the target.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto reached() const noexcept -> bool {
    return Clock::now() >= m_when;
  }

  /**
   * @brief Time remaining until the deadline, clamped at zero.
   *
   * Reads \c Clock::now(); an already-overdue deadline reports zero rather
   * than a negative duration.
   *
   * @return The non-negative time left until the target.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto remaining() const noexcept -> duration {
    auto const diff{m_when - Clock::now()};
    return diff <= duration::zero() ? duration::zero() : diff;
  }

  /**
   * @brief Time remaining until the deadline in the units \p D.
   *
   * @tparam D Duration type the result is cast to.
   *
   * @return The non-negative time left, expressed in \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D>
  [[nodiscard]] auto remaining() const noexcept -> D {
    return std::chrono::duration_cast<D>(remaining());
  }

  /**
   * @brief Three-way comparison by absolute target time.
   *
   * Earlier deadlines compare less. For a priority queue that pops the
   * next-to-fire deadline first, use \c std::greater<> as the comparator,
   * since \c std::priority_queue is a max-heap by default.
   *
   * @return The ordering of the two targets.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(deadline const&, deadline const&) noexcept = default;

  /**
   * @brief Equality by absolute target time.
   *
   * @return \c true if both deadlines have the same target.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(deadline const&, deadline const&) noexcept -> bool = default;
};

}  // namespace nexenne::chrono

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
