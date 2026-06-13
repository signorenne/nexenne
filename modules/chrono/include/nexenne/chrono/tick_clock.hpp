#pragma once

/**
 * @file
 * @brief Adapter that turns a \c tick_backend into a Chrono-compatible clock.
 *
 * The backend supplies the raw tick source (a hardware counter, an
 * \c esp_timer_get_time() call, an RTOS tick, etc.); the adapter
 * wraps it as a clock type that the rest of the module, and the
 * standard \c std::chrono APIs, can consume.
 *
 * Example backend (1 tick = 1 microsecond, mock-friendly):
 *
 * \code
 * struct micro_backend {
 *     using rep = std::int64_t;
 *     using period = std::micro;
 *     static constexpr bool is_steady = true;
 *     static auto ticks() noexcept -> rep { return ...; }
 * };
 * using clock = nexenne::chrono::tick_clock<micro_backend>;
 * \endcode
 *
 * Epoch is backend-defined ("since boot", "since reset", "since
 * scheduler start"). Wrap-around handling is the backend's
 * responsibility; pick a wide enough \c rep, or extend the counter
 * before exposing it as \c ticks().
 *
 * @tparam Backend A type satisfying \c tick_backend.
 */

#include <chrono>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Adapter turning a \c tick_backend into a Chrono-compatible clock.
 *
 * Wraps a raw tick source, such as a hardware counter or RTOS tick, as a
 * \c clock_like type the rest of the module and the standard \c std::chrono
 * APIs can consume. The epoch and wrap-around handling are the backend's
 * responsibility.
 *
 * @tparam Backend A type satisfying \c tick_backend.
 *
 * @pre None.
 * @post None.
 */
template <tick_backend Backend>
struct tick_clock {
  using backend_type = Backend;
  using rep = typename Backend::rep;
  using period = typename Backend::period;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<tick_clock, duration>;

  static constexpr bool is_steady{static_cast<bool>(Backend::is_steady)};

  /**
   * @brief Current time read from the backend tick source.
   *
   * @return The time point built from \c Backend::ticks().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto now() noexcept -> time_point {
    return time_point{duration{Backend::ticks()}};
  }

  /**
   * @brief Build a time point from a raw tick count.
   *
   * @param t Raw backend tick count since the epoch.
   *
   * @return The corresponding \c time_point.
   *
   * @pre None.
   * @post \c to_ticks() of the result equals \p t.
   */
  [[nodiscard]] static constexpr auto from_ticks(rep const t) noexcept -> time_point {
    return time_point{duration{t}};
  }

  /**
   * @brief Extract the raw tick count from a time point.
   *
   * @param tp Time point on this clock.
   *
   * @return The ticks since the epoch held by \p tp.
   *
   * @pre None.
   * @post \c from_ticks() of the result equals \p tp.
   */
  [[nodiscard]] static constexpr auto to_ticks(time_point const tp) noexcept -> rep {
    return tp.time_since_epoch().count();
  }
};

}  // namespace nexenne::chrono
