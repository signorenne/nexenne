#pragma once

/**
 * @file
 * @brief Time-based debounce for a boolean line.
 */

#include <chrono>
#include <optional>

namespace nexenne::filter {

/**
 * @brief Time-based debounce for a boolean line.
 *
 * The counterpart to \c debounce (which counts consecutive samples):
 * this variant settles on wall/monotonic time. Per sample it answers
 * "given the new level and the timestamp, has the line settled long
 * enough that I should accept the new level as the steady state?"
 *
 * Algorithm: when the incoming level differs from the last accepted
 * value, the candidate is held for at least \c period before it is
 * promoted to stable. Any new level matching the previous stable one
 * cancels the candidate immediately.
 *
 * Zero-allocation and sample-source agnostic: feed it raw \c bool
 * readings plus a timestamp drawn from whatever monotonic clock the
 * caller has on hand. Suited to mechanical switches, GPIO lines, and
 * other bouncing digital inputs.
 *
 * \code
 * nexenne::filter::timed_debounce db{std::chrono::milliseconds{20}};
 * if (auto stable{db.update(now, raw_level)}; stable.has_value()) {
 *     dispatch(*stable);
 * }
 * \endcode
 *
 * @tparam Duration Monotonic duration type for the period and
 * timestamps (defaults to \c std::chrono::nanoseconds).
 *
 * @note Reach for this when you want the same intent as \c debounce but
 * gated by an elapsed real duration, so it stays robust to a varying or
 * unknown sample rate.
 */
template <typename Duration = std::chrono::nanoseconds>
class timed_debounce {
public:
  using duration = Duration;

private:
  duration m_period{0};
  duration m_candidate_since{0};
  bool m_stable{false};
  bool m_candidate{false};
  bool m_has_stable{false};
  bool m_has_candidate{false};

public:
  /**
   * @brief Constructs a debouncer with a zero settling period.
   *
   * With a zero period the filter behaves as a pass-through, promoting
   * every changed level immediately.
   *
   * @pre None.
   * @post \c period() is zero and \c has_stable() returns \c false.
   */
  constexpr timed_debounce() noexcept = default;

  /**
   * @brief Constructs a debouncer with a settling period.
   *
   * A negative period is clamped to zero.
   *
   * @param period Minimum time a changed level must persist before it
   * is promoted to stable.
   *
   * @pre None. A negative \p period is clamped to zero.
   * @post \c period() returns \c max(period, 0) and \c has_stable()
   * returns \c false.
   */
  explicit constexpr timed_debounce(duration const period) noexcept
      : m_period{period.count() < 0 ? duration{0} : period} {}

  /**
   * @brief Returns the current settling period.
   *
   * @return The configured period, always non-negative.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto period() const noexcept -> duration {
    return m_period;
  }

  /**
   * @brief Replaces the settling period.
   *
   * A negative period is clamped to zero.
   *
   * @param v New settling period.
   *
   * @pre None. A negative \p v is clamped to zero.
   * @post \c period() returns \c max(v, 0); any in-progress candidate
   * and the stable value are unchanged.
   */
  constexpr auto period(duration const v) noexcept -> void {
    m_period = v.count() < 0 ? duration{0} : v;
  }

  /**
   * @brief Returns the last settled level.
   *
   * @return The stable level, or \c false before any sample has been
   * observed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto stable_value() const noexcept -> bool {
    return m_stable;
  }

  /**
   * @brief Reports whether a stable level has been established.
   *
   * @return \c true once at least one sample has been observed,
   * \c false otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto has_stable() const noexcept -> bool {
    return m_has_stable;
  }

  /**
   * @brief Resets to the "no observation yet" condition.
   *
   * Useful after a source reopen, where the cached state no longer
   * reflects physical reality.
   *
   * @pre None.
   * @post \c has_stable() returns \c false and any in-progress
   * candidate is discarded; the next \c update accepts its
   * sample as the stable value.
   */
  constexpr auto reset() noexcept -> void {
    m_candidate_since = duration{0};
    m_stable = false;
    m_candidate = false;
    m_has_stable = false;
    m_has_candidate = false;
  }

  /**
   * @brief Feeds a new raw sample at a given timestamp.
   *
   * The first sample is accepted as stable immediately. A level
   * matching the current stable value cancels any pending candidate.
   * A differing level becomes a candidate that is promoted to stable
   * once it has persisted for at least \c period(). A zero \c period()
   * collapses the filter into a pass-through.
   *
   * @param now Current timestamp from a monotonic clock, in the same
   * units as \c period().
   * @param raw New raw level.
   *
   * @return The newly settled level when this update produces a
   * settled change, otherwise \c std::nullopt (still bouncing,
   * no change, or candidate just started).
   *
   * @pre \p now is monotonically non-decreasing across consecutive
   * calls for the elapsed-time comparison to be meaningful.
   * @post \c has_stable() returns \c true; \c stable_value() reflects
   * any promotion that occurred on this call.
   *
   * @complexity \c O(1).
   */
  constexpr auto update(duration const now, bool const raw) noexcept -> std::optional<bool> {
    if (!m_has_stable) {
      m_stable = raw;
      m_has_stable = true;
      m_candidate = raw;
      m_has_candidate = false;
      return raw;
    }
    if (raw == m_stable) {
      m_has_candidate = false;
      return std::nullopt;
    }
    if (m_period.count() == 0) {
      m_stable = raw;
      return raw;
    }
    if (!m_has_candidate || raw != m_candidate) {
      m_candidate = raw;
      m_candidate_since = now;
      m_has_candidate = true;
      return std::nullopt;
    }
    if ((now - m_candidate_since) >= m_period) {
      m_stable = m_candidate;
      m_has_candidate = false;
      return m_stable;
    }
    return std::nullopt;
  }
};

}  // namespace nexenne::filter
