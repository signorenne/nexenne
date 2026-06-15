/**
 * @file
 * @brief Tests for nexenne::chrono stopwatch and static_stopwatch over manual_clock.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <format>

#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/static_stopwatch.hpp>
#include <nexenne/chrono/stopwatch.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::stopwatch accumulates across pause and resume") {
  using clk = ch::basic_manual_clock<struct sw_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  CHECK(sw.is_idle());
  sw.start();
  CHECK(sw.is_running());
  clk::advance(100ms);
  sw.pause();
  CHECK(sw.is_paused());
  CHECK(sw.template elapsed<std::chrono::milliseconds>() == 100ms);
  clk::advance(500ms);  // time passes while paused: not counted
  CHECK(sw.template elapsed<std::chrono::milliseconds>() == 100ms);
  sw.resume();
  clk::advance(50ms);
  CHECK(sw.template elapsed<std::chrono::milliseconds>() == 150ms);
}

TEST_CASE("nexenne::chrono::stopwatch records laps") {
  using clk = ch::basic_manual_clock<struct sw_lap_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(10ms);
  auto const l1{sw.lap()};
  REQUIRE(l1.has_value());
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*l1) == 10ms);
  clk::advance(30ms);
  auto const l2{sw.lap()};
  REQUIRE(l2.has_value());
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*l2) == 30ms);
  CHECK(sw.lap_count() == 2);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(sw.lap_sum()) == 40ms);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*sw.lap_min()) == 10ms);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*sw.lap_max()) == 30ms);
}

TEST_CASE("nexenne::chrono::stopwatch std::format renders elapsed time") {
  using clk = ch::basic_manual_clock<struct sw_fmt_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(65s);
  CHECK(std::format("{}", sw) == "01m:05s");
}

TEST_CASE("nexenne::chrono::static_stopwatch bounds its lap buffer and counts drops") {
  using clk = ch::basic_manual_clock<struct fsw_tag>;
  clk::reset();
  ch::static_stopwatch<2, clk> sw;  // room for 2 laps (capacity first, clock second)
  sw.start();
  for (int i{0}; i < 4; ++i) {
    clk::advance(10ms);
    static_cast<void>(sw.lap());
  }
  CHECK(sw.lap_count() == 4);         // total laps seen
  CHECK(sw.stored_lap_count() == 2);  // only the buffer capacity is retained
  CHECK(sw.laps_dropped() == 2);      // the overflow was counted, not lost silently
}

}  // namespace
