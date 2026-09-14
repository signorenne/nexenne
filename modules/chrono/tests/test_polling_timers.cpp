/**
 * @file
 * @brief Tests for nexenne::chrono countdown, interval, deadline, alarm,
 *        rate_limiter.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <ratio>
#include <string>

#include <nexenne/chrono/alarm.hpp>
#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/format.hpp>
#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/rate_limiter.hpp>
#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/chrono/tick_clock.hpp>
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
  clk::advance(70ms);
  CHECK(cd.tick());
  CHECK_FALSE(cd.tick());
  CHECK(cd.is_expired());
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 10ms);
}

TEST_CASE("nexenne::chrono::countdown default-constructed is idle with zero target") {
  using clk = ch::basic_manual_clock<struct cd_default_tag>;
  clk::reset();
  ch::countdown<clk> const cd{};
  CHECK(cd.is_idle());
  CHECK_FALSE(cd.is_running());
  CHECK_FALSE(cd.is_paused());
  CHECK_FALSE(cd.is_expired());
  CHECK(cd.current_state() == ch::countdown<clk>::state::idle);
  CHECK(cd.target() == clk::duration::zero());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.template elapsed<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.progress() == doctest::Approx(0.0));
  CHECK_FALSE(cd.deadline().has_value());
}

TEST_CASE("nexenne::chrono::countdown is not expired just before, expired exactly at boundary") {
  using clk = ch::basic_manual_clock<struct cd_boundary_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  clk::advance(99ms);
  CHECK_FALSE(cd.is_expired());
  CHECK_FALSE(cd.tick());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);
  CHECK(cd.is_expired());
  CHECK(cd.tick());
  CHECK(cd.is_expired());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 0ms);
}

TEST_CASE("nexenne::chrono::countdown remaining decreases monotonically and clamps at zero") {
  using clk = ch::basic_manual_clock<struct cd_remaining_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 100ms);
  clk::advance(30ms);
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 70ms);
  clk::advance(30ms);
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 40ms);
  clk::advance(60ms);
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 20ms);
  CHECK(cd.progress() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::chrono::countdown a zero target is immediately expired on start") {
  using clk = ch::basic_manual_clock<struct cd_zero_tag>;
  clk::reset();
  ch::countdown<clk> cd{0ms};
  CHECK(cd.target() == clk::duration::zero());
  CHECK(cd.is_idle());
  CHECK_FALSE(cd.is_expired());
  cd.start();
  CHECK(cd.is_expired());
  CHECK(cd.current_state() == ch::countdown<clk>::state::expired);
  CHECK_FALSE(cd.is_running());
  CHECK_FALSE(cd.tick());
  CHECK(cd.progress() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::chrono::countdown a negative target is clamped to zero") {
  using clk = ch::basic_manual_clock<struct cd_neg_tag>;
  clk::reset();
  ch::countdown<clk> cd{-100ms};
  CHECK(cd.target() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::countdown target, extend, shrink clamp at zero") {
  using clk = ch::basic_manual_clock<struct cd_target_tag>;
  clk::reset();
  ch::countdown<clk> cd{};
  cd.target() = 100ms;
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(100ms));
  cd.extend(50ms);
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(150ms));
  cd.shrink(30ms);
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(120ms));
  cd.shrink(1s);
  CHECK(cd.target() == clk::duration::zero());
  cd.extend(-1s);
  CHECK(cd.target() == clk::duration::zero());
  cd.extend(80ms);
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(80ms));
}

TEST_CASE("nexenne::chrono::countdown pause freezes and resume continues elapsed time") {
  using clk = ch::basic_manual_clock<struct cd_pause_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  clk::advance(30ms);
  cd.pause();
  CHECK(cd.is_paused());
  clk::advance(500ms);
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 70ms);
  CHECK_FALSE(cd.is_expired());
  CHECK_FALSE(cd.tick());
  cd.resume();
  CHECK(cd.is_running());
  clk::advance(70ms);
  CHECK(cd.is_expired());
  CHECK(cd.tick());
}

TEST_CASE("nexenne::chrono::countdown pause/resume/start are no-ops from wrong states") {
  using clk = ch::basic_manual_clock<struct cd_noop_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.pause();
  CHECK(cd.is_idle());
  cd.resume();
  CHECK(cd.is_idle());
  cd.start();
  CHECK(cd.is_running());
  cd.resume();
  CHECK(cd.is_running());
  clk::advance(20ms);
  cd.start();
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 80ms);
  cd.pause();
  cd.start();
  CHECK(cd.is_paused());
}

TEST_CASE("nexenne::chrono::countdown reset returns to idle and keeps the target") {
  using clk = ch::basic_manual_clock<struct cd_reset_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  clk::advance(60ms);
  cd.reset();
  CHECK(cd.is_idle());
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(100ms));
  CHECK(cd.template elapsed<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 100ms);
}

TEST_CASE("nexenne::chrono::countdown restart and re-arm from expired clears elapsed") {
  using clk = ch::basic_manual_clock<struct cd_rearm_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  clk::advance(130ms);
  CHECK(cd.tick());
  CHECK(cd.is_expired());
  cd.start();
  CHECK(cd.is_running());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 100ms);
  clk::advance(130ms);
  CHECK(cd.tick());
  cd.restart();
  CHECK(cd.is_running());
  CHECK(cd.template elapsed<std::chrono::milliseconds>() == 0ms);
  clk::advance(100ms);
  CHECK(cd.tick());
}

TEST_CASE("nexenne::chrono::countdown holds a start point and an accumulator, not a stopwatch") {
  using cd = ch::countdown<>;
  static_assert(sizeof(cd) <= sizeof(cd::time_point) + 3 * sizeof(cd::duration));
  static_assert(sizeof(cd) < sizeof(ch::stopwatch<>));
}

TEST_CASE("nexenne::chrono::countdown keeps time after a tick expiry but not a zero-target one") {
  using clk = ch::basic_manual_clock<struct cd_expired_clock_tag>;
  clk::reset();

  ch::countdown<clk> ticked{10ms};
  ticked.start();
  clk::advance(15ms);
  REQUIRE(ticked.tick());
  clk::advance(5ms);
  CHECK(ticked.template elapsed<std::chrono::milliseconds>() == 20ms);
  CHECK(ticked.template overrun<std::chrono::milliseconds>() == 10ms);

  ch::countdown<clk> instant{0ms};
  instant.start();
  REQUIRE(instant.is_expired());
  instant.target() = 50ms;
  clk::advance(30ms);
  CHECK(instant.template elapsed<std::chrono::milliseconds>() == 0ms);
  CHECK(instant.is_expired());
}

TEST_CASE("nexenne::chrono::countdown restart with a zero target lands in expired") {
  using clk = ch::basic_manual_clock<struct cd_restart_zero_tag>;
  clk::reset();
  ch::countdown<clk> cd{0ms};
  cd.restart();
  CHECK(cd.is_expired());
}

TEST_CASE("nexenne::chrono::countdown deadline is engaged only while running") {
  using clk = ch::basic_manual_clock<struct cd_deadline_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  CHECK_FALSE(cd.deadline().has_value());
  cd.start();
  clk::advance(40ms);
  auto const d{cd.deadline()};
  REQUIRE(d.has_value());
  CHECK(*d == clk::now() + std::chrono::duration_cast<clk::duration>(60ms));
  cd.pause();
  CHECK_FALSE(cd.deadline().has_value());
  cd.resume();
  CHECK(cd.deadline().has_value());
  clk::advance(60ms);
  CHECK(cd.tick());
  CHECK_FALSE(cd.deadline().has_value());
}

TEST_CASE("nexenne::chrono::countdown ordering compares by remaining time on one now() snapshot") {
  using clk = ch::basic_manual_clock<struct cd_order_tag>;
  clk::reset();
  ch::countdown<clk> soon{50ms};
  ch::countdown<clk> later{200ms};
  soon.start();
  later.start();
  CHECK(soon < later);
  CHECK(later > soon);
  CHECK_FALSE(soon == later);
  ch::countdown<clk> twin_a{100ms};
  ch::countdown<clk> twin_b{100ms};
  twin_a.start();
  twin_b.start();
  CHECK(twin_a == twin_b);
}

TEST_CASE("nexenne::chrono::interval fires once per crossed period") {
  using clk = ch::basic_manual_clock<struct iv_tag>;
  clk::reset();
  ch::interval<clk> iv;
  iv.period() = 50ms;
  iv.start();
  CHECK_FALSE(iv.tick());
  clk::advance(50ms);
  CHECK(iv.tick());
  CHECK_FALSE(iv.tick());
  clk::advance(120ms);
  CHECK(iv.tick());
  CHECK(iv.tick());
  CHECK_FALSE(iv.tick());
  CHECK(iv.tick_count() == 3);
}

TEST_CASE("nexenne::chrono::interval default-constructed is stopped with a zero period") {
  using clk = ch::basic_manual_clock<struct iv_default_tag>;
  clk::reset();
  ch::interval<clk> iv{};
  CHECK_FALSE(iv.is_running());
  CHECK(iv.period() == clk::duration::zero());
  CHECK(iv.tick_count() == 0);
  CHECK(iv.remaining() == clk::duration::zero());
  CHECK(iv.next_tick_at() == clk::time_point::max());
  CHECK_FALSE(iv.tick());
}

TEST_CASE("nexenne::chrono::interval constructed with a period exposes it but stays stopped") {
  using clk = ch::basic_manual_clock<struct iv_ctor_tag>;
  clk::reset();
  ch::interval<clk> iv{50ms};
  CHECK(iv.period() == std::chrono::duration_cast<clk::duration>(50ms));
  CHECK_FALSE(iv.is_running());
  clk::advance(100ms);
  CHECK_FALSE(iv.tick());
}

TEST_CASE("nexenne::chrono::interval a negative period is clamped to zero and never fires") {
  using clk = ch::basic_manual_clock<struct iv_neg_tag>;
  clk::reset();
  ch::interval<clk> iv{-50ms};
  CHECK(iv.period() == clk::duration::zero());
  iv.start();
  clk::advance(1s);
  CHECK_FALSE(iv.tick());
  CHECK(iv.remaining() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::interval just-before, exactly-at, just-after the first boundary") {
  using clk = ch::basic_manual_clock<struct iv_boundary_tag>;
  clk::reset();
  ch::interval<clk> iv{100ms};
  iv.start();
  clk::advance(99ms);
  CHECK_FALSE(iv.tick());
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 0ms);
  CHECK(iv.tick());
  CHECK_FALSE(iv.tick());
  CHECK(iv.tick_count() == 1);
}

TEST_CASE(
  "nexenne::chrono::interval advancing by exactly N periods yields exactly N drained ticks"
) {
  using clk = ch::basic_manual_clock<struct iv_n_tag>;
  clk::reset();
  ch::interval<clk> iv{10ms};
  iv.start();
  clk::advance(50ms);
  int fires{0};
  while (iv.tick()) {
    ++fires;
  }
  CHECK(fires == 5);
  CHECK(iv.tick_count() == 5);
  CHECK_FALSE(iv.tick());
}

TEST_CASE("nexenne::chrono::interval one call per loop consumes one boundary and keeps the rest") {
  using clk = ch::basic_manual_clock<struct iv_coalesce_tag>;
  clk::reset();
  ch::interval<clk> iv{10ms};
  iv.start();
  clk::advance(35ms);
  CHECK(iv.tick());
  CHECK(iv.tick_count() == 1);
  CHECK(iv.tick());
  CHECK(iv.tick());
  CHECK_FALSE(iv.tick());
  CHECK(iv.tick_count() == 3);
}

TEST_CASE("nexenne::chrono::interval remaining reports zero when a boundary is pending") {
  using clk = ch::basic_manual_clock<struct iv_remaining_tag>;
  clk::reset();
  ch::interval<clk> iv{100ms};
  iv.start();
  clk::advance(40ms);
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 60ms);
  clk::advance(80ms);
  CHECK(iv.remaining() == clk::duration::zero());
  CHECK(iv.tick());
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 80ms);
}

TEST_CASE("nexenne::chrono::interval next_tick_at advances by exactly one period per tick") {
  using clk = ch::basic_manual_clock<struct iv_next_tag>;
  clk::reset();
  ch::interval<clk> iv{100ms};
  iv.start();
  auto const t0{clk::now()};
  CHECK(iv.next_tick_at() == t0 + std::chrono::duration_cast<clk::duration>(100ms));
  clk::advance(250ms);
  CHECK(iv.tick());
  CHECK(iv.next_tick_at() == t0 + std::chrono::duration_cast<clk::duration>(200ms));
  CHECK(iv.tick());
  CHECK(iv.next_tick_at() == t0 + std::chrono::duration_cast<clk::duration>(300ms));
}

TEST_CASE("nexenne::chrono::interval stop freezes; reset clears anchor and count") {
  using clk = ch::basic_manual_clock<struct iv_stop_tag>;
  clk::reset();
  ch::interval<clk> iv{50ms};
  iv.start();
  clk::advance(120ms);
  CHECK(iv.tick());
  CHECK(iv.tick());
  iv.stop();
  CHECK_FALSE(iv.is_running());
  CHECK_FALSE(iv.tick());
  CHECK(iv.tick_count() == 2);
  CHECK(iv.next_tick_at() == clk::time_point::max());
  iv.reset();
  CHECK_FALSE(iv.is_running());
  CHECK(iv.tick_count() == 0);
}

TEST_CASE("nexenne::chrono::interval restart re-anchors at now and zeroes the count") {
  using clk = ch::basic_manual_clock<struct iv_restart_tag>;
  clk::reset();
  ch::interval<clk> iv{50ms};
  iv.start();
  clk::advance(120ms);
  CHECK(iv.tick());
  CHECK(iv.tick_count() == 1);
  iv.start();
  CHECK(iv.tick_count() == 0);
  CHECK_FALSE(iv.tick());
  clk::advance(50ms);
  CHECK(iv.tick());
  CHECK(iv.tick_count() == 1);
}

TEST_CASE("nexenne::chrono::interval ordering: running sorts by next-tick, stopped sorts last") {
  using clk = ch::basic_manual_clock<struct iv_order_tag>;
  clk::reset();
  ch::interval<clk> soon{50ms};
  ch::interval<clk> later{200ms};
  ch::interval<clk> stopped{50ms};
  soon.start();
  later.start();
  CHECK(soon < later);
  CHECK(stopped > later);
  CHECK_FALSE(soon == later);
  ch::interval<clk> twin_a{100ms};
  ch::interval<clk> twin_b{100ms};
  twin_a.start();
  twin_b.start();
  CHECK(twin_a == twin_b);
}

TEST_CASE("nexenne::chrono::deadline reports reached and clamps remaining") {
  using clk = ch::basic_manual_clock<struct dl_tag>;
  clk::reset();
  auto const dl{ch::deadline<clk>::after(100ms)};
  CHECK_FALSE(dl.reached());
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 100ms);
  clk::advance(150ms);
  CHECK(dl.reached());
  CHECK(dl.remaining() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::deadline default-constructed targets the epoch") {
  using clk = ch::basic_manual_clock<struct dl_default_tag>;
  clk::reset();
  ch::deadline<clk> const dl{};
  CHECK(dl.when() == clk::time_point{});
  CHECK(dl.reached());
  CHECK(dl.remaining() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::deadline at() and explicit ctor anchor an absolute time point") {
  using clk = ch::basic_manual_clock<struct dl_at_tag>;
  clk::reset();
  auto const when{clk::now() + std::chrono::duration_cast<clk::duration>(250ms)};
  auto const a{ch::deadline<clk>::at(when)};
  ch::deadline<clk> const b{when};
  CHECK(a.when() == when);
  CHECK(b.when() == when);
  CHECK(a == b);
}

TEST_CASE("nexenne::chrono::deadline exactly at the deadline counts as reached") {
  using clk = ch::basic_manual_clock<struct dl_boundary_tag>;
  clk::reset();
  auto const dl{ch::deadline<clk>::after(100ms)};
  clk::advance(99ms);
  CHECK_FALSE(dl.reached());
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);
  CHECK(dl.reached());
  CHECK(dl.remaining() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::deadline already-past at construction is reached immediately") {
  using clk = ch::basic_manual_clock<struct dl_past_tag>;
  clk::reset();
  clk::advance(1s);
  auto const past{clk::now() - std::chrono::duration_cast<clk::duration>(100ms)};
  ch::deadline<clk> const dl{past};
  CHECK(dl.reached());
  CHECK(dl.remaining() == clk::duration::zero());
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 0ms);
}

TEST_CASE("nexenne::chrono::deadline can be reassigned to a new target") {
  using clk = ch::basic_manual_clock<struct dl_reset_tag>;
  clk::reset();
  ch::deadline<clk> dl{ch::deadline<clk>::after(100ms)};
  clk::advance(150ms);
  CHECK(dl.reached());
  dl = ch::deadline<clk>::after(200ms);
  CHECK_FALSE(dl.reached());
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 200ms);
  clk::advance(200ms);
  CHECK(dl.reached());
}

TEST_CASE("nexenne::chrono::deadline after() saturates a near-max offset") {
  using clk = ch::basic_manual_clock<struct dl_overflow_tag>;
  clk::reset();
  clk::advance(1s);
  auto const dl{ch::deadline<clk>::after(clk::duration::max())};
  CHECK_FALSE(dl.reached());
  CHECK(dl.when() == clk::time_point::max());
  CHECK(dl.remaining() > clk::duration::zero());
  auto const past{ch::deadline<clk>::after(clk::duration::min())};
  CHECK(past.reached());
  CHECK(past.remaining() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::deadline std::formatter renders remaining time") {
  using clk = ch::basic_manual_clock<struct dl_format_tag>;
  clk::reset();
  auto const dl{ch::deadline<clk>::after(65s)};
  CHECK(std::format("{}", dl) == "01m:05s");
  CHECK(std::format("{:!}", dl) == "00d:00h:01m:05s.000");
  clk::advance(2min);
  CHECK(std::format("{}", dl) == "00s");
}

TEST_CASE("nexenne::chrono::deadline orders by absolute target time") {
  using clk = ch::basic_manual_clock<struct dl_order_tag>;
  clk::reset();
  auto const soon{ch::deadline<clk>::after(50ms)};
  auto const later{ch::deadline<clk>::after(200ms)};
  CHECK(soon < later);
  CHECK(later > soon);
  CHECK_FALSE(soon == later);
  CHECK(soon != later);
  auto const twin{ch::deadline<clk>::at(soon.when())};
  CHECK(soon == twin);
}

TEST_CASE("nexenne::chrono::alarm one-shot and periodic firing") {
  using clk = ch::basic_manual_clock<struct al_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_after(clk::now(), 100ms);
  a.poll(clk::now());
  CHECK(fires == 0);
  clk::advance(100ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());

  fires = 0;
  a.arm_periodic(clk::now(), 50ms);
  clk::advance(160ms);
  a.poll(clk::now());
  CHECK(fires == 3);
  CHECK(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm periodic with a non-positive period disarms, never spins") {
  using clk = ch::basic_manual_clock<struct al_neg_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_periodic(clk::now(), -50ms);
  clk::advance(10ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm default-constructed is disarmed with no callback") {
  using clk = ch::basic_manual_clock<struct al_default_tag>;
  clk::reset();
  ch::alarm<clk> a{};
  CHECK_FALSE(a.is_armed());
  CHECK(a.mode() == ch::alarm_mode::one_shot);
  a.poll(clk::now());
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm armed with an empty callback still advances and disarms") {
  using clk = ch::basic_manual_clock<struct al_empty_tag>;
  clk::reset();
  ch::alarm<clk> a;
  a.arm_after(clk::now(), 50ms);
  CHECK(a.is_armed());
  CHECK(a.mode() == ch::alarm_mode::one_shot);
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm arm_at fires exactly at and not before the fire time") {
  using clk = ch::basic_manual_clock<struct al_at_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  auto const when{clk::now() + std::chrono::duration_cast<clk::duration>(100ms)};
  a.arm_at(when);
  CHECK(a.next_fire_time() == when);
  CHECK(a.mode() == ch::alarm_mode::one_shot);
  clk::advance(99ms);
  a.poll(clk::now());
  CHECK(fires == 0);
  clk::advance(1ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm one-shot armed in the past fires once on the first poll") {
  using clk = ch::basic_manual_clock<struct al_past_tag>;
  clk::reset();
  clk::advance(1s);
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  auto const past{clk::now() - std::chrono::duration_cast<clk::duration>(100ms)};
  a.arm_at(past);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm periodic fires once per poll boundary across several cycles") {
  using clk = ch::basic_manual_clock<struct al_periodic_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_periodic(clk::now(), 100ms);
  CHECK(a.next_fire_time() == clk::now() + std::chrono::duration_cast<clk::duration>(100ms));
  clk::advance(100ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK(a.is_armed());
  CHECK(a.next_fire_time() == clk::now() + std::chrono::duration_cast<clk::duration>(100ms));
  clk::advance(100ms);
  a.poll(clk::now());
  CHECK(fires == 2);
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(fires == 2);
}

TEST_CASE("nexenne::chrono::alarm periodic with a zero period fires once then disarms") {
  using clk = ch::basic_manual_clock<struct al_zero_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_periodic(clk::now(), 0ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm disarm stops further firing and retains the callback") {
  using clk = ch::basic_manual_clock<struct al_disarm_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_periodic(clk::now(), 50ms);
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  a.disarm();
  CHECK_FALSE(a.is_armed());
  clk::advance(500ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  a.arm_after(clk::now(), 10ms);
  clk::advance(10ms);
  a.poll(clk::now());
  CHECK(fires == 2);
}

TEST_CASE("nexenne::chrono::alarm re-arm from one-shot to periodic switches mode") {
  using clk = ch::basic_manual_clock<struct al_rearm_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_after(clk::now(), 50ms);
  CHECK(a.mode() == ch::alarm_mode::one_shot);
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());
  a.arm_periodic(clk::now(), 50ms);
  CHECK(a.mode() == ch::alarm_mode::periodic);
  clk::advance(100ms);
  a.poll(clk::now());
  CHECK(fires == 3);
  CHECK(a.is_armed());
  a.arm_at(clk::now() + std::chrono::duration_cast<clk::duration>(50ms));
  CHECK(a.mode() == ch::alarm_mode::one_shot);
}

TEST_CASE("nexenne::chrono::alarm one-shot callback can re-arm itself") {
  using clk = ch::basic_manual_clock<struct al_selfrearm_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&a, &fires] {
    ++fires;
    if (fires < 3) {
      a.arm_at(clk::now() + std::chrono::duration_cast<clk::duration>(50ms));
    }
  });
  a.arm_after(clk::now(), 50ms);
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(fires == 1);
  CHECK(a.is_armed());
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(fires == 2);
  CHECK(a.is_armed());
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(fires == 3);
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm callback can be replaced while armed") {
  using clk = ch::basic_manual_clock<struct al_replace_tag>;
  clk::reset();
  int a_fires{0};
  int b_fires{0};
  ch::alarm<clk> a;
  a.set_callback([&a_fires] { ++a_fires; });
  a.arm_periodic(clk::now(), 50ms);
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(a_fires == 1);
  a.set_callback([&b_fires] { ++b_fires; });
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(a_fires == 1);
  CHECK(b_fires == 1);
}

TEST_CASE("nexenne::chrono::rate_limiter starts full, allows a burst, then denies") {
  using clk = ch::basic_manual_clock<struct rl_burst_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{3.0, 10.0};
  CHECK(rl.capacity() == doctest::Approx(3.0));
  CHECK(rl.refill_rate() == doctest::Approx(10.0));
  CHECK(rl.tokens() == doctest::Approx(3.0));
  CHECK(rl.try_acquire());
  CHECK(rl.try_acquire());
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());
  CHECK(rl.tokens() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::chrono::rate_limiter refills lazily as the clock advances") {
  using clk = ch::basic_manual_clock<struct rl_refill_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 10.0};
  CHECK(rl.try_acquire(5.0));
  CHECK_FALSE(rl.try_acquire());
  clk::advance(100ms);
  CHECK(rl.tokens() == doctest::Approx(1.0));
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());
  clk::advance(250ms);
  CHECK(rl.tokens() == doctest::Approx(2.5));
  CHECK(rl.try_acquire(2.0));
  CHECK(rl.tokens() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::chrono::rate_limiter refill never exceeds capacity") {
  using clk = ch::basic_manual_clock<struct rl_cap_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{2.0, 100.0};
  CHECK(rl.try_acquire(2.0));
  clk::advance(10s);
  CHECK(rl.tokens() == doctest::Approx(2.0));
  CHECK(rl.try_acquire(2.0));
  CHECK_FALSE(rl.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter steady-state pacing after the initial burst") {
  using clk = ch::basic_manual_clock<struct rl_steady_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{4.0, 20.0};
  CHECK(rl.try_acquire(4.0));
  CHECK_FALSE(rl.try_acquire());
  for (int i{0}; i < 5; ++i) {
    clk::advance(50ms);
    CHECK(rl.try_acquire());
    CHECK_FALSE(rl.try_acquire());
  }
}

TEST_CASE("nexenne::chrono::rate_limiter zero token acquire trivially succeeds") {
  using clk = ch::basic_manual_clock<struct rl_zero_n_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{1.0, 1.0};
  CHECK(rl.try_acquire(1.0));
  CHECK(rl.try_acquire(0.0));
  CHECK_FALSE(rl.try_acquire(1.0));
}

TEST_CASE("nexenne::chrono::rate_limiter rejects negative and non-finite acquire counts") {
  using clk = ch::basic_manual_clock<struct rl_bad_n_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 1.0};
  CHECK_FALSE(rl.try_acquire(-1.0));
  CHECK_FALSE(rl.try_acquire(std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(rl.try_acquire(std::numeric_limits<double>::infinity()));
  CHECK(rl.tokens() == doctest::Approx(5.0));
}

TEST_CASE("nexenne::chrono::rate_limiter clamps a non-finite capacity to zero") {
  using clk = ch::basic_manual_clock<struct rl_nan_cap_tag>;
  clk::reset();
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  auto const inf{std::numeric_limits<double>::infinity()};

  ch::rate_limiter<clk> rl_nan{nan, 1.0};
  CHECK(rl_nan.capacity() == doctest::Approx(0.0));
  CHECK_FALSE(rl_nan.try_acquire());

  ch::rate_limiter<clk> rl_inf{inf, inf};
  CHECK(rl_inf.capacity() == doctest::Approx(0.0));
  CHECK_FALSE(rl_inf.try_acquire());

  ch::rate_limiter<clk> rl_ok{3.0, 10.0};
  CHECK(rl_ok.capacity() == doctest::Approx(3.0));
  CHECK(rl_ok.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter a zero refill rate never recovers") {
  using clk = ch::basic_manual_clock<struct rl_zero_rate_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{2.0, 0.0};
  CHECK(rl.try_acquire(2.0));
  CHECK_FALSE(rl.try_acquire());
  clk::advance(1h);
  CHECK(rl.tokens() == doctest::Approx(0.0));
  CHECK_FALSE(rl.try_acquire());
  CHECK(rl.until_next_token() == clk::duration::max());
}

TEST_CASE("nexenne::chrono::rate_limiter clamps negative capacity and rate to zero") {
  using clk = ch::basic_manual_clock<struct rl_neg_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{-3.0, -5.0};
  CHECK(rl.capacity() == doctest::Approx(0.0));
  CHECK(rl.refill_rate() == doctest::Approx(0.0));
  CHECK(rl.tokens() == doctest::Approx(0.0));
  CHECK_FALSE(rl.try_acquire());
  CHECK(rl.try_acquire(0.0));
}

TEST_CASE("nexenne::chrono::rate_limiter until_next_token reports zero, a wait, and rounds up") {
  using clk = ch::basic_manual_clock<struct rl_until_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 10.0};
  CHECK(rl.until_next_token() == clk::duration::zero());
  CHECK(rl.until_next_token(0.0) == clk::duration::zero());
  CHECK(rl.try_acquire(5.0));
  CHECK(rl.until_next_token() == std::chrono::duration_cast<clk::duration>(100ms));
  CHECK(rl.until_next_token(3.0) == std::chrono::duration_cast<clk::duration>(300ms));
}

TEST_CASE("nexenne::chrono::rate_limiter exact-boundary refill admits an acquire at the boundary") {
  using clk = ch::basic_manual_clock<struct rl_exact_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{1.0, 10.0};
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());
  clk::advance(99ms);
  CHECK_FALSE(rl.try_acquire());
  clk::advance(1ms);
  CHECK(rl.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter reset fills and drain empties the bucket") {
  using clk = ch::basic_manual_clock<struct rl_resetdrain_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{4.0, 10.0};
  CHECK(rl.try_acquire(4.0));
  rl.reset();
  CHECK(rl.tokens() == doctest::Approx(4.0));
  CHECK(rl.try_acquire(4.0));
  rl.drain();
  CHECK(rl.tokens() == doctest::Approx(0.0));
  CHECK_FALSE(rl.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter ignores backward clock movement") {
  using clk = ch::basic_manual_clock<struct rl_backward_tag>;
  clk::reset();
  clk::advance(1s);
  ch::rate_limiter<clk> rl{5.0, 10.0};
  CHECK(rl.try_acquire(5.0));
  clk::advance(-500ms);
  CHECK(rl.tokens() == doctest::Approx(0.0));
  CHECK_FALSE(rl.try_acquire());
  clk::advance(600ms);
  CHECK(rl.tokens() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::chrono::rate_limiter supports fractional refill rates") {
  using clk = ch::basic_manual_clock<struct rl_frac_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{1.0, 0.5};
  CHECK(rl.try_acquire());
  clk::advance(1s);
  CHECK(rl.tokens() == doctest::Approx(0.5));
  CHECK_FALSE(rl.try_acquire());
  clk::advance(1s);
  CHECK(rl.tokens() == doctest::Approx(1.0));
  CHECK(rl.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter until_next_token is unreachable above capacity") {
  using clk = ch::basic_manual_clock<struct rl_unreachable_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 10.0};
  CHECK(rl.until_next_token(8.0) == clk::duration::max());
  clk::advance(10s);
  CHECK(rl.until_next_token(8.0) == clk::duration::max());
  CHECK_FALSE(rl.try_acquire(8.0));

  ch::rate_limiter<clk> rl2{5.0, 10.0};
  CHECK(rl2.try_acquire(5.0));
  auto const wait{rl2.until_next_token(5.0)};
  CHECK(wait > clk::duration::zero());
  CHECK(wait != clk::duration::max());
}

TEST_CASE(
  "nexenne::chrono::alarm callback can replace itself while it runs, keeping its captures alive"
) {
  using clk = ch::basic_manual_clock<struct al_self_replace_tag>;
  clk::reset();
  ch::alarm<clk> a;
  int first{0};
  int second{0};
  int seen{0};
  a.set_callback([&a, &first, &second, &seen, payload = std::make_unique<int>(42)] {
    ++first;
    a.set_callback([&second] { ++second; });
    seen = *payload;
  });
  a.arm_periodic(clk::now(), 10ms);
  clk::advance(10ms);
  a.poll(clk::now());
  CHECK(first == 1);
  CHECK(seen == 42);
  clk::advance(10ms);
  a.poll(clk::now());
  CHECK(first == 1);
  CHECK(second == 1);
}

TEST_CASE("nexenne::chrono::alarm periodic callback can re-arm or reschedule itself") {
  using clk = ch::basic_manual_clock<struct al_periodic_rearm_tag>;
  clk::reset();
  ch::alarm<clk> to_one_shot;
  to_one_shot.set_callback([&to_one_shot] { to_one_shot.arm_after(clk::now(), 5ms); });
  to_one_shot.arm_periodic(clk::now(), 10ms);
  clk::advance(10ms);
  to_one_shot.poll(clk::now());
  CHECK(to_one_shot.is_armed());
  CHECK(to_one_shot.mode() == ch::alarm_mode::one_shot);
  CHECK(
    to_one_shot.next_fire_time() == clk::now() + std::chrono::duration_cast<clk::duration>(5ms)
  );

  clk::reset();
  ch::alarm<clk> rescheduled;
  rescheduled.set_callback([&rescheduled] { rescheduled.arm_periodic(clk::now(), 20ms); });
  rescheduled.arm_periodic(clk::now(), 10ms);
  clk::advance(10ms);
  rescheduled.poll(clk::now());
  CHECK(
    rescheduled.next_fire_time() == clk::now() + std::chrono::duration_cast<clk::duration>(20ms)
  );
}

/// @brief A 1 kHz RTOS-style tick backend, driven by hand.
struct ms_tick_backend {
  using rep = std::int64_t;
  using period = std::milli;
  static constexpr bool is_steady{true};
  static inline rep now_ticks{0};

  static auto ticks() noexcept -> rep {
    return now_ticks;
  }
};

TEST_CASE("nexenne::chrono::rate_limiter until_next_token rounds up on a coarse clock") {
  using clk = ch::tick_clock<ms_tick_backend>;
  ms_tick_backend::now_ticks = 0;
  ch::rate_limiter<clk> rl{1.0, 400.0};
  CHECK(rl.try_acquire());
  for (int i{0}; i < 4; ++i) {
    auto const wait{rl.until_next_token()};
    CHECK(wait > clk::duration::zero());
    ms_tick_backend::now_ticks += wait.count();
    CHECK(rl.try_acquire());
  }
}

TEST_CASE("nexenne::chrono::countdown and interval saturate instead of overflowing") {
  using dur = ch::countdown<>::duration;
  ch::countdown<> never{dur::max()};
  never.extend(1s);
  CHECK(never.target() == dur::max());
  never.shrink(std::chrono::nanoseconds::min());
  CHECK(never.target() == dur::max());
  CHECK(ch::countdown<>{std::chrono::hours::max()}.target() == dur::max());
  never.start();
  CHECK_FALSE(never.tick());
  CHECK(never.progress() < 1.0);

  ch::interval<> slow{std::chrono::hours{3'000'000}};
  CHECK(slow.period() == ch::interval<>::duration::max());
  slow.start();
  CHECK_FALSE(slow.tick());
  CHECK(slow.next_tick_at() == ch::interval<>::time_point::max());
}

TEST_CASE("nexenne::chrono::countdown and deadline remaining never read zero early") {
  using clk = ch::basic_manual_clock<struct remaining_ceil_tag>;
  clk::reset();
  ch::countdown<clk> cd{1ms};
  cd.start();
  clk::advance(100us);
  CHECK_FALSE(cd.is_expired());
  CHECK(cd.remaining<std::chrono::milliseconds>() == 1ms);
  auto const dl{ch::deadline<clk>::after(1ms)};
  clk::advance(100us);
  CHECK_FALSE(dl.reached());
  CHECK(dl.remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);
  CHECK(dl.remaining<std::chrono::milliseconds>() == 0ms);
}

}  // namespace
