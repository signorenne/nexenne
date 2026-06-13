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

#include <nexenne/chrono/concepts.hpp>

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
   * @return A deadline whose target is \c Clock::now() plus \p d.
   *
   * @pre None.
   * @post The returned deadline has \c when() equal to the \c now() read at
   *       the call plus \p d.
   */
  template <chrono_duration D>
  [[nodiscard]] static auto after(D const d) noexcept -> deadline {
    return deadline{Clock::now() + std::chrono::duration_cast<duration>(d)};
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
