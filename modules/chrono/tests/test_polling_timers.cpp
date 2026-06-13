/**
 * @file
 * @brief Tests for nexenne::chrono countdown, interval, deadline, alarm.
 */

#include <doctest/doctest.h>

#include <chrono>

#include <nexenne/chrono/alarm.hpp>
#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/utility/in_place_function.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::countdown counts down, ticks expiry once, tracks overrun") {
  using clk = ch::basic_manual_clock<struct cd_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  CHECK(cd.is_running());
  clk::advance(40ms);
  CHECK_FALSE(cd.tick());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 60ms);
  CHECK(cd.progress() == doctest::Approx(0.4));
  clk::advance(70ms);      // now past the target (110 >= 100)
  CHECK(cd.tick());        // transition fires exactly once
  CHECK_FALSE(cd.tick());  // not again
  CHECK(cd.is_expired());
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 10ms);
}

TEST_CASE("nexenne::chrono::interval fires once per crossed period") {
  using clk = ch::basic_manual_clock<struct iv_tag>;
  clk::reset();
  ch::interval<clk> iv;
  iv.set_period(50ms);
  iv.start();
  CHECK_FALSE(iv.tick());  // nothing elapsed yet
  clk::advance(50ms);
  CHECK(iv.tick());        // one boundary
  CHECK_FALSE(iv.tick());  // consumed
  clk::advance(120ms);     // two more boundaries elapsed
  CHECK(iv.tick());
  CHECK(iv.tick());  // one tick per call
  CHECK_FALSE(iv.tick());
  CHECK(iv.tick_count() == 3);
}

TEST_CASE("nexenne::chrono::deadline reports reached and clamps remaining") {
  using clk = ch::basic_manual_clock<struct dl_tag>;
  clk::reset();
  auto const dl{ch::deadline<clk>::after(100ms)};
  CHECK_FALSE(dl.reached());
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 100ms);
  clk::advance(150ms);
  CHECK(dl.reached());
  CHECK(dl.remaining() == clk::duration::zero());  // overdue clamps to zero
}

TEST_CASE("nexenne::chrono::alarm one-shot and periodic firing") {
  using clk = ch::basic_manual_clock<struct al_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_after(clk::now(), 100ms);
  a.poll(clk::now());
  CHECK(fires == 0);  // not yet
  clk::advance(100ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());  // one-shot disarmed

  fires = 0;
  a.arm_periodic(clk::now(), 50ms);
  clk::advance(160ms);  // three boundaries elapsed
  a.poll(clk::now());
  CHECK(fires == 3);    // periodic catches up
  CHECK(a.is_armed());  // still armed
}

TEST_CASE("nexenne::chrono::alarm periodic with a non-positive period disarms, never spins") {
  using clk = ch::basic_manual_clock<struct al_neg_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_periodic(clk::now(), -50ms);  // negative period must not loop forever
  clk::advance(10ms);
  a.poll(clk::now());  // fires once, then disarms instead of spinning/overflowing
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());
}

}  // namespace
