/**
 * @file
 * @brief tick_clock: adapt a raw tick source into a Chrono-compatible clock.
 *
 * On embedded targets the time source is rarely std::chrono - it is a hardware
 * counter, an esp_timer_get_time() call, or an RTOS tick. tick_clock wraps any
 * such source (a "backend" exposing rep / period / is_steady / ticks()) as a
 * clock_like type that the rest of nexenne::chrono and the standard std::chrono
 * APIs can consume. The whole module is templated on its clock precisely so this
 * works with zero special-casing.
 *
 * Here the backend is a settable software counter so the demo is deterministic;
 * a real backend's ticks() would read the hardware.
 */

#include <chrono>
#include <cstdint>
#include <print>

#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/chrono/tick_clock.hpp>

namespace {

namespace ch = nexenne::chrono;

// A backend where one tick == one microsecond (period = std::micro). It must
// expose: a signed-integral rep, a positive std::ratio period (seconds/tick),
// a compile-time is_steady, and a noexcept static ticks() returning rep. Those
// four members are exactly what the tick_backend concept requires.
struct micro_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady{true};

  static inline rep s_ticks{0};  // a real backend would read a hardware counter

  static auto ticks() noexcept -> rep {
    return s_ticks;
  }
};

using micro_clock = ch::tick_clock<micro_backend>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  micro_backend::s_ticks = 0;

  // The adapter is a full clock: now() builds a time_point from the backend's
  // current ticks, and the duration unit follows the backend's period (us here).
  auto const t0{micro_clock::now()};
  micro_backend::s_ticks = 2500;  // advance the counter by 2500 us
  auto const t1{micro_clock::now()};
  std::println(
    "elapsed: {} us", std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
  );

  // from_ticks / to_ticks bridge raw counts and time_points - handy when an ISR
  // or driver hands you a bare counter value.
  auto const tp{micro_clock::from_ticks(1'000'000)};  // 1e6 us == 1 s
  std::println(
    "1_000_000 ticks = {} s",
    std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count()
  );
  std::println("round-trips back to ticks: {}", micro_clock::to_ticks(tp));

  // Because micro_clock satisfies steady_clock_like, every chrono primitive
  // accepts it. Drive a stopwatch off the wrapped hardware counter with no
  // adaptation code at all.
  micro_backend::s_ticks = 0;
  ch::stopwatch<micro_clock> sw;
  sw.start();
  micro_backend::s_ticks = 5000;  // 5000 us pass on the backend
  std::println("stopwatch on tick_clock: {} us", sw.elapsed<std::chrono::microseconds>().count());

  // elapsed: 2500 us
  // 1_000_000 ticks = 1 s
  // round-trips back to ticks: 1000000
  // stopwatch on tick_clock: 5000 us
  return 0;
}
