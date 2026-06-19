/**
 * @file
 * @brief Tests for nexenne::chrono countdown, interval, deadline, alarm,
 *        rate_limiter.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <limits>

#include <nexenne/chrono/alarm.hpp>
#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/rate_limiter.hpp>
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
  CHECK_FALSE(cd.deadline().has_value());  // not running
}

TEST_CASE("nexenne::chrono::countdown is not expired just before, expired exactly at boundary") {
  using clk = ch::basic_manual_clock<struct cd_boundary_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();
  clk::advance(99ms);  // just before
  CHECK_FALSE(cd.is_expired());
  CHECK_FALSE(cd.tick());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);  // exactly at the boundary: elapsed == target
  CHECK(cd.is_expired());
  CHECK(cd.tick());  // transition at exact boundary
  CHECK(cd.is_expired());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 0ms);
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 0ms);  // exactly at, no overrun
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
  clk::advance(60ms);                                                // well past target
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 0ms);  // never negative
  CHECK(cd.template overrun<std::chrono::milliseconds>() == 20ms);
  CHECK(cd.progress() == doctest::Approx(1.0));  // clamped at one
}

TEST_CASE("nexenne::chrono::countdown a zero target is immediately expired on start") {
  using clk = ch::basic_manual_clock<struct cd_zero_tag>;
  clk::reset();
  ch::countdown<clk> cd{0ms};
  CHECK(cd.target() == clk::duration::zero());
  CHECK(cd.is_idle());
  CHECK_FALSE(cd.is_expired());  // idle is never expired
  cd.start();
  CHECK(cd.is_expired());  // straight to expired
  CHECK(cd.current_state() == ch::countdown<clk>::state::expired);
  CHECK_FALSE(cd.is_running());
  CHECK_FALSE(cd.tick());                        // never was running, so no transition
  CHECK(cd.progress() == doctest::Approx(1.0));  // zero target expired reports one
}

TEST_CASE("nexenne::chrono::countdown a negative target is clamped to zero") {
  using clk = ch::basic_manual_clock<struct cd_neg_tag>;
  clk::reset();
  ch::countdown<clk> cd{-100ms};
  CHECK(cd.target() == clk::duration::zero());
  cd.set_target(-5s);
  CHECK(cd.target() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::countdown set_target, extend, shrink clamp at zero") {
  using clk = ch::basic_manual_clock<struct cd_target_tag>;
  clk::reset();
  ch::countdown<clk> cd{};
  cd.set_target(100ms);
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(100ms));
  cd.extend(50ms);
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(150ms));
  cd.shrink(30ms);
  CHECK(cd.target() == std::chrono::duration_cast<clk::duration>(120ms));
  cd.shrink(1s);  // shrinking past zero clamps
  CHECK(cd.target() == clk::duration::zero());
  cd.extend(-1s);  // negative extend also clamps
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
  clk::advance(500ms);  // time passes while paused, elapsed frozen
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 70ms);
  CHECK_FALSE(cd.is_expired());
  CHECK_FALSE(cd.tick());  // tick is a no-op while paused
  cd.resume();
  CHECK(cd.is_running());
  clk::advance(70ms);  // 30 + 70 == 100
  CHECK(cd.is_expired());
  CHECK(cd.tick());
}

TEST_CASE("nexenne::chrono::countdown pause/resume/start are no-ops from wrong states") {
  using clk = ch::basic_manual_clock<struct cd_noop_tag>;
  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.pause();  // no-op from idle
  CHECK(cd.is_idle());
  cd.resume();  // no-op from idle
  CHECK(cd.is_idle());
  cd.start();
  CHECK(cd.is_running());
  cd.resume();  // no-op while running
  CHECK(cd.is_running());
  clk::advance(20ms);
  cd.start();  // no-op while running, must not clear elapsed
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 80ms);
  cd.pause();
  cd.start();  // no-op while paused
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
  // re-arm directly via start() from expired
  cd.start();
  CHECK(cd.is_running());
  CHECK(cd.template remaining<std::chrono::milliseconds>() == 100ms);
  clk::advance(130ms);
  CHECK(cd.tick());  // fires again on the new cycle
  // restart() does reset + start
  cd.restart();
  CHECK(cd.is_running());
  CHECK(cd.template elapsed<std::chrono::milliseconds>() == 0ms);
  clk::advance(100ms);
  CHECK(cd.tick());
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
  CHECK_FALSE(cd.deadline().has_value());  // idle
  cd.start();
  clk::advance(40ms);
  auto const d{cd.deadline()};
  REQUIRE(d.has_value());
  CHECK(*d == clk::now() + std::chrono::duration_cast<clk::duration>(60ms));
  cd.pause();
  CHECK_FALSE(cd.deadline().has_value());  // not running
  cd.resume();
  CHECK(cd.deadline().has_value());
  clk::advance(60ms);
  CHECK(cd.tick());
  CHECK_FALSE(cd.deadline().has_value());  // expired
}

TEST_CASE("nexenne::chrono::countdown ordering compares by remaining time on one now() snapshot") {
  using clk = ch::basic_manual_clock<struct cd_order_tag>;
  clk::reset();
  ch::countdown<clk> soon{50ms};
  ch::countdown<clk> later{200ms};
  soon.start();
  later.start();
  CHECK(soon < later);  // soon fires first => smaller remaining
  CHECK(later > soon);
  CHECK_FALSE(soon == later);
  ch::countdown<clk> twin_a{100ms};
  ch::countdown<clk> twin_b{100ms};
  twin_a.start();
  twin_b.start();
  CHECK(twin_a == twin_b);  // equal remaining
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

TEST_CASE("nexenne::chrono::interval default-constructed is stopped with a zero period") {
  using clk = ch::basic_manual_clock<struct iv_default_tag>;
  clk::reset();
  ch::interval<clk> iv{};
  CHECK_FALSE(iv.is_running());
  CHECK(iv.period() == clk::duration::zero());
  CHECK(iv.tick_count() == 0);
  CHECK(iv.remaining() == clk::duration::zero());
  CHECK(iv.next_tick_at() == clk::time_point::max());  // stopped sorts last
  CHECK_FALSE(iv.tick());
}

TEST_CASE("nexenne::chrono::interval constructed with a period exposes it but stays stopped") {
  using clk = ch::basic_manual_clock<struct iv_ctor_tag>;
  clk::reset();
  ch::interval<clk> iv{50ms};
  CHECK(iv.period() == std::chrono::duration_cast<clk::duration>(50ms));
  CHECK_FALSE(iv.is_running());
  clk::advance(100ms);
  CHECK_FALSE(iv.tick());  // not started yet
}

TEST_CASE("nexenne::chrono::interval a negative period is clamped to zero and never fires") {
  using clk = ch::basic_manual_clock<struct iv_neg_tag>;
  clk::reset();
  ch::interval<clk> iv{-50ms};
  CHECK(iv.period() == clk::duration::zero());
  iv.start();
  clk::advance(1s);
  CHECK_FALSE(iv.tick());  // zero period never fires
  CHECK(iv.remaining() == clk::duration::zero());
  iv.set_period(-1s);
  CHECK(iv.period() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::interval just-before, exactly-at, just-after the first boundary") {
  using clk = ch::basic_manual_clock<struct iv_boundary_tag>;
  clk::reset();
  ch::interval<clk> iv{100ms};
  iv.start();
  clk::advance(99ms);
  CHECK_FALSE(iv.tick());  // just before
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);  // exactly at: now == anchor + period
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 0ms);
  CHECK(iv.tick());  // fires at the exact boundary
  CHECK_FALSE(iv.tick());
  CHECK(iv.tick_count() == 1);
}

TEST_CASE("nexenne::chrono::interval advancing by exactly N periods yields exactly N drained ticks"
) {
  using clk = ch::basic_manual_clock<struct iv_n_tag>;
  clk::reset();
  ch::interval<clk> iv{10ms};
  iv.start();
  clk::advance(50ms);  // exactly five periods
  int fires{0};
  while (iv.tick()) {
    ++fires;
  }
  CHECK(fires == 5);
  CHECK(iv.tick_count() == 5);
  CHECK_FALSE(iv.tick());
}

TEST_CASE("nexenne::chrono::interval coalesce-vs-drain: one call per loop skips missed periods") {
  using clk = ch::basic_manual_clock<struct iv_coalesce_tag>;
  clk::reset();
  ch::interval<clk> iv{10ms};
  iv.start();
  clk::advance(35ms);  // three full periods elapsed, plus 5ms slack
  // Calling tick() once consumes exactly one boundary (caller chooses to skip).
  CHECK(iv.tick());
  CHECK(iv.tick_count() == 1);
  // The anchor only advanced one period, so two boundaries remain pending.
  CHECK(iv.tick());
  CHECK(iv.tick());
  CHECK_FALSE(iv.tick());  // 5ms slack is not a full period
  CHECK(iv.tick_count() == 3);
}

TEST_CASE("nexenne::chrono::interval remaining reports zero when a boundary is pending") {
  using clk = ch::basic_manual_clock<struct iv_remaining_tag>;
  clk::reset();
  ch::interval<clk> iv{100ms};
  iv.start();
  clk::advance(40ms);
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 60ms);
  clk::advance(80ms);  // 120 elapsed: a boundary is pending but unconsumed
  CHECK(iv.remaining() == clk::duration::zero());
  CHECK(iv.tick());  // consume it; anchor now at 100
  CHECK(iv.template remaining<std::chrono::milliseconds>() == 80ms);  // 100 + 100 - 120
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
  CHECK_FALSE(iv.tick());       // stopped never fires
  CHECK(iv.tick_count() == 2);  // count preserved by stop
  CHECK(iv.next_tick_at() == clk::time_point::max());
  iv.reset();
  CHECK_FALSE(iv.is_running());
  CHECK(iv.tick_count() == 0);  // reset clears count
}

TEST_CASE("nexenne::chrono::interval restart re-anchors at now and zeroes the count") {
  using clk = ch::basic_manual_clock<struct iv_restart_tag>;
  clk::reset();
  ch::interval<clk> iv{50ms};
  iv.start();
  clk::advance(120ms);
  CHECK(iv.tick());
  CHECK(iv.tick_count() == 1);
  iv.start();  // re-arm: anchors at now, resets count
  CHECK(iv.tick_count() == 0);
  CHECK_FALSE(iv.tick());  // nothing elapsed since the new anchor
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
  CHECK(soon < later);     // earlier next-tick sorts first
  CHECK(stopped > later);  // stopped (max) sorts after any running
  CHECK_FALSE(soon == later);
  ch::interval<clk> twin_a{100ms};
  ch::interval<clk> twin_b{100ms};
  twin_a.start();
  twin_b.start();
  CHECK(twin_a == twin_b);  // same next-tick
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

TEST_CASE("nexenne::chrono::deadline default-constructed targets the epoch") {
  using clk = ch::basic_manual_clock<struct dl_default_tag>;
  clk::reset();
  ch::deadline<clk> const dl{};
  CHECK(dl.when() == clk::time_point{});
  CHECK(dl.reached());  // now == epoch >= epoch
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
  CHECK_FALSE(dl.reached());  // just before
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 1ms);
  clk::advance(1ms);    // exactly at
  CHECK(dl.reached());  // now == when
  CHECK(dl.remaining() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::deadline already-past at construction is reached immediately") {
  using clk = ch::basic_manual_clock<struct dl_past_tag>;
  clk::reset();
  clk::advance(1s);
  auto const past{clk::now() - std::chrono::duration_cast<clk::duration>(100ms)};
  ch::deadline<clk> const dl{past};
  CHECK(dl.reached());
  CHECK(dl.remaining() == clk::duration::zero());  // never negative
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 0ms);
}

TEST_CASE("nexenne::chrono::deadline can be reassigned to a new target") {
  using clk = ch::basic_manual_clock<struct dl_reset_tag>;
  clk::reset();
  ch::deadline<clk> dl{ch::deadline<clk>::after(100ms)};
  clk::advance(150ms);
  CHECK(dl.reached());
  dl = ch::deadline<clk>::after(200ms);  // re-arm to a fresh deadline
  CHECK_FALSE(dl.reached());
  CHECK(dl.template remaining<std::chrono::milliseconds>() == 200ms);
  clk::advance(200ms);
  CHECK(dl.reached());
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

TEST_CASE("nexenne::chrono::alarm default-constructed is disarmed with no callback") {
  using clk = ch::basic_manual_clock<struct al_default_tag>;
  clk::reset();
  ch::alarm<clk> a{};
  CHECK_FALSE(a.is_armed());
  CHECK(a.mode() == ch::alarm_mode::one_shot);
  a.poll(clk::now());  // disarmed poll is a harmless no-op
  CHECK_FALSE(a.is_armed());
}

TEST_CASE("nexenne::chrono::alarm armed with an empty callback still advances and disarms") {
  using clk = ch::basic_manual_clock<struct al_empty_tag>;
  clk::reset();
  ch::alarm<clk> a;  // no callback set
  a.arm_after(clk::now(), 50ms);
  CHECK(a.is_armed());
  CHECK(a.mode() == ch::alarm_mode::one_shot);
  clk::advance(50ms);
  a.poll(clk::now());  // must not crash on a null callback
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
  CHECK(fires == 0);  // just before
  clk::advance(1ms);  // exactly at
  a.poll(clk::now());
  CHECK(fires == 1);  // fires at the exact boundary
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
  a.arm_at(past);  // already overdue
  a.poll(clk::now());
  CHECK(fires == 1);  // fires once, not in a loop
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
  clk::advance(50ms);  // partial period
  a.poll(clk::now());
  CHECK(fires == 2);  // no fire before the next boundary
}

TEST_CASE("nexenne::chrono::alarm periodic with a zero period fires once then disarms") {
  using clk = ch::basic_manual_clock<struct al_zero_tag>;
  clk::reset();
  int fires{0};
  ch::alarm<clk> a;
  a.set_callback([&fires] { ++fires; });
  a.arm_periodic(clk::now(), 0ms);  // zero period: must not spin forever
  a.poll(clk::now());               // now >= next (next == now), fires once
  CHECK(fires == 1);
  CHECK_FALSE(a.is_armed());  // disarmed instead of looping
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
  CHECK(fires == 1);  // disarmed: no more fires
  // callback retained: re-arm without setting it again
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
  a.arm_periodic(clk::now(), 50ms);  // re-arm as periodic
  CHECK(a.mode() == ch::alarm_mode::periodic);
  clk::advance(100ms);
  a.poll(clk::now());
  CHECK(fires == 3);
  CHECK(a.is_armed());
  a.arm_at(clk::now() + std::chrono::duration_cast<clk::duration>(50ms));  // back to one-shot
  CHECK(a.mode() == ch::alarm_mode::one_shot);
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
  a.set_callback([&b_fires] { ++b_fires; });  // swap mid-flight
  clk::advance(50ms);
  a.poll(clk::now());
  CHECK(a_fires == 1);  // old callback no longer fires
  CHECK(b_fires == 1);  // new one does
}

TEST_CASE("nexenne::chrono::rate_limiter starts full, allows a burst, then denies") {
  using clk = ch::basic_manual_clock<struct rl_burst_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{3.0, 10.0};  // capacity 3, 10 tokens/sec
  CHECK(rl.capacity() == doctest::Approx(3.0));
  CHECK(rl.refill_rate() == doctest::Approx(10.0));
  CHECK(rl.tokens() == doctest::Approx(3.0));  // starts full
  CHECK(rl.try_acquire());
  CHECK(rl.try_acquire());
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());  // bucket empty
  CHECK(rl.tokens() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::chrono::rate_limiter refills lazily as the clock advances") {
  using clk = ch::basic_manual_clock<struct rl_refill_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 10.0};  // one token per 100ms
  CHECK(rl.try_acquire(5.0));           // drain to empty
  CHECK_FALSE(rl.try_acquire());
  clk::advance(100ms);
  CHECK(rl.tokens() == doctest::Approx(1.0));  // one refilled
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());
  clk::advance(250ms);  // 2.5 tokens
  CHECK(rl.tokens() == doctest::Approx(2.5));
  CHECK(rl.try_acquire(2.0));
  CHECK(rl.tokens() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::chrono::rate_limiter refill never exceeds capacity") {
  using clk = ch::basic_manual_clock<struct rl_cap_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{2.0, 100.0};
  CHECK(rl.try_acquire(2.0));                  // empty
  clk::advance(10s);                           // would refill far past capacity
  CHECK(rl.tokens() == doctest::Approx(2.0));  // clamped at capacity
  CHECK(rl.try_acquire(2.0));
  CHECK_FALSE(rl.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter steady-state pacing after the initial burst") {
  using clk = ch::basic_manual_clock<struct rl_steady_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{4.0, 20.0};  // one token per 50ms
  // burst drains the bucket
  CHECK(rl.try_acquire(4.0));
  CHECK_FALSE(rl.try_acquire());
  // steady state: exactly one token every 50ms
  for (int i{0}; i < 5; ++i) {
    clk::advance(50ms);
    CHECK(rl.try_acquire());
    CHECK_FALSE(rl.try_acquire());  // no second token in the same window
  }
}

TEST_CASE("nexenne::chrono::rate_limiter zero token acquire trivially succeeds") {
  using clk = ch::basic_manual_clock<struct rl_zero_n_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{1.0, 1.0};
  CHECK(rl.try_acquire(1.0));        // drain
  CHECK(rl.try_acquire(0.0));        // zero always succeeds, even when empty
  CHECK_FALSE(rl.try_acquire(1.0));  // still empty
}

TEST_CASE("nexenne::chrono::rate_limiter rejects negative and non-finite acquire counts") {
  using clk = ch::basic_manual_clock<struct rl_bad_n_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 1.0};
  CHECK_FALSE(rl.try_acquire(-1.0));  // negative is rejected
  CHECK_FALSE(rl.try_acquire(std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(rl.try_acquire(std::numeric_limits<double>::infinity()));
  CHECK(rl.tokens() == doctest::Approx(5.0));  // untouched by rejected acquires
}

TEST_CASE("nexenne::chrono::rate_limiter a zero refill rate never recovers") {
  using clk = ch::basic_manual_clock<struct rl_zero_rate_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{2.0, 0.0};  // no refill
  CHECK(rl.try_acquire(2.0));
  CHECK_FALSE(rl.try_acquire());
  clk::advance(1h);
  CHECK(rl.tokens() == doctest::Approx(0.0));  // still empty
  CHECK_FALSE(rl.try_acquire());
  CHECK(rl.until_next_token() == clk::duration::max());  // never available
}

TEST_CASE("nexenne::chrono::rate_limiter clamps negative capacity and rate to zero") {
  using clk = ch::basic_manual_clock<struct rl_neg_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{-3.0, -5.0};
  CHECK(rl.capacity() == doctest::Approx(0.0));
  CHECK(rl.refill_rate() == doctest::Approx(0.0));
  CHECK(rl.tokens() == doctest::Approx(0.0));
  CHECK_FALSE(rl.try_acquire());  // nothing to give
  CHECK(rl.try_acquire(0.0));     // zero still ok
}

TEST_CASE("nexenne::chrono::rate_limiter until_next_token reports zero, a wait, and rounds up") {
  using clk = ch::basic_manual_clock<struct rl_until_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{5.0, 10.0};                       // one token per 100ms
  CHECK(rl.until_next_token() == clk::duration::zero());     // full, available now
  CHECK(rl.until_next_token(0.0) == clk::duration::zero());  // non-positive n
  CHECK(rl.try_acquire(5.0));                                // empty
  // need one token at 10/sec => 100ms
  CHECK(rl.until_next_token() == std::chrono::duration_cast<clk::duration>(100ms));
  // need three tokens => 300ms
  CHECK(rl.until_next_token(3.0) == std::chrono::duration_cast<clk::duration>(300ms));
}

TEST_CASE("nexenne::chrono::rate_limiter exact-boundary refill admits an acquire at the boundary") {
  using clk = ch::basic_manual_clock<struct rl_exact_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{1.0, 10.0};  // one token per 100ms
  CHECK(rl.try_acquire());              // drain the one token
  CHECK_FALSE(rl.try_acquire());
  clk::advance(99ms);
  CHECK_FALSE(rl.try_acquire());  // just shy of one token
  clk::advance(1ms);              // exactly 100ms elapsed => one token
  CHECK(rl.try_acquire());        // admitted at the exact boundary
}

TEST_CASE("nexenne::chrono::rate_limiter reset fills and drain empties the bucket") {
  using clk = ch::basic_manual_clock<struct rl_resetdrain_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{4.0, 10.0};
  CHECK(rl.try_acquire(4.0));  // empty it
  rl.reset();                  // back to full
  CHECK(rl.tokens() == doctest::Approx(4.0));
  CHECK(rl.try_acquire(4.0));
  rl.drain();  // now empty again
  CHECK(rl.tokens() == doctest::Approx(0.0));
  CHECK_FALSE(rl.try_acquire());
}

TEST_CASE("nexenne::chrono::rate_limiter ignores backward clock movement") {
  using clk = ch::basic_manual_clock<struct rl_backward_tag>;
  clk::reset();
  clk::advance(1s);
  ch::rate_limiter<clk> rl{5.0, 10.0};
  CHECK(rl.try_acquire(5.0));                  // anchor at t=1s, empty
  clk::advance(-500ms);                        // clock goes backward
  CHECK(rl.tokens() == doctest::Approx(0.0));  // backward dt clamped to zero, no refill
  CHECK_FALSE(rl.try_acquire());
  clk::advance(600ms);  // net +100ms from the empty anchor => one token
  CHECK(rl.tokens() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::chrono::rate_limiter supports fractional refill rates") {
  using clk = ch::basic_manual_clock<struct rl_frac_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{1.0, 0.5};  // one token every 2 seconds
  CHECK(rl.try_acquire());             // empty
  clk::advance(1s);
  CHECK(rl.tokens() == doctest::Approx(0.5));  // half a token
  CHECK_FALSE(rl.try_acquire());
  clk::advance(1s);  // another half
  CHECK(rl.tokens() == doctest::Approx(1.0));
  CHECK(rl.try_acquire());
}

}  // namespace
