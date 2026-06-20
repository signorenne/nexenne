#pragma once

/**
 * @file
 * @brief Token-bucket rate limiter.
 *
 * Maintains a bucket of up to \c capacity tokens that refills at
 * \c refill_per_sec tokens per second. \c try_acquire(n) succeeds
 * if at least \c n tokens are available, consuming them. Used for:
 *
 *   - log throttling (drop messages once the bucket is empty)
 *   - retry pacing (request a token before each retry)
 *   - producer / consumer back-pressure
 *
 * Refill is lazy: state advances on \c try_acquire and \c tokens()
 * calls. No background thread, no allocation.
 *
 * The bucket starts full, so the first burst can consume up to
 * \c capacity tokens immediately, then steady-state pacing catches up.
 *
 * Tokens are floating-point so fractional rates (e.g. one token
 * every 1.5 seconds -> \c refill_per_sec = 0.666...) are valid.
 *
 * @tparam Clock Steady clock used to compute elapsed time.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Token-bucket rate limiter with lazy refill.
 *
 * Holds up to \c capacity tokens that refill at \c refill_per_sec tokens per
 * second. \c try_acquire(n) consumes \c n tokens when at least that many are
 * available. The bucket starts full, so an initial burst can drain it
 * immediately before steady-state pacing takes over. Refill is lazy, advancing
 * only on \c try_acquire and \c tokens, with no background thread or
 * allocation. Tokens are floating-point, so fractional rates are valid.
 *
 * @tparam Clock Steady clock used to compute elapsed time.
 *
 * @pre None.
 * @post A freshly constructed limiter starts full.
 */
template <steady_clock_like Clock = std::chrono::steady_clock>
class rate_limiter {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

private:
  double m_capacity{0.0};
  double m_refill_per_sec{0.0};
  double m_tokens{0.0};
  time_point m_last{};
  bool m_anchored{false};

  // Clamp a rate parameter to a finite, non-negative value. Negative inputs and
  // non-finite ones (NaN, infinities) map to zero so they cannot propagate into
  // the token count, where a NaN would make every comparison false and silently
  // disable limiting.
  [[nodiscard]] static constexpr auto clamp_rate(double const v) noexcept -> double {
    return (v > 0.0 && v <= std::numeric_limits<double>::max()) ? v : 0.0;
  }

  auto refill() noexcept -> void {
    auto const now{Clock::now()};
    if (!m_anchored) {
      m_last = now;
      m_anchored = true;
      return;
    }
    // Guard against a non-monotonic clock (e.g. \c manual_clock
    // set backward in a test). For \c steady_clock this branch
    // is dead.
    auto dt_ns{std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_last).count()};
    if (dt_ns < 0) {
      dt_ns = 0;
    }
    auto const dt_sec{static_cast<double>(dt_ns) * 1e-9};
    m_tokens = std::min(m_capacity, m_tokens + dt_sec * m_refill_per_sec);
    // Hold the anchor monotonic: a backward clock must not move it back, which
    // would re-credit the skipped interval as refill on the next forward step.
    m_last = std::max(m_last, now);
  }

public:
  /**
   * @brief Construct a full bucket of \p capacity tokens.
   *
   * Negative and non-finite arguments (NaN, infinities) are clamped to zero.
   *
   * @param capacity Maximum tokens the bucket can hold.
   * @param refill_per_sec Tokens added per second.
   *
   * @pre None.
   * @post \c capacity() and \c refill_rate() are the finite non-negative clamps
   *       of the arguments and the bucket starts full.
   */
  constexpr rate_limiter(double const capacity, double const refill_per_sec) noexcept
      : m_capacity{clamp_rate(capacity)}
      , m_refill_per_sec{clamp_rate(refill_per_sec)}
      , m_tokens{clamp_rate(capacity)} {}

  /**
   * @brief Try to consume \p n tokens.
   *
   * Lazily refills, then deducts \p n tokens if enough are available. A NaN
   * or negative \p n returns \c false with no side effects; a zero \p n
   * trivially succeeds. A tiny epsilon admits acquires that are off by a few
   * ULPs of refill arithmetic, and on success the token count is clamped
   * non-negative so that epsilon cannot accumulate into real debt.
   *
   * @param n Number of tokens to consume; defaults to one.
   *
   * @return \c true if the tokens were consumed, else \c false with the
   *         bucket untouched.
   *
   * @pre None.
   * @post On \c true, the available token count dropped by \p n and remains
   *       non-negative; on \c false, the count is unchanged apart from the
   *       lazy refill.
   */
  [[nodiscard]] auto try_acquire(double const n = 1.0) noexcept -> bool {
    if (!std::isfinite(n) || n < 0.0) {
      return false;
    }
    if (n == 0.0) {
      return true;
    }
    refill();
    constexpr auto eps{1e-9};
    if (m_tokens + eps < n) {
      return false;
    }
    m_tokens -= n;
    if (m_tokens < 0.0) {
      m_tokens = 0.0;
    }
    return true;
  }

  /**
   * @brief Tokens currently available, after a lazy refill.
   *
   * @return The number of tokens on hand.
   *
   * @pre None.
   * @post The result lies in the closed range \c [0, capacity()].
   */
  [[nodiscard]] auto tokens() noexcept -> double {
    refill();
    return m_tokens;
  }

  /**
   * @brief Time to wait until \p n tokens become available.
   *
   * Lazily refills first. Pair with \c std::this_thread::sleep_for to block
   * until ready, or with an event loop's scheduler to poll efficiently.
   *
   * @param n Number of tokens to wait for; defaults to one.
   *
   * @return Zero when \p n tokens are already on hand or \p n is not
   *         positive; \c duration::max() when the bucket can never reach
   *         \p n, such as a zero refill rate while short; otherwise the
   *         rounded-up wait.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto until_next_token(double const n = 1.0) noexcept -> duration {
    if (!std::isfinite(n) || n <= 0.0) {
      return duration::zero();
    }
    refill();
    constexpr auto eps{1e-9};
    if (m_tokens + eps >= n) {
      return duration::zero();
    }
    if (m_refill_per_sec <= 0.0) {
      return duration::max();
    }
    auto const deficit{n - m_tokens};
    auto const seconds{deficit / m_refill_per_sec};

    // Round up so the caller sleeps slightly longer than the
    // bare minimum - otherwise float->int truncation can cause
    // a busy-spin where the caller wakes one ULP too early.
    auto const wanted_ns{std::ceil(seconds * 1e9)};
    constexpr auto max_ns{static_cast<double>(std::numeric_limits<std::int64_t>::max())};
    if (!std::isfinite(wanted_ns) || wanted_ns >= max_ns) {
      return duration::max();
    }
    return std::chrono::duration_cast<duration>(
      std::chrono::nanoseconds{static_cast<std::int64_t>(wanted_ns)}
    );
  }

  /**
   * @brief Maximum tokens the bucket can hold.
   *
   * @return The configured, non-negative capacity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> double {
    return m_capacity;
  }

  /**
   * @brief Refill rate in tokens per second.
   *
   * @return The configured, non-negative refill rate.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto refill_rate() const noexcept -> double {
    return m_refill_per_sec;
  }

  /**
   * @brief Refill the bucket to full immediately.
   *
   * @pre None.
   * @post The available token count equals \c capacity() and the refill
   *       anchor is the current time.
   */
  auto reset() noexcept -> void {
    m_tokens = m_capacity;
    m_last = Clock::now();
    m_anchored = true;
  }

  /**
   * @brief Empty the bucket immediately.
   *
   * @pre None.
   * @post The available token count is zero and the refill anchor is the
   *       current time.
   */
  auto drain() noexcept -> void {
    m_tokens = 0.0;
    m_last = Clock::now();
    m_anchored = true;
  }
};

}  // namespace nexenne::chrono
