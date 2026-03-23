/**
 * @file
 * @brief static_stopwatch: allocation-free lap timing with an overflow policy.
 *
 * A static_stopwatch mirrors stopwatch (start / pause / resume / lap / min /
 * max / average) but stores laps in a fixed-size std::array instead of a
 * std::vector, so it never touches the heap - safe for an MCU or any
 * heap-averse target. Its distinguishing feature is the overflow policy: once
 * the buffer fills, extra laps are still counted and folded into the running
 * sum and average, but not stored, and laps_dropped() reports the overflow.
 * The manual clock keeps the output deterministic.
 */

#include <array>
#include <chrono>
#include <print>

#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/static_stopwatch.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct sw_static_tag>;

auto ms(clk::duration const d) -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();

  // Capacity 3: room for three stored laps. We will deliberately record five so
  // the last two overflow.
  ch::static_stopwatch<3, clk> sw;
  sw.start();

  constexpr std::array<clk::duration, 5> segments{10ms, 20ms, 30ms, 40ms, 50ms};
  for (auto const seg : segments) {
    clk::advance(seg);
    auto const closed{sw.lap()};  // always returns the segment, even when dropped
    std::println("lap closed: {} ms", ms(*closed));
  }

  // lap_count() counts every lap observed; stored_lap_count() counts only the
  // ones retained in the buffer; laps_dropped() is the difference.
  std::println(
    "observed {}, stored {}, dropped {}", sw.lap_count(), sw.stored_lap_count(), sw.laps_dropped()
  );

  // min / max are computed over the *stored* laps only (the dropped values are
  // gone), but sum and average use every observed lap, so the mean stays
  // faithful even after overflow.
  std::println("stored min {} ms, max {} ms", ms(*sw.lap_min()), ms(*sw.lap_max()));
  std::println("sum (all laps) {} ms, average {} ms", ms(sw.lap_sum()), ms(*sw.lap_average()));

  // pause() freezes the accumulator; time advanced while paused is not counted.
  sw.pause();
  clk::advance(1000ms);
  sw.resume();
  clk::advance(5ms);
  std::println("total elapsed {} ms (paused gap excluded)", ms(sw.elapsed()));

  // observed 5, stored 3, dropped 2
  // stored min 10 ms, max 30 ms
  // sum (all laps) 150 ms, average 30 ms
  // total elapsed 155 ms (paused gap excluded)
  return 0;
}
