#pragma once

/**
 * @file
 * @brief Hand-advanced clock for deterministic tests.
 *
 * Every chrono primitive in this module is templated on its clock,
 * so passing \c manual_clock instead of \c std::chrono::steady_clock
 * lets unit tests advance time precisely without
 * \c std::this_thread::sleep_for, with no flakiness and no real-time delay.
 *
 * Storage is static (mirrors the chrono clock requirement that
 * \c now() be a static member). Use \c reset() between test cases
 * to avoid leaking state across them.
 *
 * Two distinct tags give independent clocks in the same TU:
 *
 * \code
 * struct tag_a {}; struct tag_b {};
 * using clk_a = nexenne::chrono::basic_manual_clock<tag_a>;
 * using clk_b = nexenne::chrono::basic_manual_clock<tag_b>;
 * clk_a::advance(std::chrono::milliseconds{100});  // tag_b unaffected
 * \endcode
 *
 * \warning Not thread-safe. Designed for single-threaded
 *          deterministic tests. Concurrent access to \c advance()
 *          or \c now() from multiple threads is a data race.
 *
 * \warning \c advance(negative) rolls the clock backward,
 *          violating the \c is_steady = true contract this type
 *          advertises. Most chrono primitives in this module
 *          either guard against backward time (\c rate_limiter,
 *          \c frame_timer) or document that they don't, but
 *          third-party code consuming \c manual_clock as a
 *          \c steady_clock_like type may misbehave. Use only in
 *          tests that specifically exercise the backward-time path.
 */

#include <chrono>
#include <cstdint>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Hand-advanced \c steady_clock_like clock for deterministic tests.
 *
 * Satisfies \c steady_clock_like, so it can be dropped in wherever a clock is
 * expected, but its time only moves when test code calls \c advance or
 * \c set. The current time is static state keyed by the \p Tag type, so two
 * distinct tags give independent clocks in the same translation unit.
 *
 * @tparam Tag Distinguishing tag type that selects an independent clock.
 *
 * @pre None.
 * @post None.
 *
 * @warning Not thread-safe; concurrent access to \c advance or \c now from
 *          multiple threads is a data race.
 * @warning A negative \c advance rolls the clock backward, violating the
 *          \c is_steady contract this type advertises.
 */
template <typename Tag = struct default_manual_tag>
class basic_manual_clock {
public:
  using rep = std::int64_t;
  using period = std::nano;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<basic_manual_clock, duration>;

  static constexpr bool is_steady{true};

private:
  static inline duration s_now{duration::zero()};

public:
  /**
   * @brief Current virtual time.
   *
   * @return The time point reflecting the accumulated advances since the
   *         last \c reset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto now() noexcept -> time_point {
    return time_point{s_now};
  }

  /**
   * @brief Advance the virtual time by \p d.
   *
   * A negative \p d moves the clock backward.
   *
   * @tparam D Source duration type, deduced from \p d.
   * @param d Amount to add to the current virtual time.
   *
   * @pre None.
   * @post \c now() reflects the prior time plus \p d.
   */
  template <chrono_duration D>
  static auto advance(D const d) noexcept -> void {
    s_now += std::chrono::duration_cast<duration>(d);
  }

  /**
   * @brief Set the virtual time to the absolute point \p tp.
   *
   * @param tp Time point to set the clock to.
   *
   * @pre None.
   * @post \c now() equals \p tp.
   */
  static auto set(time_point const tp) noexcept -> void {
    s_now = tp.time_since_epoch();
  }

  /**
   * @brief Reset the virtual time to the epoch.
   *
   * @pre None.
   * @post \c now() equals the \p Tag clock epoch.
   */
  static auto reset() noexcept -> void {
    s_now = duration::zero();
  }
};

/**
 * @brief Default manual clock for tests.
 *
 * An alias for \c basic_manual_clock with the default tag. Use a distinct tag
 * via \c basic_manual_clock directly when two independent virtual clocks are
 * needed in one file.
 *
 * @pre None.
 * @post None.
 */
using manual_clock = basic_manual_clock<>;

}  // namespace nexenne::chrono
