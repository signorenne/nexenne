/**
 * @file
 * @brief tick_clock: adapt a raw tick source into a Chrono-compatible clock.
 *
 * On embedded targets the time source is rarely std::chrono: it is a hardware
 * counter, an esp_timer_get_time() call, or an RTOS tick. tick_clock wraps any
 * such source (a "backend" exposing rep / period / is_steady / ticks()) as a
 * clock_like type that the rest of nexenne::chrono and the standard std::chrono
 * APIs can consume. The whole module is templated on its clock precisely so this
 * works with zero special-casing.
 *
 * Here the backend is a settable software counter so the demo is deterministic;
 * a real backend's ticks() would read the hardware.
 *
 * The program walks three steps:
 *
 * 1. The adapter is a full clock: now() builds a time_point from the backend's
 *    current ticks, and the duration unit follows the backend's period.
 * 2. from_ticks and to_ticks bridge raw counts and time_points, handy when an ISR
 *    or driver hands you a bare counter value.
 * 3. The adapted clock satisfies steady_clock_like, so every chrono primitive
 *    accepts it: a stopwatch runs off the wrapped counter with no adaptation code.
 *
 * Expected output:
 *
 * \code
 * elapsed: 2500 us
 * 1_000_000 ticks = 1 s
 * round-trips back to ticks: 1000000
 * stopwatch on tick_clock: 5000 us
 * \endcode
 */

#include <chrono>
#include <cstdint>
#include <print>

#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/chrono/tick_clock.hpp>

namespace {

namespace ch = nexenne::chrono;

/**
 * @brief Tick backend where one tick is one microsecond.
 *
 * Exposes exactly what the tick_backend concept requires: a signed-integral
 * \c rep, a positive \c std::ratio \c period (seconds per tick), a compile-time
 * \c is_steady, and a \c noexcept static \c ticks() returning \c rep.
 *
 * @pre None.
 * @post None.
 */
struct micro_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady{true};

  static inline rep s_ticks{0};  ///< Software counter; a real backend reads hardware.

  static auto ticks() noexcept -> rep {
    return s_ticks;
  }
};

using micro_clock = ch::tick_clock<micro_backend>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  micro_backend::s_ticks = 0;

  auto const t0{micro_clock::now()};
  micro_backend::s_ticks = 2500;
  auto const t1{micro_clock::now()};
  std::println(
    "elapsed: {} us", std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
  );

  auto const tp{micro_clock::from_ticks(1'000'000)};
  std::println(
    "1_000_000 ticks = {} s",
    std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count()
  );
  std::println("round-trips back to ticks: {}", micro_clock::to_ticks(tp));

  micro_backend::s_ticks = 0;
  ch::stopwatch<micro_clock> sw;
  sw.start();
  micro_backend::s_ticks = 5000;
  std::println("stopwatch on tick_clock: {} us", sw.elapsed<std::chrono::microseconds>().count());

  return 0;
}
